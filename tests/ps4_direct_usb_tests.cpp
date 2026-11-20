#include "platform/ps4/usb_access.hpp"
#include "storage/read_only.hpp"
#include "storage/directory.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
unsigned assertions = 0, version_calls = 0, enable_calls = 0, restore_calls = 0, native_usb_opens = 0;
std::string fixture;
bool sdk_available = true, deny_enable = false, deny_restore = false;
bool version_carry = false;
bool usb_mounted = false, deny_parent_open = false;
bool unjailed = false, change_namespace = true, sandbox_usb_visible = false, deny_mount_listing = false;
int sdk_version = 0x100;
McGoldhenBackup expected_backup{};
void require(bool condition, const char* expression) {
    ++assertions;
    if (!condition) throw std::runtime_error(expression);
}
#define REQUIRE(value) require(bool(value), #value)
bool under(const std::string& path, const std::string& parent) {
    return path == parent || (path.size() > parent.size() && path.compare(0, parent.size(), parent) == 0 && path[parent.size()] == '/');
}
std::string native_path(const char* raw) {
    std::string path(raw);
    if (under(path, "/mnt")) {
        if (!unjailed || !change_namespace) { errno = ENOENT; return {}; }
        return fixture + path;
    }
    if (under(path, "/usb1")) {
        if (!sandbox_usb_visible) { errno = ENOENT; return {}; }
        return fixture + "/mnt" + path;
    }
    return path;
}
void reset() {
    REQUIRE(!mc::direct_usb_access_status().active);
    version_calls = enable_calls = restore_calls = native_usb_opens = 0;
    sdk_available = change_namespace = true;
    deny_enable = deny_restore = unjailed = sandbox_usb_visible = deny_mount_listing = false;
    sdk_version = 0x100;
    version_carry = false;
    usb_mounted = deny_parent_open = false;
    std::filesystem::remove_all(fixture + "/mnt");
    std::filesystem::create_directory(fixture + "/mnt");
}
void create_usb() {
    usb_mounted = true;
    std::filesystem::create_directories(fixture + "/mnt/usb1/Music");
    std::filesystem::create_directory(fixture + "/mnt/usb1/ملفات عربية");
    std::ofstream(fixture + "/mnt/usb1/Music/أغنية.wav") << "native usb fixture";
}
std::vector<mc::Source> scan(std::vector<std::string>& diagnostics, const std::atomic<bool>* cancel = nullptr) {
    return mc::discover_sources("/mnt", {"/usb1"}, &diagnostics, cancel);
}
}

extern "C" int mc_goldhen_sdk_command(uint64_t command, void* data) {
    if (command == MC_GOLDHEN_VERSION) {
        ++version_calls; REQUIRE(data == nullptr);
        if (!sdk_available) { errno = ENOSYS; return -1; }
        return mc_goldhen_sdk_result(command, static_cast<uint64_t>(sdk_version), version_carry);
    }
    REQUIRE(data != nullptr);
    if (command == MC_GOLDHEN_JAILBREAK) {
        ++enable_calls;
        REQUIRE(!unjailed);
        if (deny_enable) { errno = EPERM; return -1; }
        auto* backup = static_cast<McGoldhenBackup*>(data);
        REQUIRE(backup->cr_uid == 0 && backup->fd_rdir == nullptr);
        expected_backup = {};
        expected_backup.cr_uid = 101;
        expected_backup.cr_ruid = 102;
        expected_backup.cr_rgid = 103;
        expected_backup.cr_groups = 104;
        expected_backup.cr_paid = UINT64_C(0x3800000000000011);
        expected_backup.cr_caps[0] = 105;
        expected_backup.cr_caps[1] = 106;
        expected_backup.cr_prison = reinterpret_cast<void*>(uintptr_t(107));
        expected_backup.fd_cdir = reinterpret_cast<void*>(uintptr_t(108));
        expected_backup.fd_jdir = reinterpret_cast<void*>(uintptr_t(109));
        expected_backup.fd_rdir = reinterpret_cast<void*>(uintptr_t(110));
        *backup = expected_backup;
        unjailed = true;
        return 0;
    }
    REQUIRE(command == MC_GOLDHEN_UNJAILBREAK);
    ++restore_calls;
    REQUIRE(std::memcmp(data, &expected_backup, sizeof(expected_backup)) == 0);
    if (deny_restore) { errno = EPERM; return -1; }
    unjailed = false;
    return 0;
}

extern "C" int mc_test_usb_open(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args; va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int)); va_end(args);
    }
    std::string name(path);
    if (under(name, "/mnt") || under(name, "/usb1")) {
        ++native_usb_opens;
        REQUIRE((flags & O_ACCMODE) == O_RDONLY);
        REQUIRE(!(flags & (O_CREAT | O_TRUNC | O_APPEND)));
        REQUIRE(flags & O_NOFOLLOW);
    }
    if (deny_parent_open && name == "/mnt") { errno = EACCES; return -1; }
    auto mapped = native_path(path);
    return mapped.empty() ? -1 : ::open(mapped.c_str(), flags, mode);
}
extern "C" DIR* mc_test_usb_opendir(const char* path) {
    if (deny_mount_listing && std::string(path) == "/mnt") { errno = EACCES; return nullptr; }
    auto mapped = native_path(path);
    return mapped.empty() ? nullptr : ::opendir(mapped.c_str());
}
namespace mc {
void startup_trace(const char*, int64_t, const char*) noexcept {}
void startup_trace_retry() noexcept {}
}

extern "C" int mc_test_usb_fstat(int fd, struct stat* output) {
    int result = ::fstat(fd, output);
    if (result == 0) {
        const int saved = errno;
        char path[PATH_MAX]{};
        // Host fixtures are directories on one filesystem. Model the distinct
        // USB mount device while keeping real handles, inode identity and reads.
        if (usb_mounted && ::fcntl(fd, F_GETPATH, path) == 0 && under(path, fixture + "/mnt/usb1"))
            ++output->st_dev;
        errno = saved;
    }
    return result;
}

int main(int argc, char** argv) {
    std::string project = argc > 1 ? argv[1] : ".";
    std::string pattern = project + "/build/ps4-direct-usb-XXXXXX";
    char* created = mkdtemp(&pattern[0]); if (!created) return 2; fixture = created;
    using Case = std::pair<const char*, std::function<void()>>;
    const std::vector<Case> cases = {
        {"known_sdk_version_is_accepted_with_either_carry_flag", [] {
            reset();
            for (int carry : {0, 1}) {
                errno = EBUSY;
                REQUIRE(mc_goldhen_sdk_result(MC_GOLDHEN_VERSION, 0x100, carry) == 0x100);
                REQUIRE(errno == EBUSY);
            }
        }},
        {"version_reply_exception_does_not_hide_real_access_restore_or_sdk_errors", [] {
            reset();
            for (uint64_t command : {uint64_t(MC_GOLDHEN_JAILBREAK), uint64_t(MC_GOLDHEN_UNJAILBREAK)}) {
                errno = 0; REQUIRE(mc_goldhen_sdk_result(command, 0x100, 1) == -1); REQUIRE(errno == 256);
                errno = 0; REQUIRE(mc_goldhen_sdk_result(command, EPERM, 1) == -1); REQUIRE(errno == EPERM);
            }
            for (uint64_t error : {uint64_t(ENOSYS), uint64_t(EPERM), UINT64_C(0x200)}) {
                errno = 0; REQUIRE(mc_goldhen_sdk_result(MC_GOLDHEN_VERSION, error, 1) == -1);
                REQUIRE(errno == static_cast<int>(error));
            }
            errno = 0; REQUIRE(mc_goldhen_sdk_result(MC_GOLDHEN_UNJAILBREAK, 0, 1) == -1); REQUIRE(errno == EIO);
        }},
        {"reported_v8_errno_256_reply_allows_real_native_usb_discovery", [] {
            reset(); create_usb(); version_carry = true;
            { mc::DirectUsbAccessScope scope; std::vector<std::string> diagnostics;
              auto sources = scan(diagnostics); REQUIRE(sources.size() == 1); REQUIRE(sources[0].root == "/mnt/usb1");
              auto status = mc::direct_usb_access_status(); REQUIRE(status.sdk_version == 0x100);
              REQUIRE(status.stage == mc::UsbAccessStage::Enabled); REQUIRE(status.error == 0);
              REQUIRE(version_calls == 1); REQUIRE(enable_calls == 1);
              mc::ReadOnlyFile file; std::string error; REQUIRE(file.open(sources[0], "Music/أغنية.wav", error));
              auto bytes = file.read_all(128, error); REQUIRE(std::string(bytes.begin(), bytes.end()) == "native usb fixture"); }
            REQUIRE(restore_calls == 1); REQUIRE(!unjailed);
        }},
        {"unmounted_usb_slot_directories_are_not_storage_sources", [] {
            reset();
            for (unsigned slot = 0; slot < 8; ++slot)
                std::filesystem::create_directory(fixture + "/mnt/usb" + std::to_string(slot));
            mc::DirectUsbAccessScope scope; std::vector<std::string> diagnostics;
            REQUIRE(scan(diagnostics).empty());
            REQUIRE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const std::string& line) {
                return line == "/mnt/usb1 | No mounted USB filesystem";
            }));
            create_usb(); auto sources = scan(diagnostics);
            REQUIRE(sources.size() == 1); REQUIRE(sources[0].root == "/mnt/usb1");
            REQUIRE(version_calls == 1 && enable_calls == 1);
        }},
        {"mounted_empty_usb_is_detected_without_requiring_media_files", [] {
            reset(); usb_mounted = true;
            std::filesystem::create_directory(fixture + "/mnt/usb1");
            mc::DirectUsbAccessScope scope; std::vector<std::string> diagnostics;
            auto sources = scan(diagnostics); REQUIRE(sources.size() == 1);
            std::atomic<bool> cancel{false}; mc::PageRequest request; request.source = sources[0];
            auto page = mc::scan_page(request, cancel); REQUIRE(page.error.empty()); REQUIRE(page.entries.empty());
        }},
        {"readable_child_survives_denied_parent_metadata_and_listing", [] {
            reset(); create_usb(); deny_parent_open = deny_mount_listing = true;
            mc::DirectUsbAccessScope scope; std::vector<std::string> diagnostics;
            auto sources = scan(diagnostics); REQUIRE(sources.size() == 1); REQUIRE(sources[0].root == "/mnt/usb1");
        }},
        {"unavailable_sdk_never_changes_process_credentials", [] {
            reset(); sdk_available = false; mc::DirectUsbAccess access;
            errno = EBUSY; REQUIRE(!access.prepare()); REQUIRE(errno == EBUSY);
            auto status = access.status(); REQUIRE(status.stage == mc::UsbAccessStage::SdkUnavailable);
            REQUIRE(status.error == ENOSYS); REQUIRE(!status.active);
            REQUIRE(access.restore()); REQUIRE(enable_calls == 0); REQUIRE(restore_calls == 0);
        }},
        {"unknown_sdk_abi_is_rejected_before_the_backup_buffer_is_used", [] {
            reset(); mc::DirectUsbAccess access;
            for (int version : {0, 0x200, 0x10000}) {
                sdk_version = version; REQUIRE(!access.prepare());
                REQUIRE(access.status().stage == mc::UsbAccessStage::UnsupportedSdk);
                REQUIRE(!access.status().active);
            }
            REQUIRE(enable_calls == 0); REQUIRE(restore_calls == 0);
        }},
        {"access_denial_is_reported_and_can_be_retried_on_refresh", [] {
            reset(); deny_enable = true; mc::DirectUsbAccess access;
            REQUIRE(!access.prepare()); REQUIRE(access.status().error == EPERM);
            REQUIRE(access.status().stage == mc::UsbAccessStage::EnableFailed);
            REQUIRE(access.restore()); REQUIRE(restore_calls == 0);
            deny_enable = false; REQUIRE(access.prepare()); REQUIRE(access.status().active);
            REQUIRE(access.restore()); REQUIRE(!unjailed);
        }},
        {"successful_access_keeps_and_restores_the_exact_original_backup", [] {
            reset(); mc::DirectUsbAccess access;
            sdk_version = 0x101; REQUIRE(access.prepare()); REQUIRE(access.prepare());
            REQUIRE(version_calls == 1); REQUIRE(enable_calls == 1); REQUIRE(access.status().sdk_version == 0x101);
            REQUIRE(access.restore()); REQUIRE(access.restore()); REQUIRE(restore_calls == 1);
            REQUIRE(!access.status().active); REQUIRE(access.status().stage == mc::UsbAccessStage::Restored);
        }},
        {"failed_restore_retains_the_backup_for_a_later_successful_restore", [] {
            reset(); mc::DirectUsbAccess access; REQUIRE(access.prepare());
            deny_restore = true; REQUIRE(!access.restore()); REQUIRE(access.status().active);
            REQUIRE(access.status().error == EPERM);
            deny_restore = false; REQUIRE(access.restore()); REQUIRE(!access.status().active);
        }},
        {"hidden_usb_becomes_visible_and_uses_real_native_read_only_handles", [] {
            reset(); create_usb();
            mc::Source before; std::string error;
            REQUIRE(!mc::source_from_path("/mnt/usb1", before, error)); REQUIRE(errno == ENOENT);
            { mc::DirectUsbAccessScope scope; std::vector<std::string> diagnostics;
              auto sources = scan(diagnostics); REQUIRE(sources.size() == 1); REQUIRE(sources[0].root == "/mnt/usb1");
              REQUIRE(version_calls == 1 && enable_calls == 1);
              REQUIRE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const std::string& text) { return text.find("GoldHEN process access enabled") != std::string::npos; }));
              REQUIRE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const std::string& text) { return text.find("/mnt/usb1 | Readable") == 0; }));
              mc::ReadOnlyFile file; REQUIRE(file.open(sources[0], "Music/أغنية.wav", error));
              auto bytes = file.read_all(128, error); REQUIRE(std::string(bytes.begin(), bytes.end()) == "native usb fixture");
              mc::PageRequest request; request.source = sources[0]; std::atomic<bool> cancel{false};
              auto page = mc::scan_page(request, cancel); REQUIRE(page.error.empty()); REQUIRE(page.entries.size() == 2);
              REQUIRE(std::any_of(page.entries.begin(), page.entries.end(), [](const mc::Entry& entry) { return entry.name == "ملفات عربية"; }));
              REQUIRE(native_usb_opens > 0); }
            REQUIRE(restore_calls == 1); REQUIRE(!unjailed);
        }},
        {"refresh_finds_a_new_usb_without_overwriting_the_credential_backup", [] {
            reset();
            { mc::DirectUsbAccessScope scope; std::vector<std::string> diagnostics;
              REQUIRE(scan(diagnostics).empty()); create_usb(); REQUIRE(scan(diagnostics).size() == 1);
              REQUIRE(scan(diagnostics).size() == 1); REQUIRE(version_calls == 1); REQUIRE(enable_calls == 1); }
            REQUIRE(restore_calls == 1);
        }},
        {"sdk_failure_still_allows_a_readable_title_visible_usb", [] {
            reset(); create_usb(); sdk_available = false; sandbox_usb_visible = true;
            mc::DirectUsbAccessScope scope; std::vector<std::string> diagnostics;
            auto sources = scan(diagnostics); REQUIRE(sources.size() == 1); REQUIRE(sources[0].root == "/usb1");
            REQUIRE(enable_calls == 0); REQUIRE(!mc::direct_usb_access_status().active);
        }},
        {"sdk_success_does_not_report_an_unreadable_mount_as_a_source", [] {
            reset(); create_usb(); change_namespace = false;
            mc::DirectUsbAccessScope scope; std::vector<std::string> diagnostics;
            REQUIRE(scan(diagnostics).empty());
            REQUIRE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const std::string& text) { return text.find("/mnt/usb1 | open: errno=2") == 0; }));
        }},
        {"already_cancelled_discovery_does_not_request_process_access", [] {
            reset(); create_usb(); std::atomic<bool> cancel{true}; std::vector<std::string> diagnostics;
            REQUIRE(scan(diagnostics, &cancel).empty()); REQUIRE(version_calls == 0); REQUIRE(enable_calls == 0);
        }},
        {"direct_child_probes_work_when_the_mount_parent_cannot_be_listed", [] {
            reset(); create_usb(); deny_mount_listing = true;
            mc::DirectUsbAccessScope scope; std::vector<std::string> diagnostics;
            auto sources = scan(diagnostics); REQUIRE(sources.size() == 1); REQUIRE(sources[0].root == "/mnt/usb1");
        }},
        {"native_reads_reject_escape_links_and_replaced_usb_identity", [] {
            reset(); create_usb();
            std::ofstream(fixture + "/protected.txt") << "untouched";
            REQUIRE(::symlink((fixture + "/protected.txt").c_str(), (fixture + "/mnt/usb1/escape.wav").c_str()) == 0);
            mc::DirectUsbAccessScope scope; std::vector<std::string> diagnostics;
            auto sources = scan(diagnostics); REQUIRE(sources.size() == 1);
            mc::ReadOnlyFile file; std::string error;
            REQUIRE(!file.open(sources[0], "escape.wav", error)); REQUIRE(!file.open(sources[0], "../protected.txt", error));
            REQUIRE(::rename((fixture + "/mnt/usb1").c_str(), (fixture + "/mnt/offline").c_str()) == 0);
            std::filesystem::create_directory(fixture + "/mnt/usb1");
            REQUIRE(!mc::source_present(sources[0])); REQUIRE(!file.open(sources[0], "Music/أغنية.wav", error));
            std::ifstream protected_file(fixture + "/protected.txt"); std::string content; protected_file >> content; REQUIRE(content == "untouched");
        }},
        {"native_title_and_global_aliases_are_deduplicated_by_identity", [] {
            reset(); create_usb(); sandbox_usb_visible = true;
            mc::DirectUsbAccessScope scope; std::vector<std::string> diagnostics;
            auto sources = scan(diagnostics); REQUIRE(sources.size() == 1); REQUIRE(mc::source_present(sources[0]));
        }}
    };
    unsigned failures = 0;
    std::ofstream report(project + "/reports/ps4-direct-usb-tests.json");
    report << "{\"hardware_tested\":false,\"target\":\"production PS4 discovery with simulated GoldHEN namespace/mount devices and real host read-only handles\",\"tests\":[";
    for (size_t index = 0; index < cases.size(); ++index) {
        unsigned before = assertions; std::string error;
        try { cases[index].second(); } catch (const std::exception& exception) { error = exception.what(); ++failures; }
        std::printf("%s %s %s\n", error.empty() ? "PASS" : "FAIL", cases[index].first, error.c_str());
        if (index) report << ',';
        report << "{\"name\":\"" << cases[index].first << "\",\"passed\":" << (error.empty() ? "true" : "false") << ",\"assertions\":" << assertions - before << '}';
    }
    report << "],\"assertions\":" << assertions << ",\"passed\":" << (failures ? "false" : "true") << "}\n";
    std::filesystem::remove_all(fixture);
    return failures ? 1 : 0;
}
