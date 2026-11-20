#include "platform/platform.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>

namespace mc {
class HeadlessAudio final:public AudioSink {
    std::string dump_;
    FILE* file_=nullptr;
    std::atomic<uint64_t> played_{0};
    std::atomic<bool> paused_{false};
    std::mutex mutex_;
    std::condition_variable wake_;
public:
    explicit HeadlessAudio(std::string dump):dump_(std::move(dump)) {}
    ~HeadlessAudio() override { close(); }
    bool open(std::string& error) override {
        if (!dump_.empty()) { file_=std::fopen(dump_.c_str(),"wb");if (!file_) { error="Cannot create host PCM capture";return false; } }played_=0;return true;
    }
    bool push(const int16_t* data,size_t frames,const std::atomic<bool>& cancel) override {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock,std::chrono::duration<double>(double(frames)/output_sample_rate),[&]{return cancel.load();});
        if (cancel || paused_) return false;
        if (file_ && std::fwrite(data,sizeof(int16_t)*2,frames,file_)!=frames) return false;
        played_.fetch_add(frames);return true;
    }
    uint64_t played_frames() const override { return played_; }
    void pause(bool p) override { paused_=p; }
    void flush() override { played_=0; }
    void close() override { if (file_) { std::fclose(file_);file_=nullptr; } }
};
std::unique_ptr<AudioSink> make_headless_audio_sink(const std::string& dump) { return std::unique_ptr<AudioSink>(new HeadlessAudio(dump)); }
}
