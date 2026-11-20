#include "platform/platform.hpp"
#include "startup_trace.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <orbis/AudioOut.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

namespace mc {
template<class Call> static auto boot_call(const char* stage,Call call) {
    startup_trace(stage,0,"begin");
    auto result=call();startup_trace(stage,static_cast<int64_t>(result),"returned");return result;
}
class Ps4Audio final:public AudioSink {
    int user_=-1,handle_=-1;
    uint64_t played_=0;
    bool paused_=false;
public:
    explicit Ps4Audio(int user):user_(user) {}
    ~Ps4Audio() override { close(); }
    bool open(std::string& error) override {
        // Homebrew audio ports belong to the system audio user.  The login user
        // returned by sceUserServiceGetInitialUser is valid for Pad, but some
        // firmwares reject it here with SCE_DEVICE_SERVICE_ERROR_INVALID_USER
        // (0x809b0001).  Keep the login user only as a compatibility fallback.
        handle_=sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM,ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,0,audio_block_frames,output_sample_rate,ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
        startup_trace("sceAudioOutOpen.system",handle_,"user=255, main, S16 stereo, 48 kHz");
        if (handle_<0 && user_!=ORBIS_USER_SERVICE_USER_ID_SYSTEM) {
            const int system_error=handle_;
            handle_=sceAudioOutOpen(user_,ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,0,audio_block_frames,output_sample_rate,ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
            startup_trace("sceAudioOutOpen.login",handle_,"compatibility fallback");
            if (handle_<0) {
                error="sceAudioOutOpen failed for system user ("+std::to_string(system_error)+") and login user ("+std::to_string(handle_)+")";
                return false;
            }
        } else if (handle_<0) {
            error="sceAudioOutOpen for system user returned "+std::to_string(handle_);
            return false;
        }
        played_=0;return true;
    }
    bool push(const int16_t* data,size_t frames,const std::atomic<bool>& cancel) override {
        if (cancel || paused_ || handle_<0 || frames>audio_block_frames) return false;
        std::array<int16_t,audio_block_frames*2> buffer{};std::memcpy(buffer.data(),data,frames*2*sizeof(int16_t));
        int rc=sceAudioOutOutput(handle_,buffer.data());if (rc<0) return false;played_+=frames;return true;
    }
    uint64_t played_frames() const override { return played_; }
    void pause(bool p) override { paused_=p;if (p && handle_>=0) { std::array<int16_t,audio_block_frames*2> silence{};sceAudioOutOutput(handle_,silence.data()); } }
    void flush() override { played_=0;if (handle_>=0) { std::array<int16_t,audio_block_frames*2> silence{};sceAudioOutOutput(handle_,silence.data()); } }
    void close() override { if (handle_>=0) { sceAudioOutClose(handle_);handle_=-1; } }
};
class Ps4Platform final:public Platform {
    int video_=-1,pad_=-1,user_=-1,buffer_set_=-1,active_=0;
    bool user_owned_=false,queue_owned_=false,audio_ready_=false,first_poll_=true;
    OrbisKernelEqueue queue_{};
    void* memory_=nullptr;off_t memory_offset_=0;size_t memory_bytes_=0;
    std::array<void*,2> buffers_{};
    std::array<uint64_t,2> revisions_{{UINT64_MAX,UINT64_MAX}};
    int64_t frame_id_=0,last_flip_=-1;
    bool wait_flip(std::string& error) {
        if (last_flip_<0) return true;
        double deadline=monotonic_now()+0.5;
        while (monotonic_now()<deadline) {
            OrbisVideoOutFlipStatus status{};int rc=sceVideoOutGetFlipStatus(video_,&status);
            if (rc<0) { error="sceVideoOutGetFlipStatus failed: "+std::to_string(rc);return false; }
            if (status.flipArg>=last_flip_) return true;
            OrbisKernelEvent event{};int count=0;OrbisKernelUseconds timeout=50000;
            sceKernelWaitEqueue(queue_,&event,1,&count,&timeout);
        }
        error="Video flip timed out";return false;
    }
public:
    ~Ps4Platform() override {
        if (pad_>=0) scePadClose(pad_);
        if (video_>=0) { std::string ignored;wait_flip(ignored);if (buffer_set_>=0) sceVideoOutUnregisterBuffers(video_,buffer_set_);int rc=sceVideoOutClose(video_);if (rc<0) memory_=nullptr; }
        if (queue_owned_) sceKernelDeleteEqueue(queue_);
        if (memory_) { sceKernelMunmap(memory_,memory_bytes_);sceKernelReleaseDirectMemory(memory_offset_,memory_bytes_); }
        if (user_owned_) sceUserServiceTerminate();
    }
    bool initialize(std::string& error) override {
        OrbisUserServiceInitializeParams params{};params.priority=ORBIS_KERNEL_PRIO_FIFO_LOWEST;
        user_owned_=boot_call("sceUserServiceInitialize",[&]{return sceUserServiceInitialize(&params);})>=0;
        int rc=boot_call("sceUserServiceGetInitialUser",[&]{return sceUserServiceGetInitialUser(&user_);});if (rc<0) { error="Cannot obtain active PS4 user: "+std::to_string(rc);return false; }
        rc=boot_call("scePadInit",[]{return scePadInit();});if (rc<0) { error="scePadInit failed: "+std::to_string(rc);return false; }
        pad_=boot_call("scePadOpen",[&]{return scePadOpen(user_,ORBIS_PAD_PORT_TYPE_STANDARD,0,nullptr);});if (pad_<0) { error="scePadOpen failed: "+std::to_string(pad_);return false; }
        audio_ready_=boot_call("sceAudioOutInit",[]{return sceAudioOutInit();})>=0;
        video_=boot_call("sceVideoOutOpen",[]{return sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN,ORBIS_VIDEO_OUT_BUS_MAIN,0,nullptr);});if (video_<0) { error="sceVideoOutOpen failed: "+std::to_string(video_);return false; }
        rc=boot_call("sceKernelCreateEqueue",[&]{return sceKernelCreateEqueue(&queue_,"PS4MC flips");});if (rc<0) { error="Cannot create video event queue";return false; }queue_owned_=true;
        rc=boot_call("sceVideoOutAddFlipEvent",[&]{return sceVideoOutAddFlipEvent(queue_,video_,nullptr);});if (rc<0) { error="Cannot register flip event";return false; }
        constexpr size_t framebuffer_bytes=size_t(screen_width)*screen_height*4,alignment=0x200000;
        memory_bytes_=(framebuffer_bytes*2+alignment-1)&~(alignment-1);
        auto direct_size=boot_call("sceKernelGetDirectMemorySize",[]{return sceKernelGetDirectMemorySize();});
        rc=boot_call("sceKernelAllocateDirectMemory",[&]{return sceKernelAllocateDirectMemory(0,direct_size,memory_bytes_,alignment,3,&memory_offset_);});
        if (rc<0) { error="Framebuffer allocation failed: "+std::to_string(rc);memory_bytes_=0;return false; }
        rc=boot_call("sceKernelMapDirectMemory",[&]{return sceKernelMapDirectMemory(&memory_,memory_bytes_,0x33,0,memory_offset_,alignment);});
        if (rc<0) { sceKernelReleaseDirectMemory(memory_offset_,memory_bytes_);memory_=nullptr;memory_bytes_=0;error="Framebuffer mapping failed";return false; }
        buffers_[0]=memory_;buffers_[1]=static_cast<uint8_t*>(memory_)+framebuffer_bytes;std::memset(memory_,0,memory_bytes_);
        OrbisVideoOutBufferAttribute attributes{};
        sceVideoOutSetBufferAttribute(&attributes,ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,ORBIS_VIDEO_OUT_TILING_MODE_LINEAR,ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9,screen_width,screen_height,screen_width);
        buffer_set_=boot_call("sceVideoOutRegisterBuffers",[&]{return sceVideoOutRegisterBuffers(video_,0,buffers_.data(),2,&attributes);});
        if (buffer_set_<0) { error="Framebuffer registration failed";return false; }
        rc=boot_call("sceVideoOutSetFlipRate",[&]{return sceVideoOutSetFlipRate(video_,ORBIS_VIDEO_OUT_FLIP_60HZ);});
        if (rc<0) rc=boot_call("sceVideoOutSetFlipRate",[&]{return sceVideoOutSetFlipRate(video_,ORBIS_VIDEO_OUT_FLIP_30HZ);});
        if (rc<0) { error="Setting video flip rate failed";return false; }return true;
    }
    bool poll(RawInput& input) override {
        OrbisPadData data{};
        int count=first_poll_?boot_call("scePadRead.first",[&]{return scePadRead(pad_,&data,1);}):scePadRead(pad_,&data,1);
        first_poll_=false;input={};input.connected=count>0 && data.connected;
        if (!input.connected) return true;
        struct Map { uint32_t native;Button portable; };
        const Map map[]={{ORBIS_PAD_BUTTON_UP,Up},{ORBIS_PAD_BUTTON_DOWN,Down},{ORBIS_PAD_BUTTON_LEFT,Left},{ORBIS_PAD_BUTTON_RIGHT,Right},
            {ORBIS_PAD_BUTTON_CROSS,Cross},{ORBIS_PAD_BUTTON_CIRCLE,Circle},{ORBIS_PAD_BUTTON_TRIANGLE,Triangle},{ORBIS_PAD_BUTTON_SQUARE,Square},
            {ORBIS_PAD_BUTTON_L1,L1},{ORBIS_PAD_BUTTON_R1,R1},{ORBIS_PAD_BUTTON_L2,L2},{ORBIS_PAD_BUTTON_R2,R2},{ORBIS_PAD_BUTTON_OPTIONS,Options},
            {ORBIS_PAD_BUTTON_L3,L3},{ORBIS_PAD_BUTTON_R3,R3},{ORBIS_PAD_BUTTON_TOUCH_PAD,Touch}};
        for (const auto& m:map) if (data.buttons&m.native) input.buttons|=m.portable;
        input.lx=(int(data.leftStick.x)-128)/127.f;input.ly=(int(data.leftStick.y)-128)/127.f;
        input.rx=(int(data.rightStick.x)-128)/127.f;input.ry=(int(data.rightStick.y)-128)/127.f;
        input.lt=data.analogButtons.l2/255.f;input.rt=data.analogButtons.r2/255.f;return true;
    }
    bool present(const uint32_t* pixels,uint64_t revision,std::string& error) override {
        if (!wait_flip(error)) return false;
        if (revisions_[active_]!=revision) { std::memcpy(buffers_[active_],pixels,size_t(screen_width)*screen_height*4);revisions_[active_]=revision; }
        int rc=sceVideoOutSubmitFlip(video_,active_,ORBIS_VIDEO_OUT_FLIP_VSYNC,frame_id_);if (rc<0) { error="Video flip failed: "+std::to_string(rc);return false; }
        last_flip_=frame_id_++;active_=1-active_;return true;
    }
    std::unique_ptr<AudioSink> make_audio_sink() override { if (!audio_ready_) return {};return std::unique_ptr<AudioSink>(new Ps4Audio(user_)); }
    std::string assets_root() const override { return "/app0/assets"; }
    std::string data_root() const override { return "/data/PS4MediaCenter"; }
};
std::unique_ptr<Platform> make_native_platform(const std::string&,const std::string&) { return std::unique_ptr<Platform>(new Ps4Platform); }
}
