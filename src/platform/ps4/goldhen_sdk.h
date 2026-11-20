#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ABI from the public GoldHEN Plugin SDK, pinned under vendor/goldhen-sdk.
// The SDK keeps the original process credentials and filesystem roots here.
typedef struct McGoldhenBackup {
    uint32_t cr_uid, cr_ruid, cr_rgid, cr_groups;
    uint64_t cr_paid, cr_caps[2];
    void* cr_prison;
    void* fd_cdir;
    void* fd_jdir;
    void* fd_rdir;
} McGoldhenBackup;

enum {
    MC_GOLDHEN_SDK_VERSION = 0x100,
    MC_GOLDHEN_VERSION = 0,
    MC_GOLDHEN_JAILBREAK = 2,
    MC_GOLDHEN_UNJAILBREAK = 3
};

// Only version and this process's access/restore commands are accepted.
// This is a safe ENOSYS stub on the host; it never executes a host syscall.
int mc_goldhen_sdk_command(uint64_t command, void* data);

// Interpret the raw syscall value and carry flag. Only the pinned version
// reply is accepted through the error channel; access/restore errors stay errors.
int mc_goldhen_sdk_result(uint64_t command, uint64_t result, int failed);

#ifdef __cplusplus
}
#endif
