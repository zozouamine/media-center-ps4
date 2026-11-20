#include "formats.hpp"
#include <algorithm>
#include <cstring>

namespace mc {
namespace {
uint32_t le16(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
uint32_t le32(const uint8_t* p) { return le16(p) | le16(p+2) << 16; }
struct Candidate { uint32_t width, height; size_t offset, length; bool png; };
std::shared_ptr<Bitmap> icon_dib(const uint8_t* data, size_t bytes, const Candidate& entry, std::string& error) {
    if (bytes < 40) return {};
    uint32_t header = le32(data), width = le32(data+4), stored_height = le32(data+8), bits = le16(data+14);
    bool top_down = (stored_height & 0x80000000u) != 0;
    uint32_t height = top_down ? (~stored_height + 1u) : stored_height;
    if (header < 40 || header > 124 || header > bytes || width != entry.width || height != entry.height*2
        || le16(data+12) != 1 || (bits != 24 && bits != 32) || le32(data+16) != 0 || le32(data+32) != 0) return {};
    size_t stride = ((static_cast<size_t>(width) * (bits/8) + 3) / 4) * 4;
    size_t mask_stride = ((width+31)/32)*4, xor_bytes = stride*entry.height, mask_bytes = mask_stride*entry.height;
    if (xor_bytes > bytes-header) return {};
    bool mask = mask_bytes <= bytes-header-xor_bytes;
    if (!mask && bits == 24) return {};
    bool alpha = false;
    if (bits == 32) for (uint32_t y = 0; y < entry.height; ++y) for (uint32_t x = 0; x < width; ++x)
        alpha = alpha || data[header+y*stride+x*4+3] != 0;
    auto* pixels = static_cast<uint8_t*>(decode_allocate(static_cast<size_t>(width)*entry.height*4));
    if (!pixels) { error = "ICO image memory limit reached"; return {}; }
    auto bitmap = std::make_shared<Bitmap>(); bitmap->width = static_cast<int>(width); bitmap->height = static_cast<int>(entry.height); bitmap->pixels = {pixels, decode_free};
    for (uint32_t y = 0; y < entry.height; ++y) for (uint32_t x = 0; x < width; ++x) {
        uint32_t row = top_down ? y : entry.height-1-y;
        const uint8_t* src = data+header+row*stride+x*(bits/8); uint8_t* dst = pixels+(static_cast<size_t>(y)*width+x)*4;
        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = bits == 32 && alpha ? src[3] : 255;
        if (mask && (data[header+xor_bytes+row*mask_stride+x/8] & (128u >> (x%8)))) dst[3] = 0;
    }
    return bitmap;
}
}
std::shared_ptr<Bitmap> decode_ico(const uint8_t* data, size_t bytes, const std::atomic<bool>& cancel, std::string& error) {
    if (!data || bytes < 6 || bytes > image_file_limit || le16(data) || le16(data+2) != 1) { error = "Unsupported or corrupt ICO"; return {}; }
    uint32_t count = le16(data+4); size_t directory_end = 6 + static_cast<size_t>(count)*16;
    if (!count || count > 256 || directory_end > bytes) { error = "ICO directory is invalid or exceeds 256 entries"; return {}; }
    std::vector<Candidate> entries;
    const uint8_t signature[] = {137,80,78,71,13,10,26,10};
    for (uint32_t i = 0; i < count && !cancel; ++i) {
        const uint8_t* entry = data+6+i*16; size_t length = le32(entry+8), offset = le32(entry+12);
        if (offset < directory_end || offset > bytes || length > bytes-offset || length < 8) continue;
        uint32_t width = entry[0] ? entry[0] : 256, height = entry[1] ? entry[1] : 256;
        bool png = !std::memcmp(data+offset, signature, sizeof(signature));
        if (png || (length >= 40 && le32(data+offset) >= 40)) entries.push_back({width,height,offset,length,png});
    }
    std::sort(entries.begin(), entries.end(), [](const Candidate& a, const Candidate& b) { return a.width*a.height > b.width*b.height; });
    for (const auto& entry : entries) {
        if (cancel) { error = "Cancelled"; return {}; }
        std::string reason;
        auto bitmap = entry.png ? decode_bitmap(data+entry.offset, entry.length, uint64_t(entry.width)*entry.height, reason) : icon_dib(data+entry.offset, entry.length, entry, reason);
        if (bitmap && bitmap->width == static_cast<int>(entry.width) && bitmap->height == static_cast<int>(entry.height)) return bitmap;
    }
    error = cancel ? "Cancelled" : "ICO requires a valid PNG or uncompressed 24/32-bit bitmap entry"; return {};
}
}
