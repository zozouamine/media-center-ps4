#include "indexer.hpp"
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mc {
Indexer::~Indexer() { cancel(); if (thread_.joinable()) thread_.join(); }
void Indexer::cancel() { cancel_ = true; }
IndexSnapshot Indexer::snapshot(bool include) { std::lock_guard<std::mutex> lock(mutex_);if (include) return snapshot_;auto result=IndexSnapshot{};result.scanned=snapshot_.scanned;result.running=snapshot_.running;result.cancelled=snapshot_.cancelled;result.truncated=snapshot_.truncated;result.error=snapshot_.error;return result; }
void Indexer::start(const Source& source, const std::string& directory, bool hidden) {
    if (snapshot(false).running) return;
    cancel(); if (thread_.joinable()) thread_.join();
    cancel_ = false;
    { std::lock_guard<std::mutex> lock(mutex_); snapshot_ = {}; snapshot_.running = true; }
    thread_ = std::thread([this, source, directory, hidden] {
        // Depth-first streaming: at most 64 open directory handles, no whole-disk list.
        struct Frame { std::unique_ptr<ReadOnlyDirectory> dir; std::string relative; };
        std::vector<Frame> stack;
        std::string error; auto root = std::unique_ptr<ReadOnlyDirectory>(new ReadOnlyDirectory);
        if (root->open(source, directory, error, &cancel_)) stack.push_back({std::move(root), directory});
        uint64_t visited = 0; bool truncated = false;size_t retained_bytes=0;
        while (!stack.empty() && !cancel_) {
            if ((visited++ & 63) == 0 && !source_present(source)) { error = "Storage disconnected"; break; }
            auto& frame = stack.back(); Entry entry;
            if (!frame.dir->next(entry, error)) { stack.pop_back(); if (!error.empty()) break; continue; }
            if (!hidden && entry.name[0] == '.') continue;
            if (entry.directory()) {
                if (stack.size() >= 64) { truncated = true; continue; }
                auto child = std::unique_ptr<ReadOnlyDirectory>(new ReadOnlyDirectory);
                if (child->open(source, entry.relative, error, &cancel_)) stack.push_back({std::move(child), entry.relative});
                else if (!error.empty()) break;
            } else {
                std::lock_guard<std::mutex> lock(mutex_); ++snapshot_.scanned;
                size_t bytes=entry.name.size()+entry.relative.size()+source.root.size()+source.label.size()+sizeof(Item);
                if (snapshot_.entries.size() < 4096 && retained_bytes+bytes<=8u*1024u*1024u) { snapshot_.entries.push_back({source, std::move(entry)});retained_bytes+=bytes; }
                else truncated = true;
            }
        }
        stack.clear();
        std::lock_guard<std::mutex> lock(mutex_); snapshot_.running = false;
        snapshot_.cancelled = cancel_; snapshot_.truncated = truncated; snapshot_.error = error;
    });
}
}
