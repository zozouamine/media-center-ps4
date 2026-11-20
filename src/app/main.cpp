#include "application.hpp"
#include "platform/ps4/startup_trace.hpp"
#include "platform/ps4/usb_access.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace mc {
#ifndef MC_PS4
class HeadlessPlatform final:public Platform {
    std::string assets_,data_,dump_;
public:
    HeadlessPlatform(std::string assets,std::string data,std::string dump):assets_(std::move(assets)),data_(std::move(data)),dump_(std::move(dump)) {}
    bool initialize(std::string&) override { return true; }
    bool poll(RawInput& input) override {
        // Validation runner only. Native frontends use OS input/vblank event waits.
        std::mutex m;std::condition_variable cv;std::unique_lock<std::mutex> lock(m);cv.wait_for(lock,std::chrono::milliseconds(8));input={};return true;
    }
    bool present(const uint32_t*,uint64_t,std::string&) override { return true; }
    std::unique_ptr<AudioSink> make_audio_sink() override { return make_headless_audio_sink(dump_); }
    std::string assets_root() const override { return assets_; }
    std::string data_root() const override { return data_; }
};
static std::string absolute(const std::string& path) {
    char cwd[4096];if (!getcwd(cwd,sizeof(cwd))) return path;
    std::string raw=!path.empty() && path[0]=='/'?path:std::string(cwd)+"/"+path;
    std::vector<std::string> parts;std::istringstream in(raw);std::string part;
    while (std::getline(in,part,'/')) { if (part.empty() || part==".") continue;if (part=="..") { if (!parts.empty()) parts.pop_back(); }else parts.push_back(part); }
    std::string out;for (const auto& p:parts) out+="/"+p;return out.empty()?"/":out;
}
static bool output_allowed(const std::string& raw,const std::vector<Source>& sources) {
    std::string path=absolute(raw);char* resolved=realpath(path.c_str(),nullptr);
    if (resolved) { path=resolved;std::free(resolved); }
    else { size_t slash=path.find_last_of('/');auto parent=path.substr(0,slash);char* p=realpath(parent.c_str(),nullptr);if (p) { path=std::string(p)+path.substr(slash);std::free(p); } }
    for (const auto& source:sources) if (path==source.root || (path.size()>source.root.size() && path.compare(0,source.root.size(),source.root)==0 && path[source.root.size()]=='/')) return false;
    return true;
}
static bool screenshot(const std::string& path,const Canvas& canvas) {
    // Standard 24-bit BMP; no mislabeled PNG or partial capture.
    if (extension(path)!="bmp") return false;
    int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);if (fd<0) return false;
    FILE* out=fdopen(fd,"wb");if (!out) { ::close(fd);return false; }
    uint8_t header[54]{};header[0]='B';header[1]='M';
    auto u32=[&](size_t offset,uint32_t value){for (unsigned i=0;i<4;++i) header[offset+i]=static_cast<uint8_t>(value>>(i*8));};
    u32(2,54+screen_width*screen_height*3);u32(10,54);u32(14,40);u32(18,screen_width);u32(22,screen_height);header[26]=1;header[28]=24;u32(34,screen_width*screen_height*3);
    bool ok=std::fwrite(header,1,54,out)==54;std::vector<uint8_t> row(screen_width*3);
    for (int y=screen_height-1;y>=0 && ok;--y) { for (int x=0;x<screen_width;++x) { uint32_t pixel=canvas.data()[size_t(y)*screen_width+x];row[x*3]=static_cast<uint8_t>(pixel>>16);row[x*3+1]=static_cast<uint8_t>(pixel>>8);row[x*3+2]=static_cast<uint8_t>(pixel); }ok=std::fwrite(row.data(),1,row.size(),out)==row.size(); }
    if (std::fclose(out)!=0) ok=false;return ok;
}
struct ScriptEvent { double begin=0,end=0;RawInput input; };
static uint32_t script_button(const std::string& key) {
    const std::pair<const char*,uint32_t> keys[]={{"X",Cross},{"O",Circle},{"TRIANGLE",Triangle},{"SQUARE",Square},{"OPTIONS",Options},{"UP",Up},{"DOWN",Down},{"LEFT",Left},{"RIGHT",Right},{"L1",L1},{"R1",R1},{"L2",L2},{"R2",R2}};
    for (const auto& p:keys) if (key==p.first) return p.second;return 0;
}
static std::vector<ScriptEvent> script(const std::string& path) {
    std::vector<ScriptEvent> out;if (path.empty()) return out;std::ifstream in(path);std::string line;
    while (std::getline(in,line) && out.size()<4096) {
        if (line.empty() || line[0]=='#') continue;std::istringstream row(line);double start,duration;std::string key;
        if (!(row>>start>>duration>>key) || start<0 || duration<0) continue;ScriptEvent event;event.begin=start/1000;event.end=(start+duration)/1000;event.input.buttons=script_button(key);
        row>>event.input.lx>>event.input.ly>>event.input.rx>>event.input.ry>>event.input.lt>>event.input.rt;out.push_back(event);
    }
    return out;
}
static const char* playback_name(Playback s) {
    switch(s) {case Playback::Idle:return "Idle";case Playback::Loading:return "Loading";case Playback::Playing:return "Playing";case Playback::Paused:return "Paused";case Playback::Seeking:return "Seeking";case Playback::Ended:return "Ended";default:return "Error";}
}
static std::string json_string(const std::string& value) {
    std::string out;for (unsigned char c:value) { if (c=='"' || c=='\\') {out+='\\';out+=char(c);}else if (c=='\n') out+="\\n";else if(c=='\r') out+="\\r";else if(c=='\t') out+="\\t";else if(c>=32) out+=char(c); }return out;
}
static std::string telemetry(const Application& app,const char* target,double elapsed,double now) {
    const auto view=app.view(now);const auto& m=app.media();const auto& browser=*view.browser;
    const Source* source=&browser.source();
    if (browser.view==LibraryView::Sources && browser.selected()<browser.sources.size()) source=&browser.sources[browser.selected()];
    const char* screens[]={"Browser","Video","Music","Photo"};
    const char* modals[]={"None","Menu","Settings","Search","Details","ConfirmClear","Message"};
    const char* libraries[]={"Sources","Directory","Favorites","LastPlayed","Queue","Index"};
    std::ostringstream out;out.precision(9);
    out<<"{\"target\":\""<<target<<"\",\"elapsed\":"<<elapsed<<",\"state\":\""<<playback_name(m.state)
       <<"\",\"position\":"<<m.position<<",\"duration\":"<<m.duration<<",\"width\":"<<m.width<<",\"height\":"<<m.height
       <<",\"osd_visible\":"<<(view.osd?"true":"false")<<",\"frame_revision\":"<<m.frame_revision<<",\"session\":"<<m.session
       <<",\"codec\":\""<<json_string(m.codec)<<"\",\"has_audio\":"<<(m.has_audio?"true":"false")
       <<",\"decoded_width\":"<<(m.frame?m.frame->width:0)<<",\"decoded_height\":"<<(m.frame?m.frame->height:0)
       <<",\"artwork_decoded\":"<<(m.artwork?"true":"false")<<",\"artwork_width\":"<<(m.artwork?m.artwork->width:0)<<",\"artwork_height\":"<<(m.artwork?m.artwork->height:0)
       <<",\"media_name\":\""<<json_string(m.item.entry.name)<<"\",\"media_title\":\""<<json_string(m.title)<<"\""
       <<",\"playlist_count\":"<<view.playlist_count<<",\"playlist_index\":"<<view.playlist_index<<",\"next_track\":\""<<json_string(view.next_track)<<"\""
       <<",\"error\":\""<<json_string(m.error)<<"\",\"screen\":\""<<screens[static_cast<int>(view.screen)]
       <<"\",\"modal\":\""<<modals[static_cast<int>(view.modal)]<<"\",\"library\":\""<<libraries[static_cast<int>(browser.view)]
       <<"\",\"selected\":"<<browser.selected()<<",\"tab\":"<<browser.tab<<",\"volume\":"<<m.volume
       <<",\"directory\":\""<<json_string(browser.directory())<<"\",\"source_label\":\""<<json_string(source->label)<<"\""
       <<",\"space_known\":"<<(source->space_known?"true":"false")<<",\"available_bytes\":"<<source->available_bytes<<",\"total_bytes\":"<<source->total_bytes
       <<",\"selected_name\":\""<<json_string(browser.selected_entry()?browser.selected_entry()->name:"")<<"\""
       <<",\"source_count\":"<<browser.sources.size()<<",\"storage_loading\":"<<(browser.loading?"true":"false")
       <<",\"photo_loading\":"<<(view.photo_loading?"true":"false")<<",\"photo_decoded\":"<<(view.photo && view.photo->bitmap?"true":"false")
       <<",\"photo_width\":"<<(view.photo?view.photo->original_width:0)<<",\"photo_height\":"<<(view.photo?view.photo->original_height:0)
       <<",\"photo_frames\":"<<(view.photo?view.photo->frames.size():0)<<",\"photo_frame\":"<<view.photo_frame
       <<",\"animation_paused\":"<<(view.animation_paused?"true":"false")
       <<",\"photo_error\":\""<<json_string(view.photo?view.photo->error:"")<<"\",\"slideshow\":"<<(view.slideshow?"true":"false")
       <<",\"rotation\":"<<view.rotation<<",\"zoom\":"<<view.zoom<<",\"ui_language\":\"English\""
       <<",\"notice\":\""<<json_string(view.notice)<<"\"}";
    return out.str();
}
#endif
}
int main(int argc,char** argv) {
    using namespace mc;
#ifdef MC_PS4
    StartupTrace startup;
    DirectUsbAccessScope usb_access;
    startup_trace("self.profile", 0, MC_PS4_SELF_PROFILE);
    bool defer_scan=true;
#else
    bool defer_scan=false;
#endif
    std::string error,assets,data,base,dump,open_path,select_path,capture,script_path,trace_path;std::vector<std::string> extras;double seconds=0;bool headless=false,debug=false;
#ifdef MC_PS4
    (void)argc;(void)argv;base="/mnt";
    // Probe both title-visible and global names. Actual visibility depends on
    // the process namespace; the selected signing profile does not prove access.
    for (unsigned index=0;index<8;++index) extras.push_back("/usb"+std::to_string(index));
#else
    assets=absolute("assets");data=absolute("build/host-state");base="/Volumes";
    for (int i=1;i<argc;++i) {
        std::string flag=argv[i];auto value=[&]()->std::string { if (i+1>=argc) {std::fprintf(stderr,"Missing value for %s\n",flag.c_str());std::exit(2);}return argv[++i]; };
        if (flag=="--headless") headless=true;else if (flag=="--debug") debug=true;else if (flag=="--source") extras.push_back(absolute(value()));
        else if (flag=="--assets") assets=absolute(value());else if (flag=="--data-dir") data=absolute(value());else if (flag=="--seconds") seconds=std::strtod(value().c_str(),nullptr);
        else if (flag=="--mount-base") base=absolute(value());
        else if (flag=="--open") open_path=value();else if (flag=="--select") select_path=value();else if (flag=="--screenshot") capture=absolute(value());else if (flag=="--audio-dump") dump=absolute(value());else if (flag=="--script") script_path=value();
        else if (flag=="--trace") trace_path=absolute(value());else if (flag=="--defer-scan") defer_scan=true;
        else if (flag=="--help") { std::puts("Media Center\n--source DIR [repeatable] --headless --seconds N --open RELATIVE\n--select RELATIVE --screenshot FILE.bmp --audio-dump FILE.s16le\n--script FILE.tsv --trace FILE.jsonl --assets DIR --data-dir DIR --debug\n--defer-scan: exercise console startup; show the UI before scanning storage\n--mount-base DIR: isolate host storage discovery for validation\nHost keyboard: arrows/WASD, Enter/Space=X, Esc=O, T=Triangle, Q=Square, [ / ]=L1/R1, 1 / 2=L2/R2, Tab=Options.");return 0; }
        else {std::fprintf(stderr,"Unknown option: %s\n",flag.c_str());return 2;}
    }
    if (headless && seconds<=0) seconds=3;
    if (!std::isfinite(seconds) || seconds<0 || seconds>86400 || (!open_path.empty() && !valid_relative(open_path))) {std::fprintf(stderr,"Invalid duration or relative media path\n");return 2;}
#endif
    std::vector<std::string> source_diagnostics;
    std::vector<Source> sources;
    if (!defer_scan) sources=discover_sources(base,extras,&source_diagnostics);
#ifndef MC_PS4
    auto protected_sources=sources;
    if (defer_scan) {
        // The validation option must preserve output boundaries before discovery.
        auto roots=extras;roots.push_back(base);
        for (const auto& root:roots) {
            Source candidate;char* resolved=realpath(root.c_str(),nullptr);
            candidate.root=resolved?resolved:absolute(root);std::free(resolved);
            protected_sources.push_back(std::move(candidate));
        }
    }
    if (!output_allowed(data,protected_sources) || (!dump.empty() && !output_allowed(dump,protected_sources)) || (!capture.empty() && !output_allowed(capture,protected_sources)) || (!trace_path.empty() && !output_allowed(trace_path,protected_sources))) {std::fprintf(stderr,"Refusing an output/app-data path inside a read-only media source\n");return 2;}
    std::unique_ptr<Platform> platform=headless?std::unique_ptr<Platform>(new HeadlessPlatform(assets,data,dump)):make_native_platform(assets,data);
    auto events=script(script_path);
    std::ofstream trace;
    if (!trace_path.empty()) { trace.open(trace_path,std::ios::out|std::ios::trunc);if (!trace) { std::fprintf(stderr,"Cannot create host telemetry file\n");return 1; } }
    double next_trace=0;
#else
    auto platform=make_native_platform(assets,data);
#endif
    startup_trace("platform.initialize.begin");
    if (!platform->initialize(error)) {startup_trace("platform.initialize.failed",0,error.c_str());std::fprintf(stderr,"Platform initialization failed: %s\n",error.c_str());return 1;}
    startup_trace("platform.initialize.ready");
    startup_trace("renderer.construct.begin");Renderer renderer;startup_trace("renderer.initialize.begin");
    if (!renderer.initialize(platform->assets_root(),error)) {startup_trace("renderer.initialize.failed",0,error.c_str());std::fprintf(stderr,"UI initialization failed: %s\n",error.c_str());return 1;}
    startup_trace("renderer.initialize.ready");startup_trace("application.construct.begin");
    Application app(*platform,std::move(sources),base,extras,debug,std::move(source_diagnostics));app.initialize();bool first=true,opened=false,selected=false;double started=monotonic_now();
    startup_trace("application.initialize.ready");
    bool presented=false,scan_started=!defer_scan;
    if (defer_scan) {app.browser().loading=true;app.browser().status="Reading storage sources...";}
#ifndef MC_PS4
    if (!app.browser().sources.empty() && !extras.empty()) app.browser().enter_source(0);
#endif
    while (app.running()) {
        RawInput input;if (!platform->poll(input)) break;double now=monotonic_now();
        if (!scan_started && presented && now-started>=0.75) {
            startup_trace("startup.storage-scan.begin");
            app.browser().rescan(base,extras);scan_started=true;first=true;
        }
#ifndef MC_PS4
        double elapsed=now-started;for (const auto& event:events) if (elapsed>=event.begin && elapsed<event.end) {input.buttons|=event.input.buttons;input.lx=event.input.lx;input.ly=event.input.ly;input.rx=event.input.rx;input.ry=event.input.ry;input.lt=event.input.lt;input.rt=event.input.rt;}
#endif
        bool dirty=app.tick(input,now);
#ifndef MC_PS4
        if (!selected && !select_path.empty() && !app.browser().loading && app.browser().view==LibraryView::Directory) {
            for (size_t i=0;i<app.browser().entries.size();++i) if (app.browser().entries[i].relative==select_path) {app.browser().move(static_cast<int>(i)-static_cast<int>(app.browser().selected()));selected=true;dirty=true;break;}
        }
        if (!opened && !open_path.empty() && !app.browser().loading && !app.browser().sources.empty() && now-started>0.2) {
            auto slash=open_path.find_last_of('/');std::string name=slash==std::string::npos?open_path:open_path.substr(slash+1);Item item{app.browser().sources.front(),{name,open_path,classify(name),0,0}};
            ReadOnlyFile f;if (f.open(item.source,item.entry.relative,error)) item.entry.size=f.size();app.open_item(item,now);opened=true;dirty=true;
        }
#endif
        if (first || dirty || renderer.animating()) {renderer.draw(app.view(now));first=false;}
        if (!presented) startup_trace("first-frame.present.begin");
        if (!platform->present(renderer.canvas().data(),renderer.revision(),error)) {startup_trace("frame.present.failed",0,error.c_str());std::fprintf(stderr,"Presentation failed: %s\n",error.c_str());return 1;}
        if (!presented) {presented=true;startup_trace("first-frame.present.ready");}
#ifndef MC_PS4
        if (trace.is_open() && elapsed>=next_trace) { trace<<telemetry(app,headless?"host headless":"native macOS",elapsed,now)<<'\n';next_trace=elapsed+0.1; }
        if (seconds>0 && now-started>=seconds) break;
#endif
    }
#ifndef MC_PS4
    if (!capture.empty() && !screenshot(capture,renderer.canvas())) {std::fprintf(stderr,"Cannot save complete BMP screenshot: %s\n",capture.c_str());return 1;}
    if (trace.is_open()) { trace.flush();if (!trace) {std::fprintf(stderr,"Host telemetry write failed\n");return 1;} }
    double finished=monotonic_now();std::puts(telemetry(app,headless?"host headless":"native macOS",finished-started,finished).c_str());
    auto view=app.view(finished);
    if (opened && (app.media().state==Playback::Error || (view.screen==Screen::Photo && view.photo && !view.photo->error.empty()))) return 1;
#endif
    return 0;
}
