#pragma once
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
// A directory is compared using read-only, no-follow handles, not path stat.
int mc_directory_matches(int descriptor, const char* absolute_path);
// Only one validated basename is accepted. The PS4 fallback is scoped here;
// it never replaces libc open/openat and retains parent identity checks.
int mc_open_child(int directory, const char* absolute_parent, const char* name, int flags, mode_t mode);
#ifdef __cplusplus
}
#endif
