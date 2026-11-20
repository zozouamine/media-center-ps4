#pragma once
#include <errno.h>
#include <stdint.h>

// SCE APIs return 0x8002xxxx; POSIX imports return -1 and set errno themselves.
static inline int mc_ps4_posix_result(int32_t result) {
    if (result == 0) return 0;
    if (((uint32_t)result & 0xffff0000u) == 0x80020000u) errno = (int)((uint32_t)result & 0xffffu);
    else if (result != -1) errno = EIO;
    return -1;
}

// Descriptor-returning APIs succeed with any nonnegative value, including fd 0.
// Do not pass a successful sceKernelOpen descriptor to the status-only adapter.
static inline int mc_ps4_fd_result(int32_t result) {
    return result >= 0 ? result : mc_ps4_posix_result(result);
}
