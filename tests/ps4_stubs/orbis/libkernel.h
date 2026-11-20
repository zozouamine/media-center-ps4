#pragma once
#include <stdint.h>

// Host simulation only. Production PS4 builds include the real SDK header.
#ifdef __cplusplus
extern "C" {
#endif
int32_t sceKernelOpen(const char* path, int32_t flags, uint32_t mode);
int32_t sceKernelMkdir(const char*, uint32_t);
int32_t sceKernelRename(const char*, const char*);
int32_t sceKernelUnlink(const char*);
uint64_t sceKernelGetProcessTime(void);
int32_t sceKernelDlsym(int32_t, const char*, void**);
uint32_t sceKernelLoadStartModule(const char*, size_t, const void*, uint32_t, void*, void*);
#ifdef __cplusplus
}
#endif
