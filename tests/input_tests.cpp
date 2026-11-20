#include "test.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sys/stat.h>
using namespace test;

TEST(button_edges_repeat_and_analog_triggers) {
    const Button buttons[]={Cross,Circle,Triangle,Square,L1,R1,L2,R2,Options,Up,Down,Left,Right};
    for(auto button:buttons) {InputFilter filter;RawInput raw;raw.buttons=button;auto e=filter.update(raw,0);CHECK(e.edge(button));e=filter.update(raw,.1);CHECK(!e.edge(button));filter.update({},.2);CHECK(filter.update(raw,.3).edge(button));}
    InputFilter filter;RawInput raw;raw.buttons=Cross;unsigned presses=0;
    for(unsigned i=0;i<1000;++i) if(filter.update(raw,i*.01).edge(Cross)) ++presses;CHECK(presses==1);
    filter.reset();raw.buttons=Down;CHECK(filter.update(raw,0).edge(Down));CHECK(!filter.update(raw,.379).repeated);CHECK(filter.update(raw,.38).has(Down));CHECK(!filter.update(raw,.39).repeated);CHECK(filter.update(raw,.501).has(Down));
    filter.reset();raw={};raw.rt=.2f;CHECK(!filter.update(raw,0).intentional);raw.rt=.7f;CHECK(filter.update(raw,.1).edge(R2));
}
TEST(osd_exact_seven_seconds_and_input_dead_zone) {
    Osd osd;InputFilter filter;osd.started(0);CHECK(osd.visible(0));CHECK(osd.visible(6.999999));CHECK(!osd.visible(7));CHECK(!osd.visible(15));
    RawInput drift;drift.lx=.1f;drift.ly=-.1f;drift.rx=.09f;drift.ry=.1f;
    for(unsigned i=0;i<1000;++i) {auto event=filter.update(drift,15+i*.01);CHECK(!event.intentional);osd.activity(15+i*.01,event.intentional);}
    CHECK(!osd.visible(26));RawInput stick;stick.rx=.65f;auto event=filter.update(stick,26);CHECK(event.intentional);osd.activity(26,event.intentional);CHECK(osd.visible(26));CHECK(osd.visible(32.999));CHECK(!osd.visible(33));
    RawInput press;press.buttons=Cross;event=filter.update(press,34);CHECK(event.edge(Cross));osd.activity(34,event.intentional);CHECK(osd.visible(34));
    osd.paused(35,true);CHECK(osd.visible(1000));osd.paused(1000,false);CHECK(osd.visible(1006.999));CHECK(!osd.visible(1007));
    osd.menu(1010,true);CHECK(osd.visible(2000));osd.menu(2000,false);CHECK(!osd.visible(2007));
}
TEST(rendering_rotation_aspect_and_arabic_font) {
    Canvas canvas;std::string error;CHECK(canvas.load_fonts(root+"/assets",error));CHECK(canvas.text_width("Media Center",44)>100);CHECK(canvas.text_width("فيلم عربي",44)>30);
    auto shaped=visual_text("فيلم عربي");CHECK(!shaped.empty());CHECK(std::any_of(shaped.begin(),shaped.end(),[](uint32_t cp){return cp>=0xfb50;}));
    Bitmap bitmap;bitmap.width=2;bitmap.height=1;auto* p=static_cast<uint8_t*>(decode_allocate(8));bitmap.pixels={p,decode_free};uint8_t pixels[8]={255,0,0,255,0,0,255,255};std::memcpy(p,pixels,8);
    canvas.clear({0,0,0});canvas.bitmap(bitmap,0,0,2,1,Aspect::Original);CHECK((canvas.data()[0]&0xffffff)==0x0000ff);CHECK((canvas.data()[1]&0xffffff)==0xff0000);
    canvas.clear({0,0,0});canvas.bitmap(bitmap,0,0,1,2,Aspect::Original,1,1);CHECK((canvas.data()[0]&0xffffff)==0x0000ff);CHECK((canvas.data()[screen_width]&0xffffff)==0xff0000);
    canvas.clear({0,0,0});canvas.bitmap(bitmap,0,0,10,10,Aspect::Fit);CHECK((canvas.data()[0]&0xffffff)==0);CHECK((canvas.data()[screen_width*2]&0xffffff)==0x0000ff);
}
TEST(section_tab_highlight_moves_smoothly_and_finishes_on_the_target) {
    Renderer renderer;std::string error;CHECK(renderer.initialize(root+"/assets",error));
    Browser browser;browser.set_sources({fixture_source()});browser.show_sources();
    ViewModel model;model.browser=&browser;model.screen=Screen::Browser;
    renderer.draw(model);auto before=renderer.revision();CHECK(!renderer.animating());
    browser.tab=1;renderer.draw(model);CHECK(renderer.animating());CHECK(renderer.revision()==before+1);
    std::this_thread::sleep_for(std::chrono::milliseconds(240));renderer.draw(model);
    CHECK(!renderer.animating());CHECK(renderer.revision()==before+2);
}
TEST(file_highlight_moves_smoothly_and_finishes_on_the_target) {
    Renderer renderer;std::string error;CHECK(renderer.initialize(root+"/assets",error));
    Browser browser;browser.set_sources({fixture_source(),fixture_source()});browser.show_sources();
    ViewModel model;model.browser=&browser;model.screen=Screen::Browser;
    renderer.draw(model);auto before=renderer.revision();CHECK(!renderer.animating());
    browser.move(1);renderer.draw(model);CHECK(renderer.animating());CHECK(renderer.revision()==before+1);
    std::this_thread::sleep_for(std::chrono::milliseconds(240));renderer.draw(model);
    CHECK(!renderer.animating());CHECK(renderer.revision()==before+2);
}
TEST(playlist_repeat_shuffle_and_previous) {
    auto source=fixture_source();std::vector<Item> items={item(source,"Music/Tone.wav"),item(source,"Music/Tone.mp3"),item(source,"Music/Tone.flac")};Playlist list;list.set(items,items[0]);Item out;
    CHECK(list.next(out,true,1,false));CHECK(out==items[0]);CHECK(list.next(out,true,0,false));CHECK(out==items[1]);CHECK(list.previous(out));CHECK(out==items[0]);
    CHECK(list.next(out,true,0,false));CHECK(list.next(out,true,0,false));CHECK(!list.next(out,true,0,false));CHECK(list.next(out,true,2,false));CHECK(out==items[0]);
    for(unsigned i=0;i<20;++i) {auto before=list.entries()[list.index()];CHECK(list.next(out,true,2,true));CHECK(!(out==before));}
}
TEST(english_interface_keeps_arabic_filename_input) {
    auto source=fixture_source();auto clock=std::make_shared<AudioClock>();std::string data=root+"/tests/generated/english-ui-state";mkdir(data.c_str(),0700);
    { std::ofstream state(data+"/state.tsv");state<<"setting\tlanguage\t1\n"; }
    TestPlatform platform(clock,data);Application app(platform,{source},"/nonexistent",{},false);app.initialize();
    double now=monotonic_now();
    auto press=[&](Button button) { RawInput raw;raw.buttons=button;app.tick(raw,now+=.1);app.tick({},now+=.02); };
    press(Options);for (int i=0;i<5;++i) press(Down);press(Cross);
    auto view=app.view(now);CHECK(view.modal==Modal::Settings);CHECK(view.menu_title=="Settings");CHECK(view.menu_items.size()==9);
    CHECK(view.menu_items.front().find("Volume:")==0);CHECK(std::none_of(view.menu_items.begin(),view.menu_items.end(),[](const std::string& label){return label.find("Language")!=std::string::npos;}));
    press(Circle);app.browser().enter_source(0);wait([&]{app.tick({},now+=.02);return !app.browser().loading;});
    app.browser().search("ملفات");wait([&]{app.tick({},now+=.02);return !app.browser().loading;});
    CHECK(app.browser().entries.size()==1);CHECK(app.browser().entries[0].name=="ملفات عربية");
    press(Options);press(Down);press(Cross);press(Triangle);
    view=app.view(now);CHECK(view.modal==Modal::Search);CHECK(view.menu_title=="Options");
    CHECK(std::find(view.keyboard_keys.begin(),view.keyboard_keys.end(),"ا")!=view.keyboard_keys.end());
    CHECK(std::find(view.keyboard_keys.begin(),view.keyboard_keys.end(),"Apply")!=view.keyboard_keys.end());
}
