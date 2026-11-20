#include "directory.hpp"
#include "platform/ps4/startup_trace.hpp"
#include <exception>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <queue>
#include <sys/stat.h>
#include <unistd.h>

namespace mc {
DirectoryPage scan_page(const PageRequest& req, const std::atomic<bool>& cancel) {
    DirectoryPage page;
    ReadOnlyDirectory directory;
    const bool category_root=req.tab>0 && req.tab<4 && req.relative==tab_directory(req.tab);
    int open_error=0;
    if (!directory.open(req.source, req.relative, page.error, &cancel, &open_error)) {
        page.cancelled = cancel.load();
        const bool present=!page.cancelled && source_present(req.source);
        page.disconnected=!page.cancelled && !present;
        if (category_root && present && open_error==ENOENT) { page.state=DirectoryPageState::Missing;page.error.clear(); }
        else if (category_root && present && open_error==ENOTDIR) { page.state=DirectoryPageState::NotDirectory;page.error.clear(); }
        return page;
    }
    page.source = req.source; directory.refresh_metadata(page.source);
    auto less = [&](const Entry& a, const Entry& b) {
        return req.has_before || req.from_end ? entry_before(b, a, req.sort) : entry_before(a, b, req.sort);
    };
    std::priority_queue<Entry, std::vector<Entry>, decltype(less)> heap(less);
    uint64_t eligible_after = 0, scanned = 0;bool has_content=false;
    while (true) {
        if (cancel.load()) { page.cancelled = true; break; }
        if ((scanned++ & 63) == 0 && !source_present(req.source)) { page.disconnected = true; page.error = "Storage disconnected"; break; }
        Entry e;
        if (!directory.next(e, page.error)) { page.cancelled = cancel.load(); break; }
        has_content=true;
        if ((!req.hidden && !e.name.empty() && e.name[0] == '.') || !kind_in_tab(e.kind, req.tab) || !name_matches(e.name, req.query)) continue;
        ++page.total;
        if (req.has_after && !entry_before(req.after, e, req.sort)) continue;
        if (req.has_before && !entry_before(e, req.before, req.sort)) continue;
        ++eligible_after;
        if (heap.size() < directory_page_size) heap.push(std::move(e));
        else if (req.has_before || req.from_end ? entry_before(heap.top(), e, req.sort) : entry_before(e, heap.top(), req.sort)) { heap.pop(); heap.push(std::move(e)); }
    }
    if (!page.error.empty() || page.cancelled) return page;
    while (!heap.empty()) { page.entries.push_back(heap.top()); heap.pop(); }
    std::sort(page.entries.begin(), page.entries.end(), [&](const Entry& a, const Entry& b) { return entry_before(a, b, req.sort); });
    page.has_next = req.has_before ? true : !req.from_end && eligible_after > page.entries.size();
    if (category_root && req.query.empty() && !req.has_after && !req.has_before) {
        if (!has_content) page.state=DirectoryPageState::Empty;
        else if (!page.total) page.state=DirectoryPageState::NoMatchingMedia;
    }
    return page;
}
SourceMonitor::SourceMonitor() { thread_ = std::thread(&SourceMonitor::run, this); }
SourceMonitor::~SourceMonitor() {
    { std::lock_guard<std::mutex> lock(mutex_); quit_ = true; }
    wake_.notify_one(); thread_.join();
}
void SourceMonitor::watch(const std::vector<Source>& sources) {
    std::lock_guard<std::mutex> lock(mutex_);
    sources_ = sources; missing_.clear(); ++generation_; changed_ = true; wake_.notify_one();
}
std::vector<std::string> SourceMonitor::take_missing() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = std::move(missing_); missing_.clear(); return result;
}
void SourceMonitor::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!quit_) {
        wake_.wait_for(lock, std::chrono::milliseconds(500), [&] { return quit_ || changed_; });
        if (quit_) break;
        changed_ = false; auto sources = sources_; auto generation = generation_;
        lock.unlock();
        std::vector<std::string> missing;
        for (const auto& source : sources) if (!source_present(source)) missing.push_back(source.root);
        lock.lock();
        if (generation != generation_) continue;
        for (const auto& root : missing) {
            missing_.push_back(root);
            sources_.erase(std::remove_if(sources_.begin(), sources_.end(), [&](const Source& s) { return s.root == root; }), sources_.end());
        }
    }
}
DirectoryWorker::DirectoryWorker() { thread_ = std::thread(&DirectoryWorker::run, this); }
DirectoryWorker::~DirectoryWorker() { quit_ = true; cancel_ = true; wake_.notify_all(); thread_.join(); }
uint64_t DirectoryWorker::request(PageRequest job) {
    std::lock_guard<std::mutex> lock(mutex_); cancel_ = true;
    job_ = std::move(job); sources_job_ = false; pending_ = true; watch_ = true; result_.reset(); source_result_.reset();
    ++generation_; wake_.notify_one(); return generation_;
}
void DirectoryWorker::rescan_sources(std::string base, std::vector<std::string> extra) {
    std::lock_guard<std::mutex> lock(mutex_); cancel_ = true; sources_job_ = true; pending_ = true; watch_ = false;
    mount_base_ = std::move(base); extras_ = std::move(extra); ++generation_; result_.reset(); source_result_.reset(); wake_.notify_one();
}
void DirectoryWorker::cancel() { std::lock_guard<std::mutex> lock(mutex_); cancel_ = true; pending_ = watch_ = false; ++generation_; result_.reset(); source_result_.reset(); }
std::shared_ptr<DirectoryPage> DirectoryWorker::take_page() { std::lock_guard<std::mutex> lock(mutex_); auto r = result_; result_.reset(); return r; }
std::shared_ptr<SourceScan> DirectoryWorker::take_sources() { std::lock_guard<std::mutex> lock(mutex_); auto r = source_result_; source_result_.reset(); return r; }
void DirectoryWorker::run() {
    while (!quit_) {
        PageRequest job; bool sources; uint64_t generation; std::string base; std::vector<std::string> extras;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            bool ready = wake_.wait_for(lock, std::chrono::milliseconds(500), [&] { return quit_ || pending_; });
            if (quit_) break;
            if (!ready) {
                bool watch = watch_; Source source = job_.source; uint64_t expected_generation = generation_;
                lock.unlock(); bool missing = watch && !source_present(source); lock.lock();
                if (missing && watch_ && generation_ == expected_generation) {
                    auto page = std::make_shared<DirectoryPage>(); page->generation = generation_; page->disconnected = true;
                    page->error = "Storage disconnected"; result_ = std::move(page); watch_ = false;
                }
                continue;
            }
            job = job_; sources = sources_job_; generation = generation_; base = mount_base_; extras = extras_;
            pending_ = false; cancel_ = false;
        }
        if (sources) {
            auto list = std::make_shared<SourceScan>();
            try {list->sources = discover_sources(base, extras, &list->diagnostics, &cancel_);}
            catch (const std::exception& error) {
                startup_trace("usb.scan.exception",0,error.what());
                list->sources.clear();list->diagnostics={std::string("Storage scan failed: ")+error.what()};
            }
            std::lock_guard<std::mutex> lock(mutex_); if (generation == generation_ && !cancel_) source_result_ = std::move(list);
        } else {
            auto page = std::make_shared<DirectoryPage>(scan_page(job, cancel_)); page->generation = generation;
            std::lock_guard<std::mutex> lock(mutex_); if (generation == generation_ && !cancel_) result_ = std::move(page);
        }
    }
}
}
