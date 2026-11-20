#pragma once
#include "storage/read_only.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace mc {
constexpr size_t decode_memory_budget = 96u * 1024u * 1024u;
constexpr size_t image_file_limit = 32u * 1024u * 1024u;
constexpr uint64_t image_pixel_limit = 16u * 1024u * 1024u;
constexpr size_t animation_frame_limit = 256;
constexpr uint64_t animation_memory_limit = 64u * 1024u * 1024u;
void* decode_allocate(size_t bytes);
void* decode_reallocate(void* pointer, size_t bytes);
void decode_free(void* pointer);
size_t decode_bytes_in_use();
struct Bitmap {
    int width = 0, height = 0;
    std::shared_ptr<uint8_t> pixels;
    const uint32_t* rgba() const { return reinterpret_cast<const uint32_t*>(pixels.get()); }
};
struct ImageFrame {
    std::shared_ptr<Bitmap> bitmap;
    uint32_t delay_ms = 100;
};
struct ImageResult {
    std::shared_ptr<Bitmap> bitmap;
    std::vector<ImageFrame> frames;
    uint32_t loop_count = 0;
    int original_width = 0, original_height = 0;
    double duration = 0;
    uint64_t generation = 0;
    std::string error, path;
};
size_t animation_frame_at(const ImageResult&, double elapsed_seconds);
std::shared_ptr<Bitmap> decode_bitmap(const uint8_t* data, size_t bytes, uint64_t pixel_limit, std::string& error);
std::shared_ptr<Bitmap> resize_bitmap(const std::shared_ptr<Bitmap>& input, int maximum);
ImageResult load_image(const Item& item, bool thumbnail, const std::atomic<bool>& cancel);
class ImageWorker {
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> quit_{false}, cancel_{false};
    Item job_{};
    bool pending_ = false, thumbnail_ = false;
    uint64_t generation_ = 0;
    std::shared_ptr<ImageResult> result_;
    void run();
public:
    ImageWorker();
    ~ImageWorker();
    uint64_t request(Item item, bool thumbnail);
    void cancel();
    std::shared_ptr<ImageResult> take();
};
}
