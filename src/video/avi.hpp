#pragma once
#include "image/image.hpp"
#include "audio/decoder.hpp"

namespace mc {
struct VideoInfo {
    int width = 0, height = 0;
    double frame_seconds = 0, duration = 0;
    uint64_t frames = 0;
    bool has_audio = false;
    std::string codec;
    AudioInfo audio;
};
class AviDecoder {
    struct Chunk { uint64_t offset = 0, first = 0; uint32_t bytes = 0; };
    ReadOnlyFile file_;
    VideoInfo info_{};
    std::vector<Chunk> video_, audio_;
    bool mjpeg_ = false, bottom_up_ = true;
    int video_stream_ = -1, audio_stream_ = -1;
    unsigned audio_bits_ = 0, audio_align_ = 0;
    uint64_t audio_frames_ = 0, audio_cursor_ = 0, audio_output_left_ = 0;
    double phase_ = 0;
    int16_t sample_a_[2]{}, sample_b_[2]{};
    bool samples_initialized_ = false;
    bool audio_failed_ = false;
    std::vector<int16_t> sample_block_;
    size_t sample_offset_ = 0, sample_frames_ = 0;
    const std::atomic<bool>* cancel_ = nullptr;
    bool parse_headers(uint64_t start, uint64_t end, std::string& error);
    bool parse_stream(uint64_t start, uint64_t end, int index, std::string& error);
    bool index_movi(uint64_t start, uint64_t end, unsigned depth, std::string& error);
    size_t read_pcm(int16_t* output, size_t frames);
    bool pull_audio(int16_t sample[2]);
public:
    bool open(const Item&, const std::atomic<bool>& cancel, std::string& error);
    void close();
    const VideoInfo& info() const { return info_; }
    std::shared_ptr<Bitmap> frame(uint64_t index, std::string& error);
    bool seek_audio(double seconds);
    size_t read_audio_stereo(int16_t* output, size_t frames);
    bool audio_failed() const { return audio_failed_; }
};
}
