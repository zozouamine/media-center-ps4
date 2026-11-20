#include "formats.hpp"
#include "tiffio.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

// Supply only allocation/memory services. The Unix/Windows file-I/O backends
// are deliberately absent; TIFFClientOpenExt always receives memory-only,
// read-only callbacks. All codec allocations share the application budget.
extern "C" {
TIFFErrorHandler _TIFFerrorHandler = nullptr;
TIFFErrorHandler _TIFFwarningHandler = nullptr;
void* _TIFFmalloc(tmsize_t bytes) { return bytes > 0 ? mc::decode_allocate(static_cast<size_t>(bytes)) : nullptr; }
void* _TIFFcalloc(tmsize_t count, tmsize_t bytes) {
    if (count <= 0 || bytes <= 0 || static_cast<uint64_t>(count) > SIZE_MAX/static_cast<uint64_t>(bytes)) return nullptr;
    size_t total = static_cast<size_t>(count)*static_cast<size_t>(bytes); void* result = mc::decode_allocate(total);
    if (result) std::memset(result, 0, total); return result;
}
void* _TIFFrealloc(void* pointer, tmsize_t bytes) { return bytes >= 0 ? mc::decode_reallocate(pointer, static_cast<size_t>(bytes)) : nullptr; }
void _TIFFfree(void* pointer) { mc::decode_free(pointer); }
void _TIFFmemset(void* pointer, int value, tmsize_t bytes) { if (bytes > 0) std::memset(pointer, value, static_cast<size_t>(bytes)); }
void _TIFFmemcpy(void* to, const void* from, tmsize_t bytes) { if (bytes > 0) std::memcpy(to, from, static_cast<size_t>(bytes)); }
int _TIFFmemcmp(const void* a, const void* b, tmsize_t bytes) { return bytes > 0 ? std::memcmp(a, b, static_cast<size_t>(bytes)) : 0; }
}
namespace mc {
namespace {
struct Input { const uint8_t* data; size_t bytes, cursor = 0; const std::atomic<bool>* cancel; std::string error; };
tmsize_t read(thandle_t handle, void* out, tmsize_t bytes) {
    auto& input = *static_cast<Input*>(handle); if (input.cancel->load()) return -1;
    if (bytes <= 0) return 0;
    size_t count = std::min<size_t>(static_cast<size_t>(bytes), input.bytes-input.cursor);
    std::memcpy(out, input.data+input.cursor, count); input.cursor += count; return static_cast<tmsize_t>(count);
}
tmsize_t no_write(thandle_t, void*, tmsize_t) { return -1; }
toff_t seek(thandle_t handle, toff_t offset, int origin) {
    auto& input = *static_cast<Input*>(handle); if (input.cancel->load()) return static_cast<toff_t>(-1);
    uint64_t base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? input.cursor : origin == SEEK_END ? input.bytes : UINT64_MAX;
    if (base == UINT64_MAX) return static_cast<toff_t>(-1);
    uint64_t target;
    if (offset & (uint64_t(1) << 63)) {
        uint64_t magnitude = ~offset+1; if (origin == SEEK_SET || magnitude > base) return static_cast<toff_t>(-1); target = base-magnitude;
    } else { if (offset > input.bytes-base) return static_cast<toff_t>(-1); target = base+offset; }
    if (target > input.bytes) return static_cast<toff_t>(-1);
    input.cursor = static_cast<size_t>(target); return target;
}
int close(thandle_t) { return 0; }
toff_t size(thandle_t handle) { return static_cast<Input*>(handle)->bytes; }
int no_map(thandle_t, void**, toff_t*) { return 0; }
void unmap(thandle_t, void*, toff_t) {}
int error_handler(TIFF*, void* user, const char*, const char* format, va_list args) {
    auto& input = *static_cast<Input*>(user);
    if (input.error.empty()) { char text[384]{}; std::vsnprintf(text, sizeof(text), format, args); input.error = text; }
    return 1;
}
int ignore_warning(TIFF*, void*, const char*, const char*, va_list) { return 1; }
}
std::shared_ptr<Bitmap> decode_tiff(const uint8_t* data, size_t bytes, const std::atomic<bool>& cancel, std::string& error) {
    if (!data || bytes < 8 || bytes > image_file_limit || cancel) { error = cancel ? "Cancelled" : "TIFF is empty, corrupt or exceeds 32 MiB"; return {}; }
    Input input{data,bytes,0,&cancel,{}};
    std::unique_ptr<TIFFOpenOptions, decltype(&TIFFOpenOptionsFree)> options(TIFFOpenOptionsAlloc(), TIFFOpenOptionsFree);
    if (!options) { error = "TIFF metadata memory limit reached"; return {}; }
    TIFFOpenOptionsSetMaxSingleMemAlloc(options.get(), 32u*1024u*1024u);
    TIFFOpenOptionsSetMaxCumulatedMemAlloc(options.get(), 32u*1024u*1024u);
    TIFFOpenOptionsSetErrorHandlerExtR(options.get(), error_handler, &input);
    TIFFOpenOptionsSetWarningHandlerExtR(options.get(), ignore_warning, nullptr);
    std::unique_ptr<TIFF, decltype(&TIFFClose)> tiff(TIFFClientOpenExt("image", "rm", &input, read, no_write, seek, close, size, no_map, unmap, options.get()), TIFFClose);
    if (!tiff) { error = "TIFF header decode failed: "+input.error; return {}; }
    uint32_t width = 0, height = 0; uint16_t compression = 0;
    if (!TIFFGetField(tiff.get(), TIFFTAG_IMAGEWIDTH, &width) || !TIFFGetField(tiff.get(), TIFFTAG_IMAGELENGTH, &height)
        || !width || !height || width > 8192 || height > 8192 || uint64_t(width)*height > image_pixel_limit) { error = "TIFF dimensions exceed the safe decode budget"; return {}; }
    TIFFGetFieldDefaulted(tiff.get(), TIFFTAG_COMPRESSION, &compression);
    if (!TIFFIsCODECConfigured(compression)) { error = "This TIFF compression is unavailable. Use uncompressed, LZW, PackBits, Deflate or CCITT TIFF."; return {}; }
    auto* pixels = static_cast<uint8_t*>(decode_allocate(static_cast<size_t>(width)*height*4));
    if (!pixels) { error = "TIFF image memory limit reached"; return {}; }
    auto bitmap = std::make_shared<Bitmap>(); bitmap->width = static_cast<int>(width); bitmap->height = static_cast<int>(height); bitmap->pixels = {pixels, decode_free};
    if (!TIFFReadRGBAImageOriented(tiff.get(), width, height, reinterpret_cast<uint32_t*>(pixels), ORIENTATION_TOPLEFT, 1) || cancel) {
        error = cancel ? "Cancelled" : "TIFF pixel decode failed: "+input.error; return {};
    }
    // libtiff returns associated RGBA. Canvas expects straight RGBA.
    for (size_t i = 0; i < static_cast<size_t>(width)*height; ++i) {
        uint32_t p = reinterpret_cast<uint32_t*>(pixels)[i]; unsigned alpha = TIFFGetA(p);
        auto straight = [&](unsigned channel) { return static_cast<uint8_t>(alpha ? std::min(255u, (channel*255+alpha/2)/alpha) : 0); };
        pixels[i*4] = straight(TIFFGetR(p)); pixels[i*4+1] = straight(TIFFGetG(p)); pixels[i*4+2] = straight(TIFFGetB(p)); pixels[i*4+3] = static_cast<uint8_t>(alpha);
    }
    return bitmap;
}
}
