#include "image.hpp"
#include "formats.hpp"
#include "video/avi.hpp"
#include "webp_alloc.h"
#include "src/webp/decode.h"
#include "src/webp/demux.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#define STBI_ONLY_PSD
#define STBI_ONLY_HDR
#define STBI_ONLY_PIC
#define STBI_ONLY_PNM
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#define STBI_MALLOC(n) mc::decode_allocate(n)
#define STBI_REALLOC(p,n) mc::decode_reallocate(p,n)
#define STBI_FREE(p) mc::decode_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace mc {
namespace {
struct alignas(std::max_align_t) AllocationHeader { size_t size; };
std::atomic<size_t> allocated{0};
}
void* decode_allocate(size_t bytes) {
    if (bytes == 0) bytes = 1;
    if (bytes > decode_memory_budget || bytes > std::numeric_limits<size_t>::max() - sizeof(AllocationHeader)) return nullptr;
    size_t before = allocated.load();
    do { if (bytes > decode_memory_budget - before) return nullptr; }
    while (!allocated.compare_exchange_weak(before, before + bytes));
    auto* header = static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader) + bytes));
    if (!header) { allocated.fetch_sub(bytes); return nullptr; }
    header->size = bytes; return header + 1;
}
void decode_free(void* pointer) {
    if (!pointer) return;
    auto* header = static_cast<AllocationHeader*>(pointer) - 1;
    allocated.fetch_sub(header->size); std::free(header);
}
void* decode_reallocate(void* pointer, size_t bytes) {
    if (!pointer) return decode_allocate(bytes);
    if (!bytes) { decode_free(pointer); return nullptr; }
    auto* header = static_cast<AllocationHeader*>(pointer) - 1;
    void* next = decode_allocate(bytes);
    if (!next) return nullptr;
    std::memcpy(next, pointer, std::min(header->size, bytes)); decode_free(pointer); return next;
}
size_t decode_bytes_in_use() { return allocated.load(); }
size_t animation_frame_at(const ImageResult& image, double elapsed) {
    if (image.frames.size() < 2 || !std::isfinite(elapsed) || elapsed <= 0) return 0;
    uint64_t duration = 0;
    for (const auto& frame : image.frames) duration += std::max<uint32_t>(40, frame.delay_ms);
    double milliseconds = elapsed * 1000.0;
    if (image.loop_count && milliseconds >= static_cast<double>(duration) * image.loop_count) return image.frames.size() - 1;
    double phase = std::fmod(milliseconds, static_cast<double>(duration));
    for (size_t i = 0; i < image.frames.size(); ++i) {
        auto delay = std::max<uint32_t>(40, image.frames[i].delay_ms);
        if (phase < delay) return i;
        phase -= delay;
    }
    return image.frames.size() - 1;
}
namespace {
uint32_t little16(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
uint32_t little32(const uint8_t* p) { return little16(p) | little16(p + 2) << 16; }
bool safe_dimensions(uint32_t width, uint32_t height) {
    return width && height && width <= 8192 && height <= 8192 && uint64_t(width) * height <= image_pixel_limit;
}
struct GifInfo { uint32_t width = 0, height = 0, frames = 0, loops = 1; };
bool inspect_gif(const std::vector<uint8_t>& bytes, bool all_frames, const std::atomic<bool>& cancel, GifInfo& info, std::string& error) {
    auto bad = [&] { error = "Unsupported or corrupt GIF"; return false; };
    if (bytes.size() < 13 || (std::memcmp(bytes.data(), "GIF87a", 6) && std::memcmp(bytes.data(), "GIF89a", 6))) return bad();
    info.width = little16(bytes.data() + 6); info.height = little16(bytes.data() + 8);
    if (!safe_dimensions(info.width, info.height)) { error = "GIF dimensions exceed the safe decode budget"; return false; }
    size_t cursor = 13;
    auto skip = [&](size_t count) { if (count > bytes.size() - cursor) return false; cursor += count; return true; };
    if ((bytes[10] & 128) && !skip(3u * (2u << (bytes[10] & 7)))) return bad();
    auto blocks = [&](bool application) {
        while (cursor < bytes.size()) {
            if (cancel) { error = "Cancelled"; return false; }
            uint8_t count = bytes[cursor++]; if (!count) return true;
            if (count > bytes.size() - cursor) return false;
            if (application && count == 3 && bytes[cursor] == 1) {
                auto repeat = little16(bytes.data() + cursor + 1); info.loops = repeat ? repeat + 1 : 0;
            }
            cursor += count;
        }
        return false;
    };
    while (cursor < bytes.size()) {
        if (cancel) { error = "Cancelled"; return false; }
        uint8_t tag = bytes[cursor++];
        if (tag == 0x3b) return info.frames ? true : bad();
        if (tag == 0x21) {
            if (cursor >= bytes.size()) return bad();
            uint8_t label = bytes[cursor++]; bool loop_extension = false;
            if (label == 0xff) {
                if (cursor >= bytes.size()) return bad();
                uint8_t count = bytes[cursor++];
                if (count > bytes.size() - cursor) return bad();
                loop_extension = count == 11 && (!std::memcmp(bytes.data() + cursor, "NETSCAPE2.0", 11) || !std::memcmp(bytes.data() + cursor, "ANIMEXTS1.0", 11));
                cursor += count;
            }
            if (!blocks(loop_extension)) return error.empty() ? bad() : false;
        } else if (tag == 0x2c) {
            if (bytes.size() - cursor < 9) return bad();
            uint32_t x = little16(bytes.data() + cursor), y = little16(bytes.data() + cursor + 2);
            uint32_t width = little16(bytes.data() + cursor + 4), height = little16(bytes.data() + cursor + 6);
            uint8_t flags = bytes[cursor + 8]; cursor += 9;
            if (!width || !height || x > info.width || y > info.height || width > info.width - x || height > info.height - y) return bad();
            if ((flags & 128) && !skip(3u * (2u << (flags & 7)))) return bad();
            if (cursor >= bytes.size() || bytes[cursor] < 2 || bytes[cursor] > 8) return bad();
            ++cursor; ++info.frames;
            if (all_frames && (info.frames > animation_frame_limit || uint64_t(info.frames) * info.width * info.height * 4 > animation_memory_limit)) {
                error = "GIF animation exceeds 256 frames or 64 MiB of pixels"; return false;
            }
            if (!blocks(false)) return error.empty() ? bad() : false;
            if (!all_frames) return true;
        } else return bad();
    }
    return bad();
}
void load_gif(const std::vector<uint8_t>& bytes, bool thumbnail, const std::atomic<bool>& cancel, ImageResult& out) {
    GifInfo info;
    if (!inspect_gif(bytes, !thumbnail, cancel, info, out.error)) return;
    out.original_width = static_cast<int>(info.width); out.original_height = static_cast<int>(info.height); out.loop_count = info.loops;
    if (thumbnail || info.frames == 1) { out.bitmap = decode_bitmap(bytes.data(), bytes.size(), image_pixel_limit, out.error); return; }
    // The pinned stb GIF implementation can reconstruct one frame at a time.
    // Copying each completed canvas avoids its all-frames loader's second full
    // allocation, and lets source disconnect/cancellation stop between frames.
    auto* state = static_cast<stbi__gif*>(decode_allocate(sizeof(stbi__gif)));
    if (!state) { out.error = "GIF memory limit reached"; return; }
    std::memset(state, 0, sizeof(*state));
    struct Cleanup { stbi__gif* gif; ~Cleanup() { decode_free(gif->out); decode_free(gif); } } cleanup{state};
    stbi__context context; stbi__start_mem(&context, bytes.data(), static_cast<int>(bytes.size()));
    uint8_t* saved_pixels = nullptr; int channels = 0;
    size_t stride = static_cast<size_t>(info.width) * info.height * 4;
    for (uint32_t i = 0; i < info.frames; ++i) {
        if (cancel) { out.error = "Cancelled"; break; }
        auto* decoded = stbi__gif_load_next(&context, state, &channels, 4, saved_pixels);
        if (!decoded || decoded == reinterpret_cast<uint8_t*>(&context) || state->w != static_cast<int>(info.width) || state->h != static_cast<int>(info.height)) {
            out.error = "GIF frame decode failed"; break;
        }
        auto* pixels = static_cast<uint8_t*>(decode_allocate(stride));
        if (!pixels) { out.error = "GIF memory limit reached"; break; }
        std::memcpy(pixels, decoded, stride);
        auto frame = std::make_shared<Bitmap>(); frame->width = state->w; frame->height = state->h; frame->pixels = {pixels, decode_free};
        out.frames.push_back({frame, static_cast<uint32_t>(std::max(40, state->delay))});
        if ((state->eflags & 0x1c) != 0x0c) saved_pixels = pixels;
    }
    if (!out.error.empty()) { out.frames.clear(); return; }
    out.bitmap = out.frames.front().bitmap;
}
bool inspect_webp(const std::vector<uint8_t>& bytes, const std::atomic<bool>& cancel, uint32_t& frames, std::string& error) {
    auto bad = [&] { error = "Unsupported or corrupt WebP"; return false; };
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WEBP", 4)) return bad();
    uint64_t end = uint64_t(little32(bytes.data() + 4)) + 8;
    if (end < 12 || end > bytes.size()) return bad();
    for (uint64_t cursor = 12; cursor < end;) {
        if (cancel) { error = "Cancelled"; return false; }
        if (end - cursor < 8) return bad();
        auto* chunk = bytes.data() + cursor; uint64_t size = little32(chunk + 4); cursor += 8;
        if (size > end - cursor) return bad();
        if (!std::memcmp(chunk, "ANMF", 4) && ++frames > animation_frame_limit) { error = "WebP animation exceeds 256 frames"; return false; }
        uint64_t padded = size + (size & 1); if (padded > end - cursor) return bad(); cursor += padded;
    }
    return true;
}
void load_webp(const std::vector<uint8_t>& bytes, bool thumbnail, const std::atomic<bool>& cancel, ImageResult& out) {
    uint32_t count = 0;
    if (!inspect_webp(bytes, cancel, count, out.error)) return;
    WebPBitstreamFeatures features{};
    if (WebPGetFeatures(bytes.data(), bytes.size(), &features) != VP8_STATUS_OK || !safe_dimensions(features.width, features.height)) {
        out.error = "WebP header or dimensions exceed the safe decode budget"; return;
    }
    out.original_width = features.width; out.original_height = features.height;
    size_t stride = static_cast<size_t>(features.width) * features.height * 4;
    if (!features.has_animation) {
        auto* pixels = static_cast<uint8_t*>(decode_allocate(stride));
        if (!pixels) { out.error = "WebP memory limit reached"; return; }
        if (!WebPDecodeRGBAInto(bytes.data(), bytes.size(), pixels, stride, features.width * 4)) { decode_free(pixels); out.error = "WebP decode failed"; return; }
        out.bitmap = std::make_shared<Bitmap>(); out.bitmap->width = features.width; out.bitmap->height = features.height; out.bitmap->pixels = {pixels, decode_free}; return;
    }
    if (!count || (!thumbnail && uint64_t(stride) * count > animation_memory_limit)) { out.error = "WebP animation exceeds 64 MiB of pixels"; return; }
    WebPData data{bytes.data(), bytes.size()}; WebPAnimDecoderOptions options{};
    if (!WebPAnimDecoderOptionsInit(&options)) { out.error = "WebP decoder version mismatch"; return; }
    options.color_mode = MODE_RGBA; options.use_threads = 0;
    std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder(WebPAnimDecoderNew(&data, &options), WebPAnimDecoderDelete);
    WebPAnimInfo info{};
    if (!decoder || !WebPAnimDecoderGetInfo(decoder.get(), &info) || info.frame_count != count || info.canvas_width != static_cast<uint32_t>(features.width) || info.canvas_height != static_cast<uint32_t>(features.height)) {
        out.error = "WebP animation initialization failed"; return;
    }
    out.loop_count = info.loop_count; int previous_timestamp = 0;
    for (uint32_t i = 0; i < (thumbnail ? 1u : count); ++i) {
        if (cancel) { out.error = "Cancelled"; break; }
        uint8_t* decoded = nullptr; int timestamp = 0;
        if (!WebPAnimDecoderGetNext(decoder.get(), &decoded, &timestamp) || timestamp < previous_timestamp) { out.error = "WebP frame decode failed"; break; }
        auto* pixels = static_cast<uint8_t*>(decode_allocate(stride));
        if (!pixels) { out.error = "WebP memory limit reached"; break; }
        std::memcpy(pixels, decoded, stride);
        auto frame = std::make_shared<Bitmap>(); frame->width = features.width; frame->height = features.height; frame->pixels = {pixels, decode_free};
        out.frames.push_back({frame, static_cast<uint32_t>(std::max(40, timestamp - previous_timestamp))}); previous_timestamp = timestamp;
    }
    if (!out.error.empty()) { out.frames.clear(); return; }
    out.bitmap = out.frames.front().bitmap;
    if (thumbnail || count == 1) out.frames.clear();
}
}
std::shared_ptr<Bitmap> decode_bitmap(const uint8_t* data, size_t bytes, uint64_t limit, std::string& error) {
    if (!data || bytes == 0 || bytes > image_file_limit || bytes > INT32_MAX) { error = "Image is empty or exceeds 32 MiB"; return {}; }
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(data, static_cast<int>(bytes), &w, &h, &channels)) { error = "Unsupported or corrupt image"; return {}; }
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || static_cast<uint64_t>(w) * h > limit) { error = "Image dimensions exceed the safe decode budget"; return {}; }
    auto* pixels = stbi_load_from_memory(data, static_cast<int>(bytes), &w, &h, &channels, 4);
    if (!pixels) { const char* reason = stbi_failure_reason(); error = "Image decode failed: " + std::string(reason ? reason : "memory budget or corrupt data"); return {}; }
    auto result = std::make_shared<Bitmap>(); result->width = w; result->height = h;
    result->pixels = std::shared_ptr<uint8_t>(pixels, [](uint8_t* p) { stbi_image_free(p); }); return result;
}
std::shared_ptr<Bitmap> resize_bitmap(const std::shared_ptr<Bitmap>& input, int maximum) {
    if (!input || maximum < 1) return {};
    if (input->width <= maximum && input->height <= maximum) return input;
    double scale = std::min(static_cast<double>(maximum) / input->width, static_cast<double>(maximum) / input->height);
    int w = std::max(1, static_cast<int>(input->width * scale)), h = std::max(1, static_cast<int>(input->height * scale));
    auto* pixels = static_cast<uint8_t*>(decode_allocate(static_cast<size_t>(w) * h * 4)); if (!pixels) return {};
    auto out = std::make_shared<Bitmap>(); out->width = w; out->height = h; out->pixels = {pixels, decode_free};
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        int sx = std::min(input->width - 1, x * input->width / w), sy = std::min(input->height - 1, y * input->height / h);
        std::memcpy(pixels + (static_cast<size_t>(y) * w + x) * 4, input->pixels.get() + (static_cast<size_t>(sy) * input->width + sx) * 4, 4);
    }
    return out;
}
ImageResult load_image(const Item& item, bool thumbnail, const std::atomic<bool>& cancel) {
    ImageResult out; out.path = item.entry.relative;
    auto ext = extension(item.entry.name);
    if (ext == "heic" || ext == "heif" || ext == "avif" || ext == "jxl") { out.error = "HEIC, HEIF, AVIF and JPEG XL decoding is unavailable in this build."; return out; }
    ReadOnlyFile file;
    if (!file.open(item.source, item.entry.relative, out.error, &cancel)) return out;
    auto bytes = file.read_all(image_file_limit, out.error); if (bytes.empty() || cancel) return out;
    if (ext == "gif") load_gif(bytes, thumbnail, cancel, out);
    else if (ext == "webp") load_webp(bytes, thumbnail, cancel, out);
    else if (ext == "ico") out.bitmap = decode_ico(bytes.data(), bytes.size(), cancel, out.error);
    else if (ext == "tif" || ext == "tiff") out.bitmap = decode_tiff(bytes.data(), bytes.size(), cancel, out.error);
    else out.bitmap = decode_bitmap(bytes.data(), bytes.size(), image_pixel_limit, out.error);
    if (out.bitmap) { out.original_width = out.bitmap->width; out.original_height = out.bitmap->height; if (thumbnail) out.bitmap = resize_bitmap(out.bitmap, 640); }
    if (cancel) { out.bitmap.reset(); out.frames.clear(); out.error = "Cancelled"; }
    if (!out.bitmap && out.error.empty()) out.error = "Preview memory limit reached";
    return out;
}
ImageWorker::ImageWorker() { thread_ = std::thread(&ImageWorker::run, this); }
ImageWorker::~ImageWorker() { quit_ = true; cancel_ = true; wake_.notify_all(); thread_.join(); }
uint64_t ImageWorker::request(Item item, bool thumbnail) {
    std::lock_guard<std::mutex> lock(mutex_); cancel_ = true; job_ = std::move(item); thumbnail_ = thumbnail;
    pending_ = true; result_.reset(); ++generation_; wake_.notify_one(); return generation_;
}
void ImageWorker::cancel() { std::lock_guard<std::mutex> lock(mutex_); cancel_ = true; pending_ = false; result_.reset(); ++generation_; }
std::shared_ptr<ImageResult> ImageWorker::take() { std::lock_guard<std::mutex> lock(mutex_); auto out = result_; result_.reset(); return out; }
void ImageWorker::run() {
    while (!quit_) {
        Item item; bool thumbnail; uint64_t generation;
        {
            std::unique_lock<std::mutex> lock(mutex_); wake_.wait(lock, [&] { return quit_ || pending_; }); if (quit_) break;
            item = job_; thumbnail = thumbnail_; generation = generation_; pending_ = false; cancel_ = false;
        }
        auto result = std::make_shared<ImageResult>();
        if (thumbnail && item.entry.kind==Kind::Video) {
            result->path=item.entry.relative;AviDecoder decoder;
            if (decoder.open(item,cancel_,result->error)) { auto info=decoder.info();result->original_width=info.width;result->original_height=info.height;result->duration=info.duration;result->bitmap=resize_bitmap(decoder.frame(0,result->error),640); }
        } else *result=load_image(item,thumbnail,cancel_);
        result->generation = generation;
        std::lock_guard<std::mutex> lock(mutex_); if (generation == generation_ && !cancel_) result_ = std::move(result);
    }
}
}
extern "C" void* mc_webp_allocate(size_t bytes) { return mc::decode_allocate(bytes); }
extern "C" void* mc_webp_callocate(size_t count, size_t bytes) {
    if (bytes && count > std::numeric_limits<size_t>::max() / bytes) return nullptr;
    size_t total = count * bytes; void* result = mc::decode_allocate(total);
    if (result) std::memset(result, 0, total);
    return result;
}
extern "C" void mc_webp_free(void* pointer) { mc::decode_free(pointer); }
