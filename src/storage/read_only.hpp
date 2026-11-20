#pragma once
#include "library/types.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <vector>

namespace mc {
bool source_from_path(const std::string& path, Source& source, std::string& error);
// Called only on a verified read-only directory descriptor by storage workers.
bool refresh_source_metadata(int directory_fd, Source& source);
bool source_present(const Source& source);
bool valid_relative(const std::string& relative);
int open_directory(const Source& source, const std::string& relative, std::string& error, int* error_code = nullptr);
// Optional diagnostics contain the result of each read-only mount probe.
std::vector<Source> discover_sources(const std::string& mount_base, const std::vector<std::string>& extra,
                                     std::vector<std::string>* diagnostics = nullptr,
                                     const std::atomic<bool>* cancel = nullptr);
// Streaming enumeration shared by the browser and the indexer. It holds one
// read-only native directory handle.
class ReadOnlyDirectory {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    ReadOnlyDirectory();
    ~ReadOnlyDirectory();
    bool open(const Source&, const std::string& relative, std::string& error, const std::atomic<bool>* cancel = nullptr,
              int* error_code = nullptr);
    bool next(Entry&, std::string& error);
    bool refresh_metadata(Source&);
};
class ReadOnlyFile {
    int fd_ = -1;
    uint64_t size_ = 0, cursor_ = 0;
    Source source_{};
    double next_source_check_ = 0;
    const std::atomic<bool>* cancel_ = nullptr;
    bool usable();
public:
    ReadOnlyFile();
    ~ReadOnlyFile();
    ReadOnlyFile(const ReadOnlyFile&) = delete;
    ReadOnlyFile& operator=(const ReadOnlyFile&) = delete;
    bool open(const Source&, const std::string& relative, std::string& error, const std::atomic<bool>* cancel = nullptr);
    void close();
    size_t read(void* output, size_t bytes);
    bool read_at(uint64_t offset, void* output, size_t bytes);
    bool seek(int64_t offset, int origin);
    uint64_t tell() const { return cursor_; }
    uint64_t size() const { return size_; }
    int duplicate_fd() const;
    std::vector<uint8_t> read_all(size_t limit, std::string& error);
};
}
