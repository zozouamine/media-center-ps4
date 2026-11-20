#pragma once
#include <cstdint>

namespace mc {
#ifdef MC_PS4
// Best-effort, bounded diagnostics in the fixed private app-data directory.
// Construct this before the other main locals so workers stop before it closes.
class StartupTrace {
public:
    StartupTrace() noexcept;
    ~StartupTrace();
    StartupTrace(const StartupTrace&) = delete;
    StartupTrace& operator=(const StartupTrace&) = delete;
};
void startup_trace(const char* stage, int64_t result = 0, const char* detail = nullptr) noexcept;
struct StartupTraceStatus {
    bool file_opened;
    int error;
    const char* operation;
};
StartupTraceStatus startup_trace_status() noexcept;
void startup_trace_retry() noexcept;
#else
inline void startup_trace(const char*, int64_t = 0, const char* = nullptr) noexcept {}
inline void startup_trace_retry() noexcept {}
#endif
}
