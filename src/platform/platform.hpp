#pragma once
#include "input/input.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace mc {
constexpr int screen_width = 1920, screen_height = 1080;
constexpr unsigned output_sample_rate = 48000, audio_block_frames = 1024;
double monotonic_now();
class AudioSink {
public:
    virtual ~AudioSink() = default;
    virtual bool open(std::string& error) = 0;
    virtual bool push(const int16_t* stereo, size_t frames, const std::atomic<bool>& cancel) = 0;
    virtual uint64_t played_frames() const = 0;
    virtual void pause(bool paused) = 0;
    virtual void flush() = 0;
    virtual void close() = 0;
};
class Platform {
public:
    virtual ~Platform() = default;
    virtual bool initialize(std::string& error) = 0;
    virtual bool poll(RawInput& input) = 0;
    virtual bool present(const uint32_t* rgba, uint64_t revision, std::string& error) = 0;
    virtual std::unique_ptr<AudioSink> make_audio_sink() = 0;
    virtual std::string assets_root() const = 0;
    virtual std::string data_root() const = 0;
};
std::unique_ptr<Platform> make_native_platform(const std::string& assets, const std::string& data);
std::unique_ptr<AudioSink> make_headless_audio_sink(const std::string& dump = {});
}
