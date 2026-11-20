#include "path_io.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef MC_VALIDATE_PS4_DIRECT_USB
extern int mc_test_usb_fstat(int, struct stat*);
#define fstat mc_test_usb_fstat
#endif

static int valid_parent(const char* path) {
    if (!path || path[0] != '/') return 0;
    size_t length = strnlen(path, 16385);
    if (!length || length > 16384) return 0;
    if (length == 1) return 1;
    size_t begin = 1;
    while (begin < length) {
        size_t end = begin;
        while (end < length && path[end] != '/') ++end;
        size_t part = end - begin;
        if (!part || part > 255 || (part == 1 && path[begin] == '.')
            || (part == 2 && path[begin] == '.' && path[begin + 1] == '.')) return 0;
        begin = end + 1;
    }
    return path[length - 1] != '/';
}

int mc_directory_matches(int descriptor, const char* absolute_path) {
    struct stat owned, current;
    if (!valid_parent(absolute_path) || fstat(descriptor, &owned)
        || !S_ISDIR(owned.st_mode)) { errno = ESTALE; return 0; }
    int current_fd = open(absolute_path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (current_fd < 0) return 0;
    int ok = fstat(current_fd, &current) == 0 && S_ISDIR(current.st_mode)
        && owned.st_dev == current.st_dev && owned.st_ino == current.st_ino;
    int saved = ok ? 0 : ESTALE;
    close(current_fd); errno = saved; return ok;
}

int mc_open_child(int directory, const char* absolute_parent, const char* name, int flags, mode_t mode) {
    if (!name || !name[0] || strnlen(name, 256) > 255 || strchr(name, '/')
        || !strcmp(name, ".") || !strcmp(name, "..") || !(flags & O_NOFOLLOW)) {
        errno = EINVAL; return -1;
    }
    int fd = openat(directory, name, flags, mode);
#ifdef MC_PS4
    // Orbis can deny openat even when opening the same full path is allowed.
    if (fd < 0 && (errno == EPERM || errno == ENOSYS || errno == EINVAL)) {
        if (!valid_parent(absolute_parent)) { errno = EINVAL; return -1; }
        size_t length = strnlen(absolute_parent, 16385);
        if (!length || length > 16384 || !mc_directory_matches(directory, absolute_parent)) return -1;
        char path[16642];
        memcpy(path, absolute_parent, length);
        if (path[length - 1] != '/') path[length++] = '/';
        size_t count = strlen(name); memcpy(path + length, name, count + 1);
        fd = open(path, flags, mode);
        int saved = errno;
        if (fd >= 0 && !mc_directory_matches(directory, absolute_parent)) {
            close(fd); fd = -1; saved = ESTALE;
        }
        errno = saved;
    }
#else
    (void)absolute_parent;
#endif
    return fd;
}
