/* PS4MC portable decoder configuration. Use SSE2 on x86; scalar otherwise. */
#if defined(__SSE2__)
#define WEBP_HAVE_SSE2 1
#endif
#define WEBP_USE_THREAD 1
