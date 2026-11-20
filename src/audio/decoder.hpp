#pragma once
#include "storage/read_only.hpp"
#include "image/image.hpp"
#include <memory>

namespace mc {
struct AudioInfo {
    unsigned sample_rate = 0, channels = 0;
    uint64_t total_frames = 0;
    double duration = 0;
    std::string codec, title, artist, album;
    std::shared_ptr<Bitmap> artwork;
    uint32_t artwork_type = 0;
};
class AudioDecoder {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    AudioDecoder();
    ~AudioDecoder();
    bool open(const Item&, const std::atomic<bool>& cancel, std::string& error);
    const AudioInfo& info() const;
    size_t read_stereo(int16_t* output, size_t output_frames);
    bool seek(double seconds, double& actual);
    bool failed() const;
    void close();
};
}
