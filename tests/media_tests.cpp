#include "test.hpp"
#include "ui/layout.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
using namespace test;

TEST(actual_image_decoding_and_safe_failure) {
    auto source=fixture_source();std::atomic<bool> cancel{false};size_t before=decode_bytes_in_use();
    for(const char* name:{"Pattern.png","Pattern.bmp","Pattern.jpg","صورة عربية.png"}) {
        auto result=load_image(item(source,std::string("Photos/")+name),false,cancel);CHECK(result.error.empty());CHECK(result.bitmap);CHECK(result.original_width==120);CHECK(result.original_height==80);
        auto* pixel=result.bitmap->pixels.get()+(40*120+60)*4;CHECK(std::abs(int(pixel[0])-60)<=4);CHECK(std::abs(int(pixel[1])-40)<=4);CHECK(std::abs(int(pixel[2])-173)<=4);
    }
    for(const char* path:{"Corrupt/empty.png","Corrupt/bad.png","Corrupt/too-large.png"}) {auto result=load_image(item(source,path),false,cancel);CHECK(!result.bitmap);CHECK(!result.error.empty());}
    CHECK(decode_allocate(decode_memory_budget+1)==nullptr);CHECK(decode_bytes_in_use()==before);
    ImageWorker worker;worker.request(item(source,"Photos/Pattern.png"),true);auto generation=worker.request(item(source,"Photos/Pattern.bmp"),true);std::shared_ptr<ImageResult> result;
    wait([&]{result=worker.take();return bool(result);});CHECK(result->generation==generation);CHECK(result->path=="Photos/Pattern.bmp");CHECK(result->bitmap);result.reset();worker.cancel();
}
TEST(actual_audio_codecs_seek_and_resampling) {
    auto source=fixture_source();auto expected=bytes(root+"/tests/generated/expected-stereo.s16le");std::atomic<bool> cancel{false};
    for(const char* name:{"Tone.wav","Tone.mp3","Tone.flac","Tone.ogg","Mono44100.wav"}) {
        AudioDecoder decoder;std::string error;CHECK(decoder.open(item(source,std::string("Music/")+name),cancel,error));CHECK(decoder.info().duration>1.8 && decoder.info().duration<2.2);
        std::vector<int16_t> buffer(4096*2);CHECK(decoder.read_stereo(buffer.data(),4096)==4096);long long energy=0;for(auto s:buffer) energy+=std::abs(int(s));CHECK(energy>1000000);
        if(std::string(name)=="Tone.wav" || std::string(name)=="Tone.flac") CHECK(std::memcmp(buffer.data(),expected.data(),4096*4)==0);
        if(std::string(name)=="Tone.mp3") CHECK(decoder.info().title=="Generated tone");
        if(std::string(name)=="Mono44100.wav") for(size_t i=0;i<4096;++i) CHECK(buffer[i*2]==buffer[i*2+1]);
        double actual=0;CHECK(decoder.seek(.5,actual));CHECK(std::abs(actual-.5)<.001);CHECK(decoder.read_stereo(buffer.data(),4096)==4096);
        if(std::string(name)=="Tone.wav" || std::string(name)=="Tone.flac") CHECK(std::memcmp(buffer.data(),expected.data()+24000*4,4096*4)==0);
        CHECK(!decoder.failed());decoder.close();
    }
    for(const char* path:{"Corrupt/empty.wav","Corrupt/bad.wav","Corrupt/bad.mp3","Corrupt/bad.flac","Corrupt/bad.ogg"}) {AudioDecoder decoder;std::string error;CHECK(!decoder.open(item(source,path),cancel,error));CHECK(!error.empty());}
}
TEST(actual_avi_frames_pcm_and_seek) {
    auto source=fixture_source();std::atomic<bool> cancel{false};AviDecoder decoder;std::string error;CHECK(decoder.open(item(source,"Sample Video.avi"),cancel,error));
    CHECK(decoder.info().width==320);CHECK(decoder.info().height==180);CHECK(decoder.info().frames==300);CHECK(std::abs(decoder.info().duration-12.5)<.001);CHECK(decoder.info().has_audio);
    auto first=decoder.frame(0,error),later=decoder.frame(240,error);CHECK(first);CHECK(later);CHECK(std::abs(int(first->pixels.get()[0])-int(later->pixels.get()[0]))>30);CHECK(!decoder.frame(300,error));
    auto expected=bytes(root+"/tests/generated/expected-stereo.s16le");std::vector<int16_t> buffer(4096*2);CHECK(decoder.read_audio_stereo(buffer.data(),4096)==4096);CHECK(std::memcmp(buffer.data(),expected.data(),4096*4)==0);
    CHECK(decoder.seek_audio(.5));CHECK(decoder.read_audio_stereo(buffer.data(),4096)==4096);CHECK(std::memcmp(buffer.data(),expected.data()+24000*4,4096*4)==0);
    first.reset();later.reset();decoder.close();CHECK(decoder.open(item(source,"Movies/Short RGB.avi"),cancel,error));CHECK(!decoder.info().has_audio);
    auto raw=decoder.frame(5,error);CHECK(raw);auto* pixel=raw->pixels.get();CHECK(pixel[0]==45);CHECK(pixel[1]==25);CHECK(pixel[2]==65);
    for(const char* path:{"Clips/Unsupported.mp4","Corrupt/empty.avi","Corrupt/bad.avi","Corrupt/truncated.avi","Corrupt/oversized.avi"}) {decoder.close();CHECK(!decoder.open(item(source,path),cancel,error));CHECK(!error.empty());}
}
TEST(video_osd_never_stops_decoder_pause_seek_and_volume) {
    auto clock=std::make_shared<AudioClock>();MediaEngine engine([clock]{return std::unique_ptr<AudioSink>(new ControlledAudio(clock));});engine.open(item(fixture_source(),"Sample Video.avi"));
    wait([&]{return engine.snapshot().state==Playback::Playing;});auto session=engine.snapshot().session;Osd osd;osd.started(0);
    clock->grant(4096);wait([&]{return engine.snapshot().position>0.06;});auto before=engine.snapshot();CHECK(before.frame);CHECK(osd.visible(6.999));CHECK(!osd.visible(7));
    clock->grant(4096);wait([&]{return engine.snapshot().position>before.position+.06;});auto after=engine.snapshot();CHECK(after.session==session);CHECK(after.frame_revision>before.frame_revision);CHECK(!osd.visible(8));
    osd.activity(8,true);CHECK(osd.visible(8));CHECK(!osd.visible(15));engine.toggle_pause();clock->grant(1024);wait([&]{return engine.snapshot().paused;});double position=engine.snapshot().position;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));CHECK(engine.snapshot().position==position);osd.paused(15,true);CHECK(osd.visible(100));
    CHECK(engine.seek_to(10));wait([&]{auto s=engine.snapshot();return s.paused && s.position>9.9 && s.state==Playback::Paused;});CHECK(engine.snapshot().session==session);CHECK(engine.snapshot().frame);
    engine.volume(0);engine.toggle_pause();clock->grant(2048);wait([&]{return !engine.snapshot().paused && engine.snapshot().position>10;});
    {std::lock_guard<std::mutex> lock(clock->mutex);CHECK(!clock->last.empty());CHECK(std::all_of(clock->last.begin(),clock->last.end(),[](int16_t v){return v==0;}));}
    engine.stop();wait([&]{return clock->closed.load();});CHECK(engine.snapshot().state==Playback::Idle);
}
TEST(audio_end_and_error_cleanup) {
    auto clock=std::make_shared<AudioClock>();MediaEngine engine([clock]{return std::unique_ptr<AudioSink>(new ControlledAudio(clock));});engine.open(item(fixture_source(),"Music/Tone.wav"));
    wait([&]{return engine.snapshot().state==Playback::Playing;});clock->grant(200000);wait([&]{return engine.snapshot().state==Playback::Ended;});CHECK(std::abs(engine.snapshot().position-2)<.001);CHECK(!engine.snapshot().seekable);
    engine.open(item(fixture_source(),"Corrupt/bad.avi"));wait([&]{return engine.snapshot().state==Playback::Error;});CHECK(!engine.snapshot().error.empty());CHECK(clock->closed);engine.stop();
}
TEST(background_music_survives_library_and_photo_navigation) {
    auto source=fixture_source();auto clock=std::make_shared<AudioClock>();TestPlatform platform(clock,root+"/tests/generated/app-state");Application app(platform,{source},"/nonexistent",{},false);app.initialize();app.browser().enter_source(0);
    wait([&]{app.tick({},monotonic_now());return !app.browser().loading;});app.open_item(item(source,"Music/Tone.wav"),monotonic_now());clock->grant(2048);
    wait([&]{app.tick({},monotonic_now());return app.media().position>0;});auto before=app.media().position;RawInput back;back.buttons=Circle;app.tick(back,monotonic_now());CHECK(app.view(monotonic_now()).screen==Screen::Browser);
    app.tick({},monotonic_now());clock->grant(2048);wait([&]{app.tick({},monotonic_now());return app.media().position>before;});before=app.media().position;
    app.open_item(item(source,"Photos/Pattern.png"),monotonic_now());wait([&]{app.tick({},monotonic_now());return app.view(monotonic_now()).photo && !app.view(monotonic_now()).photo_loading;});CHECK(app.view(monotonic_now()).photo->bitmap);
    clock->grant(2048);wait([&]{app.tick({},monotonic_now());return app.media().position>before;});CHECK(app.media().kind==Kind::Music);CHECK(app.view(monotonic_now()).screen==Screen::Photo);
}
TEST(playback_disconnect_closes_resources) {
    std::string path=root+"/tests/generated/detach-media",offline=path+"-offline";mkdir(path.c_str(),0700);auto data=bytes(root+"/tests/generated/usb-demo/Music/Tone.wav");std::ofstream out(path+"/Tone.wav",std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());out.close();
    Source source;std::string error;CHECK(source_from_path(path,source,error));auto clock=std::make_shared<AudioClock>();MediaEngine engine([clock]{return std::unique_ptr<AudioSink>(new ControlledAudio(clock));});engine.open(item(source,"Tone.wav"));
    wait([&]{return engine.snapshot().state==Playback::Playing;});clock->grant(1024);wait([&]{return engine.snapshot().position>0;});CHECK(rename(path.c_str(),offline.c_str())==0);clock->grant(1024);
    wait([&]{return engine.snapshot().state==Playback::Error;});CHECK(engine.snapshot().error.find("USB disconnected")!=std::string::npos);CHECK(clock->closed);CHECK(rename(offline.c_str(),path.c_str())==0);
}
TEST(photo_disconnect_from_favorites_returns_to_sources) {
    // No directory enumeration remains active in Favorites. The source monitor
    // must still release the image, and only stop audio from the detached source.
    for (bool same_source : {false, true}) {
        std::string path=root+"/tests/generated/detach-photo",offline=path+"-offline";
        mkdir(path.c_str(),0700);
        for (const auto& pair : {std::make_pair("Photos/Pattern.png","Photo.png"),std::make_pair("Music/Tone.wav","Tone.wav")}) {
            auto data=bytes(root+"/tests/generated/usb-demo/"+pair.first);std::ofstream out(path+"/"+pair.second,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());
        }
        Source source;std::string error;CHECK(source_from_path(path,source,error));auto other=fixture_source();
        auto clock=std::make_shared<AudioClock>();TestPlatform platform(clock,root+"/tests/generated/photo-detach-state");
        Application app(platform,{source,other},"/nonexistent",{},false);app.initialize();
        app.browser().show_items(LibraryView::Favorites,{item(source,"Photo.png")},"Favorites");
        app.open_item(same_source?item(source,"Tone.wav"):item(other,"Music/Tone.wav"),monotonic_now());
        clock->grant(2048);wait([&]{app.tick({},monotonic_now());return app.media().position>0;});
        app.open_item(item(source,"Photo.png"),monotonic_now());
        wait([&]{app.tick({},monotonic_now());auto v=app.view(monotonic_now());return v.photo && v.photo->bitmap;});
        CHECK(app.view(monotonic_now()).screen==Screen::Photo);
        CHECK(rename(path.c_str(),offline.c_str())==0);
        struct Restore { std::string from,to;~Restore(){rename(from.c_str(),to.c_str());} } restore{offline,path};
        wait([&]{app.tick({},monotonic_now());return app.browser().view==LibraryView::Sources;});
        auto v=app.view(monotonic_now());CHECK(v.screen==Screen::Browser);CHECK(!v.photo);CHECK(!v.photo_loading);CHECK(!v.slideshow);
        CHECK(app.browser().sources.size()==1);CHECK(v.notice.find("USB disconnected")!=std::string::npos);
        if (same_source) { CHECK(app.media().state==Playback::Idle);wait([&]{return clock->closed.load();}); }
        else { auto position=app.media().position;clock->grant(2048);wait([&]{app.tick({},monotonic_now());return app.media().position>position;});CHECK(app.media().state==Playback::Playing); }
    }
}
TEST(decoder_allocations_are_released) {
    CHECK(decode_bytes_in_use()==0);
    for(unsigned i=0;i<20;++i) {auto source=fixture_source();std::atomic<bool> cancel{false};auto image=load_image(item(source,"Photos/Pattern.png"),false,cancel);CHECK(image.bitmap);}
    CHECK(decode_bytes_in_use()==0);
}
