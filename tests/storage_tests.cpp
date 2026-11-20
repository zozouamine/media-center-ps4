#include "test.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <sys/mount.h>
#else
#include <sys/statfs.h>
#endif
#include <unistd.h>
using namespace test;

TEST(sandbox_usb_source_is_readable_when_global_mount_namespace_is_denied) {
    std::string pattern=root+"/tests/generated/usb-namespace-XXXXXX";
    char* created=mkdtemp(&pattern[0]);CHECK(created);
    struct Cleanup {
        std::string base;
        ~Cleanup() {
            chmod((base+"/mnt").c_str(),0700);
            unlink((base+"/usb0/ملف عربي.wav").c_str());
            rmdir((base+"/usb0").c_str());rmdir((base+"/mnt").c_str());rmdir(base.c_str());
        }
    } cleanup{created};
    std::string mounted=cleanup.base+"/usb0",global=cleanup.base+"/mnt";
    CHECK(mkdir(mounted.c_str(),0700)==0);CHECK(mkdir(global.c_str(),0111)==0);
    { std::ofstream file(mounted+"/ملف عربي.wav");file<<"fixture"; }
    std::vector<std::string> diagnostics{"old scan"};
    auto sources=discover_sources(global,{mounted,mounted},&diagnostics);
    CHECK(sources.size()==1);CHECK(sources.front().root==mounted);
    CHECK(std::count_if(diagnostics.begin(),diagnostics.end(),[&](const std::string& line) {
        return line.find(mounted+" | Readable")==0;
    })==1);
    CHECK(std::any_of(diagnostics.begin(),diagnostics.end(),[&](const std::string& line) {
        return line.find(global+" | opendir: errno="+std::to_string(EACCES))==0;
    }));
    CHECK(std::none_of(diagnostics.begin(),diagnostics.end(),[](const std::string& line){return line=="old scan";}));
    ReadOnlyFile file;std::string error;CHECK(file.open(sources.front(),"ملف عربي.wav",error));
    char data[7]{};CHECK(file.read_at(0,data,sizeof(data)));CHECK(std::string(data,sizeof(data))=="fixture");
    int fd=file.duplicate_fd();CHECK(fd>=0);CHECK((fcntl(fd,F_GETFL)&O_ACCMODE)==O_RDONLY);close(fd);
}

TEST(source_probe_errors_preserve_stage_path_and_errno) {
    auto source=fixture_source();std::vector<std::string> diagnostics;
    auto sources=discover_sources(source.root+"/not-a-mount-base",{source.root+"/Music/Tone.wav"},&diagnostics);
    CHECK(sources.empty());
    CHECK(std::any_of(diagnostics.begin(),diagnostics.end(),[&](const std::string& line) {
        return line.find(source.root+"/Music/Tone.wav | open: errno="+std::to_string(ENOTDIR))==0;
    }));
    Source rejected;std::string error;CHECK(!source_from_path(source.root+"/does-not-exist",rejected,error));
    CHECK(error.find("errno="+std::to_string(ENOENT))!=std::string::npos);
}

TEST(empty_source_refresh_discovers_new_sandbox_mount_and_replaces_diagnostics) {
    std::string pattern=root+"/tests/generated/usb-refresh-XXXXXX";
    char* created=mkdtemp(&pattern[0]);CHECK(created);
    struct Cleanup {
        std::string base;
        ~Cleanup() { rmdir((base+"/usb0").c_str());rmdir((base+"/mnt").c_str());rmdir(base.c_str()); }
    } cleanup{created};
    CHECK(mkdir((cleanup.base+"/mnt").c_str(),0700)==0);
    Browser browser;std::string mount=cleanup.base+"/usb0";
    auto scan=[&] { browser.rescan(cleanup.base+"/mnt",{mount});wait([&]{browser.poll();return !browser.loading;}); };
    scan();CHECK(browser.sources.empty());CHECK(!browser.source_diagnostics.empty());
    CHECK(mkdir(mount.c_str(),0700)==0);scan();
    CHECK(browser.sources.size()==1);CHECK(browser.sources.front().root==mount);
    CHECK(std::any_of(browser.source_diagnostics.begin(),browser.source_diagnostics.end(),[&](const std::string& line) {
        return line.find(mount+" | Readable")==0;
    }));
    CHECK(std::none_of(browser.source_diagnostics.begin(),browser.source_diagnostics.end(),[&](const std::string& line) {
        return line.find(mount+" | Cannot resolve")==0;
    }));
    browser.enter_source(0);wait([&]{browser.poll();return !browser.loading;});
    CHECK(browser.view==LibraryView::Directory);CHECK(browser.status=="No files in this folder");
}

TEST(usb_diagnostics_remain_accessible_without_a_source_and_scroll_to_last_probe) {
    std::vector<std::string> diagnostics;
    for (unsigned i=0;i<16;++i) diagnostics.push_back("Probe "+std::to_string(i)+" | open: errno=2");
    auto clock=std::make_shared<AudioClock>();TestPlatform platform(clock,root+"/tests/generated/usb-diagnostics-state");
    Application app(platform,{},root+"/tests/generated/no-mount-base",{},false,diagnostics);app.initialize();
    RawInput input;input.connected=true;double now=1;
    auto tick=[&](uint32_t buttons) { input.buttons=buttons;now+=0.2;app.tick(input,now); };
    tick(0);tick(Square);auto view=app.view(now);
    CHECK(view.modal==Modal::Details);CHECK(view.menu_title=="USB diagnostics");CHECK(view.information.size()==diagnostics.size()+1);
    for (unsigned i=0;i<25;++i) tick(Down);
    view=app.view(now);CHECK(view.information_scroll+information_page_rows==view.information.size());
    CHECK(view.information.back()==diagnostics.back());
    for (unsigned i=0;i<25;++i) tick(Up);
    CHECK(app.view(now).information_scroll==0);tick(Circle);CHECK(app.view(now).modal==Modal::None);
    tick(0);tick(Square);CHECK(app.view(now).information_scroll==0);
}

TEST(usb_discovery_probes_children_when_mount_listing_is_denied) {
    std::string pattern=root+"/tests/generated/mount-probes-XXXXXX";
    char* created=mkdtemp(&pattern[0]);CHECK(created);
    struct Cleanup {
        std::string base;
        ~Cleanup() { chmod(base.c_str(),0700);rmdir((base+"/usb0").c_str());rmdir((base+"/usb7").c_str());rmdir(base.c_str()); }
    } cleanup{created};
    CHECK(mkdir((cleanup.base+"/usb0").c_str(),0700)==0);CHECK(mkdir((cleanup.base+"/usb7").c_str(),0700)==0);
    CHECK(chmod(cleanup.base.c_str(),0111)==0);
    errno=0;DIR* denied=opendir(cleanup.base.c_str());bool unreadable=!denied && errno==EACCES;
    if (denied) closedir(denied);CHECK(unreadable);
    auto sources=discover_sources(cleanup.base,{});CHECK(sources.size()==2);
    CHECK(sources[0].root==cleanup.base+"/usb0");CHECK(sources[1].root==cleanup.base+"/usb7");
    CHECK(source_present(sources[0]));CHECK(source_present(sources[1]));
    auto deduplicated=discover_sources(cleanup.base,{cleanup.base+"/usb0"});CHECK(deduplicated.size()==2);
}

TEST(unicode_search_and_validation) {
    CHECK(name_matches("فيلم عربي.mp4","عربي"));CHECK(name_matches("إِختبار.mkv","اختبار"));CHECK(name_matches("Sample Video.avi","VIDEO"));
    CHECK(!name_matches("عربي","other"));CHECK(utf8_decode("عربي").size()==4);CHECK(utf8_decode(std::string("\xc0\xaf",2)).size()==2);
    CHECK(!valid_relative("../outside"));CHECK(!valid_relative("Movies/../../outside"));CHECK(!valid_relative("/etc/passwd"));
    CHECK(!valid_relative("a//b"));CHECK(!valid_relative(std::string("a\0b",3)));CHECK(valid_relative("ملفات عربية/ملف بمسافات.txt"));
}
TEST(read_only_flags_and_source_boundary) {
    auto source=fixture_source();ReadOnlyFile file;std::string error;
    CHECK(file.open(source,"Music/Tone.wav",error));int fd=file.duplicate_fd();CHECK(fd>=0);CHECK((fcntl(fd,F_GETFL)&O_ACCMODE)==O_RDONLY);
    char byte='x';errno=0;CHECK(::write(fd,&byte,1)==-1);CHECK(errno==EBADF);::close(fd);
    CHECK(!file.open(source,"escape-link.txt",error));CHECK(!file.open(source,"escape-dir/outside.txt",error));CHECK(!file.open(source,"../outside.txt",error));
    CHECK(!file.open(source,"Movies/../../outside.txt",error));Source wrong=source;++wrong.inode;CHECK(!file.open(wrong,"Music/Tone.wav",error));
    auto long_path=bytes(root+"/tests/generated/long-relative.txt");std::string relative(long_path.begin(),long_path.end());CHECK(relative.size()>1024);
    CHECK(file.open(source,relative,error));char text[12]{};CHECK(file.read_at(0,text,12));CHECK(std::string(text,12)=="long-path-ok");
    CHECK(!file.seek(-1,SEEK_SET));CHECK(!file.seek(1,SEEK_END));CHECK(file.seek(0,SEEK_SET));CHECK(file.read(text,3)==3);CHECK(file.tell()==3);
}
TEST(keyset_pagination_large_directory_and_sorting) {
    auto source=fixture_source();std::atomic<bool> cancel{false};PageRequest request;request.source=source;request.relative="Large";
    auto page=scan_page(request,cancel);CHECK(page.error.empty());CHECK(page.total==4205);CHECK(page.entries.size()==128);CHECK(page.entries.front().name=="item_00000.wav");CHECK(page.has_next);
    std::string previous;size_t seen=0;
    for(unsigned i=0;i<40;++i) {
        for(const auto& entry:page.entries) { CHECK(previous.empty() || entry.name>previous);previous=entry.name;++seen; }
        if(!page.has_next) break;request.has_after=true;request.after=page.entries.back();page=scan_page(request,cancel);CHECK(page.entries.size()<=128);
    }
    CHECK(seen==4205);CHECK(previous=="item_04204.wav");
    request={};request.source=source;request.relative="Large";request.has_before=true;request.before={"item_00128.wav","Large/item_00128.wav",Kind::Music,0,0};
    page=scan_page(request,cancel);CHECK(page.entries.size()==128);CHECK(page.entries.front().name=="item_00000.wav");CHECK(page.entries.back().name=="item_00127.wav");
    request={};request.source=source;request.query="عربية";page=scan_page(request,cancel);CHECK(page.total==1);CHECK(page.entries[0].name=="ملفات عربية");
    request={};request.source=source;request.hidden=true;page=scan_page(request,cancel);CHECK(std::any_of(page.entries.begin(),page.entries.end(),[](const Entry& e){return e.name==".hidden.txt";}));
    CHECK(std::none_of(page.entries.begin(),page.entries.end(),[](const Entry& e){return e.name=="escape-link.txt"||e.name=="escape-dir";}));
    request.sort=Sort::Size;page=scan_page(request,cancel);bool file=false;uint64_t size=UINT64_MAX;
    for(const auto& entry:page.entries) {if(entry.directory()) CHECK(!file);else {file=true;CHECK(entry.size<=size);size=entry.size;}}
    cancel=true;page=scan_page(request,cancel);CHECK(page.cancelled);
}
TEST(browser_parent_focus_and_page_round_trip) {
    Browser browser;browser.set_sources({fixture_source()});browser.enter_source(0);
    wait([&]{browser.poll();return !browser.loading;});auto it=std::find_if(browser.entries.begin(),browser.entries.end(),[](const Entry& e){return e.name=="Movies";});CHECK(it!=browser.entries.end());
    browser.move(static_cast<int>(it-browser.entries.begin()));CHECK(browser.enter_selected_folder());wait([&]{browser.poll();return !browser.loading;});CHECK(browser.directory()=="Movies");
    browser.focus=Focus::Details;browser.back();CHECK(browser.focus==Focus::List);CHECK(browser.directory()=="Movies");browser.back();wait([&]{browser.poll();return !browser.loading;});
    CHECK(browser.directory().empty());CHECK(browser.selected_entry()->name=="Movies");CHECK(browser.focus==Focus::List);
    it=std::find_if(browser.entries.begin(),browser.entries.end(),[](const Entry& e){return e.name=="Large";});browser.move(static_cast<int>(it-browser.entries.begin())-static_cast<int>(browser.selected()));CHECK(browser.enter_selected_folder());
    wait([&]{browser.poll();return !browser.loading;});browser.move(127);browser.move(1);wait([&]{browser.poll();return !browser.loading;});CHECK(browser.page_index()==1);CHECK(browser.selected_entry()->name=="item_00128.wav");
    browser.move(-1);wait([&]{browser.poll();return !browser.loading;});CHECK(browser.page_index()==0);CHECK(browser.selected_entry()->name=="item_00127.wav");
    browser.move(-127);CHECK(browser.selected()==0);browser.move(-1);wait([&]{browser.poll();return !browser.loading;});
    CHECK(browser.page_index()==32);CHECK(browser.selected_entry()->name=="item_04204.wav");
    browser.move(1);wait([&]{browser.poll();return !browser.loading;});CHECK(browser.page_index()==0);CHECK(browser.selected_entry()->name=="item_00000.wav");
    browser.change_tab(1);wait([&]{browser.poll();return !browser.loading;});CHECK(browser.directory()=="Videos");CHECK(!browser.entries.empty());CHECK(browser.focus==Focus::List);
}
TEST(browser_selection_wraps_in_both_directions) {
    Browser browser;browser.set_sources({fixture_source(),fixture_source()});browser.show_sources();
    CHECK(browser.selected()==0);browser.move(-1);CHECK(browser.selected()==1);browser.move(1);CHECK(browser.selected()==0);
}
TEST(category_folders_isolate_types_reset_search_and_restore_focus) {
    Browser browser;browser.set_sources({fixture_source()});browser.enter_source(0);
    auto loaded=[&]{wait([&]{browser.poll();return !browser.loading;});};loaded();
    CHECK(std::any_of(browser.entries.begin(),browser.entries.end(),[](const Entry& e){return e.name=="Readme.txt" && e.kind==Kind::Other;}));
    browser.search("Readme");loaded();CHECK(browser.entries.size()==1);
    browser.change_tab(1);loaded();CHECK(browser.query.empty());CHECK(browser.directory()=="Videos");CHECK(browser.page_index()==0);
    CHECK(browser.entries.size()==2);
    for (const auto& e:browser.entries) {CHECK(e.directory() || e.kind==Kind::Video);CHECK(e.relative.find("Videos/")==0);}
    CHECK(browser.selected_entry()->name=="Nested");CHECK(browser.enter_selected_folder());loaded();
    CHECK(browser.directory()=="Videos/Nested");CHECK(browser.entries.size()==1);CHECK(browser.entries[0].name=="VideoOnly.avi");
    browser.back();loaded();CHECK(browser.directory()=="Videos");CHECK(browser.selected_entry()->name=="Nested");
    browser.change_tab(1);loaded();CHECK(browser.directory()=="Music");
    for (const auto& e:browser.entries) {CHECK(e.directory() || e.kind==Kind::Music);CHECK(e.relative.find("Music/")==0);}
    browser.change_tab(1);loaded();CHECK(browser.directory()=="Photos");
    for (const auto& e:browser.entries) {CHECK(e.directory() || e.kind==Kind::Photo);CHECK(e.relative.find("Photos/")==0);}
    browser.change_tab(1);loaded();CHECK(browser.tab==0);CHECK(browser.directory().empty());
    browser.change_tab(-1);loaded();CHECK(browser.tab==3);CHECK(browser.directory()=="Photos");
    browser.back();CHECK(browser.view==LibraryView::Sources);browser.enter_source(0);loaded();CHECK(browser.directory()=="Photos");
    std::string missing=root+"/tests/generated/missing-category";mkdir(missing.c_str(),0700);
    Source source;std::string error;CHECK(source_from_path(missing,source,error));
    browser.set_sources({source});browser.show_sources();browser.change_tab(-2);browser.enter_source(0);loaded();
    CHECK(browser.tab==1);CHECK(browser.entries.empty());CHECK(!browser.status.empty());CHECK(browser.view==LibraryView::Directory);
    browser.change_tab(-1);loaded();CHECK(browser.directory().empty());CHECK(browser.focus==Focus::List);
}
TEST(category_folders_report_missing_empty_and_wrong_media_without_errno) {
    std::string pattern=root+"/tests/generated/category-status-XXXXXX";
    char* created=mkdtemp(&pattern[0]);CHECK(created);
    struct Cleanup {
        std::string base;
        ~Cleanup() {
            unlink((base+"/Music/track.mp3").c_str());unlink((base+"/Music/readme.txt").c_str());
            rmdir((base+"/Videos").c_str());rmdir((base+"/Music").c_str());rmdir((base+"/Photos").c_str());rmdir(base.c_str());
        }
    } cleanup{created};
    Source source;std::string error;CHECK(source_from_path(cleanup.base,source,error));
    auto load=[&](Browser& browser) { wait([&]{browser.poll();return !browser.loading;}); };
    Browser browser;browser.set_sources({source});
    const char* folders[]={"Videos","Music","Photos"};
    for (int tab=1;tab<4;++tab) {
        browser.show_sources();browser.tab=tab;browser.enter_source(0);load(browser);
        CHECK(browser.entries.empty());CHECK(browser.status==std::string("The \"")+folders[tab-1]+"\" folder does not exist on this USB.");
        CHECK(browser.status.find("errno")==std::string::npos);
        CHECK(mkdir((cleanup.base+"/"+folders[tab-1]).c_str(),0700)==0);
        browser.refresh();load(browser);CHECK(browser.status==std::string("The \"")+folders[tab-1]+"\" folder is empty.");
    }
    {std::ofstream file(cleanup.base+"/Music/readme.txt");file<<"not audio";}
    browser.show_sources();browser.tab=2;browser.enter_source(0);load(browser);
    CHECK(browser.entries.empty());CHECK(browser.status=="The \"Music\" folder contains no audio files.");
    {std::ofstream file(cleanup.base+"/Music/track.mp3");file<<"fixture";}
    browser.refresh();load(browser);CHECK(browser.status.empty());CHECK(browser.entries.size()==1);
    CHECK(browser.entries[0].name=="track.mp3");CHECK(browser.entries[0].kind==Kind::Music);
}
TEST(storage_name_and_available_space_come_from_open_source) {
    auto source=fixture_source();CHECK(source.space_known);CHECK(source.total_bytes>0);CHECK(source.available_bytes<=source.total_bytes);CHECK(!source.label.empty());
    int descriptor=open(source.root.c_str(),O_RDONLY|O_DIRECTORY);CHECK(descriptor>=0);
    struct statfs actual{};CHECK(fstatfs(descriptor,&actual)==0);
    CHECK(source.total_bytes==uint64_t(actual.f_bsize)*uint64_t(actual.f_blocks));
    Source refreshed=source;refreshed.available_bytes=UINT64_MAX;CHECK(refresh_source_metadata(descriptor,refreshed));
    CHECK(refreshed.available_bytes<=refreshed.total_bytes);
    uint64_t expected=uint64_t(actual.f_bsize)*uint64_t(std::max<decltype(actual.f_bavail)>(0,actual.f_bavail));
    auto difference=refreshed.available_bytes>expected?refreshed.available_bytes-expected:expected-refreshed.available_bytes;
    CHECK(difference<64u*1024u*1024u);close(descriptor);
#if defined(__APPLE__)
    CHECK(source.volume_label_known);CHECK(!source.filesystem.empty());CHECK(!source.mount_name.empty());
#endif
    PageRequest request;request.source=source;std::atomic<bool> cancel{false};auto page=scan_page(request,cancel);
    CHECK(page.source.root==source.root);CHECK(page.source.label==source.label);CHECK(page.source.space_known);
    refreshed.space_known=true;CHECK(!refresh_source_metadata(-1,refreshed));CHECK(!refreshed.space_known);CHECK(refreshed.available_bytes==0);
}
TEST(indexing_cancellation_and_memory_bound) {
    Indexer index;index.start(fixture_source(),"Large",false);wait([&]{return index.snapshot(false).scanned>0 || !index.snapshot(false).running;});index.cancel();wait([&]{return !index.snapshot(false).running;});
    auto partial=index.snapshot();CHECK(partial.cancelled || partial.scanned==4205);CHECK(partial.entries.size()<=4096);
    index.start(fixture_source(),"Large",false);wait([&]{return !index.snapshot(false).running;});auto complete=index.snapshot();CHECK(complete.scanned==4205);CHECK(complete.entries.size()==4096);CHECK(complete.truncated);
}
TEST(persistence_round_trip_and_owned_data_clear) {
    auto source=fixture_source();auto media=item(source,"Music/Tone.wav");std::string data=root+"/tests/generated/state-store";
    SavedState saved;saved.settings.volume=35;toggle_favorite(saved,media);record_played(saved,media);enqueue(saved.music_queue,media);
    {StateStore store(data,{source});store.save(saved);} // Destructor flushes the final queued transaction.
    {StateStore store(data,{source});auto read=store.read();CHECK(read.settings.volume==35);CHECK(read.favorites.size()==1);CHECK(read.favorites[0]==media);CHECK(read.last_played.size()==1);CHECK(read.music_queue.size()==1);
        std::ofstream keep(data+"/keep.txt");keep<<"owned by another feature";keep.close();store.clear_owned_data();}
    CHECK(access((data+"/state.tsv").c_str(),F_OK)!=0);CHECK(access((data+"/keep.txt").c_str(),F_OK)==0);
    std::string forbidden=source.root+"/should-never-be-created";
    {StateStore store(forbidden,{source});store.save(saved);CHECK(!store.error().empty());}
    CHECK(access(forbidden.c_str(),F_OK)!=0);
    std::string invalid=root+"/tests/generated/private-data-must-not-exist";
    {StateStore store(invalid+"/../unsafe",{source});store.save(saved);CHECK(!store.error().empty());}
    CHECK(access(invalid.c_str(),F_OK)!=0);
}
TEST(simulated_disconnect_returns_to_sources) {
    auto base=root+"/tests/generated/detach-browser",offline=base+"-offline";mkdir(base.c_str(),0700);std::ofstream f(base+"/file.txt");f<<"fixture";f.close();
    Source source;std::string error;CHECK(source_from_path(base,source,error));Browser browser;browser.set_sources({source});browser.enter_source(0);wait([&]{browser.poll();return !browser.loading;});
    CHECK(rename(base.c_str(),offline.c_str())==0);CHECK(!source_present(source));wait([&]{browser.poll();return browser.view==LibraryView::Sources;},3);CHECK(browser.status.find("disconnected")!=std::string::npos);CHECK(browser.focus==Focus::List);
    CHECK(rename(offline.c_str(),base.c_str())==0);
}
