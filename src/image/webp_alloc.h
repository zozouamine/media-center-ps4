#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* mc_webp_allocate(size_t bytes);
void* mc_webp_callocate(size_t count, size_t bytes);
void mc_webp_free(void* pointer);
#ifdef __cplusplus
}
#endif
