#include "view.hpp"
#include "layout.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace mc {
namespace {
constexpr Color usb_color{63,200,237},video_color{255,177,75},music_color{87,219,153},photo_color{196,139,255};
Color item_color(Kind kind) {
    switch (kind) {
        case Kind::Source:return usb_color;case Kind::Video:return video_color;
        case Kind::Music:return music_color;case Kind::Photo:return photo_color;
        case Kind::Folder:return Color{244,205,90};default:return muted;
    }
}
}
bool Renderer::initialize(const std::string& assets,std::string& error) {
    if (!canvas_.load_fonts(assets,error)) return false;
    auto texture=[&](const std::string& name) {
        std::ifstream file(assets+"/ui/"+name,std::ios::binary|std::ios::ate);
        if (!file) { error="Bundled UI texture missing: "+name;return std::shared_ptr<Bitmap>{}; }
        auto length=file.tellg();if (length<1 || length>1048576) { error="Invalid UI texture size";return std::shared_ptr<Bitmap>{}; }
        std::vector<uint8_t> bytes(static_cast<size_t>(length));file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()),length)) { error="Cannot read UI texture";return std::shared_ptr<Bitmap>{}; }
        return decode_bitmap(bytes.data(),bytes.size(),1024*1024,error);
    };
    logo_=texture("ps4-logo.png");if (!logo_) return false;
    usb_icon_=texture("usb.png");if (!usb_icon_) return false;
    video_icon_=texture("videos.png");if (!video_icon_) return false;
    music_icon_=texture("music.png");if (!music_icon_) return false;
    photo_icon_=texture("photos.png");if (!photo_icon_) return false;
    folder_icon_=texture("folder.png");if (!folder_icon_) return false;
    playing_icon_=texture("playing.png");if (!playing_icon_) return false;
    const char* names[]={"previous","backward-10","play","pause","forward-10","next","stop","back","aspect","options"};
    for (size_t i=0;i<transport_icons_.size();++i) { transport_icons_[i]=texture(std::string("transport/")+names[i]+".png");if (!transport_icons_[i]) return false; }
    const char* buttons[]={"cross","circle","triangle","square"};
    for (size_t i=0;i<button_icons_.size();++i) { button_icons_[i]=texture(std::string("buttons/")+buttons[i]+".png");if (!button_icons_[i]) return false; }
    return true;
}
void Renderer::icon(Kind kind,int x,int y,int size,Color color) {
    const std::shared_ptr<Bitmap>* artwork=kind==Kind::Source?&usb_icon_:kind==Kind::Video?&video_icon_:kind==Kind::Music?&music_icon_:kind==Kind::Photo?&photo_icon_:kind==Kind::Folder?&folder_icon_:nullptr;
    if (artwork && *artwork) canvas_.bitmap(**artwork,x,y,size,size);
    else canvas_.icon(kind,x,y,size,color);
}
void Renderer::button(Button value,int x,int y,const std::string& label,int size) {
    size_t index=value==Cross?0:value==Circle?1:value==Triangle?2:value==Square?3:button_icons_.size();
    if (index>=button_icons_.size() || !button_icons_[index]) { canvas_.button(value,x,y,label,size);return; }
    int face=std::max(24,std::min(40,size+3));
    canvas_.bitmap(*button_icons_[index],x,y,face,face);
    canvas_.text_in_row(label,x+face+19,y,310,face,size,white,false);
}
void Renderer::update_tab_transition(double now,int tab) {
    constexpr double duration=.22;
    if (observed_tab_<0) { observed_tab_=tab;tab_position_=tab_from_=tab_target_=tab;return; }
    if (tab!=observed_tab_) {
        if (tab_transition_active_) {
            double progress=std::max(0.0,std::min(1.0,(now-tab_transition_started_)/duration));
            double eased=progress*progress*(3-2*progress);
            tab_position_=tab_from_+(tab_target_-tab_from_)*eased;
        }
        observed_tab_=tab;tab_from_=tab_position_;tab_target_=tab;tab_transition_started_=now;tab_transition_active_=true;
    }
    if (tab_transition_active_) {
        double progress=(now-tab_transition_started_)/duration;
        if (progress>=1) { tab_position_=tab_target_;tab_transition_active_=false; }
        else {
            progress=std::max(0.0,progress);double eased=progress*progress*(3-2*progress);
            tab_position_=tab_from_+(tab_target_-tab_from_)*eased;
        }
    }
}
void Renderer::update_list_transition(double now,double position,const std::string& context) {
    constexpr double duration=.22;
    if (context!=list_context_) {
        list_context_=context;list_position_=list_from_=list_target_=position;list_transition_active_=false;return;
    }
    if (std::abs(position-list_target_)>.001) {
        if (list_transition_active_) {
            double progress=std::max(0.0,std::min(1.0,(now-list_transition_started_)/duration));
            double eased=progress*progress*(3-2*progress);
            list_position_=list_from_+(list_target_-list_from_)*eased;
        }
        list_from_=list_position_;list_target_=position;list_transition_started_=now;
        list_transition_active_=std::abs(list_target_-list_from_)>.001;
    }
    if (list_transition_active_) {
        double progress=(now-list_transition_started_)/duration;
        if (progress>=1) { list_position_=list_target_;list_transition_active_=false; }
        else {
            progress=std::max(0.0,progress);double eased=progress*progress*(3-2*progress);
            list_position_=list_from_+(list_target_-list_from_)*eased;
        }
    }
}
void Renderer::header(const ViewModel& m,const std::string& title) {
    if (logo_) canvas_.bitmap(*logo_,68,36,87,74);
    canvas_.rect(165,38,1,72,rule);canvas_.text_in_row(title,193,38,1050,72,48);
    const Source* source=nullptr;
    if (m.browser) {
        if (m.screen==Screen::Music && !m.media.item.source.root.empty()) {
            source=&m.media.item.source;
            for (const auto& current:m.browser->sources) if (current.root==source->root) { source=&current;break; }
        } else if (m.browser->view==LibraryView::Directory) source=&m.browser->source();
        else if (m.browser->view==LibraryView::Sources && m.browser->selected()<m.browser->sources.size()) source=&m.browser->sources[m.browser->selected()];
    }
    std::string label=source?source->label:"USB sources";
    std::string space=source?(source->space_known?format_size(source->available_bytes)+" available":"Space unavailable"):"Select a source";
    icon(Kind::Source,1320,43,48,usb_color);canvas_.text_in_row(label,1390,31,255,40,35);
    canvas_.text_in_row(space,1390,77,255,30,24,muted);
    canvas_.rect(1668,37,2,71,accent);
    time_t now=std::time(nullptr);tm local{};char clock[32]{},date[48]{};
    if (localtime_r(&now,&local)) { std::strftime(clock,sizeof(clock),"%H:%M",&local);std::strftime(date,sizeof(date),"%d %b %Y",&local); }
    canvas_.text(clock,1710,26,48,white,170);canvas_.text(date,1702,81,25,muted,190);canvas_.rect(0,138,1920,1,rule);
}
void Renderer::browser(const ViewModel& m) {
    const auto& b=*m.browser;header(m,"Media Center");
    const char* labels[]={"USB Files","Videos","Music","Photos"};
    const Kind icons[]={Kind::Source,Kind::Video,Kind::Music,Kind::Photo};
    int highlight_x=72+static_cast<int>(std::round(tab_position_*449));
    if (b.focus==Focus::Tabs) canvas_.rounded_panel(highlight_x,145,429,82,16,blue,accent,3);
    else canvas_.rounded_rect(highlight_x,145,429,82,16,blue);
    for (int i=0;i<4;++i) {
        int x=72+i*449;
        int width=canvas_.text_width(labels[i],43),left=x+(429-width-67)/2;
        icon(icons[i],left,165,41,item_color(icons[i]));canvas_.text_in_row(labels[i],left+67,145,width+2,82,43,white,false);
    }
    canvas_.rect(0,236,1920,1,rule);canvas_.rounded_rect(65,270,1030,649,16,panel);
    if (b.view==LibraryView::Directory) canvas_.text_in_row(b.source().label+" / "+(b.directory().empty()?"USB Files":b.directory()),74,237,1750,30,24,muted);
    if (!b.entries.empty()) {
        std::string context=std::to_string(b.tab)+":"+std::to_string(static_cast<int>(b.view))+":"+b.directory()+":"+b.query;
        update_list_transition(monotonic_now(),static_cast<double>(b.selected()-b.scroll()),context);
        if (b.focus==Focus::List) canvas_.rounded_rect(65,270+static_cast<int>(std::round(list_position_*72)),1030,72,14,blue);
    } else list_transition_active_=false;
    for (size_t row=0;row<9 && b.scroll()+row<b.entries.size();++row) {
        size_t index=b.scroll()+row;const auto& entry=b.entries[index];int y=270+static_cast<int>(row)*72;
        bool selected=index==b.selected();
        bool playing_here=false;
        if (m.media.kind==Kind::Music && (m.media.state==Playback::Playing || m.media.state==Playback::Paused || m.media.state==Playback::Seeking)) {
            Item item;if (b.item_at(index,item)) playing_here=item.source.root==m.media.item.source.root && item.entry.relative==m.media.item.entry.relative;
        }
        if (selected && b.focus!=Focus::List) canvas_.rounded_panel(65,y,1030,72,14,panel,rule,2);
        if (playing_here && playing_icon_) canvas_.bitmap(*playing_icon_,102,y+17,39,39);
        else icon(entry.kind,102,y+17,39,item_color(entry.kind));
        bool unavailable=entry.kind==Kind::Other;
        const Source* row_source=b.view==LibraryView::Sources && index<b.sources.size()?&b.sources[index]:nullptr;
        if (unavailable || row_source) {
            canvas_.text_in_row(entry.name,173,y+7,row_source?795:680,31,34);
            std::string subtitle=unavailable?"File · Cannot open":row_source->space_known?format_size(row_source->available_bytes)+" available of "+format_size(row_source->total_bytes):"Space unavailable";
            canvas_.text_in_row(subtitle,173,y+41,795,24,24,unavailable?muted:usb_color,false);
        } else canvas_.text_in_row(entry.name,173,y,entry.directory()?807:680,72,43);
        if (entry.directory()) { canvas_.line(1040,y+23,1052,y+35,white,3);canvas_.line(1052,y+35,1040,y+47,white,3); }
        else { auto size=format_size(entry.size);int width=canvas_.text_width(size,34);canvas_.text_in_row(size,1065-width,y,width+2,72,34,muted,false); }
        if (!(selected && b.focus==Focus::List)) canvas_.rect(90,y+71,980,1,rule);
    }
    if (b.entries.empty()) {
        icon(Kind::Source,523,410,108,muted);
        canvas_.wrapped_text(b.status,185,565,38,white,790,4);
    }
    if (b.entries.size()>9 || b.total>128) {
        canvas_.rect(1113,270,7,649,rule);
        double offset=double(b.page_index()*128+b.selected());double fraction=b.total?offset/double(b.total):0;
        int thumb=std::max(38,static_cast<int>(649*std::min(1.0,9.0/std::max<uint64_t>(9,b.total))));
        canvas_.rect(1113,270+static_cast<int>((649-thumb)*std::max(0.0,std::min(1.0,fraction))),7,thumb,accent);
    }
    canvas_.rounded_panel(1154,270,702,649,16,panel,b.focus==Focus::Details?accent:rule,b.focus==Focus::Details?2:1);
    const auto* entry=b.selected_entry();
    if (entry) {
        canvas_.rounded_rect(1178,295,654,351,14,{2,7,18});
        if (m.preview && m.preview->bitmap && m.preview->path==entry->relative) canvas_.bitmap(*m.preview->bitmap,1178,295,654,351);
        else { icon(entry->kind,1435,379,140,item_color(entry->kind));canvas_.text_in_row(entry->kind==Kind::Other?"Cannot open this file":"No preview available",1218,570,574,42,29,muted); }
        const char* keys[]={"Name:","Size:","Type:"};
        const Source* selected_source=b.view==LibraryView::Sources && b.selected()<b.sources.size()?&b.sources[b.selected()]:nullptr;
        std::string values[]={entry->name,selected_source && selected_source->space_known?format_size(selected_source->total_bytes):entry->directory()?"—":format_size(entry->size),kind_name(entry->kind)};
        for (int i=0;i<3;++i) { canvas_.text(keys[i],1181,663+i*47,34,accent,153);canvas_.text(values[i],1350,663+i*47,34,white,470); }
        int y=804;
        if (selected_source) { canvas_.text("Available:",1181,y,31,accent,165);canvas_.text(selected_source->space_known?format_size(selected_source->available_bytes):"Unavailable",1350,y,32,white,470);y+=45; }
        if (m.preview && m.preview->bitmap && m.preview->path==entry->relative) {
            std::string value=m.preview->duration>0?format_time(m.preview->duration):std::to_string(m.preview->original_width)+" × "+std::to_string(m.preview->original_height);
            canvas_.text(m.preview->duration>0?"Duration:":"Dimensions:",1181,y,31,accent,165);
            canvas_.text(value,1350,y,32,white,470);y+=45;
        }
        canvas_.text("Path:",1181,y,32,accent,153);
        std::string path=entry->kind==Kind::Source?entry->relative:b.source().root+"/"+entry->relative;
        Item virtual_item;if (b.view!=LibraryView::Directory && b.selected_item(virtual_item)) path=virtual_item.source.root+"/"+virtual_item.entry.relative;
        canvas_.wrapped_text(path,1350,y,27,muted,470,2);
    } else canvas_.text("Select an item to see details",1210,548,35,muted,590);
    std::string status=m.notice.empty()?b.status:m.notice;
    if (b.loading) status="Reading files... navigation stays responsive";
    if (!b.query.empty()) status="Search: "+b.query+"  ·  "+status;
    if (!status.empty()) canvas_.text(status,74,935,28,muted,1770);
    canvas_.rect(0,980,1920,1,rule);
    if (entry && entry->kind!=Kind::Other) button(Cross,72,1003,entry->directory() || entry->kind==Kind::Photo?"Open":"Play",37);
    if (b.view!=LibraryView::Sources || b.focus!=Focus::List) button(Circle,318,1003,"Back",37);
    button(Triangle,553,1003,"Options",37);
    if (entry) button(Square,853,1003,"Details",37);
    else if (b.view==LibraryView::Sources) button(Square,853,1003,"USB diagnostics",37);
    std::string count=std::to_string(entry?b.page_index()*128+b.selected()+1:0)+" / "+std::to_string(b.total);canvas_.text(count,1690,1006,34,white,180,false);
    if (m.media.kind==Kind::Music && (m.media.state==Playback::Playing || m.media.state==Playback::Paused)) {
        icon(Kind::Music,1215,1005,28,accent);canvas_.text(m.media.item.entry.name,1260,1010,25,muted,390);
    }
}
void Renderer::video(const ViewModel& m) {
    canvas_.clear({0,0,0});
    if (m.media.frame) canvas_.bitmap(*m.media.frame,0,0,1920,1080,m.video_aspect);
    if (m.media.state==Playback::Loading) canvas_.text("Opening video...",660,495,48,white,700);
    if (m.media.state==Playback::Error) { canvas_.wrapped_text(m.media.error,290,418,42,white,1350,5); }
    if (!video_layout::visible(m.osd,m.media.state)) return;
    canvas_.gradient_overlay(0,video_layout::overlay_top,1920,1080-video_layout::overlay_top,{0,3,10},0,244);
    canvas_.text_in_row(m.media.item.entry.name,80,846,1510,34,34,white);
    std::string status=m.media.state==Playback::Ended?"Finished":m.media.state==Playback::Error?"Playback failed":m.media.state==Playback::Loading?"Opening...":m.media.state==Playback::Seeking?"Seeking...":m.media.paused?"Paused":"Playing";
    canvas_.rounded_panel(1668,846,172,34,10,panel,rule);
    int status_width=canvas_.text_width(status,25);canvas_.text_in_row(status,1754-status_width/2,846,status_width+2,34,25,white,false);
    canvas_.rounded_rect(80,894,1760,6,3,rule);double progress=m.media.duration>0?m.media.position/m.media.duration:0;
    int filled=static_cast<int>(1760*std::max(0.0,std::min(1.0,progress)));
    canvas_.rounded_rect(80,894,filled,6,3,accent);canvas_.circle(80+filled,897,6,accent,6);
    canvas_.text_in_row(format_time(m.media.position),80,912,200,28,28,white,false);
    int duration_width=canvas_.text_width(format_time(m.media.duration),28);
    canvas_.text_in_row(format_time(m.media.duration),1840-duration_width,912,duration_width+2,28,28,white,false);
    if (m.playlist_count>1) {
        std::string position=std::to_string(m.playlist_index+1)+" / "+std::to_string(m.playlist_count);
        int width=canvas_.text_width(position,23);canvas_.text_in_row(position,960-width/2,912,width+2,28,23,muted,false);
    }
    const size_t icons[]={0,1,m.media.paused || m.media.state==Playback::Ended?2u:3u,4,5,6};
    auto enabled=video_layout::enabled(m.media,m.playlist_count);
    for (int i=0;i<6;++i) {
        int x=video_layout::controls_left+i*video_layout::control_stride,y=video_layout::controls_top;
        Color color=enabled[i]?white:Color{83,99,124};
        canvas_.rounded_panel(x,y,video_layout::control_width,video_layout::control_height,15,i==2 && enabled[i]?blue:panel,enabled[i]?rule:Color{22,33,49});
        canvas_.tinted_bitmap(*transport_icons_[icons[i]],x+(video_layout::control_width-28)/2,y+(video_layout::control_height-28)/2,28,28,color);
    }
    canvas_.rounded_panel(video_layout::back_left,video_layout::back_top,video_layout::back_width,video_layout::back_height,14,panel,rule);
    canvas_.tinted_bitmap(*transport_icons_[7],video_layout::back_left+(video_layout::back_width-28)/2,video_layout::back_top+(video_layout::back_height-28)/2,28,28,white);
    canvas_.rounded_panel(video_layout::aspect_left,video_layout::aspect_top,video_layout::aspect_width,video_layout::aspect_height,14,panel,rule);
    canvas_.tinted_bitmap(*transport_icons_[8],video_layout::aspect_left+(video_layout::aspect_width-28)/2,video_layout::aspect_top+(video_layout::aspect_height-28)/2,28,28,m.video_aspect==Aspect::Fit?white:accent);
    canvas_.rounded_panel(video_layout::options_left,video_layout::options_top,video_layout::options_width,video_layout::options_height,14,panel,rule);
    canvas_.tinted_bitmap(*transport_icons_[9],video_layout::options_left+(video_layout::options_width-28)/2,video_layout::options_top+(video_layout::options_height-28)/2,28,28,white);
    if (m.media.has_audio) {
        canvas_.text_in_row("Volume "+std::to_string(m.media.volume)+"%",176,959,226,27,25,white,false);
        canvas_.text_in_row("Up / Down",176,990,226,21,18,muted,false);
    }
    canvas_.text_in_row(m.notice,80,1040,1760,24,22,accent,false);
}
void Renderer::music(const ViewModel& m) {
    header(m,"Media Center");
    canvas_.rounded_panel(music_layout::back_left,music_layout::back_top,music_layout::back_width,music_layout::back_height,12,panel,rule);
    canvas_.tinted_bitmap(*transport_icons_[7],90,166,24,24,white);canvas_.text_in_row("Back",130,156,75,44,26,white,false);
    button(Circle,224,164,"",24);
    canvas_.rounded_panel(72,219,400,400,24,panel,rule);
    if (m.media.artwork) canvas_.bitmap(*m.media.artwork,88,235,368,368);
    else {
        icon(Kind::Music,181,323,182,music_color);
        const std::string caption=m.media.state==Playback::Loading?"Loading artwork...":"No embedded artwork";
        int width=canvas_.text_width(caption,27);canvas_.text_in_row(caption,272-width/2,554,width+2,35,27,muted,false);
    }
    canvas_.text_in_row("NOW PLAYING",516,201,1250,37,28,music_color,false);
    canvas_.text_in_row(m.media.title.empty()?m.media.item.entry.name:m.media.title,516,259,1332,65,58);
    canvas_.text_in_row(m.media.artist.empty()?"Unknown artist":m.media.artist,516,345,1332,48,39,muted);
    if (!m.media.album.empty()) canvas_.text_in_row(m.media.album,516,407,1332,41,34,muted);
    std::string status=m.media.state==Playback::Loading?"Opening audio...":m.media.state==Playback::Error?"Playback failed":m.media.state==Playback::Ended?"Finished":m.media.state==Playback::Seeking?"Seeking...":m.media.paused?"Paused":"Playing";
    canvas_.rounded_panel(516,479,227,46,12,panel,rule);
    canvas_.text_in_row(status,533,479,195,46,29,m.media.paused?muted:music_color,false);
    canvas_.text_in_row(m.media.codec,769,479,1050,46,28,muted,false);
    if (!m.media.error.empty()) canvas_.wrapped_text(m.media.error,516,534,29,white,1320,2);
    else {
        canvas_.text_in_row("Volume "+std::to_string(m.media.volume)+"%",516,550,205,36,28,muted,false);
        canvas_.rect(755,565,280,6,rule);canvas_.rect(755,565,280*std::max(0,std::min(100,m.media.volume))/100,6,music_color);
        const char* repeat[]={"Off","One","All"};
        canvas_.text_in_row(std::string("Repeat: ")+repeat[std::max(0,std::min(2,m.settings.repeat))]+"    Shuffle: "+(m.settings.shuffle?"On":"Off"),1114,550,730,36,28,muted,false);
    }
    canvas_.rect(72,643,1776,10,rule);double progress=m.media.duration>0?m.media.position/m.media.duration:0;
    int filled=static_cast<int>(1776*std::max(0.0,std::min(1.0,progress)));canvas_.rect(72,643,filled,10,music_color);
    canvas_.circle(72+filled,648,9,music_color,9);
    canvas_.text_in_row(format_time(m.media.position),72,674,290,42,37,white,false);
    int duration_width=canvas_.text_width(format_time(m.media.duration),37);canvas_.text_in_row(format_time(m.media.duration),1848-duration_width,674,duration_width+2,42,37,white,false);
    if (!m.next_track.empty()) canvas_.text_in_row("Next: "+m.next_track,425,680,1040,30,25,muted);
    bool active=m.media.state==Playback::Playing || m.media.state==Playback::Paused || m.media.state==Playback::Seeking || m.media.state==Playback::Ended;
    const char* labels[]={"Previous","Back 10s",m.media.state==Playback::Ended?"Replay":m.media.paused?"Play":"Pause","Forward 10s","Next","Stop"};
    const char* keys[]={"L1","L2","","R2","R1",""};
    const size_t icons[]={0,1,m.media.paused || m.media.state==Playback::Ended?2u:3u,4,5,6};
    const bool enabled[]={m.playlist_count>1,m.media.seekable,active,m.media.seekable,m.playlist_count>1,m.media.state!=Playback::Idle};
    for (int i=0;i<6;++i) {
        int x=music_layout::controls_left+i*music_layout::control_stride,y=music_layout::controls_top;Color color=enabled[i]?white:muted;
        canvas_.rounded_panel(x,y,music_layout::control_width,music_layout::control_height,16,i==2 && active?blue:panel,rule);
        if (enabled[i]) {
            if (i==2 || i==5) button(i==2?Cross:Square,x+8,y+10,"",24);
            else { canvas_.rounded_panel(x+8,y+10,36,24,6,panel,rule);canvas_.text_in_row(keys[i],x+12,y+10,28,24,19,muted,false); }
        }
        canvas_.rounded_panel(x+60,y+10,48,48,15,i==2 && active?Color{34,110,238}:Color{15,28,49},i==2 && active?Color{64,135,252}:rule);
        canvas_.tinted_bitmap(*transport_icons_[icons[i]],x+68,y+18,32,32,color);
        int width=canvas_.text_width(labels[i],28);canvas_.text_in_row(labels[i],x+(music_layout::control_width-width)/2,y+78,width+2,28,28,color,false);
    }
    std::string controls=m.media.has_audio?"Up / Down  Volume":"";
    if (m.media.seekable) controls+=(controls.empty()?"":"    ·    ")+std::string("Left / Right  ±10 seconds");
    canvas_.text_in_row(m.notice.empty()?controls:m.notice,74,928,1765,38,27,muted,false);
    canvas_.rect(0,980,1920,1,rule);
    button(Circle,75,1003,"Back",32);button(Triangle,415,1003,"Options",32);
    if (m.media.state!=Playback::Idle) button(Square,770,1003,"Stop music",32);
    if (m.playlist_count) canvas_.text(std::to_string(m.playlist_index+1)+" / "+std::to_string(m.playlist_count),1660,1006,35,white,220);
}
void Renderer::photo(const ViewModel& m) {
    canvas_.clear({0,0,0});
    if (m.photo && m.photo->bitmap) {
        auto frame=m.photo->frames.empty()?m.photo->bitmap:m.photo->frames[std::min(m.photo_frame,m.photo->frames.size()-1)].bitmap;
        canvas_.bitmap(*frame,60,45,1800,875,m.photo_aspect,m.zoom,m.rotation);
        if (m.animation_paused && m.photo->frames.size()>1) canvas_.text_in_row("Animation paused",75,46,500,42,30,white,false);
    }
    if (m.photo_loading) canvas_.text("Opening image...",650,473,48,white,710);
    else if (m.photo && !m.photo->error.empty()) canvas_.wrapped_text(m.photo->error,300,425,42,white,1320,4);
    canvas_.rect(0,930,1920,150,panel);canvas_.text(m.photo_item.entry.name,75,944,36,white,1150);
    if (m.photo && m.photo->bitmap) canvas_.text(std::to_string(m.photo->original_width)+" × "+std::to_string(m.photo->original_height)+"  ·  "+format_size(m.photo_item.entry.size),1300,949,30,muted,545);
    if (m.photo && m.photo->bitmap && !m.photo_loading) button(Cross,75,1003,m.slideshow?"Pause slideshow":"Slideshow",34);
    button(Circle,510,1003,"Back",34);button(Triangle,785,1003,"Information",34);button(Square,1120,1003,m.photo_aspect==Aspect::Fit?"Fit":"Original",34);
    canvas_.text(std::to_string(m.playlist_index+1)+" / "+std::to_string(m.playlist_count),1690,1006,33,white,170);
}
void Renderer::modal(const ViewModel& m) {
    if (m.modal==Modal::None) return;
    if (m.modal==Modal::Search) {
        canvas_.rounded_panel(342,170,1236,725,22,panel,accent,2);
        canvas_.text("Search this folder",386,193,46,white,1130);canvas_.rounded_rect(387,264,1142,72,14,background);canvas_.text(m.search_text.empty()?"Enter a name...":m.search_text,409,279,39,white,1096);
        for (size_t i=0;i<m.keyboard_keys.size();++i) { int col=static_cast<int>(i%10),row=static_cast<int>(i/10),x=388+col*113,y=365+row*73;
            canvas_.rounded_rect(x,y,103,61,11,i==m.keyboard_selected?blue:background);canvas_.text(m.keyboard_keys[i],x+11,y+10,34,white,81,false);
        }
        canvas_.text("X Add / choose    O Cancel    Square Backspace    Triangle Alphabet    Options Apply",386,834,27,muted,1140);return;
    }
    int x=470,y=187,w=980,h=700;canvas_.rounded_panel(x,y,w,h,22,panel,accent,2);
    canvas_.text(m.menu_title,x+41,y+23,47,white,w-82);
    if (m.modal==Modal::Details || m.modal==Modal::Message) {
        size_t maximum=m.information.size()>information_page_rows?m.information.size()-information_page_rows:0;
        size_t start=std::min(m.information_scroll,maximum);
        int row_y=y+113;
        for (size_t i=start;i<m.information.size() && i<start+information_page_rows;++i) {
            canvas_.wrapped_text(m.information[i],x+42,row_y,32,white,w-85,2);row_y+=83;
        }
        button(Circle,x+42,y+h-70,"Close",31);
        if (maximum) canvas_.text("Up / Down  Scroll",x+345,y+h-64,27,muted,w-390);
        return;
    }
    size_t start=m.menu_selected>=9?m.menu_selected-8:0;
    for (size_t i=start;i<m.menu_items.size() && i<start+9;++i) {
        int row_y=y+104+static_cast<int>(i-start)*55;canvas_.rounded_rect(x+27,row_y,w-54,52,11,i==m.menu_selected?blue:panel);
        canvas_.text(m.menu_items[i],x+48,row_y+6,35,white,w-100);
    }
    button(Cross,x+38,y+h-70,"Select",31);button(Circle,x+343,y+h-70,"Close",31);
}
void Renderer::draw(const ViewModel& model) {
    double now=monotonic_now();
    int current_tab=model.screen==Screen::Browser && model.browser?model.browser->tab:-1;
    if (current_tab>=0) update_tab_transition(now,current_tab);else { tab_transition_active_=false;list_transition_active_=false; }
    canvas_.clear();switch (model.screen) { case Screen::Browser:browser(model);break;case Screen::Video:video(model);break;case Screen::Music:music(model);break;case Screen::Photo:photo(model);break; }
    modal(model);
    ++revision_;
}
}
