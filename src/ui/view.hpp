#pragma once
#include "browser.hpp"
#include "canvas.hpp"
#include "media/engine.hpp"
#include "settings/store.hpp"
#include <array>

namespace mc {
enum class Screen { Browser, Video, Music, Photo };
enum class Modal { None, Menu, Settings, Search, Details, ConfirmClear, Message };
constexpr size_t information_page_rows = 6;
struct ViewModel {
    const Browser* browser = nullptr;
    Screen screen = Screen::Browser;
    Modal modal = Modal::None;
    Settings settings;
    MediaSnapshot media;
    std::shared_ptr<ImageResult> preview, photo;
    Item photo_item{};
    bool photo_loading = false, slideshow = false, osd = true;
    bool animation_paused = false;
    size_t photo_frame = 0;
    float zoom = 1.f;
    int rotation = 0;
    Aspect video_aspect = Aspect::Fit, photo_aspect = Aspect::Fit;
    size_t playlist_count = 0, playlist_index = 0;
    std::string next_track;
    std::vector<std::string> menu_items, keyboard_keys, information;
    size_t menu_selected = 0, keyboard_selected = 0;
    size_t information_scroll = 0;
    std::string menu_title, search_text, notice;
};
class Renderer {
    Canvas canvas_;
    std::shared_ptr<Bitmap> logo_,usb_icon_,video_icon_,music_icon_,photo_icon_,folder_icon_,playing_icon_;
    std::array<std::shared_ptr<Bitmap>,10> transport_icons_;
    std::array<std::shared_ptr<Bitmap>,4> button_icons_;
    uint64_t revision_ = 0;
    double tab_position_ = 0,tab_from_ = 0,tab_target_ = 0,tab_transition_started_ = 0;
    double list_position_ = 0,list_from_ = 0,list_target_ = 0,list_transition_started_ = 0;
    int observed_tab_ = -1;
    std::string list_context_;
    bool tab_transition_active_ = false,list_transition_active_ = false;
    void icon(Kind,int x,int y,int size,Color);
    void button(Button,int x,int y,const std::string& label,int size=28);
    void update_tab_transition(double now,int tab);
    void update_list_transition(double now,double position,const std::string& context);
    void header(const ViewModel&,const std::string& title);
    void browser(const ViewModel&);
    void video(const ViewModel&);
    void music(const ViewModel&);
    void photo(const ViewModel&);
    void modal(const ViewModel&);
public:
    bool initialize(const std::string& assets,std::string& error);
    void draw(const ViewModel& model);
    bool animating() const { return tab_transition_active_ || list_transition_active_; }
    const Canvas& canvas() const { return canvas_; }
    uint64_t revision() const { return revision_; }
};
}
