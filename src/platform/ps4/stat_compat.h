#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <sys/mount.h>
#else
#include <sys/statfs.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
// FreeBSD 9 / Orbis kernel layout, independent of the SDK's incorrect mode_t.
typedef struct { int64_t seconds,nanoseconds; } McKernelTime;
typedef struct {
    uint32_t device,inode;
    uint16_t mode,links;
    uint32_t uid,gid,rdevice;
    McKernelTime access,modified,changed;
    int64_t size,blocks;
    uint32_t block_size,flags,generation,spare;
    McKernelTime birth;
} McKernelStat;
int mc_ps4_decode_stat(const void* bytes,size_t length,struct stat* output);

// FreeBSD 9 / Orbis statfs has an eight-byte fsid and 88-byte mount names.
// The SDK declares a four-byte fsid and the later 1024-byte name fields.
typedef struct {
    uint32_t version,type;
    uint64_t flags,block_size,io_size,blocks,free_blocks;
    int64_t available_blocks;
    uint64_t files;
    int64_t free_files;
    uint64_t sync_writes,async_writes,sync_reads,async_reads,spare[10];
    uint32_t name_max,owner;
    int32_t filesystem_id[2];
    char char_spare[80],filesystem_name[16],mount_from[88],mount_on[88];
} McKernelStatFs;
int mc_ps4_decode_statfs(const void* bytes,size_t length,struct statfs* output);
typedef char McVerifyStatAbi[(sizeof(McKernelStat)==120 && offsetof(McKernelStat,size)==72) ? 1 : -1];
typedef char McVerifyStatFsAbi[(sizeof(McKernelStatFs)==472 && offsetof(McKernelStatFs,mount_on)==384) ? 1 : -1];
#ifdef __cplusplus
}
static_assert(sizeof(McKernelStat)==120 && offsetof(McKernelStat,size)==0x48 && offsetof(McKernelStat,mode)==8,"Orbis stat ABI changed");
static_assert(sizeof(McKernelStatFs)==472 && offsetof(McKernelStatFs,filesystem_name)==280
    && offsetof(McKernelStatFs,mount_on)==384,"Orbis statfs ABI changed");
#endif
