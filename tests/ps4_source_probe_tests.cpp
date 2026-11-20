#include "storage/read_only.hpp"
#include "platform/ps4/startup_trace.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
unsigned assertions = 0;
void check(bool value, const char* expression) {
    ++assertions;
    if (!value) throw std::runtime_error(expression);
}
#define REQUIRE(value) check(bool(value), #value)

struct Fixture {
    std::string root, mount, missing, file, link, arabic;
    explicit Fixture(const std::string& project) {
        std::string pattern = project + "/build/ps4-source-probe-XXXXXX";
        char* directory = mkdtemp(&pattern[0]);
        if (!directory) throw std::runtime_error("Cannot create temporary source-probe fixture");
        root = directory; mount = root + "/usb1"; missing = root + "/unmapped-usb1";
        file = root + "/ordinary-file.txt"; link = root + "/usb-link"; arabic = root + "/ملفات عربية";
        if (mkdir(mount.c_str(), 0700) || mkdir(arabic.c_str(), 0700) || symlink(mount.c_str(), link.c_str()))
            throw std::runtime_error("Cannot create source-probe directories");
        std::ofstream output(file); output << "source-probe fixture\n";
    }
    ~Fixture() { unlink(link.c_str()); unlink(file.c_str()); rmdir(arabic.c_str()); rmdir(mount.c_str()); rmdir(root.c_str()); }
};
void require_closed(int fd) {
    errno = 0;
    REQUIRE(fcntl(fd, F_GETFD) == -1);
    REQUIRE(errno == EBADF);
}
}

namespace mc {
// The production source branch uses real host descriptors in this simulation.
// Console startup logging and console syscalls are not simulated.
void startup_trace(const char*, int64_t, const char*) noexcept {}
void startup_trace_retry() noexcept {}
}

int main(int argc, char** argv) {
    const std::string project = argc > 1 ? argv[1] : ".";
    Fixture fixture(project);
    using Case = std::pair<const char*, std::function<void()>>;
    const std::vector<Case> cases = {
        {"missing_mount_preserves_posix_enoent", [&] {
            mc::Source source; std::string error;
            errno = EACCES;
            REQUIRE(!mc::source_from_path(fixture.missing, source, error));
            REQUIRE(error.find("open: errno=2") == 0);
            REQUIRE(errno == ENOENT);
        }},
        {"ordinary_file_cannot_be_used_as_a_source", [&] {
            mc::Source source; std::string error;
            REQUIRE(!mc::source_from_path(fixture.file, source, error));
            REQUIRE(errno == ENOTDIR);
            REQUIRE(error.find("open: errno=" + std::to_string(ENOTDIR)) == 0);
            std::ifstream input(fixture.file); std::string line; std::getline(input, line);
            REQUIRE(line == "source-probe fixture");
        }},
        {"source_symlink_is_rejected_without_following_it", [&] {
            mc::Source source; std::string error;
            REQUIRE(!mc::source_from_path(fixture.link, source, error));
            REQUIRE(errno == ELOOP || errno == ENOTDIR);
            REQUIRE(error.find("open: errno=") == 0);
            struct stat st{}; REQUIRE(lstat(fixture.link.c_str(), &st) == 0);
            REQUIRE(S_ISLNK(st.st_mode));
        }},
        {"descriptor_zero_is_a_valid_source_handle_and_is_closed", [&] {
            struct RestoreStdin {
                int saved = dup(STDIN_FILENO);
                ~RestoreStdin() { if (saved >= 0) { dup2(saved, STDIN_FILENO); close(saved); } }
            } original;
            REQUIRE(original.saved >= 0);
            REQUIRE(close(STDIN_FILENO) == 0);
            mc::Source source; std::string error;
            REQUIRE(mc::source_from_path(fixture.mount, source, error));
            REQUIRE(source.root == fixture.mount); REQUIRE(error.empty());
            require_closed(STDIN_FILENO);
        }},
        {"readable_source_records_identity_and_clears_old_error", [&] {
            struct stat expected{}; REQUIRE(stat(fixture.mount.c_str(), &expected) == 0);
            mc::Source source; std::string error = "old error";
            REQUIRE(mc::source_from_path(fixture.mount, source, error));
            REQUIRE(error.empty()); REQUIRE(source.root == fixture.mount);
            REQUIRE(source.device == static_cast<uint64_t>(expected.st_dev));
            REQUIRE(source.inode == static_cast<uint64_t>(expected.st_ino));
        }},
        {"arabic_source_paths_remain_readable_with_the_english_profile_label", [&] {
            mc::Source source; std::string error;
            REQUIRE(mc::source_from_path(fixture.arabic, source, error));
            REQUIRE(source.root == fixture.arabic); REQUIRE(error.empty());
            std::vector<std::string> diagnostics;
            auto sources = mc::discover_sources(fixture.missing, {fixture.arabic}, &diagnostics);
            REQUIRE(sources.size() == 1); REQUIRE(sources.front().root == fixture.arabic);
            REQUIRE(!diagnostics.empty());
            REQUIRE(diagnostics.front() == "SELF access profile: default");
        }}
    };
    unsigned failures = 0;
    std::ofstream report(project + "/reports/ps4-source-probe-tests.json");
    report << "{\n  \"target\": \"host / production PS4 source-probe branch; no native fallback or libkernel mock\",\n"
           << "  \"hardware_tested\": false,\n  \"tests\": [\n";
    for (size_t index = 0; index < cases.size(); ++index) {
        unsigned before = assertions; std::string error;
        try { cases[index].second(); } catch (const std::exception& exception) { error = exception.what(); ++failures; }
        std::printf("%s %s%s%s\n", error.empty() ? "PASS" : "FAIL", cases[index].first, error.empty() ? "" : " — ", error.c_str());
        report << "    {\"name\": \"" << cases[index].first << "\", \"passed\": " << (error.empty() ? "true" : "false")
               << ", \"assertions\": " << assertions - before << "}" << (index + 1 == cases.size() ? "\n" : ",\n");
    }
    report << "  ],\n  \"passed\": " << (failures ? "false" : "true") << ",\n  \"assertions\": " << assertions
           << ",\n  \"failed\": " << failures << "\n}\n";
    std::printf("%zu tests, %u assertions, %u failures\n", cases.size(), assertions, failures);
    return failures ? 1 : 0;
}
