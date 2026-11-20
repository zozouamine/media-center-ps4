/*
 * GoldHEN SDK syscall adapter, derived from the public MIT-licensed SDK.
 * See vendor/goldhen-sdk/source/Syscall.c and licenses/GoldHEN-SDK-LICENSE.txt.
 */
#include "goldhen_sdk.h"
#include <errno.h>

int mc_goldhen_sdk_command(uint64_t command, void* data) {
    if ((command != MC_GOLDHEN_VERSION && command != MC_GOLDHEN_JAILBREAK
         && command != MC_GOLDHEN_UNJAILBREAK)
        || (command == MC_GOLDHEN_VERSION ? data != 0 : data == 0)) {
        errno = EINVAL;
        return -1;
    }
#ifdef MC_PS4
    // Match orbis_syscall(500, command, data): indirect syscall 0, with the
    // requested number in rdi. Use libc errno without a new loader import.
    uint64_t result = 0;
    unsigned char failed;
    __asm__ volatile("syscall"
        : "+a"(result), "=@ccc"(failed)
        : "D"(UINT64_C(500)), "S"(command), "d"(data)
        : "rcx", "r11", "memory");
    return mc_goldhen_sdk_result(command, result, failed);
#else
    errno = ENOSYS;
    return -1;
#endif
}
