#include "startup_trace.hpp"
#ifdef MC_PS4
#include "kernel_result.h"
#include "storage/path_io.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <mutex>
#include <orbis/libkernel.h>
#include <unistd.h>

namespace mc {
namespace {
std::atomic<int> trace_fd{-1};
std::atomic<unsigned> trace_records{0};
enum class TraceOperation { None, OpenData, CreateDirectory, OpenDirectory, OpenLog, Write, Flush };
std::atomic<int> trace_error{0};
std::atomic<TraceOperation> trace_operation{TraceOperation::None};
std::mutex trace_open_mutex;
constexpr int directory_flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
void trace_failure(TraceOperation operation, int error) noexcept {
    trace_operation.store(operation);
    trace_error.store(error);
}
void open_trace() noexcept {
    std::lock_guard<std::mutex> lock(trace_open_mutex);
    if (trace_fd.load() >= 0) return;
    int parent = ::open("/data", directory_flags);
    if (parent >= 0) {
        int made = mc_ps4_posix_result(sceKernelMkdir("/data/PS4MediaCenter", 0700));
        if (made == 0 || errno == EEXIST) {
            int directory = mc_open_child(parent, "/data", "PS4MediaCenter", directory_flags, 0);
            if (directory >= 0) {
                int fd = mc_open_child(directory, "/data/PS4MediaCenter", "startup.log",
                    O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
                const int open_error = errno;
                ::close(directory);
                trace_fd.store(fd);
                if (fd < 0) trace_failure(TraceOperation::OpenLog, open_error);
                else { trace_error.store(0);trace_operation.store(TraceOperation::None); }
            }
            else trace_failure(TraceOperation::OpenDirectory, errno);
        } else trace_failure(TraceOperation::CreateDirectory, errno);
        ::close(parent);
    } else trace_failure(TraceOperation::OpenData, errno);
}
}
StartupTrace::StartupTrace() noexcept {
    const int saved = errno;
    trace_records.store(0); trace_error.store(0); trace_operation.store(TraceOperation::None);
    open_trace();
    startup_trace("main.enter");
    errno = saved;
}
void startup_trace_retry() noexcept {
    const int saved = errno;
    if (trace_fd.load() < 0) {
        open_trace();
        startup_trace("usb.direct.log-reopened");
    }
    errno = saved;
}
StartupTrace::~StartupTrace() {
    startup_trace("main.exit");
    int fd = trace_fd.exchange(-1);
    if (fd >= 0) ::close(fd);
}
void startup_trace(const char* stage, int64_t result, const char* detail) noexcept {
    int fd = trace_fd.load();
    if (fd < 0 || trace_records.fetch_add(1) >= 512) return;
    const int saved = errno;
    char line[512];
    int length = std::snprintf(line, sizeof(line), "%llu us | %.96s | result=%lld (0x%llx) | %.240s\n",
        static_cast<unsigned long long>(sceKernelGetProcessTime()), stage ? stage : "unknown",
        static_cast<long long>(result), static_cast<unsigned long long>(result), detail ? detail : "");
    if (length > 0) {
        size_t bytes = static_cast<size_t>(length);
        if (bytes >= sizeof(line)) bytes = sizeof(line) - 1;
        size_t done = 0;
        while (done < bytes) {
            ssize_t written = ::write(fd, line + done, bytes - done);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { trace_failure(TraceOperation::Write, written < 0 ? errno : EIO); break; }
            done += static_cast<size_t>(written);
        }
        if (done == bytes && fsync(fd) < 0) trace_failure(TraceOperation::Flush, errno);
    }
    errno = saved;
}
StartupTraceStatus startup_trace_status() noexcept {
    const char* operation = "none";
    switch (trace_operation.load()) {
        case TraceOperation::OpenData: operation = "open /data"; break;
        case TraceOperation::CreateDirectory: operation = "create app directory"; break;
        case TraceOperation::OpenDirectory: operation = "open app directory"; break;
        case TraceOperation::OpenLog: operation = "open startup.log"; break;
        case TraceOperation::Write: operation = "write startup.log"; break;
        case TraceOperation::Flush: operation = "flush startup.log"; break;
        default: break;
    }
    return {trace_fd.load() >= 0, trace_error.load(), operation};
}
}
#endif
