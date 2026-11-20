#include "test.hpp"
#include "ui/layout.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
using namespace test;

namespace {
std::vector<uint8_t> pattern_pixels() {
    std::vector<uint8_t> pixels(120*80*4);
    for (unsigned y=0;y<80;++y) for (unsigned x=0;x<120;++x) {
        auto* p=pixels.data()+(y*120+x)*4;p[0]=x;p[1]=y;p[2]=173;p[3]=255;
    }
    return pixels;
}
std::string fresh_state(const char* name) {
    auto path=root+"/tests/generated/"+name;mkdir(path.c_str(),0700);
    std::ofstream out(path+"/state.tsv");out<<"setting\tauto_next\t0\nsetting\trepeat\t0\nsetting\tshuffle\t0\nsetting\tvolume\t100\n";
    return path;
}
void press(Application& app,Button button,double now) {
    RawInput raw;raw.buttons=button;app.tick(raw,now);app.tick({},now+.001);
}
void click(Application& app,int x,int y,double now) {
    RawInput raw;raw.pointer_pressed=true;raw.pointer_clicks=1;raw.pointer_x=x;raw.pointer_y=y;
    app.tick(raw,now);app.tick({},now+.001);
}
void select_menu(Application& app,const std::string& text,double now) {
    auto view=app.view(now);auto found=std::find(view.menu_items.begin(),view.menu_items.end(),text);CHECK(found!=view.menu_items.end());
    size_t index=static_cast<size_t>(found-view.menu_items.begin());
    for (size_t i=0;i<index;++i) press(app,Down,now+=.01);
    press(app,Cross,now+.01);
}
}

TEST(extended_image_formats_decode_real_pixels_and_reject_bad_inputs) {
    auto source=fixture_source();std::atomic<bool> cancel{false};auto expected=pattern_pixels();size_t before=decode_bytes_in_use();
    for (const char* name:{"Pattern.tga","Pattern.ppm","Pattern.pnm","Pattern.psd","Pattern lossless.webp",
                          "Pattern.tiff","Pattern LZW.tif","Pattern PackBits.tif","Pattern Deflate.tif","PNG icon.ico","Bitmap icon.ico"}) {
        auto result=load_image(item(source,std::string("Photos/")+name),false,cancel);
        CHECK(result.error.empty());CHECK(result.bitmap);CHECK(result.original_width==120);CHECK(result.original_height==80);
        CHECK(std::memcmp(result.bitmap->pixels.get(),expected.data(),expected.size())==0);CHECK(result.frames.empty());
    }
    {
        auto webp=load_image(item(source,"Photos/Pattern.webp"),false,cancel);CHECK(webp.bitmap);CHECK(webp.error.empty());
        auto* pixel=webp.bitmap->pixels.get()+(40*120+60)*4;CHECK(std::abs(int(pixel[0])-60)<10);CHECK(std::abs(int(pixel[1])-40)<10);CHECK(std::abs(int(pixel[2])-173)<10);
        auto gray=load_image(item(source,"Photos/Pattern.pgm"),false,cancel);CHECK(gray.bitmap);
        auto* g=gray.bitmap->pixels.get()+(40*120+60)*4;CHECK(g[0]==40 && g[1]==40 && g[2]==40 && g[3]==255);
        auto hdr=load_image(item(source,"Photos/Pattern.hdr"),false,cancel);CHECK(hdr.bitmap);
        auto* h=hdr.bitmap->pixels.get();CHECK(h[0]==255 && h[1]>=180 && h[1]<=190 && h[2]>=130 && h[2]<=140);
        auto mono=load_image(item(source,"Photos/Monochrome Group4.tif"),false,cancel);CHECK(mono.bitmap);
        CHECK(mono.bitmap->pixels.get()[0]==0);CHECK(mono.bitmap->pixels.get()[(79*120)*4]==255);
    }
    for (const char* name:{"Bad.gif","Truncated.gif","Too many frames.gif","Animation memory limit.gif",
                          "Bad.webp","Too many frames.webp","Bad.ico","Bad.tiff","Unsupported JPEG TIFF.tif"}) {
        auto result=load_image(item(source,std::string("Corrupt/")+name),false,cancel);
        CHECK(!result.bitmap);CHECK(result.frames.empty());CHECK(!result.error.empty());CHECK(decode_bytes_in_use()==before);
    }
    for (const char* suffix:{"heic","heif","avif","jxl"}) {
        auto result=load_image(item(source,std::string("Photos/unavailable.")+suffix),false,cancel);
        CHECK(!result.bitmap);CHECK(result.error.find("unavailable")!=std::string::npos);
    }
    // Exhaust the shared pixel/codec budget, then prove every decoder fails
    // safely and releases its partial allocations.
    std::unique_ptr<void,decltype(&decode_free)> pressure(decode_allocate(decode_memory_budget-1024),decode_free);CHECK(pressure);
    for (const char* name:{"Pattern.png","Animated.gif","Animated.webp","Pattern.tiff","PNG icon.ico"}) {
        auto result=load_image(item(source,std::string("Photos/")+name),false,cancel);CHECK(!result.bitmap);CHECK(!result.error.empty());
        CHECK(decode_bytes_in_use()==decode_memory_budget-1024);
    }
    pressure.reset();CHECK(decode_bytes_in_use()==before);
    cancel=true;auto cancelled=load_image(item(source,"Photos/Animated.webp"),false,cancel);CHECK(!cancelled.bitmap);CHECK(cancelled.frames.empty());
}

TEST(gif_and_webp_animation_timing_disposal_thumbnails_and_cleanup) {
    auto source=fixture_source();std::atomic<bool> cancel{false};size_t before=decode_bytes_in_use();
    for (const char* name:{"Animated.gif","Animated.webp"}) {
        {
            auto result=load_image(item(source,std::string("Photos/")+name),false,cancel);
            CHECK(result.error.empty());CHECK(result.bitmap);CHECK(result.frames.size()==3);CHECK(result.loop_count==0);
            const int colors[3][3]={{230,35,35},{35,230,35},{35,35,230}};const unsigned delays[]={120,180,240};
            for (size_t i=0;i<3;++i) {CHECK(result.frames[i].delay_ms==delays[i]);auto* p=result.frames[i].bitmap->pixels.get()+(40*120+60)*4;
                CHECK(p[0]==colors[i][0] && p[1]==colors[i][1] && p[2]==colors[i][2] && p[3]==255);}
            CHECK(animation_frame_at(result,0)==0);CHECK(animation_frame_at(result,.13)==1);CHECK(animation_frame_at(result,.31)==2);CHECK(animation_frame_at(result,.55)==0);
            result.loop_count=2;CHECK(animation_frame_at(result,1.2)==2);
            auto thumbnail=load_image(item(source,std::string("Photos/")+name),true,cancel);CHECK(thumbnail.bitmap);CHECK(thumbnail.frames.empty());CHECK(thumbnail.original_width==120);
        }
        CHECK(decode_bytes_in_use()==before);
    }
    {
        auto result=load_image(item(source,"Photos/Disposal.gif"),false,cancel);auto golden=bytes(root+"/tests/generated/expected-gif-disposal.rgba");
        CHECK(result.error.empty());CHECK(result.frames.size()==3);CHECK(golden.size()==3*32*24*4);
        for (size_t i=0;i<result.frames.size();++i) CHECK(std::memcmp(result.frames[i].bitmap->pixels.get(),golden.data()+i*32*24*4,32*24*4)==0);
    }
    CHECK(decode_bytes_in_use()==before);
    {ImageWorker worker;worker.request(item(source,"Photos/Animated.gif"),false);auto generation=worker.request(item(source,"Photos/Pattern.tiff"),false);
        std::shared_ptr<ImageResult> result;wait([&]{result=worker.take();return bool(result);});CHECK(result->generation==generation);CHECK(result->bitmap);CHECK(result->frames.empty());}
    CHECK(decode_bytes_in_use()==before);
}

TEST(embedded_artwork_metadata_pixels_and_bad_cover_fallback) {
    auto source=fixture_source();std::atomic<bool> cancel{false};auto expected=pattern_pixels();size_t before=decode_bytes_in_use();
    for (const char* name:{"Artwork.wav","Artwork.mp3","Artwork v24.mp3","Artwork.flac","Artwork.ogg","Artwork Arabic.mp3"}) {
        AudioDecoder decoder;std::string error;CHECK(decoder.open(item(source,std::string("Music/")+name),cancel,error));CHECK(error.empty());
        auto& info=decoder.info();CHECK(info.artwork);CHECK(info.artwork->width==120 && info.artwork->height==80);
        CHECK(std::memcmp(info.artwork->pixels.get(),expected.data(),expected.size())==0);CHECK(info.artwork_type==3);
        if (std::string(name)=="Artwork Arabic.mp3") CHECK(info.title=="موسيقى عربية");
        if (std::string(name)=="Artwork.mp3" || std::string(name)=="Artwork.wav" || std::string(name)=="Artwork v24.mp3") {
            CHECK(info.title=="Artwork test track");CHECK(info.artist=="PS4MC test fixtures");CHECK(info.album=="Embedded artwork");
        }
        int16_t buffer[2048]{};CHECK(decoder.read_stereo(buffer,1024)==1024);CHECK(std::any_of(buffer,buffer+2048,[](int16_t v){return v!=0;}));
        decoder.close();CHECK(decode_bytes_in_use()==before);
    }
    {
        AudioDecoder decoder;std::string error;CHECK(decoder.open(item(source,"Music/Corrupt artwork.mp3"),cancel,error));CHECK(!decoder.info().artwork);
        int16_t buffer[2048]{};CHECK(decoder.read_stereo(buffer,1024)==1024);CHECK(std::any_of(buffer,buffer+2048,[](int16_t v){return v!=0;}));
    }
    {
        AudioDecoder decoder;std::string error;CHECK(decoder.open(item(source,"Music/Playlist/01 - First track.wav"),cancel,error));
        auto cover=decoder.info().artwork;auto expected_cover=bytes(root+"/tests/generated/expected-artwork-cover.rgba");
        CHECK(cover && cover->width==512 && cover->height==512);CHECK(std::memcmp(cover->pixels.get(),expected_cover.data(),expected_cover.size())==0);
        CHECK(decoder.info().duration==35);
    }
    CHECK(decode_bytes_in_use()==before);
}

TEST(common_ffmpeg_music_formats_decode_seek_and_finish_cleanly) {
    auto source=fixture_source();std::atomic<bool> cancel{false};
    for (const char* name:{"Tone.aac","Tone.m4a","Tone.opus","Tone.wma","Tone.aiff"}) {
        AudioDecoder decoder;std::string error;CHECK(decoder.open(item(source,std::string("Music/")+name),cancel,error));CHECK(error.empty());
        CHECK(decoder.info().duration>1.9 && decoder.info().duration<2.2);CHECK(decoder.info().channels==2);CHECK(decoder.info().sample_rate==48000);
        int16_t buffer[4096]{};CHECK(decoder.read_stereo(buffer,2048)==2048);
        CHECK(std::any_of(buffer,buffer+4096,[](int16_t value){return value!=0;}));
        double actual=0;CHECK(decoder.seek(1.0,actual));CHECK(std::abs(actual-1.0)<.001);CHECK(decoder.read_stereo(buffer,2048)==2048);CHECK(!decoder.failed());
    }
}

TEST(compact_music_buttons_control_real_playback_playlist_back_and_stop) {
    auto source=fixture_source();auto clock=std::make_shared<AudioClock>();TestPlatform platform(clock,fresh_state("music-controls-state"));
    Application app(platform,{source},"/nonexistent",{},false);app.initialize();app.browser().change_tab(2);app.browser().enter_source(0);
    auto pump=[&]{app.tick({},monotonic_now());};auto loaded=[&]{wait([&]{pump();return !app.browser().loading;});};loaded();
    auto it=std::find_if(app.browser().entries.begin(),app.browser().entries.end(),[](const Entry& e){return e.name=="Playlist";});CHECK(it!=app.browser().entries.end());
    app.browser().move(static_cast<int>(it-app.browser().entries.begin()));CHECK(app.browser().enter_selected_folder());loaded();CHECK(app.browser().entries.size()==2);
    app.open_item(item(source,"Music/Playlist/01 - First track.wav"),monotonic_now());clock->grant(2048);
    wait([&]{pump();return app.media().state==Playback::Playing && app.media().position>0;});CHECK(app.media().artwork);CHECK(app.view(monotonic_now()).playlist_count==2);
    auto session=app.media().session;
    auto control=[&](unsigned index){click(app,music_layout::controls_left+index*music_layout::control_stride+music_layout::control_width/2,music_layout::controls_top+55,monotonic_now());};
    control(2);clock->grant(1024);wait([&]{pump();return app.media().state==Playback::Paused;});double paused=app.media().position;
    control(3);wait([&]{pump();return app.media().state==Playback::Paused && app.media().position>paused+9.9;});CHECK(app.media().session==session);
    control(1);wait([&]{pump();return app.media().state==Playback::Paused && std::abs(app.media().position-paused)<.001;});
    click(app,music_layout::controls_left+music_layout::control_width+5,music_layout::controls_top+55,monotonic_now());
    CHECK(app.media().session==session);CHECK(app.media().paused);
    // Circle/Back leaves the browser usable while the same audio session continues.
    click(app,music_layout::back_left+40,music_layout::back_top+20,monotonic_now());CHECK(app.view(monotonic_now()).screen==Screen::Browser);CHECK(app.media().session==session);
    app.open_item(item(source,"Music/Playlist/01 - First track.wav"),monotonic_now());clock->grant(1024);wait([&]{pump();return app.media().state==Playback::Playing;});
    control(4);clock->grant(1024);wait([&]{pump();return app.media().state==Playback::Playing && app.media().item.entry.name=="02 - Next track.wav";});
    CHECK(app.view(monotonic_now()).playlist_index==1);CHECK(app.media().artwork);
    control(0);clock->grant(1024);wait([&]{pump();return app.media().state==Playback::Playing && app.media().item.entry.name=="01 - First track.wav";});
    control(5);wait([&]{pump();return app.media().state==Playback::Idle && clock->closed.load();});CHECK(app.view(monotonic_now()).screen==Screen::Browser);CHECK(!app.media().artwork);
    app.browser().show_sources();app.open_item(item(source,"Music/Artwork.wav"),monotonic_now());clock->grant(1024);wait([&]{pump();return app.media().state==Playback::Playing;});
    // A direct open without directory context creates a one-item queue.
    CHECK(app.view(monotonic_now()).playlist_count==1);session=app.media().session;
    control(0);control(4);CHECK(app.media().session==session);
    press(app,Square,monotonic_now());wait([&]{pump();return app.media().state==Playback::Idle;});
}

TEST(compact_video_buttons_control_real_frames_seek_playlist_and_stop) {
    auto source=fixture_source();auto clock=std::make_shared<AudioClock>();auto state=fresh_state("video-controls-state");
    {
        StateStore store(state,{source});auto saved=store.read();
        saved.video_queue={item(source,"Sample Video.avi"),item(source,"Clips/OSD timing.avi")};store.save(saved);
    }
    TestPlatform platform(clock,state);Application app(platform,{source},"/nonexistent",{},false);app.initialize();
    auto pump=[&]{app.tick({},monotonic_now());};
    auto control=[&](unsigned index){click(app,video_layout::controls_left+index*video_layout::control_stride+video_layout::control_width/2,video_layout::controls_top+35,monotonic_now());};
    app.open_item(item(source,"Sample Video.avi"),monotonic_now());clock->grant(4096);
    wait([&]{pump();return app.media().state==Playback::Playing && app.media().position>.05;});
    CHECK(app.media().frame);CHECK(app.view(monotonic_now()).playlist_count==2);auto session=app.media().session;
    control(2);clock->grant(1024);wait([&]{pump();return app.media().state==Playback::Paused;});
    double paused=app.media().position;auto frame=app.media().frame;auto revision=app.media().frame_revision;
    control(3);wait([&]{pump();return app.media().state==Playback::Paused && app.media().position>paused+9.9;});
    CHECK(app.media().session==session);CHECK(app.media().frame_revision>revision);
    CHECK(std::memcmp(frame->pixels.get(),app.media().frame->pixels.get(),frame->width*frame->height*4)!=0);
    // Both AVI seeks snap down to a 24-fps frame boundary.
    control(1);wait([&]{pump();return app.media().state==Playback::Paused && std::abs(app.media().position-paused)<.09;});
    auto position=app.media().position;
    click(app,video_layout::controls_left+video_layout::control_width+5,video_layout::controls_top+35,monotonic_now());
    CHECK(app.media().position==position);CHECK(app.media().paused);CHECK(app.media().session==session);
    click(app,video_layout::aspect_left+50,video_layout::aspect_top+20,monotonic_now());
    CHECK(app.view(monotonic_now()).video_aspect==Aspect::Fill);CHECK(app.media().paused);
    press(app,Square,monotonic_now());CHECK(app.view(monotonic_now()).video_aspect==Aspect::Original);CHECK(app.media().session==session);
    click(app,video_layout::options_left+50,video_layout::options_top+20,monotonic_now());
    CHECK(app.view(monotonic_now()).modal==Modal::Menu);press(app,Circle,monotonic_now());CHECK(app.view(monotonic_now()).screen==Screen::Video);
    control(4);clock->grant(2048);wait([&]{pump();return app.media().state==Playback::Playing && app.media().item.entry.relative=="Clips/OSD timing.avi";});
    CHECK(app.view(monotonic_now()).playlist_index==1);CHECK(app.media().duration==20);CHECK(app.media().session!=session);
    control(0);clock->grant(2048);wait([&]{pump();return app.media().state==Playback::Playing && app.media().item.entry.relative=="Sample Video.avi";});
    CHECK(app.view(monotonic_now()).playlist_index==0);CHECK(app.media().frame);
    control(5);wait([&]{pump();return app.media().state==Playback::Idle && clock->closed.load();});
    CHECK(app.view(monotonic_now()).screen==Screen::Browser);CHECK(!app.media().frame);
    app.open_item(item(source,"Sample Video.avi"),monotonic_now());clock->grant(1024);wait([&]{pump();return app.media().state==Playback::Playing;});
    click(app,video_layout::back_left+50,video_layout::back_top+20,monotonic_now());
    wait([&]{pump();return app.media().state==Playback::Idle && clock->closed.load();});CHECK(app.view(monotonic_now()).screen==Screen::Browser);
}

TEST(video_controls_reveal_without_action_keep_pause_and_replay_after_timeout) {
    auto source=fixture_source();auto clock=std::make_shared<AudioClock>();TestPlatform platform(clock,fresh_state("video-osd-controls-state"));
    Application app(platform,{source},"/nonexistent",{},false);app.initialize();double now=1000;
    auto pump=[&]{app.tick({},now);};
    auto control=[&](unsigned index){click(app,video_layout::controls_left+index*video_layout::control_stride+video_layout::control_width/2,video_layout::controls_top+35,now);};
    // A short real AVI exercises EOF/replay without decoding 300 large frames.
    app.open_item(item(source,"Movies/Silent MJPEG.avi"),now);clock->grant(4096);
    wait([&]{pump();return app.media().state==Playback::Playing && app.media().position>.05;});
    CHECK(app.view(now).playlist_count==1);auto session=app.media().session;
    now+=1;control(0);now+=1;control(4);CHECK(app.media().session==session);CHECK(app.media().state==Playback::Playing);
    CHECK(app.view(now+6.999).osd);now+=7;pump();CHECK(!app.view(now).osd);
    double before=app.media().position;auto revision=app.media().frame_revision;
    now+=1;control(2);CHECK(app.view(now).osd);CHECK(app.media().state==Playback::Playing);CHECK(!clock->paused);CHECK(app.media().session==session);
    clock->grant(4096);wait([&]{pump();return app.media().position>before+.06;});CHECK(app.media().frame_revision>revision);
    CHECK(app.view(now+6.999).osd);CHECK(!app.view(now+7).osd);
    now+=.1;control(2);clock->grant(1024);wait([&]{pump();return app.media().state==Playback::Paused;});
    auto paused=app.media().position;now+=100;pump();CHECK(app.view(now).osd);CHECK(app.media().position==paused);
    control(2);clock->grant(output_sample_rate*3);wait([&]{pump();return app.media().state==Playback::Ended;});
    CHECK(!app.media().seekable);now+=8;pump();CHECK(!app.view(now).osd);
    // EOF controls stay drawn and clickable even after the activity timer expires.
    control(1);CHECK(app.media().state==Playback::Ended);CHECK(app.media().session==session);
    now+=1;control(2);clock->grant(2048);wait([&]{pump();return app.media().state==Playback::Playing && app.media().position>0;});
    CHECK(app.media().session!=session);CHECK(app.media().position<1);CHECK(app.media().frame);
    control(5);wait([&]{pump();return app.media().state==Playback::Idle;});CHECK(app.view(now).screen==Screen::Browser);
    now+=1;app.open_item(item(source,"Corrupt/bad.avi"),now);wait([&]{pump();return app.media().state==Playback::Error;});
    session=app.media().session;now+=8;pump();control(2);CHECK(app.media().state==Playback::Error);CHECK(app.media().session==session);
    click(app,video_layout::back_left+50,video_layout::back_top+20,now);wait([&]{pump();return app.media().state==Playback::Idle;});
    CHECK(app.view(now).screen==Screen::Browser);
}

TEST(photo_animation_pause_resume_and_nonmedia_information) {
    auto source=fixture_source();auto clock=std::make_shared<AudioClock>();TestPlatform platform(clock,fresh_state("animation-controls-state"));
    Application app(platform,{source},"/nonexistent",{},false);app.initialize();double now=monotonic_now();
    app.open_item(item(source,"Photos/Animated.gif"),now);wait([&]{app.tick({},now);auto v=app.view(now);return v.photo && !v.photo_loading;});
    CHECK(app.view(now).photo->frames.size()==3);app.tick({},now+.13);CHECK(app.view(now+.13).photo_frame==1);
    press(app,Options,now+.14);select_menu(app,"Pause animation",now+.15);now+=.23;
    CHECK(app.view(now).animation_paused);auto frozen=app.view(now).photo_frame;app.tick({},now+3);CHECK(app.view(now+3).photo_frame==frozen);
    press(app,Options,now+3.01);select_menu(app,"Play animation",now+3.02);app.tick({},now+3.35);CHECK(!app.view(now+3.35).animation_paused);
    press(app,Circle,now+3.36);CHECK(app.view(now+3.36).screen==Screen::Browser);CHECK(!app.view(now+3.36).photo);
    app.browser().enter_source(0);wait([&]{app.tick({},monotonic_now());return !app.browser().loading;});app.browser().search("Readme");
    wait([&]{app.tick({},monotonic_now());return !app.browser().loading;});CHECK(app.browser().entries.size()==1);
    press(app,Cross,monotonic_now());CHECK(app.media().state==Playback::Idle);CHECK(app.view(monotonic_now()).screen==Screen::Browser);CHECK(app.view(monotonic_now()).notice.find("Cannot open")!=std::string::npos);
    press(app,Square,monotonic_now());auto view=app.view(monotonic_now());CHECK(view.modal==Modal::Details);
    CHECK(std::any_of(view.information.begin(),view.information.end(),[](const std::string& line){return line.find("Cannot open")!=std::string::npos;}));
}
