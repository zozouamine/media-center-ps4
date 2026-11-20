#include "platform.hpp"
#include <time.h>
#ifdef MC_PS4
#include <orbis/libkernel.h>
#endif
namespace mc {
double monotonic_now() {
#ifdef MC_PS4
    // SDK time.h uses Linux clock IDs; use the native process clock on Orbis.
    return static_cast<double>(sceKernelGetProcessTime())/1e6;
#else
    timespec t{};
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) / 1e9;
#endif
}
}
