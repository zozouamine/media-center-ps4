#pragma once
#include "storage/directory.hpp"
namespace mc {
struct IndexSnapshot {
    std::vector<Item> entries;
    uint64_t scanned = 0;
    bool running = false, cancelled = false, truncated = false;
    std::string error;
};
class Indexer {
    std::thread thread_;
    std::mutex mutex_;
    std::atomic<bool> cancel_{false};
    IndexSnapshot snapshot_{};
public:
    ~Indexer();
    void start(const Source&, const std::string& directory, bool hidden);
    void cancel();
    IndexSnapshot snapshot(bool include_entries = true);
};
}
