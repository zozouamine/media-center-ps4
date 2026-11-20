#pragma once
#include "read_only.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace mc {
constexpr size_t directory_page_size = 128;
enum class DirectoryPageState { Ready, Missing, NotDirectory, Empty, NoMatchingMedia };
struct PageRequest {
    Source source;
    std::string relative, query;
    Sort sort = Sort::Name;
    int tab = 0;
    bool hidden = false, has_after = false, has_before = false, from_end = false;
    Entry after{}, before{};
};
struct DirectoryPage {
    std::vector<Entry> entries;
    Source source;
    uint64_t total = 0, generation = 0;
    bool has_next = false, cancelled = false, disconnected = false;
    DirectoryPageState state = DirectoryPageState::Ready;
    std::string error;
};
struct SourceScan {
    std::vector<Source> sources;
    std::vector<std::string> diagnostics;
};
DirectoryPage scan_page(const PageRequest&, const std::atomic<bool>& cancel);
// Watch known source identities independently of the currently displayed folder.
// This also covers a decoded photo or a favorite while no directory job is active.
class SourceMonitor {
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool quit_ = false, changed_ = false;
    uint64_t generation_ = 0;
    std::vector<Source> sources_;
    std::vector<std::string> missing_;
    void run();
public:
    SourceMonitor();
    ~SourceMonitor();
    void watch(const std::vector<Source>&);
    std::vector<std::string> take_missing();
};
class DirectoryWorker {
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> quit_{false}, cancel_{false};
    bool pending_ = false, sources_job_ = false, watch_ = false;
    uint64_t generation_ = 0;
    PageRequest job_{};
    std::string mount_base_;
    std::vector<std::string> extras_;
    std::shared_ptr<DirectoryPage> result_;
    std::shared_ptr<SourceScan> source_result_;
    void run();
public:
    DirectoryWorker();
    ~DirectoryWorker();
    uint64_t request(PageRequest request);
    void rescan_sources(std::string mount_base, std::vector<std::string> extras);
    void cancel();
    std::shared_ptr<DirectoryPage> take_page();
    std::shared_ptr<SourceScan> take_sources();
};
}
