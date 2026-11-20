#include "store.hpp"
#include "storage/read_only.hpp"
#include "storage/path_io.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#ifdef MC_PS4
#include <orbis/libkernel.h>
#include "platform/ps4/kernel_result.h"
#endif

namespace mc {
static std::string encode(const std::string& value) {
    static const char hex[] = "0123456789ABCDEF"; std::string out;
    for (unsigned char c : value) {
        if (c == '%' || c == '\t' || c == '\n' || c == '\r' || c < 32) { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out;
}
static std::string decode(const std::string& value) {
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c-'0' : c >= 'A' && c <= 'F' ? c-'A'+10 : -1; };
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size() && digit(value[i+1]) >= 0 && digit(value[i+2]) >= 0) {
            char c = static_cast<char>((digit(value[i+1]) << 4) | digit(value[i+2])); if (c) out += c; i += 2;
        } else out += value[i];
    }
    return out;
}
static int create_private_directory(const std::string& path) {
    // Validate the complete path before creating any component.
    if (path.size()<2 || path[0]!='/' || !valid_relative(path.substr(1))) { errno=EINVAL;return -1; }
#ifdef MC_PS4
    if (path!="/data/PS4MediaCenter") { errno=EACCES;return -1; }
    int parent=::open("/data",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if (parent<0) return -1;
    // mkdirat/renameat/unlinkat in the installed SDK are ENOSYS stubs.
    int result=mc_ps4_posix_result(sceKernelMkdir(path.c_str(),0700));
    if (result && errno!=EEXIST) { int saved=errno;::close(parent);errno=saved;return -1; }
    int fd=mc_open_child(parent,"/data","PS4MediaCenter",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC,0);
    int saved=errno;::close(parent);errno=saved;return fd;
#else
    int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC); size_t begin = 1;
    while (fd >= 0 && begin < path.size()) {
        size_t end = path.find('/', begin); if (end == std::string::npos) end = path.size();
        auto part = path.substr(begin, end - begin);
        if (part.empty() || part == "." || part == "..") { ::close(fd); return -1; }
        if (mkdirat(fd, part.c_str(), 0700) != 0 && errno != EEXIST) { int saved=errno;::close(fd);errno=saved;return -1; }
        int next = openat(fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        int saved=errno;::close(fd);errno=saved;fd = next; begin = end + 1;
    }
    return fd;
#endif
}
#ifdef MC_PS4
static bool private_path_matches(const std::string& root,int descriptor) {
    if (root!="/data/PS4MediaCenter" || !mc_directory_matches(descriptor,root.c_str())) {
        errno=ESTALE;return false;
    }
    return true;
}
#endif
static int replace_owned_state(const std::string& root,int descriptor) {
#ifdef MC_PS4
    if (!private_path_matches(root,descriptor)) return -1;
    return mc_ps4_posix_result(sceKernelRename((root+"/state.tmp").c_str(),(root+"/state.tsv").c_str()));
#else
    (void)root;return renameat(descriptor,"state.tmp",descriptor,"state.tsv");
#endif
}
static int remove_owned_state(const std::string& root,int descriptor,const char* name) {
    if (std::strcmp(name,"state.tsv") && std::strcmp(name,"state.tmp")) { errno=EINVAL;return -1; }
#ifdef MC_PS4
    if (!private_path_matches(root,descriptor)) return -1;
    return mc_ps4_posix_result(sceKernelUnlink((root+"/"+name).c_str()));
#else
    (void)root;return unlinkat(descriptor,name,0);
#endif
}
StateStore::StateStore(std::string root, const std::vector<Source>& sources) : root_(std::move(root)) {
    bool overlap = false;
    for (const auto& source : sources) if (root_ == source.root || (root_.size() > source.root.size() && root_.compare(0, source.root.size(), source.root) == 0 && root_[source.root.size()] == '/')) overlap = true;
    source_overlap_ = overlap;
    if (!overlap) directory_fd_ = create_private_directory(root_); enabled_ = directory_fd_ >= 0;
    if (!enabled_) error_ = "App data unavailable: " + std::string(std::strerror(errno));
    if (overlap) error_ = "App data path overlaps a media source; no directory created and saving disabled";
    thread_ = std::thread(&StateStore::run, this);
}
StateStore::~StateStore() {
    { std::lock_guard<std::mutex> lock(mutex_); quit_ = true; wake_.notify_all(); }
    thread_.join(); if (directory_fd_ >= 0) ::close(directory_fd_);
}
std::string StateStore::error() { std::lock_guard<std::mutex> lock(mutex_); return error_; }
void StateStore::protect_sources(const std::vector<Source>& sources) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& source : sources) {
        if (root_ == source.root || (root_.size() > source.root.size() && root_.compare(0, source.root.size(), source.root) == 0 && root_[source.root.size()] == '/')) {
            source_overlap_ = true;
            enabled_ = false; dirty_ = clear_ = false; error_ = "App data path overlaps a media source; saving disabled";
        }
    }
}
bool StateStore::retry_open() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (directory_fd_ >= 0 || source_overlap_) return false;
    // The worker cannot use a missing descriptor; install a recovered one only
    // while holding the mutex it uses to accept enabled state transactions.
    directory_fd_ = create_private_directory(root_);
    enabled_ = directory_fd_ >= 0;
    if (!enabled_) { error_ = "App data unavailable: " + std::string(std::strerror(errno)); return false; }
    error_.clear();
    return true;
}
SavedState StateStore::read() {
    SavedState result; if (directory_fd_ < 0) return result;
    int fd = mc_open_child(directory_fd_, root_.c_str(), "state.tsv", O_RDONLY | O_NOFOLLOW | O_CLOEXEC, 0);
    if (fd < 0) return result;
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > 2097152) { ::close(fd); return result; }
    std::string data(static_cast<size_t>(st.st_size), '\0'); size_t got = 0;
    while (got < data.size()) { ssize_t n = ::read(fd, &data[got], data.size() - got); if (n <= 0) break; got += static_cast<size_t>(n); }
    ::close(fd); data.resize(got); std::istringstream stream(data); std::string line;
    while (std::getline(stream, line)) {
        std::vector<std::string> fields; std::istringstream row(line); std::string field;
        while (std::getline(row, field, '\t')) fields.push_back(decode(field));
        if (fields.size() == 3 && fields[0] == "setting") {
            char* end = nullptr; long v = std::strtol(fields[2].c_str(), &end, 10); if (!end || *end) continue;
            auto& s = result.settings;
            if (fields[1] == "volume") s.volume = static_cast<int>(std::max(0L, std::min(100L, v)));
            if (fields[1] == "slideshow") s.slideshow_seconds = static_cast<int>(std::max(2L, std::min(30L, v)));
            if (fields[1] == "repeat") s.repeat = static_cast<int>(std::max(0L, std::min(2L, v)));
            if (fields[1] == "hidden") s.hidden = v != 0;
            if (fields[1] == "auto_next") s.auto_next = v != 0;
            if (fields[1] == "shuffle") s.shuffle = v != 0;
        } else if (fields.size() == 8 && fields[1].size() <= 4096 && fields[3].size() <= 16384) {
            if (fields[1].empty() || fields[1][0] != '/') continue;
            Item item; item.source.root = fields[1]; item.source.label = fields[2]; item.entry.relative = fields[3]; item.entry.name = fields[4];
            int k = std::atoi(fields[5].c_str()); if (k < 0 || k > static_cast<int>(Kind::Other)) continue;
            item.entry.kind = static_cast<Kind>(k); item.entry.size = std::strtoull(fields[6].c_str(), nullptr, 10); item.entry.modified = std::strtoll(fields[7].c_str(), nullptr, 10);
            auto* list = fields[0] == "favorite" ? &result.favorites : fields[0] == "last" ? &result.last_played
                : fields[0] == "music" ? &result.music_queue : fields[0] == "video" ? &result.video_queue : nullptr;
            size_t maximum = fields[0] == "last" ? 64 : fields[0] == "favorite" ? 512 : 256;
            if (list && list->size() < maximum) list->push_back(std::move(item));
        }
    }
    return result;
}
bool StateStore::write_state(const SavedState& state, std::string& error) {
    std::ostringstream out; const auto& s = state.settings;
    auto setting = [&](const char* key, int v) { out << "setting\t" << key << '\t' << v << '\n'; };
    setting("volume", s.volume); setting("slideshow", s.slideshow_seconds); setting("repeat", s.repeat);
    setting("hidden", s.hidden); setting("auto_next", s.auto_next); setting("shuffle", s.shuffle);
    auto list = [&](const char* kind, const std::vector<Item>& items) {
        for (const auto& item : items) out << kind << '\t' << encode(item.source.root) << '\t' << encode(item.source.label) << '\t'
            << encode(item.entry.relative) << '\t' << encode(item.entry.name) << '\t' << static_cast<int>(item.entry.kind) << '\t'
            << item.entry.size << '\t' << item.entry.modified << '\n';
    };
    list("favorite", state.favorites); list("last", state.last_played); list("music", state.music_queue); list("video", state.video_queue);
    auto data = out.str(); if (data.size() > 2097152) { error = "Saved library exceeds 2 MiB; saving skipped"; return false; }
    int fd = mc_open_child(directory_fd_, root_.c_str(), "state.tmp", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) { error = "Cannot write app data: " + std::string(std::strerror(errno)); return false; }
    size_t done = 0;
    while (done < data.size()) { ssize_t n = ::write(fd, data.data() + done, data.size() - done); if (n < 0 && errno == EINTR) continue; if (n <= 0) break; done += static_cast<size_t>(n); }
    bool ok = done == data.size() && fsync(fd) == 0; ::close(fd);
    if (ok) ok = replace_owned_state(root_,directory_fd_) == 0;
    if (!ok) { int saved=errno;remove_owned_state(root_,directory_fd_,"state.tmp");error="Saving app data failed: "+std::string(std::strerror(saved)); }
    return ok;
}
void StateStore::save(const SavedState& state) { std::lock_guard<std::mutex> lock(mutex_); pending_ = state; dirty_ = true; wake_.notify_one(); }
void StateStore::clear_owned_data() { std::lock_guard<std::mutex> lock(mutex_); clear_ = true; dirty_ = false; wake_.notify_one(); }
void StateStore::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        wake_.wait(lock, [&] { return quit_ || dirty_ || clear_; });
        bool enabled = enabled_, clear = clear_, dirty = dirty_, quitting = quit_;SavedState state = pending_;
        dirty_ = clear_ = false;lock.unlock();std::string error;
        // Small app-data transactions are serialized. Only these two owned names are ever removed.
        if (enabled && clear) {
            if (remove_owned_state(root_,directory_fd_,"state.tsv") && errno!=ENOENT) error="Cannot clear app data: "+std::string(std::strerror(errno));
            if (remove_owned_state(root_,directory_fd_,"state.tmp") && errno!=ENOENT) error="Cannot clear app data: "+std::string(std::strerror(errno));
        }
        else if (enabled && dirty) write_state(state, error);
        lock.lock();if (!error.empty()) error_ = std::move(error);if (quitting) break;
    }
}
bool is_favorite(const SavedState& state, const Item& item) { return std::find(state.favorites.begin(), state.favorites.end(), item) != state.favorites.end(); }
void toggle_favorite(SavedState& state, const Item& item) {
    auto it = std::find(state.favorites.begin(), state.favorites.end(), item);
    if (it != state.favorites.end()) state.favorites.erase(it); else if (state.favorites.size() < 512) state.favorites.push_back(item);
}
void record_played(SavedState& state, const Item& item) {
    auto it = std::find(state.last_played.begin(), state.last_played.end(), item);
    if (it != state.last_played.end()) state.last_played.erase(it);
    state.last_played.insert(state.last_played.begin(), item); if (state.last_played.size() > 64) state.last_played.resize(64);
}
}
