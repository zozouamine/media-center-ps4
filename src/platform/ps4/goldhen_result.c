#include "goldhen_sdk.h"
#include <errno.h>

int mc_goldhen_sdk_result(uint64_t command, uint64_t result, int failed) {
    // The user's v8 console returned the SDK version 0x100 with carry set,
    // which the generic POSIX conversion reported as -1 / errno=256. Accept
    // this exact known version only for the read-only version query. Never
    // reinterpret this value for a credential-changing or restore command.
    if (command == MC_GOLDHEN_VERSION && result == MC_GOLDHEN_SDK_VERSION)
        return MC_GOLDHEN_SDK_VERSION;
    if (failed) {
        errno = result > 0 && result <= 4095 ? (int)result : EIO;
        return -1;
    }
    return (int32_t)result;
}
