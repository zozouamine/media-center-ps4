#include "canvas.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace mc {
namespace {
struct ArabicForm { uint32_t base,isolated,final,initial,medial; };
#include "arabic_forms.inc"
const ArabicForm* form(uint32_t cp) {
    auto it=std::lower_bound(std::begin(arabic_forms),std::end(arabic_forms),cp,[](const ArabicForm& a,uint32_t c){ return a.base<c; });
    return it!=std::end(arabic_forms) && it->base==cp?it:nullptr;
}
bool arabic(uint32_t c) { return (c>=0x600 && c<=0x8ff) || (c>=0xfb50 && c<=0xfdff) || (c>=0xfe70 && c<=0xfeff); }
bool mark(uint32_t c) { return c>=0x64b && c<=0x65f; }
}
std::vector<uint32_t> visual_text(const std::string& text) {
    auto original=utf8_decode(text),shaped=original;
    for (size_t i=0;i<original.size();++i) {
        const auto* current=form(original[i]);if (!current) continue;
        size_t prev=i,next=i+1;while (prev>0 && mark(original[prev-1])) --prev;while (next<original.size() && mark(original[next])) ++next;
        const auto* previous=prev?form(original[prev-1]):nullptr;const auto* following=next<original.size()?form(original[next]):nullptr;
        bool joins_prev=previous && previous->initial && current->final;
        bool joins_next=following && following->final && current->initial;
        uint32_t glyph=joins_prev && joins_next && current->medial?current->medial:joins_prev && current->final?current->final:joins_next && current->initial?current->initial:current->isolated;
        if (glyph) shaped[i]=glyph;
        if (original[i]==0x644 && next==i+1 && next<original.size()) {
            uint32_t base=original[next]==0x622?0xfef5:original[next]==0x623?0xfef7:original[next]==0x625?0xfef9:original[next]==0x627?0xfefb:0;
            if (base) { shaped[i]=base+(joins_prev?1:0);shaped[next]=0;++i; }
        }
    }
    for (size_t i=0;i<shaped.size();) {
        if (!arabic(shaped[i])) { ++i;continue; }
        size_t end=i+1;
        while (end<shaped.size() && (arabic(shaped[end]) || shaped[end]==0 || shaped[end]==' ')) ++end;
        while (end>i && shaped[end-1]==' ') --end;
        std::reverse(shaped.begin()+static_cast<ptrdiff_t>(i),shaped.begin()+static_cast<ptrdiff_t>(end));i=end;
    }
    shaped.erase(std::remove(shaped.begin(),shaped.end(),0),shaped.end());return shaped;
}
struct FontBook::Impl {
    std::vector<uint8_t> latin_data,arabic_data;
    stbtt_fontinfo latin{},arabic{};
    bool loaded=false;
    std::unordered_map<uint64_t,Glyph> cache;
    size_t bytes=0;
};
FontBook::FontBook():impl_(new Impl) {}
FontBook::~FontBook()=default;
bool FontBook::load(const std::string& root,std::string& error) {
    auto read=[&](const std::string& name,std::vector<uint8_t>& data) {
        std::ifstream f(root+"/fonts/"+name,std::ios::binary|std::ios::ate);if (!f) return false;
        auto n=f.tellg();if (n<32 || n>4194304) return false;data.resize(static_cast<size_t>(n));f.seekg(0);return bool(f.read(reinterpret_cast<char*>(data.data()),n));
    };
    if (!read("PS4MC-Sans.ttf",impl_->latin_data) || !read("PS4MC-Arabic.ttf",impl_->arabic_data)) { error="Bundled font assets missing";return false; }
    if (!stbtt_InitFont(&impl_->latin,impl_->latin_data.data(),0) || !stbtt_InitFont(&impl_->arabic,impl_->arabic_data.data(),0)) { error="Bundled fonts are invalid";return false; }
    impl_->loaded=true;return true;
}
const Glyph& FontBook::glyph(uint32_t cp,int size) {
    static Glyph empty;if (!impl_->loaded) return empty;size=std::max(12,std::min(96,static_cast<int>(std::round(size*interface_font_scale))));
    uint64_t key=(uint64_t(cp)<<8)|static_cast<unsigned>(size);auto found=impl_->cache.find(key);if (found!=impl_->cache.end()) return found->second;
    if (impl_->cache.size()>=768 || impl_->bytes>4u*1024u*1024u) { impl_->cache.clear();impl_->bytes=0; }
    const auto* face=arabic(cp)?&impl_->arabic:&impl_->latin;
    int index=stbtt_FindGlyphIndex(face,static_cast<int>(cp));if (!index) { face=&impl_->latin;index=stbtt_FindGlyphIndex(face,0x25a1); }
    // Arabic fonts have much taller ascender metrics. A shared em size/baseline
    // keeps Arabic filenames as readable as Latin on a television.
    float scale=stbtt_ScaleForMappingEmToPixels(face,static_cast<float>(size)*0.95f);int ascent,descent,gap,advance,bearing;
    stbtt_GetFontVMetrics(face,&ascent,&descent,&gap);stbtt_GetGlyphHMetrics(face,index,&advance,&bearing);
    Glyph glyph;glyph.ascent=static_cast<int>(std::round(size*0.85f));glyph.advance=static_cast<int>(std::round(advance*scale));
    auto* alpha=stbtt_GetGlyphBitmap(face,scale,scale,index,&glyph.width,&glyph.height,&glyph.xoff,&glyph.yoff);
    if (alpha && glyph.width>0 && glyph.height>0) { glyph.alpha.assign(alpha,alpha+static_cast<size_t>(glyph.width)*glyph.height);stbtt_FreeBitmap(alpha,nullptr);impl_->bytes+=glyph.alpha.size(); }
    auto result=impl_->cache.emplace(key,std::move(glyph));return result.first->second;
}
Canvas::Canvas():pixels_(static_cast<size_t>(screen_width)*screen_height) {}
uint32_t Canvas::pack(Color c) { return 0xff000000u|(uint32_t(c.b)<<16)|(uint32_t(c.g)<<8)|c.r; }
void Canvas::clear(Color c) { std::fill(pixels_.begin(),pixels_.end(),pack(c)); }
void Canvas::rect(int x,int y,int w,int h,Color c) {
    int right=std::min(screen_width,x+w),bottom=std::min(screen_height,y+h);x=std::max(0,x);y=std::max(0,y);
    if (x>=right || y>=bottom) return;
    uint32_t color=pack(c);for (int row=y;row<bottom;++row) std::fill(pixels_.begin()+static_cast<ptrdiff_t>(row*screen_width+x),pixels_.begin()+static_cast<ptrdiff_t>(row*screen_width+right),color);
}
void Canvas::gradient_overlay(int x,int y,int w,int h,Color color,uint8_t top_alpha,uint8_t bottom_alpha) {
    if (w<=0 || h<=0) return;
    int left=std::max(0,x),right=std::min(screen_width,x+w),bottom=std::min(screen_height,y+h);
    for (int row=std::max(0,y);row<bottom;++row) {
        auto alpha=static_cast<uint8_t>(int(top_alpha)+(int(bottom_alpha)-int(top_alpha))*(row-y)/std::max(1,h-1));
        for (int col=left;col<right;++col) blend(col,row,color,alpha);
    }
}
void Canvas::border(int x,int y,int w,int h,Color c,int t) { rect(x,y,w,t,c);rect(x,y+h-t,w,t,c);rect(x,y,t,h,c);rect(x+w-t,y,t,h,c); }
void Canvas::rounded_rect(int x,int y,int w,int h,int radius,Color color) {
    radius=std::max(0,std::min(radius,std::min(w,h)/2));
    if (!radius) { rect(x,y,w,h,color);return; }
    rect(x,y+radius,w,h-radius*2,color);
    for (int row=0;row<radius;++row) {
        double distance=radius-row-0.5;
        int inset=static_cast<int>(std::ceil(radius-std::sqrt(radius*radius-distance*distance)));
        rect(x+inset,y+row,w-inset*2,1,color);rect(x+inset,y+h-1-row,w-inset*2,1,color);
    }
}
void Canvas::rounded_panel(int x,int y,int w,int h,int radius,Color fill,Color stroke,int thickness) {
    rounded_rect(x,y,w,h,radius,stroke);
    rounded_rect(x+thickness,y+thickness,w-thickness*2,h-thickness*2,std::max(0,radius-thickness),fill);
}
void Canvas::line(int x1,int y1,int x2,int y2,Color c,int t) {
    int dx=std::abs(x2-x1),sx=x1<x2?1:-1,dy=-std::abs(y2-y1),sy=y1<y2?1:-1,err=dx+dy;
    while (true) { rect(x1-t/2,y1-t/2,t,t,c);if (x1==x2 && y1==y2) break;int e=2*err;if (e>=dy) { err+=dy;x1+=sx; }if (e<=dx) { err+=dx;y1+=sy; } }
}
void Canvas::circle(int x,int y,int r,Color c,int t) {
    if (r<=0 || t<=0) return;
    for (int dy=-r;dy<=r;++dy) for (int dx=-r;dx<=r;++dx) {
        double distance=std::sqrt(double(dx*dx+dy*dy));
        double outer=std::max(0.0,std::min(1.0,r+0.5-distance));
        double inner=t>=r?1.0:std::max(0.0,std::min(1.0,distance-(r-t)+0.5));
        blend(x+dx,y+dy,c,static_cast<uint8_t>(255*std::min(outer,inner)));
    }
}
void Canvas::blend(int x,int y,Color c,uint8_t a) {
    if (x<0 || y<0 || x>=screen_width || y>=screen_height || !a) return;
    auto& pixel=pixels_[static_cast<size_t>(y)*screen_width+x];if (a==255) { pixel=pack(c);return; }
    uint32_t inv=255-a,r=((pixel&255)*inv+c.r*a)/255,g=(((pixel>>8)&255)*inv+c.g*a)/255,b=(((pixel>>16)&255)*inv+c.b*a)/255;
    pixel=0xff000000u|(b<<16)|(g<<8)|r;
}
int Canvas::text_width(const std::string& text,int size) { int width=0;for (auto cp:visual_text(text)) width+=fonts_.glyph(cp,size).advance;return width; }
void Canvas::text(const std::string& text,int x,int y,int size,Color color,int maximum,bool rtl_align) {
    auto cps=visual_text(text);int width=0;for (auto c:cps) width+=fonts_.glyph(c,size).advance;
    auto source=utf8_decode(text);if (rtl_align && !source.empty() && arabic(source.front()) && width<maximum) x+=maximum-width;
    int cursor=x,right=std::min(screen_width,x+maximum);bool clipped=false;
    for (auto cp:cps) {
        const auto& g=fonts_.glyph(cp,size);
        if (cursor+g.advance>right) { clipped=true;break; }
        for (int row=0;row<g.height;++row) for (int col=0;col<g.width;++col) {
            int px=cursor+g.xoff+col;if (px>=x && px<right) blend(px,y+g.ascent+g.yoff+row,color,g.alpha[static_cast<size_t>(row)*g.width+col]);
        }
        cursor+=g.advance;
    }
    if (clipped && maximum>size*2) { const auto& dot=fonts_.glyph('.',size);int start=right-dot.advance*3;
        for (int i=0;i<3;++i) for (int row=0;row<dot.height;++row) for (int col=0;col<dot.width;++col) blend(start+i*dot.advance+dot.xoff+col,y+dot.ascent+dot.yoff+row,color,dot.alpha[static_cast<size_t>(row)*dot.width+col]);
    }
}
void Canvas::text_in_row(const std::string& value,int x,int y,int width,int height,int size,Color color,bool rtl_align) {
    int top=size*2,bottom=0;
    for (auto cp:visual_text(value)) {
        const auto& glyph=fonts_.glyph(cp,size);
        if (!glyph.height) continue;
        top=std::min(top,glyph.ascent+glyph.yoff);bottom=std::max(bottom,glyph.ascent+glyph.yoff+glyph.height);
    }
    if (bottom<=top) { top=0;bottom=size; }
    text(value,x,y+(height-(bottom-top))/2-top,size,color,width,rtl_align);
}
void Canvas::wrapped_text(const std::string& text,int x,int y,int size,Color color,int width,int lines) {
    std::string current;int line_index=0;
    for (auto cp:utf8_decode(text)) { auto next=current+utf8_encode(cp);if (!current.empty() && text_width(next,size)>width) { this->text(current,x,y+line_index*(size+5),size,color,width);current.clear();if (++line_index>=lines) return; }current+=utf8_encode(cp); }
    if (!current.empty() && line_index<lines) this->text(current,x,y+line_index*(size+5),size,color,width);
}
void Canvas::icon(Kind k,int x,int y,int s,Color c) {
    int t=std::max(2,s/12);
    if (k==Kind::Folder) { rect(x,y+s/5,s/2,s/5,c);rect(x,y+s/3,s,s*3/5,c); }
    else if (k==Kind::Video) {
        border(x,y,s,s,c,t);line(x+s/4,y,x+s/4,y+s,c,t);line(x+s*3/4,y,x+s*3/4,y+s,c,t);
        for (int i=1;i<4;++i) { rect(x,y+i*s/4,s/4,t,c);rect(x+s*3/4,y+i*s/4,s/4,t,c); }
    } else if (k==Kind::Other) {
        int left=x+s/6,right=x+s*5/6,fold=x+s*2/3;
        line(left,y,fold,y,c,t);line(fold,y,right,y+s/6,c,t);line(right,y+s/6,right,y+s,c,t);
        line(right,y+s,left,y+s,c,t);line(left,y+s,left,y,c,t);
        line(fold,y,fold,y+s/6,c,t);line(fold,y+s/6,right,y+s/6,c,t);
        for (int i=2;i<=4;++i) line(left+s/8,y+i*s/6,right-s/8,y+i*s/6,c,t);
    } else if (k==Kind::Music) {
        rect(x+s*2/3,y+s/8,t,s*2/3,c);rect(x+s/3,y+s/4,t,s*2/3,c);line(x+s/3,y+s/4,x+s*2/3,y+s/8,c,t*2);
        rect(x+s/7,y+s*3/4,s/4,s/6,c);rect(x+s/2,y+s*3/5,s/4,s/6,c);
    } else if (k==Kind::Photo) {
        y+=s/10;
        border(x,y,s,s*4/5,c,t);circle(x+s/4,y+s/4,s/12,c,t);line(x+s/8,y+s*3/5,x+s/2,y+s/3,c,t);line(x+s/2,y+s/3,x+s*7/8,y+s*3/5,c,t);
    } else if (k==Kind::Source) {
        border(x+s/4,y,s/2,s/3,c,t);rect(x+s/5,y+s/3,s*3/5,s*2/3,c);rect(x+s*2/5,y+s/10,t,s/9,background);rect(x+s*3/5,y+s/10,t,s/9,background);
    }
}
void Canvas::bitmap(const Bitmap& b,int x,int y,int w,int h,Aspect aspect,float zoom,int rotation) {
    if (!b.pixels || b.width<=0 || b.height<=0 || w<=0 || h<=0) return;
    rotation=(rotation%4+4)%4;int rw=rotation%2?b.height:b.width,rh=rotation%2?b.width:b.height;
    float clamped_zoom=std::max(0.25f,std::min(4.f,zoom));
    double scale=aspect==Aspect::Original?1.0:aspect==Aspect::Fill?std::max(double(w)/rw,double(h)/rh):std::min(double(w)/rw,double(h)/rh);
    scale*=clamped_zoom;int dw=std::max(1,static_cast<int>(rw*scale)),dh=std::max(1,static_cast<int>(rh*scale));
    int dx=x+(w-dw)/2,dy=y+(h-dh)/2;int left=std::max(0,std::max(x,dx)),top=std::max(0,std::max(y,dy));
    int right=std::min(screen_width,std::min(x+w,dx+dw)),bottom=std::min(screen_height,std::min(y+h,dy+dh));
    if (left>=right || top>=bottom) return;
    const uint8_t* src=b.pixels.get();
    if (rotation==0) {
        auto store=[&](uint32_t& dst,const uint8_t* p) {
            uint8_t a=p[3];
            if (a==255) dst=0xff000000u|(uint32_t(p[2])<<16)|(uint32_t(p[1])<<8)|p[0];
            else if (a) {
                uint32_t pixel=dst,inv=255-a;
                uint32_t r=((pixel&255)*inv+p[0]*a)/255,g=(((pixel>>8)&255)*inv+p[1]*a)/255,bb=(((pixel>>16)&255)*inv+p[2]*a)/255;
                dst=0xff000000u|(bb<<16)|(g<<8)|r;
            }
        };
        if (std::abs(scale-1.0)<1e-9 && dw==b.width && dh==b.height) {
            if (left==0 && top==0 && right==screen_width && bottom==screen_height && dx==0 && dy==0 && b.width==screen_width && b.height==screen_height) {
                std::memcpy(pixels_.data(),src,static_cast<size_t>(screen_width)*screen_height*4);
                return;
            }
            for (int py=top;py<bottom;++py) {
                int sy=py-dy;uint32_t* dst=pixels_.data()+static_cast<size_t>(py)*screen_width;
                const uint8_t* src_row=src+static_cast<size_t>(sy)*b.width*4;
                int row_left=left-dx,row_right=right-dx;
                if (row_left==0 && row_right==b.width) {
                    std::memcpy(dst+left,src_row,static_cast<size_t>(right-left)*4);
                } else {
                    for (int px=left;px<right;++px) store(dst[px],src_row+(px-dx)*4);
                }
            }
            return;
        }
        double inv_scale=1.0/scale;
        for (int py=top;py<bottom;++py) {
            int sy=std::min(b.height-1,std::max(0,static_cast<int>((py-dy)*inv_scale)));
            uint32_t* dst=pixels_.data()+static_cast<size_t>(py)*screen_width;
            const uint8_t* src_row=src+static_cast<size_t>(sy)*b.width*4;
            for (int px=left;px<right;++px) {
                int sx=std::min(b.width-1,std::max(0,static_cast<int>((px-dx)*inv_scale)));
                store(dst[px],src_row+sx*4);
            }
        }
        return;
    }
    for (int py=top;py<bottom;++py) for (int px=left;px<right;++px) {
        int tx=std::min(rw-1,static_cast<int>((px-dx)/scale)),ty=std::min(rh-1,static_cast<int>((py-dy)/scale));int sx,sy;
        if (rotation==1) { sx=ty;sy=b.height-1-tx; }else if (rotation==2) { sx=b.width-1-tx;sy=b.height-1-ty; }else { sx=b.width-1-ty;sy=tx; }
        auto* p=src+(static_cast<size_t>(sy)*b.width+sx)*4;blend(px,py,{p[0],p[1],p[2]},p[3]);
    }
}
void Canvas::button(Button b,int x,int y,const std::string& label,int size) {
    Color c=b==Cross?Color{70,150,255}:b==Circle?Color{255,70,91}:b==Triangle?Color{78,231,133}:Color{200,91,242};
    int face=std::max(24,std::min(40,size+3)),radius=face/2,stroke=face<32?2:3;
    circle(x+radius,y+radius,radius,c,2);
    if (b==Cross) { line(x+face/4,y+face/4,x+face*3/4,y+face*3/4,c,stroke);line(x+face*3/4,y+face/4,x+face/4,y+face*3/4,c,stroke); }
    if (b==Circle) circle(x+radius,y+radius,face*3/10,c,stroke);
    if (b==Triangle) { line(x+radius,y+face/5,x+face/5,y+face*3/4,c,stroke);line(x+face/5,y+face*3/4,x+face*4/5,y+face*3/4,c,stroke);line(x+face*4/5,y+face*3/4,x+radius,y+face/5,c,stroke); }
    if (b==Square) border(x+face/4,y+face/4,face/2+1,face/2+1,c,stroke);
    text_in_row(label,x+face+19,y,310,face,size,white,false);
}
void Canvas::tinted_bitmap(const Bitmap& bitmap,int x,int y,int w,int h,Color color) {
    if (!bitmap.pixels || bitmap.width<=0 || bitmap.height<=0 || w<=0 || h<=0) return;
    for (int row=0;row<h;++row) for (int col=0;col<w;++col) {
        int sx=col*bitmap.width/w,sy=row*bitmap.height/h;
        uint8_t alpha=bitmap.pixels.get()[(static_cast<size_t>(sy)*bitmap.width+sx)*4+3];
        blend(x+col,y+row,color,alpha);
    }
}
}
