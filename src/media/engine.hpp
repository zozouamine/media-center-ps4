#pragma once
#include "audio/decoder.hpp"
#include "platform/platform.hpp"
#include "video/decoder.hpp"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace mc {
enum class Playback { Idle, Loading, Playing, Paused, Seeking, Ended, Error };
struct MediaSnapshot {
    Playback state = Playback::Idle;
    Item item;
    Kind kind = Kind::Other;
    double position = 0, duration = 0;
    bool paused = false, seekable = false, has_audio = false;
    int volume = 80;
    uint64_t session = 0, frame_revision = 0;
    int width = 0, height = 0;
    std::shared_ptr<Bitmap> frame;
    std::shared_ptr<Bitmap> artwork;
    std::string error, codec, title, artist, album;
};
class MediaEngine {
    std::function<std::unique_ptr<AudioSink>()> sink_factory_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> quit_{false}, cancel_{false};
    std::atomic<int> volume_{80};
    bool open_pending_ = false, stop_pending_ = false, pause_pending_ = false, seek_pending_ = false;
    Item open_item_{};
    double seek_target_ = 0;
    uint64_t session_ = 0;
    MediaSnapshot snapshot_{};
    void run();
public:
    explicit MediaEngine(std::function<std::unique_ptr<AudioSink>()> factory);
    ~MediaEngine();
    void open(Item);
    void stop();
    void toggle_pause();
    bool seek_by(double seconds);
    bool seek_to(double seconds);
    void volume(int percent);
    MediaSnapshot snapshot();
};
}
