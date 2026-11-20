#include "application.hpp"
#include "ui/layout.hpp"
#include "platform/ps4/startup_trace.hpp"
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace mc {
Application::Application(Platform& platform,std::vector<Source> sources,std::string base,std::vector<std::string> extras,bool debug,std::vector<std::string> diagnostics)
    :store_(platform.data_root(),sources),media_([&platform]{return platform.make_audio_sink();}),mount_base_(std::move(base)),extras_(std::move(extras)),debug_(debug) {
    saved_=store_.read();browser_.set_sources(std::move(sources),std::move(diagnostics));browser_.hidden=saved_.settings.hidden;media_.volume(saved_.settings.volume);
}
void Application::initialize() { browser_.show_sources();notice_=store_.error(); }
void Application::persist() { saved_modified_=true;store_.save(saved_); }
void Application::keyboard() {
    keyboard_chars_.clear();std::string chars=keyboard_arabic_?"ابتثجحخدذرزسشصضطظعغفقكلمنهويءأإآةىؤئ0123456789 ._-":"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-";
    for (auto c:utf8_decode(chars)) keyboard_chars_.push_back(c==' '?"Space":utf8_encode(c));
    keyboard_chars_.push_back("Del");keyboard_chars_.push_back("Clear");keyboard_chars_.push_back("Apply");keyboard_chars_.push_back("ABC");keyboard_chars_.push_back("Close");key_selected_=0;
}
void Application::info(const Item& item) {
    modal_=Modal::Details;menu_title_="File information";information_.clear();information_scroll_=0;
    information_.push_back("Name: "+item.entry.name);information_.push_back("Type: "+std::string(kind_name(item.entry.kind))+"    Size: "+(item.entry.directory()?"—":format_size(item.entry.size)));
    information_.push_back("Path: "+item.source.root+"/"+item.entry.relative);
    if (item.entry.kind==Kind::Other) information_.push_back("Cannot open: this is not a video, music or image file.");
    if (photo_ && photo_->bitmap && item==photo_item_) information_.push_back("Dimensions: "+std::to_string(photo_->original_width)+" × "+std::to_string(photo_->original_height));
    else if (media_snapshot_.item==item && media_snapshot_.duration>0) information_.push_back("Duration: "+format_time(media_snapshot_.duration)+"    Codec: "+media_snapshot_.codec);
    else if (preview_ && preview_->path==item.entry.relative && preview_->bitmap) information_.push_back(preview_->duration>0?"Duration: "+format_time(preview_->duration):"Dimensions: "+std::to_string(preview_->original_width)+" × "+std::to_string(preview_->original_height));
}
void Application::usb_diagnostics() {
    modal_=Modal::Details;menu_title_="USB diagnostics";information_scroll_=0;
    information_={"USB access: read only. Use Refresh to run a new scan."};
#ifdef MC_PS4
    information_.push_back("Startup log: /data/PS4MediaCenter/startup.log");
    const auto trace = startup_trace_status();
    if (trace.error) information_.push_back(std::string("Startup log unavailable: ") + trace.operation
        + " errno=" + std::to_string(trace.error));
    else if (trace.file_opened) information_.push_back("Startup log is open for this run.");
#endif
    if (browser_.loading) information_.push_back("A storage scan is still running.");
    else if (browser_.source_diagnostics.empty()) information_.push_back("No scan results yet. Choose Refresh.");
    else information_.insert(information_.end(),browser_.source_diagnostics.begin(),browser_.source_diagnostics.end());
}
void Application::settings_items() {
    auto& s=saved_.settings;const char* repeat[]={"Off","One","All"};
    menu_title_="Settings";menu_items_={
        "Volume: "+std::to_string(s.volume)+"%",
        "Slideshow: "+std::to_string(s.slideshow_seconds)+" s",
        std::string("Show hidden files: ")+(s.hidden?"On":"Off"),
        std::string("Play next automatically: ")+(s.auto_next?"On":"Off"),
        std::string("Repeat: ")+repeat[s.repeat],
        std::string("Shuffle: ")+(s.shuffle?"On":"Off"),
        "About Media Center", "Clear internal app data...", "Close"};
}
void Application::adjust_setting(int direction,double now) {
    auto& s=saved_.settings;int sign=direction<0?-1:1;
    switch (menu_selected_) {
        case 0:s.volume=std::max(0,std::min(100,s.volume+sign*5));media_.volume(s.volume);break;
        case 1:s.slideshow_seconds=std::max(2,std::min(30,s.slideshow_seconds+sign));slide_due_=now+s.slideshow_seconds;break;
        case 2:s.hidden=!s.hidden;browser_.set_hidden(s.hidden);break;
        case 3:s.auto_next=!s.auto_next;break;
        case 4:s.repeat=(s.repeat+sign+3)%3;break;
        case 5:s.shuffle=!s.shuffle;break;
        case 6:modal_=Modal::Message;menu_title_="Media Center 12.00";information_={"Software media playback and read-only USB browsing.","Music: WAV, MP3, FLAC, Ogg, AAC, M4A, Opus, WMA and AIFF.","Images: GIF, WebP, JPEG, PNG, BMP, TIFF, ICO, TGA, PSD, HDR and PNM.","Video: MP4, MKV, AVI, MOV, WebM, MPEG and TS/M2TS.","H.264, HEVC, AV1, VP8/VP9 and MPEG video with decoded audio.","Controller: X select, O back, Triangle options, Square details."};return;
        case 7:modal_=Modal::ConfirmClear;menu_title_="Clear internal app data?";menu_items_={"Cancel","Clear favorites, history, queues and settings"};menu_selected_=0;return;
        default:modal_=Modal::None;return;
    }
    settings_items();persist();
}
void Application::menu(double now) {
    if (modal_==Modal::Menu) { modal_=Modal::None;osd_.menu(now,false);return; }
    modal_=Modal::Menu;menu_selected_=0;menu_items_.clear();actions_.clear();menu_title_="Options";
    auto add=[&](const std::string& name,Action action){menu_items_.push_back(name);actions_.push_back(action);};
    Item item;bool valid=browser_.selected_item(item);
    if (screen_==Screen::Video) {
        add("Aspect ratio: Fit / Fill / Original",Action::Aspect);add("File information",Action::Information);
        if (video_list_.entries().size()>1) add("Video playlist",Action::Playlist);
        if (media_snapshot_.state==Playback::Ended) add("Replay",Action::Replay);add("Stop and return to library",Action::Stop);
    } else if (screen_==Screen::Music) {
        const char* repeats[]={"Off","One","All"};add(std::string("Repeat: ")+repeats[saved_.settings.repeat],Action::Repeat);
        add(std::string("Shuffle: ")+(saved_.settings.shuffle?"On":"Off"),Action::Shuffle);add("Playlist",Action::Playlist);add("File information",Action::Information);add("Stop music",Action::Stop);
    } else if (screen_==Screen::Photo) {
        add("View: Fit / Original",Action::Aspect);add("Rotate clockwise",Action::Rotate);add(slideshow_?"Pause slideshow":"Start slideshow",Action::Slideshow);
        add("Slideshow interval: "+std::to_string(saved_.settings.slideshow_seconds)+" seconds",Action::SlideshowSpeed);add("File information",Action::Information);
        if (photo_ && photo_->frames.size()>1) add(animation_paused_?"Play animation":"Pause animation",Action::Animation);
    } else {
        add("Refresh",Action::Refresh);
        if (browser_.view==LibraryView::Directory) {
            add("Search this folder",Action::Search);add("Sort by name",Action::SortName);add("Sort by date",Action::SortDate);add("Sort by size",Action::SortSize);add("Sort by type",Action::SortType);
            if (valid) { add(is_favorite(saved_,item)?"Remove from favorites":"Add to favorites",Action::Favorite);
                if (item.entry.kind==Kind::Music || item.entry.kind==Kind::Video) add("Add to playlist",Action::Enqueue);add("File information",Action::Information); }
            auto index=indexer_.snapshot(false);add(index.running?"Cancel indexing":"Index this folder",index.running?Action::CancelIndex:Action::Index);if (index.scanned) add("Indexed files",Action::Indexed);
        }
        add("Favorites",Action::Favorites);add("Last played",Action::LastPlayed);
        if (media_snapshot_.kind==Kind::Music && media_snapshot_.state!=Playback::Idle) add("Now playing",Action::NowPlaying);
        add("USB sources",Action::Sources);add("Rescan USB sources",Action::Rescan);add("Settings",Action::Settings);add("Exit application",Action::Quit);
        if (browser_.sources.empty() || debug_) add("USB diagnostics",Action::UsbDiagnostics);
    }
    add("Close",Action::Close);osd_.menu(now,true);
}
void Application::activate(Action action,double now) {
    modal_=Modal::None;osd_.menu(now,false);Item item;bool valid=browser_.selected_item(item);notice_.clear();
    switch (action) {
        case Action::Refresh:if (browser_.view==LibraryView::Directory) browser_.refresh();else browser_.rescan(mount_base_,extras_);break;
        case Action::Search:modal_=Modal::Search;search_text_=browser_.query;keyboard();break;
        case Action::SortName:browser_.set_sort(Sort::Name);break;case Action::SortDate:browser_.set_sort(Sort::Date);break;
        case Action::SortSize:browser_.set_sort(Sort::Size);break;case Action::SortType:browser_.set_sort(Sort::Type);break;
        case Action::Favorite:if (valid) { toggle_favorite(saved_,item);persist(); }break;
        case Action::Enqueue:if (valid) { enqueue(item.entry.kind==Kind::Music?saved_.music_queue:saved_.video_queue,item);persist();notice_="Added to the saved playlist"; }break;
        case Action::Information:if (screen_==Screen::Photo) info(photo_item_);else if (screen_==Screen::Music || screen_==Screen::Video) info(media_snapshot_.item);else if (valid) info(item);break;
        case Action::Favorites:browser_.show_items(LibraryView::Favorites,saved_.favorites,"Favorites");screen_=Screen::Browser;break;
        case Action::LastPlayed:browser_.show_items(LibraryView::LastPlayed,saved_.last_played,"Last played");screen_=Screen::Browser;break;
        case Action::Sources:browser_.show_sources();screen_=Screen::Browser;break;
        case Action::Rescan:browser_.show_sources();browser_.rescan(mount_base_,extras_);screen_=Screen::Browser;break;
        case Action::UsbDiagnostics:usb_diagnostics();break;
        case Action::Settings:modal_=Modal::Settings;menu_selected_=0;settings_items();break;
        case Action::Index:indexer_.start(browser_.source(),browser_.directory(),saved_.settings.hidden);notice_="Indexing in the background; cancel from Options";break;
        case Action::CancelIndex:indexer_.cancel();notice_="Indexing cancelled";break;
        case Action::Indexed:{auto index=indexer_.snapshot();browser_.show_items(LibraryView::Index,std::move(index.entries),index.truncated?"Indexed files (bounded to 4096 items / 8 MiB)":"Indexed files");break;}
        case Action::NowPlaying:screen_=Screen::Music;break;
        case Action::Playlist:browser_.show_items(LibraryView::Queue,screen_==Screen::Video?video_list_.entries():music_list_.entries(),"Current playlist");if (screen_==Screen::Video) media_.stop();screen_=Screen::Browser;break;
        case Action::Repeat:saved_.settings.repeat=(saved_.settings.repeat+1)%3;persist();break;
        case Action::Shuffle:saved_.settings.shuffle=!saved_.settings.shuffle;persist();break;
        case Action::Stop:media_.stop();screen_=Screen::Browser;break;
        case Action::Aspect:if (screen_==Screen::Photo) { photo_aspect_=photo_aspect_==Aspect::Fit?Aspect::Original:Aspect::Fit;zoom_=1; }else video_aspect_=static_cast<Aspect>((static_cast<int>(video_aspect_)+1)%3);break;
        case Action::Rotate:rotation_=(rotation_+1)%4;break;
        case Action::Slideshow:slideshow_=!slideshow_;slide_due_=now+saved_.settings.slideshow_seconds;break;
        case Action::SlideshowSpeed:saved_.settings.slideshow_seconds=saved_.settings.slideshow_seconds>=30?2:std::min(30,saved_.settings.slideshow_seconds+3);slide_due_=now+saved_.settings.slideshow_seconds;persist();break;
        case Action::Animation:
            if (photo_ && photo_->frames.size()>1) {
                if (animation_paused_) animation_started_+=now-animation_paused_at_;
                else animation_paused_at_=now;
                animation_paused_=!animation_paused_;
            }
            break;
        case Action::Replay:open_item(media_snapshot_.item,now);break;
        case Action::Quit:running_=false;break;case Action::Close:break;
    }
}
void Application::handle_modal(const InputEvent& e,double now) {
    if (e.edge(Circle) || (e.edge(Options) && modal_!=Modal::Search) || (e.edge(Square) && modal_==Modal::Details)) { modal_=Modal::None;osd_.menu(now,false);return; }
    if (modal_==Modal::Search) {
        int delta=e.has(Left)?-1:e.has(Right)?1:e.has(Up)?-10:e.has(Down)?10:0;
        key_selected_=static_cast<size_t>(std::max<int64_t>(0,std::min<int64_t>(static_cast<int64_t>(keyboard_chars_.size())-1,static_cast<int64_t>(key_selected_)+delta)));
        auto backspace=[&]{auto chars=utf8_decode(search_text_);if (!chars.empty()) chars.pop_back();search_text_.clear();for (auto c:chars) search_text_+=utf8_encode(c);};
        if (e.edge(Square)) backspace();if (e.edge(Triangle)) { keyboard_arabic_=!keyboard_arabic_;keyboard(); }
        if (e.edge(Options)) { browser_.search(search_text_);modal_=Modal::None; }
        if (e.edge(Cross)) {
            size_t chars=keyboard_chars_.size()-5;
            if (key_selected_<chars) { if (search_text_.size()<256) search_text_+=keyboard_chars_[key_selected_]=="Space"?" ":keyboard_chars_[key_selected_]; }
            else if (key_selected_==chars) backspace();else if (key_selected_==chars+1) search_text_.clear();
            else if (key_selected_==chars+2) { browser_.search(search_text_);modal_=Modal::None; }
            else if (key_selected_==chars+3) { keyboard_arabic_=!keyboard_arabic_;keyboard(); }else modal_=Modal::None;
        }
        return;
    }
    if (modal_==Modal::Details || modal_==Modal::Message) {
        size_t maximum=information_.size()>information_page_rows?information_.size()-information_page_rows:0;
        information_scroll_=std::min(information_scroll_,maximum);
        if (e.has(Up) && information_scroll_) --information_scroll_;
        if (e.has(Down) && information_scroll_+information_page_rows<information_.size()) ++information_scroll_;
        return;
    }
    if (e.has(Up) && menu_selected_) --menu_selected_;if (e.has(Down) && menu_selected_+1<menu_items_.size()) ++menu_selected_;
    if (modal_==Modal::Settings && (e.has(Left) || e.has(Right) || e.edge(Cross))) { adjust_setting(e.has(Left)?-1:1,now);return; }
    if (!e.edge(Cross)) return;
    if (modal_==Modal::ConfirmClear) {
        if (menu_selected_==1) { saved_={};saved_modified_=true;store_.clear_owned_data();media_.volume(saved_.settings.volume);browser_.set_hidden(false);notice_="Internal app data cleared"; }
        modal_=Modal::None;return;
    }
    if (menu_selected_<actions_.size()) activate(actions_[menu_selected_],now);
}
void Application::open_item(Item item,double now,bool foreground) {
    if (item.entry.kind==Kind::Photo) {
        std::vector<Item> list;
        if (browser_.view==LibraryView::Directory) {
            for (const auto& entry:browser_.entries) if (entry.kind==Kind::Photo) list.push_back({browser_.source(),entry});
        } else list={item};
        photo_list_.set(std::move(list),item);
        photo_frame_=0;animation_paused_=false;
        photo_item_=item;photo_.reset();photo_loading_=true;photo_generation_=photos_.request(item,false);screen_=Screen::Photo;zoom_=1;rotation_=0;slideshow_=false;slide_due_=now+saved_.settings.slideshow_seconds;dirty_=true;return;
    }
    if (item.entry.kind!=Kind::Music && item.entry.kind!=Kind::Video) { notice_="Cannot open this file. Only video, music and image files can be opened.";dirty_=true;return; }
    if (item.entry.kind==Kind::Music && media_snapshot_.kind==Kind::Music
        && (media_snapshot_.state==Playback::Playing || media_snapshot_.state==Playback::Paused || media_snapshot_.state==Playback::Seeking)
        && media_snapshot_.item.source.root==item.source.root && media_snapshot_.item.entry.relative==item.entry.relative) {
        screen_=Screen::Music;notice_.clear();dirty_=true;return;
    }
    Playlist& list=item.entry.kind==Kind::Music?music_list_:video_list_;
    if (foreground) {
        auto queue=item.entry.kind==Kind::Music?saved_.music_queue:saved_.video_queue;
        if (queue.empty() && browser_.view==LibraryView::Directory) for (const auto& entry:browser_.entries) if (entry.kind==item.entry.kind) queue.push_back({browser_.source(),entry});
        list.set(std::move(queue),item);
    }
    media_.open(item);media_snapshot_=media_.snapshot();if (foreground) screen_=item.entry.kind==Kind::Music?Screen::Music:Screen::Video;
    osd_.started(now);handled_end_=0;notice_.clear();dirty_=true;if (debug_) std::fprintf(stderr,"Opening %s\n",item.entry.relative.c_str());
}
void Application::next_media(bool previous,bool automatic,double now) {
    Playlist& list=media_snapshot_.kind==Kind::Music?music_list_:video_list_;Item item;
    bool ok=previous?list.previous(item):list.next(item,automatic,saved_.settings.repeat,saved_.settings.shuffle);
    if (ok) { for (const auto& source:browser_.sources) if (source.root==item.source.root) item.source=source;open_item(item,now,false); }
}
void Application::photo_next(bool previous,double now) {
    Item item;bool ok=previous?photo_list_.previous(item):photo_list_.next(item,false,2,false);
    if (ok) { photo_item_=item;photo_.reset();photo_loading_=true;photo_generation_=photos_.request(item,false);zoom_=1;rotation_=0;slide_due_=now+saved_.settings.slideshow_seconds;photo_frame_=0;animation_paused_=false; }
}
void Application::preview_selection(double now) {
    Item item;bool valid=browser_.selected_item(item);std::string key=valid?item.source.root+"/"+item.entry.relative:"";
    if (key!=preview_selection_) { preview_selection_=key;previews_.cancel();preview_.reset();preview_pending_=valid && (item.entry.kind==Kind::Photo || item.entry.kind==Kind::Video);pending_preview_=item;preview_due_=now+0.15;dirty_=true; }
    if (preview_pending_ && now>=preview_due_) { preview_pending_=false;preview_generation_=previews_.request(pending_preview_,true); }
}
void Application::pointer_input(const RawInput& raw,InputEvent& e,double now) {
    if (!raw.connected || !raw.pointer_pressed) return;
    int x=raw.pointer_x,y=raw.pointer_y;
    if (x<0 || y<0 || x>=screen_width || y>=screen_height) return;
    e.intentional=true;
    auto inside=[&](int left,int top,int width,int height){return x>=left && y>=top && x<left+width && y<top+height;};
    auto press=[&](Button button){e.pressed|=button;};
    if (modal_!=Modal::None) {
        if (modal_==Modal::Search) {
            if (inside(388,365,1130,438)) {
                size_t col=static_cast<size_t>((x-388)/113),row=static_cast<size_t>((y-365)/73),index=row*10+col;
                if ((x-388)%113<103 && (y-365)%73<61 && index<keyboard_chars_.size()) { key_selected_=index;press(Cross); }
            }
        } else if (inside(497,291,926,495) && modal_!=Modal::Details && modal_!=Modal::Message) {
            size_t start=menu_selected_>=9?menu_selected_-8:0,index=start+static_cast<size_t>((y-291)/55);
            if (index<menu_items_.size()) { menu_selected_=index;press(Cross); }
        } else if (inside(508,815,310,70)) press(modal_==Modal::Details || modal_==Modal::Message?Circle:Cross);
        else if (inside(813,815,310,70) && modal_!=Modal::Details && modal_!=Modal::Message) press(Circle);
        return;
    }
    if (screen_==Screen::Music) {
        if (inside(music_layout::back_left,music_layout::back_top,music_layout::back_width,music_layout::back_height) || inside(75,995,250,64)) press(Circle);
        else if (inside(415,995,290,64)) press(Triangle);
        else if (inside(770,995,365,64)) press(Square);
        else if (inside(music_layout::controls_left,music_layout::controls_top,music_layout::controls_width,music_layout::control_height)) {
            size_t index=static_cast<size_t>((x-music_layout::controls_left)/music_layout::control_stride);
            if (index>5 || (x-music_layout::controls_left)%music_layout::control_stride>=music_layout::control_width) return;
            const Button buttons[]={L1,L2,Cross,R2,R1,Square};
            bool active=media_snapshot_.state==Playback::Playing || media_snapshot_.state==Playback::Paused || media_snapshot_.state==Playback::Seeking || media_snapshot_.state==Playback::Ended;
            const bool enabled[]={music_list_.entries().size()>1,media_snapshot_.seekable,active,media_snapshot_.seekable,music_list_.entries().size()>1,media_snapshot_.state!=Playback::Idle};
            if (enabled[index]) press(buttons[index]);
        }
    } else if (screen_==Screen::Browser) {
        if (inside(72,145,1776,82)) {
            int tab=(x-72)/449;if (tab<4 && (x-72)%449<429) { browser_.change_tab(tab-browser_.tab);browser_.focus=Focus::List; }
        } else if (inside(65,270,1030,648)) {
            size_t index=browser_.scroll()+static_cast<size_t>((y-270)/72);
            if (!browser_.loading && index<browser_.entries.size()) { browser_.focus=Focus::List;browser_.move(static_cast<int>(index)-static_cast<int>(browser_.selected()));if (raw.pointer_clicks>=2) press(Cross); }
        } else if (inside(553,995,260,64)) press(Triangle);
        else if (inside(853,995,260,64)) press(Square);
        else if (inside(318,995,230,64)) press(Circle);
        else if (inside(72,995,230,64)) press(Cross);
    } else if (screen_==Screen::Photo) {
        if (inside(75,995,390,64)) press(Cross);else if (inside(510,995,240,64)) press(Circle);
        else if (inside(785,995,310,64)) press(Triangle);else if (inside(1120,995,290,64)) press(Square);
    } else if (screen_==Screen::Video && video_layout::visible(osd_.visible(now),media_snapshot_.state)) {
        if (inside(video_layout::back_left,video_layout::back_top,video_layout::back_width,video_layout::back_height)) press(Circle);
        else if (inside(video_layout::options_left,video_layout::options_top,video_layout::options_width,video_layout::options_height)) press(Triangle);
        else if (inside(video_layout::aspect_left,video_layout::aspect_top,video_layout::aspect_width,video_layout::aspect_height)) press(Square);
        else if (inside(video_layout::controls_left,video_layout::controls_top,video_layout::controls_width,video_layout::control_height)) {
            size_t index=static_cast<size_t>((x-video_layout::controls_left)/video_layout::control_stride);
            if (index>=video_layout::buttons.size() || (x-video_layout::controls_left)%video_layout::control_stride>=video_layout::control_width) return;
            auto enabled=video_layout::enabled(media_snapshot_,video_list_.entries().size());
            if (enabled[index]) press(video_layout::buttons[index]);
        }
    }
}
bool Application::tick(const RawInput& raw,double now) {
    dirty_=false;auto e=input_.update(raw,now);pointer_input(raw,e,now);
    if (screen_==Screen::Video && modal_==Modal::None && e.edge(R3)) {
        if (osd_.hidden_by_user()) osd_.activity(now,true); else osd_.hide();
    } else osd_.activity(now,e.intentional);
    if (e.intentional) dirty_=true;
    if (browser_.poll()) {
        dirty_=true;store_.protect_sources(browser_.sources);
        if (store_.retry_open()) {
            if (!saved_modified_) {
                saved_=store_.read();browser_.set_hidden(saved_.settings.hidden);media_.volume(saved_.settings.volume);
            } else persist();
            notice_=store_.error();
        }
    }
    auto snapshot=media_.snapshot();
    if (media_snapshot_.state==Playback::Loading && snapshot.state==Playback::Playing) osd_.started(now);
    if (snapshot.state!=media_snapshot_.state || snapshot.session!=media_snapshot_.session || snapshot.frame_revision!=media_snapshot_.frame_revision || int(snapshot.position*10)!=int(media_snapshot_.position*10) || snapshot.error!=media_snapshot_.error) dirty_=true;
    media_snapshot_=std::move(snapshot);osd_.paused(now,media_snapshot_.paused);
    if (media_snapshot_.session!=last_recorded_ && (media_snapshot_.position>0 || media_snapshot_.frame)) { record_played(saved_,media_snapshot_.item);persist();last_recorded_=media_snapshot_.session; }
    if (media_snapshot_.state==Playback::Ended && handled_end_!=media_snapshot_.session) { handled_end_=media_snapshot_.session;if (saved_.settings.auto_next) next_media(false,true,now); }
    if (media_snapshot_.state==Playback::Error && media_snapshot_.error.find("USB disconnected")!=std::string::npos) browser_.disconnected(media_snapshot_.item.source.root);
    auto disconnected=browser_.take_disconnected();
    for (const auto& root:disconnected) {
        if (media_snapshot_.item.source.root==root) { media_.stop();media_snapshot_=media_.snapshot(); }
        photos_.cancel();previews_.cancel();indexer_.cancel();photo_.reset();preview_.reset();
        photo_loading_=slideshow_=preview_pending_=false;preview_selection_.clear();
        screen_=Screen::Browser;modal_=Modal::None;osd_.menu(now,false);notice_=browser_.status;dirty_=true;
    }
    if (!disconnected.empty()) return true;
    if (auto result=previews_.take()) if (result->generation==preview_generation_) { preview_=std::move(result);dirty_=true; }
    if (auto result=photos_.take()) if (result->generation==photo_generation_) { photo_=std::move(result);photo_loading_=false;dirty_=true;slide_due_=now+saved_.settings.slideshow_seconds;animation_started_=now;photo_frame_=0;animation_paused_=false;if (photo_->bitmap) { record_played(saved_,photo_item_);persist(); }else slideshow_=false; }
    if (screen_==Screen::Photo && photo_ && photo_->frames.size()>1) {
        auto frame=animation_frame_at(*photo_,(animation_paused_?animation_paused_at_:now)-animation_started_);
        if (frame!=photo_frame_) { photo_frame_=frame;dirty_=true; }
    }
    if (now>=next_index_tick_) { auto index=indexer_.snapshot(false);if (index.running) { notice_="Indexing: "+std::to_string(index.scanned)+" files (Options to cancel)";dirty_=true; }next_index_tick_=now+0.5; }
    if (modal_!=Modal::None) handle_modal(e,now);
    else if (screen_==Screen::Browser) {
        if (e.edge(Triangle) || e.edge(Options)) menu(now);
        else if (e.edge(Square)) { Item item;if (browser_.selected_item(item)) info(item);else if (auto* selected=browser_.selected_entry()) {
            modal_=Modal::Details;menu_title_="Storage information";information_scroll_=0;information_={"Name: "+selected->name,"Path: "+selected->relative};
            if (browser_.selected()<browser_.sources.size()) {
                const auto& source=browser_.sources[browser_.selected()];
                information_.push_back(source.space_known?"Available: "+format_size(source.available_bytes)+"    Total: "+format_size(source.total_bytes):"Space: unavailable");
                if (!source.filesystem.empty()) information_.push_back("Filesystem: "+source.filesystem);
            }
            information_.push_back("Storage access: read only");
        } else if (browser_.view==LibraryView::Sources) usb_diagnostics(); }
        else if (e.edge(Circle)) { browser_.back();notice_.clear(); }
        else if (e.edge(L1)) browser_.change_tab(-1);else if (e.edge(R1)) browser_.change_tab(1);
        else if (browser_.focus==Focus::Tabs) { if (e.has(Left)) browser_.change_tab(-1);if (e.has(Right)) browser_.change_tab(1);if (e.has(Down) || e.edge(Cross)) browser_.focus=Focus::List; }
        else if (browser_.focus==Focus::Details) { if (e.has(Left) || e.has(Up) || e.has(Down)) browser_.focus=Focus::List;if (e.edge(Cross)) { Item item;if (browser_.selected_item(item)) info(item); } }
        else { if (e.has(Up)) browser_.move(-1);if (e.has(Down)) browser_.move(1);if (e.has(Left)) browser_.focus=Focus::Tabs;if (e.has(Right)) browser_.focus=Focus::Details;
            if (e.edge(Cross) && !browser_.enter_selected_folder()) { Item item;if (!browser_.loading && browser_.selected_item(item)) open_item(item,now); }
        }
        preview_selection(now);
    } else if (screen_==Screen::Video || screen_==Screen::Music) {
        if (e.edge(Circle)) { if (screen_==Screen::Video) media_.stop();screen_=Screen::Browser; }
        else if (e.edge(Triangle) || e.edge(Options)) menu(now);
        else if (e.edge(Square) && screen_==Screen::Music) { media_.stop();screen_=Screen::Browser;notice_="Music stopped"; }
        else if (e.edge(Cross)) { if (media_snapshot_.state==Playback::Ended) open_item(media_snapshot_.item,now);else media_.toggle_pause(); }
        else {
            if (e.has(Left) || e.has(L2)) if (!media_.seek_by(-10)) notice_="Seek is unavailable for this file";
            if (e.has(Right) || e.has(R2)) if (!media_.seek_by(10)) notice_="Seek is unavailable for this file";
            if (e.edge(L1)) next_media(true,false,now);if (e.edge(R1)) next_media(false,false,now);
            if (media_snapshot_.has_audio && (e.has(Up) || e.has(Down))) { saved_.settings.volume=std::max(0,std::min(100,saved_.settings.volume+(e.has(Up)?5:-5)));media_.volume(saved_.settings.volume);persist(); }
            if (e.edge(Square) && screen_==Screen::Video) video_aspect_=static_cast<Aspect>((static_cast<int>(video_aspect_)+1)%3);
        }
    } else {
        if (e.edge(Circle)) { photos_.cancel();photo_.reset();photo_loading_=slideshow_=false;screen_=Screen::Browser; }
        else if (e.edge(Triangle)) info(photo_item_);else if (e.edge(Options)) menu(now);
        else {
            if (e.edge(Cross) && !photo_loading_ && photo_ && photo_->bitmap) { slideshow_=!slideshow_;slide_due_=now+saved_.settings.slideshow_seconds; }
            if (e.has(Left) || e.edge(L1)) photo_next(true,now);if (e.has(Right) || e.edge(R1)) photo_next(false,now);
            if (e.has(Up)) zoom_=std::min(4.f,zoom_+0.25f);if (e.has(Down)) zoom_=std::max(0.25f,zoom_-0.25f);
            if (e.edge(Square)) { photo_aspect_=photo_aspect_==Aspect::Fit?Aspect::Original:Aspect::Fit;zoom_=1; }
            if (slideshow_ && !photo_loading_ && now>=slide_due_) { photo_next(false,now);dirty_=true; }
        }
    }
    bool visible=osd_.visible(now);if (visible!=previous_osd_visible_) { previous_osd_visible_=visible;dirty_=true; }
    double tick=std::floor(now);if (tick!=last_draw_tick_) { last_draw_tick_=tick;dirty_=true; }
    return dirty_;
}
ViewModel Application::view(double now) const {
    ViewModel out;out.browser=&browser_;out.screen=screen_;out.modal=modal_;out.settings=saved_.settings;out.media=media_snapshot_;out.preview=preview_;out.photo=photo_;out.photo_item=photo_item_;
    out.photo_loading=photo_loading_;out.slideshow=slideshow_;out.zoom=zoom_;out.rotation=rotation_;out.video_aspect=video_aspect_;out.photo_aspect=photo_aspect_;
    out.photo_frame=photo_frame_;out.animation_paused=animation_paused_;
    out.information_scroll=information_scroll_;
    out.osd=osd_.visible(now);out.menu_items=menu_items_;out.menu_selected=menu_selected_;out.menu_title=menu_title_;out.information=information_;out.keyboard_keys=keyboard_chars_;out.keyboard_selected=key_selected_;out.search_text=search_text_;out.notice=notice_;
    const Playlist& list=screen_==Screen::Photo?photo_list_:media_snapshot_.kind==Kind::Video?video_list_:music_list_;out.playlist_count=list.entries().size();out.playlist_index=list.index();
    if (screen_==Screen::Music && music_list_.entries().size()>1) {
        out.next_track=saved_.settings.shuffle?"Shuffle enabled":music_list_.entries()[(music_list_.index()+1)%music_list_.entries().size()].entry.name;
    }
    return out;
}
}
