#include "engine.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace mc {
MediaEngine::MediaEngine(std::function<std::unique_ptr<AudioSink>()> factory):sink_factory_(std::move(factory)) { thread_=std::thread(&MediaEngine::run,this); }
MediaEngine::~MediaEngine() { quit_=true;cancel_=true;wake_.notify_all();thread_.join(); }
MediaSnapshot MediaEngine::snapshot() { std::lock_guard<std::mutex> lock(mutex_);auto out=snapshot_;out.volume=volume_;return out; }
void MediaEngine::open(Item item) {
    std::lock_guard<std::mutex> lock(mutex_);cancel_=true;open_item_=std::move(item);open_pending_=true;stop_pending_=pause_pending_=seek_pending_=false;++session_;
    snapshot_={};snapshot_.state=Playback::Loading;snapshot_.item=open_item_;snapshot_.kind=open_item_.entry.kind;snapshot_.session=session_;wake_.notify_one();
}
void MediaEngine::stop() { std::lock_guard<std::mutex> lock(mutex_);cancel_=true;stop_pending_=true;open_pending_=pause_pending_=seek_pending_=false;++session_;snapshot_={};snapshot_.session=session_;wake_.notify_one(); }
void MediaEngine::toggle_pause() {
    std::lock_guard<std::mutex> lock(mutex_);if (snapshot_.state!=Playback::Playing && snapshot_.state!=Playback::Paused && snapshot_.state!=Playback::Seeking) return;
    pause_pending_=!pause_pending_;wake_.notify_one();
}
bool MediaEngine::seek_by(double delta) {
    std::lock_guard<std::mutex> lock(mutex_);if (!snapshot_.seekable || !std::isfinite(delta)) return false;
    seek_target_=std::max(0.0,std::min(snapshot_.duration,(seek_pending_?seek_target_:snapshot_.position)+delta));seek_pending_=true;snapshot_.state=Playback::Seeking;wake_.notify_one();return true;
}
bool MediaEngine::seek_to(double target) {
    std::lock_guard<std::mutex> lock(mutex_);if (!snapshot_.seekable || !std::isfinite(target)) return false;
    seek_target_=std::max(0.0,std::min(snapshot_.duration,target));seek_pending_=true;snapshot_.state=Playback::Seeking;wake_.notify_one();return true;
}
void MediaEngine::volume(int percent) { volume_=std::max(0,std::min(100,percent)); }
void MediaEngine::run() {
    AudioDecoder audio;VideoDecoder video;std::unique_ptr<AudioSink> sink;
    MediaSnapshot state;bool active=false;uint64_t generation=0,submitted=0;
    double base=0,wall_start=0;std::array<int16_t,audio_block_frames*2> pcm{};
    auto close_sink=[&] { if (sink) { sink->close();sink.reset(); } };
    auto close_media=[&] { audio.close();video.close();active=false;state.frame.reset();state.artwork.reset();submitted=0; };
    auto close_all=[&] { close_sink();close_media(); };
    auto publish=[&] { std::lock_guard<std::mutex> lock(mutex_);if (generation==session_) { state.volume=volume_;snapshot_=state; } };
    auto fail=[&](std::string error) { state.state=Playback::Error;state.error=std::move(error);state.paused=false;close_all();publish(); };
    while (!quit_) {
        bool opening=false,stopping=false,toggle=false,seeking=false;Item item;double target=0;uint64_t new_generation=0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!active && !open_pending_ && !stop_pending_) wake_.wait(lock,[&] { return quit_ || open_pending_ || stop_pending_; });
            if (quit_) break;
            opening=open_pending_;stopping=stop_pending_;toggle=pause_pending_;seeking=seek_pending_;item=open_item_;target=seek_target_;new_generation=session_;
            open_pending_=stop_pending_=pause_pending_=seek_pending_=false;
            if (opening || stopping) cancel_=false;
        }
        if (stopping) { close_all();generation=new_generation;state={};state.session=generation;publish();continue; }
        if (opening) {
            close_media();generation=new_generation;state={};state.session=generation;state.item=item;state.kind=item.entry.kind;state.state=Playback::Loading;
            std::string error;bool ok=false;
            if (state.kind==Kind::Video) {
                ok=video.open(item,cancel_,error);
                if (ok) { auto info=video.info();state.duration=info.duration;state.has_audio=info.has_audio;state.width=info.width;state.height=info.height;state.codec=info.codec;state.frame=video.frame_at(0,error);ok=bool(state.frame);state.frame_revision=1; }
            } else if (state.kind==Kind::Music) {
                ok=audio.open(item,cancel_,error);
                if (ok) { auto info=audio.info();state.duration=info.duration;state.has_audio=true;state.codec=info.codec;state.title=info.title;state.artist=info.artist;state.album=info.album;state.artwork=info.artwork; }
            } else error="The selected file is not a playable audio/video type";
            if (cancel_) { close_media();continue; }
            if (!ok) { if (state.has_audio) close_sink();fail(error.empty()?"Decoder initialization failed":error);continue; }
            if (state.has_audio) {
                if (sink) {
                    sink->pause(true);sink->flush();sink->pause(false);
                } else {
                    sink=sink_factory_();
                    if (!sink || !sink->open(error)) { fail("Audio output unavailable: "+error);continue; }
                }
            } else close_sink();
            if (cancel_) { close_media();if (!state.has_audio) close_sink();continue; }
            base=0;wall_start=monotonic_now();submitted=0;state.position=0;state.seekable=true;state.paused=false;state.state=Playback::Playing;active=true;publish();
        }
        if (!active) continue;
        if (cancel_) continue;
        if (!source_present(state.item.source)) { fail("USB disconnected. Playback stopped and media resources closed.");continue; }
        if (toggle) {
            state.paused=!state.paused;if (sink) sink->pause(state.paused);
            if (!state.paused) wall_start=monotonic_now()-state.position;
            state.state=state.paused?Playback::Paused:Playback::Playing;publish();
        }
        if (seeking) {
            double actual=target;
            if (state.kind==Kind::Video) {
                std::string error;
                if (!video.seek(target,actual,error)) { fail(error.empty()?"Video seek failed":error);continue; }
            }
            else if (!audio.seek(target,actual)) { state.error="Seek failed for this file";state.state=state.paused?Playback::Paused:Playback::Playing;publish();continue; }
            if (sink) { sink->flush();sink->pause(state.paused); }
            base=actual;submitted=0;wall_start=monotonic_now()-actual;state.position=actual;
            if (state.kind==Kind::Video && actual<state.duration) {
                std::string error;
                auto frame=video.frame_at(actual,error);if (!frame) { fail(error);continue; }state.frame=std::move(frame);++state.frame_revision;
            }
            state.state=state.paused?Playback::Paused:Playback::Playing;state.error.clear();publish();
        }
        if (state.paused || state.state==Playback::Ended) {
            std::unique_lock<std::mutex> lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(50),[&]{ return quit_ || open_pending_ || stop_pending_ || pause_pending_ || seek_pending_; });continue;
        }
        if (state.has_audio) {
            uint64_t expected=static_cast<uint64_t>(std::ceil(std::max(0.0,state.duration-base)*output_sample_rate-1e-8));
            size_t count=static_cast<size_t>(std::min<uint64_t>(audio_block_frames,expected>submitted?expected-submitted:0));
            if (count) {
                size_t got=state.kind==Kind::Video?video.read_audio_stereo(pcm.data(),count):audio.read_stereo(pcm.data(),count);
                if (cancel_) continue;
                if (state.kind==Kind::Video && video.audio_failed()) { fail("Video audio read/decode failed or storage disconnected");continue; }
                if (state.kind==Kind::Video && got<count) std::fill(pcm.begin()+static_cast<ptrdiff_t>(got*2),pcm.begin()+static_cast<ptrdiff_t>(count*2),0);
                else if (got<count) { fail("Audio data ended before its declared duration; the file may be corrupt");continue; }
                int gain=volume_;for (size_t i=0;i<count*2;++i) pcm[i]=static_cast<int16_t>(int32_t(pcm[i])*gain/100);
                if (!sink->push(pcm.data(),count,cancel_)) { if (!cancel_) fail("Audio output failed");continue; }submitted+=count;
            }
            state.position=std::min(state.duration,base+double(sink->played_frames())/output_sample_rate);
            if (!count && sink->played_frames()<submitted) {
                std::unique_lock<std::mutex> lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(5));
            }
        } else state.position=std::min(state.duration,monotonic_now()-wall_start);
        if (state.kind==Kind::Video && state.position<state.duration) {
            std::string error;auto frame=video.frame_at(state.position,error);if (!frame) { if (!cancel_) fail(error);continue; }
            if (frame!=state.frame) { state.frame=std::move(frame);++state.frame_revision; }
        }
        if (state.position>=state.duration-0.00001) {
            state.position=state.duration;state.state=Playback::Ended;if (sink) sink->pause(true);
            // Playback is finished; keep only the last displayed bitmap, close file/decoder handles.
            audio.close();video.close();state.seekable=false;
        }
        publish();
        if (!state.has_audio && state.state!=Playback::Ended) {
            std::unique_lock<std::mutex> lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(3),[&]{ return quit_ || open_pending_ || stop_pending_ || pause_pending_ || seek_pending_; });
        }
    }
    close_all();
}
}
