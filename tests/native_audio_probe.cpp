#include "platform/platform.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <thread>
int main(int argc,char** argv) {
    if(argc<3) return 2;auto platform=mc::make_native_platform(argv[1],argv[2]);auto sink=platform->make_audio_sink();std::string error;
    if(!sink || !sink->open(error)) {std::fprintf(stderr,"Native audio output: %s\n",error.c_str());return 1;}
    std::array<int16_t,1024*2> silence{};std::atomic<bool> cancel{false};if(!sink->push(silence.data(),1024,cancel)) return 1;
    auto deadline=mc::monotonic_now()+2;while(sink->played_frames()<1024 && mc::monotonic_now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    bool ok=sink->played_frames()==1024;std::printf("CoreAudio 48 kHz stereo: %s, %llu silence frames consumed\n",ok?"PASS":"FAIL",static_cast<unsigned long long>(sink->played_frames()));sink->close();return ok?0:1;
}
