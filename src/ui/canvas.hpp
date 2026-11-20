#pragma once
#include "image/image.hpp"
#include "library/types.hpp"
#include "platform/platform.hpp"
#include <memory>
#include <unordered_map>

namespace mc {
struct Color { uint8_t r, g, b; };
constexpr Color background{3,9,27}, panel{4,12,31}, blue{5,80,224}, accent{56,151,255}, white{244,247,255}, muted{160,176,203}, rule{33,51,79};
constexpr float interface_font_scale = 0.82f;
struct Glyph { std::vector<uint8_t> alpha; int width=0,height=0,xoff=0,yoff=0,advance=0,ascent=0; };
class FontBook {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    FontBook();
    ~FontBook();
    bool load(const std::string& assets_root,std::string& error);
    const Glyph& glyph(uint32_t codepoint,int pixels);
};
std::vector<uint32_t> visual_text(const std::string& text);
enum class Aspect { Fit, Fill, Original };
class Canvas {
    std::vector<uint32_t> pixels_;
    FontBook fonts_;
    static uint32_t pack(Color color);
    void blend(int x,int y,Color color,uint8_t alpha);
public:
    Canvas();
    bool load_fonts(const std::string& assets_root,std::string& error) { return fonts_.load(assets_root,error); }
    const uint32_t* data() const { return pixels_.data(); }
    uint32_t* data() { return pixels_.data(); }
    void clear(Color color=background);
    void rect(int x,int y,int w,int h,Color color);
    void gradient_overlay(int x,int y,int w,int h,Color color,uint8_t top_alpha,uint8_t bottom_alpha);
    void border(int x,int y,int w,int h,Color color,int thickness=1);
    void rounded_rect(int x,int y,int w,int h,int radius,Color color);
    void rounded_panel(int x,int y,int w,int h,int radius,Color fill,Color border,int thickness=1);
    void line(int x1,int y1,int x2,int y2,Color color,int thickness=1);
    void circle(int cx,int cy,int radius,Color color,int thickness=2);
    int text_width(const std::string& text,int size);
    void text(const std::string& text,int x,int y,int size,Color color=white,int maximum_width=1800,bool rtl_align=true);
    void text_in_row(const std::string& text,int x,int y,int width,int height,int size,Color color=white,bool rtl_align=true);
    void wrapped_text(const std::string& text,int x,int y,int size,Color color,int maximum_width,int maximum_lines=3);
    void icon(Kind kind,int x,int y,int size,Color color=white);
    void bitmap(const Bitmap&,int x,int y,int w,int h,Aspect aspect=Aspect::Fit,float zoom=1.f,int rotation=0);
    void tinted_bitmap(const Bitmap&,int x,int y,int w,int h,Color color);
    void button(Button button,int x,int y,const std::string& label,int size=28);
};
}
