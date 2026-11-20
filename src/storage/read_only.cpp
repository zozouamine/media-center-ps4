#include "read_only.hpp"
#include "path_io.h"
#include "platform/platform.hpp"
#include "platform/ps4/startup_trace.hpp"
#include "platform/ps4/usb_access.hpp"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <sys/attr.h>
#include <sys/mount.h>
#else
#include <sys/statfs.h>
#endif
#include <unistd.h>

#ifdef MC_VALIDATE_PS4_DIRECT_USB
extern "C" int mc_test_usb_open(const char*, int, ...);
extern "C" DIR* mc_test_usb_opendir(const char*);
extern "C" int mc_test_usb_fstat(int, struct stat*);
#endif

namespace mc {
static int open_source_root(const char* path, int flags) {
#ifdef MC_VALIDATE_PS4_DIRECT_USB
    return mc_test_usb_open(path, flags);
#else
    return ::open(path, flags);
#endif
}
static DIR* open_mount_directory(const char* path) {
#ifdef MC_VALIDATE_PS4_DIRECT_USB
    return mc_test_usb_opendir(path);
#else
    return ::opendir(path);
#endif
}
static int stat_source_handle(int fd, struct stat* output) {
#ifdef MC_VALIDATE_PS4_DIRECT_USB
    return mc_test_usb_fstat(fd, output);
#else
    return ::fstat(fd, output);
#endif
}
static std::string io_error(const char* action, int code = errno) {
    return std::string(action) + ": errno=" + std::to_string(code) + " (" + std::strerror(code) + ")";
}
static std::string bounded_string(const char* value, size_t limit) {
    return std::string(value, strnlen(value, limit));
}
bool refresh_source_metadata(int fd, Source& source) {
    source.space_known = false; source.total_bytes = source.available_bytes = 0;
    struct statfs stats{};
    // The installed OpenOrbis statvfs wrappers do not populate their output.
    // The PS4 adapter also corrects the SDK's undersized filesystem-id field.
    startup_trace("storage.capacity.begin",fd,source.root.c_str());
    if (fstatfs(fd, &stats) != 0) {startup_trace("storage.capacity.failed",errno,source.root.c_str());return false;}
    startup_trace("storage.capacity.ready",0,source.root.c_str());
#if defined(__APPLE__) || defined(MC_PS4) || defined(__FreeBSD__)
    source.filesystem = bounded_string(stats.f_fstypename, sizeof(stats.f_fstypename));
    source.mount_name = bounded_string(stats.f_mntonname, sizeof(stats.f_mntonname));
    if (!source.volume_label_known && !source.mount_name.empty()) {
        auto slash = source.mount_name.find_last_of('/');
        auto name = source.mount_name.substr(slash == std::string::npos ? 0 : slash + 1);
        if (!name.empty()) source.label = std::move(name);
    }
#endif
#if defined(__APPLE__)
    struct attrlist attrs{}; attrs.bitmapcount = ATTR_BIT_MAP_COUNT;
    attrs.volattr = ATTR_VOL_INFO | ATTR_VOL_NAME;
    struct VolumeName { uint32_t length; attrreference_t name; char value[1024]; } result{};
    if (fgetattrlist(fd, &attrs, &result, sizeof(result), FSOPT_NOFOLLOW) == 0
        && result.length <= sizeof(result) && result.length >= sizeof(uint32_t) + sizeof(attrreference_t)) {
        int64_t offset = static_cast<int64_t>(offsetof(VolumeName, name)) + result.name.attr_dataoffset;
        uint64_t length = result.name.attr_length;
        if (offset >= static_cast<int64_t>(sizeof(uint32_t) + sizeof(attrreference_t)) && length > 1
            && static_cast<uint64_t>(offset) <= result.length && length <= result.length - static_cast<uint64_t>(offset)) {
            const char* name = reinterpret_cast<const char*>(&result) + offset;
            auto label = bounded_string(name, static_cast<size_t>(length));
            if (!label.empty()) { source.label = std::move(label); source.volume_label_known = true; }
        }
    }
#endif
    uint64_t block_size = static_cast<uint64_t>(stats.f_bsize), blocks = static_cast<uint64_t>(stats.f_blocks);
    uint64_t available = stats.f_bavail > 0 ? static_cast<uint64_t>(stats.f_bavail) : 0;
    if (!block_size || !blocks || blocks > UINT64_MAX / block_size) return false;
    source.total_bytes = blocks * block_size;
    source.available_bytes = std::min(available, blocks) * block_size;
    source.space_known = true;
    return true;
}
bool valid_relative(const std::string& path) {
    if (path.size() > 16384 || path.find('\0') != std::string::npos || (!path.empty() && path[0] == '/')) return false;
    size_t begin = 0;
    while (begin < path.size()) {
        size_t end = path.find('/', begin); if (end == std::string::npos) end = path.size();
        auto part = path.substr(begin, end - begin);
        if (part.empty() || part == "." || part == ".." || part.size() > 255) return false;
        begin = end + 1;
    }
    return path.empty() || path.back() != '/';
}
bool source_from_path(const std::string& path, Source& source, std::string& error) {
    startup_trace("usb.probe.begin",0,path.c_str());
    if (path.empty() || path.find('\0') != std::string::npos) { error = "Invalid source path"; return false; }
#ifdef MC_PS4
    // OpenOrbis realpath calls readlink on /proc/self/fd, but readlink is ENOSYS.
    // Console source paths are absolute mount paths and reject traversal/links.
    if (path[0]!='/' || !valid_relative(path.substr(1))) { error="Invalid absolute storage path";return false; }
    std::string root=path;
#else
    char* resolved = realpath(path.c_str(), nullptr);
    if (!resolved) { error = io_error("Cannot resolve storage source"); return false; }
    std::string root(resolved); std::free(resolved);
#endif
    constexpr int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
    errno = 0;
    int fd = open_source_root(root.c_str(), flags);
    // Keep the v5 libc path and its existing _open import. The native fallback
    // in v6 added a loader dependency, even though discovery runs after the UI.
    if (fd < 0) { startup_trace("usb.open.failed",errno,root.c_str());error = io_error("open"); return false; }
    startup_trace("usb.open.ready",fd,root.c_str());
    struct stat st{};
    startup_trace("usb.fstat.begin",fd,root.c_str());
    if (stat_source_handle(fd, &st) != 0) {
        int code = errno;startup_trace("usb.fstat.failed",code,root.c_str()); ::close(fd); error = io_error("fstat", code); return false;
    }
    startup_trace("usb.fstat.ready",st.st_mode,root.c_str());
    if (!S_ISDIR(st.st_mode)) {
        char mode[32]; std::snprintf(mode, sizeof(mode), "%#o", static_cast<unsigned>(st.st_mode));
        ::close(fd); error = std::string("type: mode=") + mode + " (not a directory)"; return false;
    }
    auto slash = root.find_last_of('/');
    source = {}; source.root = root; source.label = root.substr(slash + 1);
    source.device = static_cast<uint64_t>(st.st_dev); source.inode = static_cast<uint64_t>(st.st_ino);
    if (source.label.empty()) source.label = "Storage";
    refresh_source_metadata(fd, source); ::close(fd);
    startup_trace("usb.probe.readable",source.space_known?1:0,root.c_str());
    error.clear();
    return true;
}
bool source_present(const Source& source) {
    struct stat st{};
    if (source.root.empty()) return false;
    int fd = open_source_root(source.root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return false;
    bool present = stat_source_handle(fd, &st) == 0 && S_ISDIR(st.st_mode)
        && static_cast<uint64_t>(st.st_dev) == source.device && static_cast<uint64_t>(st.st_ino) == source.inode;
    int saved = errno; ::close(fd); errno = saved; return present;
}
static int open_beneath(const Source& source, const std::string& relative, bool directory, std::string& error, int* error_code = nullptr) {
    if (error_code) *error_code = 0;
    if (!valid_relative(relative)) { if (error_code) *error_code=EINVAL;error = "Unsafe or oversized relative path"; return -1; }
    if (!source_present(source)) { if (error_code) *error_code=ESTALE;error = "Storage disconnected or replaced"; return -1; }
    int fd = open_source_root(source.root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) { if (error_code) *error_code=errno;error = io_error("Cannot open storage"); return -1; }
    struct stat root_st{};
    if (stat_source_handle(fd, &root_st) != 0 || static_cast<uint64_t>(root_st.st_dev) != source.device
        || static_cast<uint64_t>(root_st.st_ino) != source.inode) {
        int code=errno?errno:ESTALE;::close(fd);if (error_code) *error_code=code;error = "Storage changed while opening"; return -1;
    }
    size_t begin = 0; std::string parent = source.root;
    while (begin < relative.size()) {
        size_t end = relative.find('/', begin); if (end == std::string::npos) end = relative.size();
        bool last = end == relative.size();
        int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC;
        if (!last || directory) flags |= O_DIRECTORY;
        auto name = relative.substr(begin, end - begin);
        int next = mc_open_child(fd, parent.c_str(), name.c_str(), flags, 0);
        int saved = errno; ::close(fd); fd = next; errno = saved;
        if (fd < 0) { if (error_code) *error_code=saved;error = io_error("Cannot open selected path",saved); return -1; }
        parent += "/" + name;
        begin = end + 1;
    }
    struct stat st{};
    if (stat_source_handle(fd, &st) != 0 || (directory ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode))
        || static_cast<uint64_t>(st.st_dev) != source.device) {
        int code=errno?errno:(directory?ENOTDIR:EISDIR);::close(fd);if (error_code) *error_code=code;error = "Unsupported entry or path leaves the source"; return -1;
    }
    return fd;
}
int open_directory(const Source& s, const std::string& r, std::string& e, int* code) { return open_beneath(s, r, true, e, code); }
std::vector<Source> discover_sources(const std::string& mount_base, const std::vector<std::string>& extra,
                                     std::vector<std::string>* diagnostics, const std::atomic<bool>* cancel) {
    startup_trace("usb.scan.begin",0,mount_base.c_str());
    std::vector<Source> out;
    std::vector<std::string> attempted;
    if (diagnostics) diagnostics->clear();
#ifdef MC_PS4
    startup_trace("usb.self-profile", 0, MC_PS4_SELF_PROFILE);
    if (diagnostics) diagnostics->push_back(std::string("SELF access profile: ") + MC_PS4_SELF_PROFILE);
    // GoldHEN's documented SDK command changes this application's filesystem
    // namespace. It runs only in the deferred storage worker, after the UI.
    // File access below still uses native read-only descriptors, with no network.
    if (!(cancel && cancel->load())) {
        bool enabled = prepare_direct_usb_access();
        if (diagnostics) diagnostics->push_back(direct_usb_access_status().description());
        if (enabled) startup_trace_retry();
    }
    // Jailbroken /mnt can contain readable usbN placeholders without a drive.
    // A mounted USB filesystem has a different device from that parent. If
    // parent access is denied, keep probing children rather than rejecting them.
    bool parent_device_known = false;
    uint64_t parent_device = 0;
    int parent_fd = open_source_root(mount_base.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (parent_fd >= 0) {
        struct stat parent{};
        parent_device_known = stat_source_handle(parent_fd, &parent) == 0 && S_ISDIR(parent.st_mode);
        if (parent_device_known) parent_device = static_cast<uint64_t>(parent.st_dev);
        ::close(parent_fd);
    }
#endif
    auto add = [&](const std::string& path) {
        if (cancel && cancel->load()) return;
        if (out.size() >= 32 || attempted.size() >= 64
            || std::find(attempted.begin(), attempted.end(), path) != attempted.end()) return;
        attempted.push_back(path);
        Source s; std::string error;
        bool readable = source_from_path(path, s, error);
#ifdef MC_PS4
        const std::string mount_prefix = mount_base + "/";
        bool global_slot = path.compare(0, mount_prefix.size(), mount_prefix) == 0
            && search_key(path.substr(mount_prefix.size())).compare(0, 3, "usb") == 0;
        bool title_slot = path.size() == 5 && path.compare(0, 4, "/usb") == 0
            && path[4] >= '0' && path[4] <= '7';
        if (readable && parent_device_known && (global_slot || title_slot) && s.device == parent_device) {
            readable = false; error = "No mounted USB filesystem";
            startup_trace("usb.slot.unmounted", static_cast<int64_t>(s.device), path.c_str());
        }
#endif
        if (diagnostics) diagnostics->push_back(path + " | " + (readable
            ? (s.space_known ? "Readable" : "Readable; space unavailable") : error));
        if (!readable) return;
        if (std::none_of(out.begin(), out.end(), [&](const Source& old) {
            return old.root == s.root || ((s.device && s.inode) && old.device == s.device && old.inode == s.inode);
        })) out.push_back(std::move(s));
    };
    for (const auto& root : extra) add(root);
    // A readable mounted child can exist even when listing /mnt is denied.
    for (unsigned index=0;index<8;++index) add(mount_base+"/usb"+std::to_string(index));
    startup_trace("usb.mount-list.open.begin",0,mount_base.c_str());
    DIR* d = open_mount_directory(mount_base.c_str());
    startup_trace("usb.mount-list.open.end",d?0:errno,mount_base.c_str());
    if (d) {
        int read_error = 0;
        startup_trace("usb.mount-list.read.begin",0,mount_base.c_str());
        while (true) {
            errno = 0; auto* de = readdir(d);
            if (!de) { read_error = errno; break; }
            size_t length=strnlen(de->d_name,sizeof(de->d_name));
            if (length==sizeof(de->d_name)) {read_error=EIO;break;}
            std::string name(de->d_name,length);
            auto key = search_key(name);
            if (key.size() >= 3 && key.compare(0, 3, "usb") == 0) add(mount_base + "/" + name);
        }
        closedir(d);
        startup_trace("usb.mount-list.read.end",read_error,mount_base.c_str());
        if (read_error && diagnostics) diagnostics->push_back(mount_base + " | " + io_error("readdir", read_error));
    } else if (diagnostics) diagnostics->push_back(mount_base + " | " + io_error("opendir"));
    std::sort(out.begin(), out.end(), [](const Source& a, const Source& b) { return a.root < b.root; });
    startup_trace("usb.scan.ready",static_cast<int64_t>(out.size()),mount_base.c_str());
    return out;
}
struct ReadOnlyDirectory::Impl {
    Source source;
    std::string relative, absolute;
    DIR* directory = nullptr;
    const std::atomic<bool>* cancel = nullptr;
    ~Impl() { if (directory) closedir(directory); }
};
ReadOnlyDirectory::ReadOnlyDirectory() = default;
ReadOnlyDirectory::~ReadOnlyDirectory() = default;
bool ReadOnlyDirectory::open(const Source& source, const std::string& relative, std::string& error, const std::atomic<bool>* cancel, int* error_code) {
    impl_.reset(); error.clear(); auto impl = std::unique_ptr<Impl>(new Impl);
    impl->source = source; impl->relative = relative; impl->cancel = cancel;
    impl->absolute = source.root + (relative.empty() ? "" : "/" + relative);
    int fd = open_directory(source, relative, error, error_code); if (fd < 0) return false;
    impl->directory = fdopendir(fd);
    if (!impl->directory) { int saved = errno; ::close(fd);if (error_code) *error_code=saved;error = io_error("Cannot enumerate directory", saved); return false; }
    impl_ = std::move(impl); return true;
}
bool ReadOnlyDirectory::refresh_metadata(Source& source) {
    return impl_ && impl_->directory ? refresh_source_metadata(dirfd(impl_->directory), source) : false;
}
bool ReadOnlyDirectory::next(Entry& entry, std::string& error) {
    if (!impl_) return false;
    if (impl_->cancel && impl_->cancel->load()) { error = "Cancelled"; return false; }
    while (true) {
        errno = 0; auto* de = readdir(impl_->directory);
        if (!de) { if (errno) error = io_error("Directory read failed"); return false; }
        size_t length = strnlen(de->d_name, sizeof(de->d_name));
        if (length == sizeof(de->d_name)) { error = "Invalid directory entry name"; return false; }
        std::string name(de->d_name, length);
        if (name == "." || name == "..") continue;
        auto relative = impl_->relative.empty() ? name : impl_->relative + "/" + name;
        if (!valid_relative(relative)) continue;
        struct stat st{};
#ifdef MC_PS4
        int fd = mc_open_child(dirfd(impl_->directory), impl_->absolute.c_str(), name.c_str(),
                               O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, 0);
        if (fd < 0) {
            if (errno == ELOOP || errno == ENOTDIR || errno == ENOENT) continue;
            error = io_error("Cannot inspect directory entry"); return false;
        }
        int result = stat_source_handle(fd, &st), saved = errno; ::close(fd); errno = saved;
        if (result) { error = io_error("Cannot inspect directory entry"); return false; }
#else
        if (fstatat(dirfd(impl_->directory), name.c_str(), &st, AT_SYMLINK_NOFOLLOW)) continue;
#endif
        if ((!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) || static_cast<uint64_t>(st.st_dev) != impl_->source.device) continue;
        entry = {name, relative, S_ISDIR(st.st_mode) ? Kind::Folder : classify(name),
                 S_ISREG(st.st_mode) && st.st_size >= 0 ? static_cast<uint64_t>(st.st_size) : 0, static_cast<int64_t>(st.st_mtime)};
        return true;
    }
}
ReadOnlyFile::ReadOnlyFile() = default;
ReadOnlyFile::~ReadOnlyFile() { close(); }
bool ReadOnlyFile::open(const Source& source, const std::string& relative, std::string& error, const std::atomic<bool>* cancel) {
    close();
    fd_ = open_beneath(source, relative, false, error);
    if (fd_ < 0) return false;
    struct stat st{};
    if (stat_source_handle(fd_, &st) != 0 || st.st_size < 0) { close(); error = "Invalid file size"; return false; }
    size_ = static_cast<uint64_t>(st.st_size); source_ = source; cancel_ = cancel; next_source_check_ = 0;
    return true;
}
void ReadOnlyFile::close() { if (fd_ >= 0) ::close(fd_); fd_ = -1; size_ = cursor_ = 0; cancel_ = nullptr; }
bool ReadOnlyFile::usable() {
    if (fd_ < 0 || (cancel_ && cancel_->load())) return false;
    double now = monotonic_now();
    if (now >= next_source_check_) { next_source_check_ = now + 0.2; if (!source_present(source_)) return false; }
    return true;
}
size_t ReadOnlyFile::read(void* output, size_t bytes) {
    if (!usable() || cursor_ >= size_) return 0;
    bytes = static_cast<size_t>(std::min<uint64_t>(bytes, size_ - cursor_));
    size_t done = 0;
    while (done < bytes) {
        if (!usable()) break;
        size_t block = std::min<size_t>(bytes - done, 65536);
        ssize_t n = pread(fd_, static_cast<uint8_t*>(output) + done, block, static_cast<off_t>(cursor_ + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        done += static_cast<size_t>(n);
    }
    cursor_ += done; return done;
}
bool ReadOnlyFile::read_at(uint64_t offset, void* output, size_t bytes) {
    if (offset > size_ || bytes > size_ - offset) return false;
    uint64_t saved = cursor_; cursor_ = offset; size_t got = read(output, bytes); cursor_ = saved;
    return got == bytes;
}
bool ReadOnlyFile::seek(int64_t offset, int origin) {
    if (!usable()) return false;
    uint64_t base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? cursor_ : origin == SEEK_END ? size_ : UINT64_MAX;
    if (base == UINT64_MAX) return false;
    uint64_t target;
    if (offset < 0) { uint64_t magnitude = static_cast<uint64_t>(-(offset + 1)) + 1; if (magnitude > base) return false; target = base - magnitude; }
    else { if (static_cast<uint64_t>(offset) > size_ - base) return false; target = base + static_cast<uint64_t>(offset); }
    if (target > size_) return false;
    cursor_ = target; return true;
}
int ReadOnlyFile::duplicate_fd() const { return fd_ < 0 ? -1 : dup(fd_); }
std::vector<uint8_t> ReadOnlyFile::read_all(size_t limit, std::string& error) {
    if (size_ == 0 || size_ > limit) { error = size_ ? "File exceeds the memory limit" : "File is empty"; return {}; }
    std::vector<uint8_t> data(static_cast<size_t>(size_));
    if (!read_at(0, data.data(), data.size())) { error = "Read failed, cancelled, or storage disconnected"; return {}; }
    return data;
}
}
