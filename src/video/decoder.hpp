#pragma once
#include "avi.hpp"

namespace mc {
// Timestamp-based playback; FFmpeg stays behind the portable application API.
class VideoDecoder {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    VideoDecoder();
    ~VideoDecoder();
    bool open(const Item&, const std::atomic<bool>& cancel, std::string& error);
    void close();
    const VideoInfo& info() const;
    std::shared_ptr<Bitmap> frame_at(double seconds, std::string& error);
    bool seek(double seconds, double& actual, std::string& error);
    size_t read_audio_stereo(int16_t* output, size_t frames);
    bool audio_failed() const;
};
}
