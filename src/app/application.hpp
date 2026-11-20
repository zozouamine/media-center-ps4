#pragma once
#include "library/indexer.hpp"
#include "playlists/playlist.hpp"
#include "ui/view.hpp"
#include "video/osd.hpp"

namespace mc {
class Application {
    enum class Action { Refresh, Search, SortName, SortDate, SortSize, SortType, Favorite, Enqueue, Information,
        Favorites, LastPlayed, Sources, Rescan, UsbDiagnostics, Settings, Index, CancelIndex, Indexed, NowPlaying,
        Playlist, Repeat, Shuffle, Stop, Aspect, Rotate, Slideshow, SlideshowSpeed, Animation, Replay, Quit, Close };
    StateStore store_;
    SavedState saved_;
    Browser browser_;
    MediaEngine media_;
    ImageWorker previews_,photos_;
    Indexer indexer_;
    InputFilter input_;
    Osd osd_;
    Screen screen_=Screen::Browser;
    Modal modal_=Modal::None;
    std::vector<Action> actions_;
    std::vector<std::string> menu_items_,information_;
    std::string menu_title_,notice_,mount_base_;
    std::vector<std::string> extras_;
    size_t menu_selected_=0,key_selected_=0;
    size_t information_scroll_=0;
    bool keyboard_arabic_=false,running_=true,photo_loading_=false,slideshow_=false,dirty_=true,debug_=false;
    std::string search_text_,preview_selection_;
    std::vector<std::string> keyboard_chars_;
    std::shared_ptr<ImageResult> preview_,photo_;
    Item pending_preview_{},photo_item_{};
    bool preview_pending_=false;
    bool previous_osd_visible_=true;
    bool saved_modified_=false;
    bool animation_paused_=false;
    size_t photo_frame_=0;
    uint64_t preview_generation_=0,photo_generation_=0,last_recorded_=0,handled_end_=0;
    double preview_due_=0,slide_due_=0,next_index_tick_=0,last_draw_tick_=-1;
    double animation_started_=0,animation_paused_at_=0;
    float zoom_=1;
    int rotation_=0;
    Aspect video_aspect_=Aspect::Fit,photo_aspect_=Aspect::Fit;
    Playlist music_list_,video_list_,photo_list_;
    MediaSnapshot media_snapshot_{};
    void menu(double now);
    void activate(Action,double now);
    void handle_modal(const InputEvent&,double now);
    void settings_items();
    void adjust_setting(int direction,double now);
    void keyboard();
    void info(const Item&);
    void usb_diagnostics();
    void next_media(bool previous,bool automatic,double now);
    void photo_next(bool previous,double now);
    void persist();
    void preview_selection(double now);
    void pointer_input(const RawInput&,InputEvent&,double now);
public:
    Application(Platform&,std::vector<Source> sources,std::string mount_base,std::vector<std::string> extras,
                bool debug=false,std::vector<std::string> source_diagnostics={});
    void initialize();
    bool tick(const RawInput&,double now);
    void open_item(Item item,double now,bool foreground=true);
    ViewModel view(double now) const;
    bool running() const { return running_; }
    Browser& browser() { return browser_; }
    const MediaSnapshot& media() const { return media_snapshot_; }
};
}
