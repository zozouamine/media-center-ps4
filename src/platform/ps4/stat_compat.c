#include "stat_compat.h"
#include <errno.h>
#include <string.h>
#include "kernel_result.h"

int mc_ps4_decode_stat(const void* bytes,size_t length,struct stat* output) {
    McKernelStat kernel;
    if (!bytes || !output || length<sizeof(kernel)) { errno=EINVAL;return -1; }
    memcpy(&kernel,bytes,sizeof(kernel));
    memset(output,0,sizeof(*output));
    output->st_dev=kernel.device;output->st_ino=kernel.inode;output->st_mode=kernel.mode;output->st_nlink=kernel.links;
    output->st_uid=kernel.uid;output->st_gid=kernel.gid;output->st_rdev=kernel.rdevice;
    output->st_size=kernel.size;output->st_blocks=kernel.blocks;output->st_blksize=kernel.block_size;
#if defined(__APPLE__)
    output->st_atimespec.tv_sec=kernel.access.seconds;output->st_atimespec.tv_nsec=kernel.access.nanoseconds;
    output->st_mtimespec.tv_sec=kernel.modified.seconds;output->st_mtimespec.tv_nsec=kernel.modified.nanoseconds;
    output->st_ctimespec.tv_sec=kernel.changed.seconds;output->st_ctimespec.tv_nsec=kernel.changed.nanoseconds;
    output->st_birthtimespec.tv_sec=kernel.birth.seconds;output->st_birthtimespec.tv_nsec=kernel.birth.nanoseconds;
#else
    output->st_atim.tv_sec=kernel.access.seconds;output->st_atim.tv_nsec=kernel.access.nanoseconds;
    output->st_mtim.tv_sec=kernel.modified.seconds;output->st_mtim.tv_nsec=kernel.modified.nanoseconds;
    output->st_ctim.tv_sec=kernel.changed.seconds;output->st_ctim.tv_nsec=kernel.changed.nanoseconds;
#if defined(MC_PS4) || defined(__FreeBSD__)
    output->st_birthtim.tv_sec=kernel.birth.seconds;output->st_birthtim.tv_nsec=kernel.birth.nanoseconds;
#endif
#endif
#if defined(MC_PS4) || defined(__APPLE__) || defined(__FreeBSD__)
    output->st_flags=kernel.flags;output->st_gen=kernel.generation;
#endif
    return 0;
}

#if defined(MC_PS4) || defined(__APPLE__) || defined(__FreeBSD__)
static void copy_statfs_string(char* output,size_t capacity,const char* source,size_t length) {
    size_t count=strnlen(source,length);
    if (count==length) --count;
    if (count>=capacity) count=capacity-1;
    memcpy(output,source,count);output[count]=0;
}
#endif
int mc_ps4_decode_statfs(const void* bytes,size_t length,struct statfs* output) {
    McKernelStatFs kernel;
    if (!bytes || !output || length<sizeof(kernel)) { errno=EINVAL;return -1; }
    memcpy(&kernel,bytes,sizeof(kernel));
    memset(output,0,sizeof(*output));
    output->f_bsize=kernel.block_size;
    output->f_blocks=kernel.blocks;output->f_bfree=kernel.free_blocks;output->f_bavail=kernel.available_blocks;
    output->f_files=kernel.files;output->f_ffree=kernel.free_files;
#if defined(MC_PS4) || defined(__APPLE__) || defined(__FreeBSD__)
    output->f_iosize=kernel.io_size;
    output->f_type=kernel.type;output->f_flags=kernel.flags;output->f_owner=kernel.owner;
    memcpy(&output->f_fsid,kernel.filesystem_id,sizeof(output->f_fsid)<sizeof(kernel.filesystem_id)
        ? sizeof(output->f_fsid) : sizeof(kernel.filesystem_id));
    copy_statfs_string(output->f_fstypename,sizeof(output->f_fstypename),kernel.filesystem_name,sizeof(kernel.filesystem_name));
    copy_statfs_string(output->f_mntfromname,sizeof(output->f_mntfromname),kernel.mount_from,sizeof(kernel.mount_from));
    copy_statfs_string(output->f_mntonname,sizeof(output->f_mntonname),kernel.mount_on,sizeof(kernel.mount_on));
#endif
#if defined(MC_PS4) || defined(__FreeBSD__)
    output->f_version=kernel.version;output->f_namemax=kernel.name_max;
    output->f_syncwrites=kernel.sync_writes;output->f_asyncwrites=kernel.async_writes;
    output->f_syncreads=kernel.sync_reads;output->f_asyncreads=kernel.async_reads;
#endif
    return 0;
}

#ifdef MC_PS4
#include <fcntl.h>
#include <unistd.h>
// Raw entry points take the kernel layout, not the SDK's public struct stat.
extern int32_t sceKernelStat(const char*,void*);
extern int _fstat(int,void*);
extern int _fstatfs(int,void*);
int fstat(int fd,struct stat* output) {
    unsigned char raw[256]={0};
    if (!output) { errno=EFAULT;return -1; }
    int result=mc_ps4_posix_result(_fstat(fd,raw));
    return result ? result : mc_ps4_decode_stat(raw,sizeof(raw),output);
}
int stat(const char* path,struct stat* output) {
    unsigned char raw[256]={0};
    if (!path || !output) { errno=EFAULT;return -1; }
    int result=mc_ps4_posix_result(sceKernelStat(path,raw));
    return result ? result : mc_ps4_decode_stat(raw,sizeof(raw),output);
}
// The browser rejects links; inspect readable entries without following one.
int lstat(const char* path,struct stat* output) {
    if (!path || !output) { errno=EFAULT;return -1; }
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if (fd<0) return -1;
    int result=fstat(fd,output),saved=errno;close(fd);errno=saved;return result;
}
int fstatat(int directory,const char* path,struct stat* output,int flags) {
    if (!path || !output) { errno=EFAULT;return -1; }
    if (flags & ~AT_SYMLINK_NOFOLLOW) { errno=EINVAL;return -1; }
    if (path[0]=='/') return flags & AT_SYMLINK_NOFOLLOW ? lstat(path,output) : stat(path,output);
    if (directory==AT_FDCWD) { errno=ENOSYS;return -1; }
    int fd=openat(directory,path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|(flags & AT_SYMLINK_NOFOLLOW ? O_NOFOLLOW : 0));
    if (fd<0) return -1;
    int result=fstat(fd,output),saved=errno;close(fd);errno=saved;return result;
}
int fstatfs(int fd,struct statfs* output) {
    // Reserve more than either BSD layout; decode only the known Orbis fields.
    unsigned char raw[4096]={0};
    if (!output) { errno=EFAULT;return -1; }
    int result=mc_ps4_posix_result(_fstatfs(fd,raw));
    return result ? result : mc_ps4_decode_statfs(raw,sizeof(raw),output);
}
// The archive contains weak 64-bit aliases beside its strong, broken functions.
#undef stat64
#undef fstat64
#undef lstat64
#undef fstatat64
int stat64(const char*,struct stat*) __attribute__((weak,alias("stat")));
int fstat64(int,struct stat*) __attribute__((weak,alias("fstat")));
int lstat64(const char*,struct stat*) __attribute__((weak,alias("lstat")));
int fstatat64(int,const char*,struct stat*,int) __attribute__((weak,alias("fstatat")));
#endif
