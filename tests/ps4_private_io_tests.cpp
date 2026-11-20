#include "platform/ps4/startup_trace.hpp"
#include "settings/store.hpp"
#include "storage/path_io.h"
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
unsigned assertions = 0, relative_calls = 0;
std::string fixture;
bool deny_app = false, deny_log = false;
void require(bool ok, const char* expression) { ++assertions; if (!ok) throw std::runtime_error(expression); }
#define REQUIRE(value) require(bool(value), #value)
std::string mapped(const char* path) {
    std::string value(path);
    if (value == "/data" || value.compare(0, 6, "/data/") == 0) return fixture + value;
    return value;
}
std::string contents(const std::string& path) {
    std::ifstream input(path); return std::string(std::istreambuf_iterator<char>(input), {});
}
}
extern "C" int mc_test_open(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list args; va_start(args, flags); mode = static_cast<mode_t>(va_arg(args, int)); va_end(args); }
    if ((deny_app && std::string(path) == "/data/PS4MediaCenter") || (deny_log && std::string(path) == "/data/PS4MediaCenter/startup.log")) {
        errno = EACCES; return -1;
    }
    return ::open(mapped(path).c_str(), flags, mode);
}
extern "C" int mc_test_openat(int, const char*, int, ...) { ++relative_calls; errno = EPERM; return -1; }
extern "C" int32_t sceKernelMkdir(const char* path, uint32_t mode) { return ::mkdir(mapped(path).c_str(), mode); }
extern "C" int32_t sceKernelRename(const char* from, const char* to) { return ::rename(mapped(from).c_str(), mapped(to).c_str()); }
extern "C" int32_t sceKernelUnlink(const char* path) { return ::unlink(mapped(path).c_str()); }
extern "C" uint64_t sceKernelGetProcessTime() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

int main(int argc, char** argv) {
    std::string project = argc > 1 ? argv[1] : ".";
    std::string pattern = project + "/build/ps4-private-io-XXXXXX";
    char* created = mkdtemp(&pattern[0]); if (!created) return 2; fixture = created;
    std::filesystem::create_directories(fixture + "/data");
    const std::string app = fixture + "/data/PS4MediaCenter";
    using Case = std::pair<const char*, std::function<void()>>;
    const std::vector<Case> cases = {
        {"startup_log_recovers_from_confirmed_openat_eperm", [&] {
            errno = EBUSY;
            { mc::StartupTrace trace; auto status = mc::startup_trace_status();
              REQUIRE(status.file_opened); REQUIRE(status.error == 0); REQUIRE(errno == EBUSY);
              mc::startup_trace("first-frame.present.ready", 7, "private log fixture"); }
            auto log = contents(app + "/startup.log");
            REQUIRE(log.find("main.enter") != std::string::npos);
            REQUIRE(log.find("first-frame.present.ready") != std::string::npos);
            REQUIRE(log.find("main.exit") != std::string::npos); REQUIRE(relative_calls >= 2);
        }},
        {"settings_read_write_and_clear_use_only_the_owned_paths", [&] {
            std::filesystem::create_directory(fixture + "/usb1");
            std::ofstream(fixture + "/usb1/untouched.txt") << "USB must remain unchanged";
            mc::Source usb; usb.root = fixture + "/usb1";
            { mc::StateStore store("/data/PS4MediaCenter", {usb}); REQUIRE(store.error().empty());
              mc::SavedState state; state.settings.volume = 37;
              state.favorites.push_back({usb, {"أغنية.mp3", "Music/أغنية.mp3", mc::Kind::Music, 12, 0}}); store.save(state); }
            REQUIRE(contents(app + "/state.tsv").find("Music/أغنية.mp3") != std::string::npos);
            { mc::StateStore store("/data/PS4MediaCenter", {usb}); auto state = store.read();
              REQUIRE(state.settings.volume == 37); REQUIRE(state.favorites.size() == 1);
              REQUIRE(state.favorites[0].entry.name == "أغنية.mp3"); store.clear_owned_data(); }
            REQUIRE(!std::filesystem::exists(app + "/state.tsv")); REQUIRE(!std::filesystem::exists(app + "/state.tmp"));
            REQUIRE(contents(fixture + "/usb1/untouched.txt") == "USB must remain unchanged");
            REQUIRE(std::filesystem::exists(app + "/startup.log"));
        }},
        {"fallback_rejects_a_replaced_parent_before_creating_a_file", [&] {
            int directory = ::open(app.c_str(), O_RDONLY | O_DIRECTORY); REQUIRE(directory >= 0);
            REQUIRE(::rename(app.c_str(), (app + "-old").c_str()) == 0); REQUIRE(::mkdir(app.c_str(), 0700) == 0);
            int fd = mc_open_child(directory, "/data/PS4MediaCenter", "state.tmp", O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
            REQUIRE(fd < 0); REQUIRE(errno == ESTALE); REQUIRE(!std::filesystem::exists(app + "/state.tmp"));
            ::close(directory); std::filesystem::remove(app); REQUIRE(::rename((app + "-old").c_str(), app.c_str()) == 0);
        }},
        {"fallback_rejects_symlinks_traversal_and_missing_no_follow", [&] {
            std::ofstream(fixture + "/protected.txt") << "protected";
            REQUIRE(::symlink((fixture + "/protected.txt").c_str(), (app + "/linked.log").c_str()) == 0);
            int directory = ::open(app.c_str(), O_RDONLY | O_DIRECTORY); REQUIRE(directory >= 0);
            REQUIRE(mc_open_child(directory, "/data/PS4MediaCenter", "linked.log", O_WRONLY | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600) < 0);
            REQUIRE(contents(fixture + "/protected.txt") == "protected");
            for (const char* name : {"../protected.txt", "state/../state.tmp", "..", ".", ""}) {
                REQUIRE(mc_open_child(directory, "/data/PS4MediaCenter", name, O_WRONLY | O_CREAT | O_NOFOLLOW, 0600) < 0);
                REQUIRE(errno == EINVAL);
            }
            REQUIRE(mc_open_child(directory, "/data/PS4MediaCenter", "state.tmp", O_WRONLY | O_CREAT, 0600) < 0);
            REQUIRE(mc_open_child(directory, "/data/../data/PS4MediaCenter", "state.tmp", O_WRONLY | O_CREAT | O_NOFOLLOW, 0600) < 0);
            REQUIRE(!std::filesystem::exists(app + "/state.tmp")); ::close(directory);
        }},
        {"startup_failure_reports_the_actual_absolute_path_error", [&] {
            deny_app = true;
            { mc::StartupTrace trace; auto status = mc::startup_trace_status(); REQUIRE(!status.file_opened);
              REQUIRE(status.error == EACCES); REQUIRE(std::string(status.operation) == "open app directory"); }
            deny_app = false; deny_log = true;
            { mc::StartupTrace trace; auto status = mc::startup_trace_status(); REQUIRE(!status.file_opened);
              REQUIRE(status.error == EACCES); REQUIRE(std::string(status.operation) == "open startup.log"); }
            deny_log = false;
        }},
        {"private_store_cannot_be_redirected_to_the_usb", [&] {
            mc::Source source; source.root = "/data";
            mc::StateStore store("/data/PS4MediaCenter", {source}); REQUIRE(!store.error().empty());
            REQUIRE(!store.retry_open());
            mc::SavedState state; store.save(state); store.clear_owned_data();
            REQUIRE(contents(fixture + "/usb1/untouched.txt") == "USB must remain unchanged");
            mc::StateStore elsewhere("/mnt/usb1/AppData"); REQUIRE(!elsewhere.error().empty());
        }},
        {"startup_log_and_settings_recover_after_process_access_is_enabled", [&] {
            deny_app = true;
            { mc::StartupTrace trace; mc::StateStore store("/data/PS4MediaCenter");
              REQUIRE(!mc::startup_trace_status().file_opened); REQUIRE(!store.error().empty());
              REQUIRE(!store.retry_open());
              deny_app = false;
              errno = EBUSY; mc::startup_trace_retry(); REQUIRE(errno == EBUSY);
              REQUIRE(mc::startup_trace_status().file_opened); REQUIRE(mc::startup_trace_status().error == 0);
              REQUIRE(store.retry_open()); REQUIRE(store.error().empty()); REQUIRE(!store.retry_open());
              mc::SavedState state; state.settings.volume = 49; store.save(state); }
            REQUIRE(contents(app + "/startup.log").find("usb.direct.log-reopened") != std::string::npos);
            { mc::StateStore store("/data/PS4MediaCenter"); REQUIRE(store.read().settings.volume == 49); store.clear_owned_data(); }
            REQUIRE(contents(fixture + "/usb1/untouched.txt") == "USB must remain unchanged");
        }}
    };
    unsigned failures = 0;
    std::ofstream report(project + "/reports/ps4-private-io-tests.json");
    report << "{\"hardware_tested\":false,\"target\":\"production PS4 logging/settings/path helpers with openat EPERM and mapped host handles\",\"tests\":[";
    for (size_t index = 0; index < cases.size(); ++index) {
        unsigned before = assertions; std::string error;
        try { cases[index].second(); } catch (const std::exception& exception) { error = exception.what(); ++failures; }
        std::printf("%s %s %s\n", error.empty() ? "PASS" : "FAIL", cases[index].first, error.c_str());
        if (index) report << ',';
        report << "{\"name\":\"" << cases[index].first << "\",\"passed\":" << (error.empty() ? "true" : "false") << ",\"assertions\":" << assertions - before << '}';
    }
    report << "],\"assertions\":" << assertions << ",\"passed\":" << (failures ? "false" : "true") << "}\n";
    std::filesystem::remove_all(fixture); return failures ? 1 : 0;
}
