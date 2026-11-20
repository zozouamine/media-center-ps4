// The prebuilt chrono object uses Linux CLOCK_MONOTONIC (1) on FreeBSD.
// Supply its four entry points so its clock implementation is not linked in.
#ifdef MC_PS4
#include <chrono>
#include <system_error>
#include <time.h>
#include <orbis/libkernel.h>
#include "kernel_result.h"

// FFmpeg also calls the POSIX clock API. Translate the musl header's IDs
// explicitly; the prebuilt SDK implementation uses incompatible native IDs.
extern "C" int clock_gettime(clockid_t clock, struct timespec* value) {
    if (!value) { errno=EFAULT;return -1; }
    if (clock==CLOCK_MONOTONIC || clock==CLOCK_MONOTONIC_RAW || clock==CLOCK_MONOTONIC_COARSE) {
        uint64_t us=sceKernelGetProcessTime();
        value->tv_sec=static_cast<time_t>(us/1000000);
        value->tv_nsec=static_cast<long>((us%1000000)*1000);return 0;
    }
    if (clock==CLOCK_REALTIME || clock==CLOCK_REALTIME_COARSE) {
        OrbisKernelTimeval time{};
        if (mc_ps4_posix_result(sceKernelGettimeofday(&time))) return -1;
        value->tv_sec=time.tv_sec;value->tv_nsec=time.tv_usec*1000;return 0;
    }
    errno=EINVAL;return -1;
}

_LIBCPP_BEGIN_NAMESPACE_STD
namespace chrono {
steady_clock::time_point steady_clock::now() _NOEXCEPT {
    return time_point(duration_cast<duration>(microseconds(sceKernelGetProcessTime())));
}
system_clock::time_point system_clock::now() _NOEXCEPT {
    OrbisKernelTimeval value{};
    if (mc_ps4_posix_result(sceKernelGettimeofday(&value)))
        __throw_system_error(errno,"Cannot read PS4 realtime clock");
    return time_point(duration_cast<duration>(seconds(value.tv_sec)+microseconds(value.tv_usec)));
}
time_t system_clock::to_time_t(const time_point& value) _NOEXCEPT {
    return static_cast<time_t>(duration_cast<seconds>(value.time_since_epoch()).count());
}
system_clock::time_point system_clock::from_time_t(time_t value) _NOEXCEPT {
    return time_point(duration_cast<duration>(seconds(value)));
}
}
_LIBCPP_END_NAMESPACE_STD
#endif
