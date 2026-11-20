#include "browser.hpp"
#include <algorithm>

namespace mc {
namespace {
std::string category_status(DirectoryPageState state,int tab) {
    const std::string folder=tab_directory(tab);
    if (state==DirectoryPageState::Missing) return "The \""+folder+"\" folder does not exist on this USB.";
    if (state==DirectoryPageState::NotDirectory) return "A file named \""+folder+"\" exists, but it is not a folder.";
    if (state==DirectoryPageState::Empty) return "The \""+folder+"\" folder is empty.";
    if (state==DirectoryPageState::NoMatchingMedia) {
        const char* media=tab==1?"video":tab==2?"audio":"photo";
        return "The \""+folder+"\" folder contains no "+media+" files.";
    }
    return {};
}
}
const Entry* Browser::selected_entry() const { return location_.selected < entries.size() ? &entries[location_.selected] : nullptr; }
bool Browser::selected_item(Item& item) const { return item_at(location_.selected, item); }
bool Browser::item_at(size_t index, Item& item) const {
    if (index >= entries.size() || view == LibraryView::Sources) return false;
    if (view == LibraryView::Directory) item = {location_.request.source, entries[index]};
    else {
        if (index >= virtual_items_.size()) return false;
        item = virtual_items_[index];
        for (auto& s : sources) if (s.root == item.source.root) { item.source = s; break; }
    }
    return true;
}
void Browser::set_sources(std::vector<Source> list, std::vector<std::string> diagnostics) {
    sources = std::move(list); source_diagnostics = std::move(diagnostics);
    monitor_.watch(sources); if (view == LibraryView::Sources) show_sources();
}
void Browser::rescan(std::string base, std::vector<std::string> extras) {
    source_diagnostics.clear();
    worker_.rescan_sources(std::move(base), std::move(extras)); loading = true; status = "Reading storage sources...";
}
bool Browser::poll() {
    bool changed = false;
    for (const auto& root : monitor_.take_missing()) { disconnected(root); changed = true; }
    if (auto list = worker_.take_sources()) {
        loading = false; set_sources(std::move(list->sources), std::move(list->diagnostics)); changed = true;
    }
    if (auto page = worker_.take_page()) {
        if (page->generation != request_generation_) return changed;
        loading = false; changed = true;
        if (page->disconnected) { disconnected(); return true; }
        if (!page->source.root.empty()) {
            location_.request.source = page->source;
            for (auto& source : sources) if (source.root == page->source.root) source = page->source;
        }
        status = category_status(page->state,location_.request.tab);
        if (status.empty()) status=page->error;
        entries = std::move(page->entries); total = page->total; has_next = page->has_next;
        if (select_page_end_ && !entries.empty()) {
            location_.selected=entries.size()-1;location_.scroll=entries.size()>9?entries.size()-9:0;select_page_end_=false;
        }
        if (!restore_path_.empty()) {
            auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.relative == restore_path_; });
            if (it != entries.end()) location_.selected = static_cast<size_t>(it - entries.begin());
            restore_path_.clear();
        }
        if (entries.empty()) { location_.selected = location_.scroll = 0; if (status.empty()) status = query.empty() ? "No files in this folder" : "No matching files"; }
        else { location_.selected = std::min(location_.selected, entries.size() - 1); location_.scroll = std::min(location_.scroll, location_.selected); move(0); }
    }
    return changed;
}
std::vector<std::string> Browser::take_disconnected() {
    auto events = std::move(disconnected_events_); disconnected_events_.clear(); return events;
}
void Browser::load(bool reset) {
    if (view != LibraryView::Directory) return;
    if (reset) { location_.selected = location_.scroll = location_.page_index = 0; location_.request.has_after = location_.request.has_before = location_.request.from_end = false;select_page_end_=false; }
    location_.request.tab = tab; location_.request.sort = sort; location_.request.hidden = hidden; location_.request.query = query;
    request_generation_ = worker_.request(location_.request); loading = true; status = "Reading files...";
}
void Browser::show_sources() {
    worker_.cancel(); view = LibraryView::Sources; focus = Focus::List; history_.clear(); location_ = {};
    entries.clear(); virtual_items_.clear(); has_next = false; total = sources.size(); loading = false;
    for (const auto& s : sources) entries.push_back({s.label, s.root, Kind::Source, 0, 0});
    status = sources.empty() ? "No readable USB source. Choose Refresh or open USB diagnostics." : "Select a storage source";
}
void Browser::show_items(LibraryView type, std::vector<Item> items, std::string title) {
    worker_.cancel(); view = type; virtual_items_ = std::move(items); entries.clear();
    for (const auto& item : virtual_items_) entries.push_back(item.entry);
    location_.selected = location_.scroll = location_.page_index = 0; focus = Focus::List;
    total = entries.size(); has_next = loading = false; status = std::move(title);
}
void Browser::enter_source(size_t index) {
    if (index >= sources.size()) return;
    worker_.cancel(); history_.clear(); location_ = {}; location_.request.source = sources[index];
    location_.request.relative = tab_directory(tab);
    view = LibraryView::Directory; focus = Focus::List; query.clear(); entries.clear(); load(true);
}
bool Browser::enter_selected_folder() {
    if (loading || !selected_entry()) return false;
    if (view == LibraryView::Sources) { enter_source(location_.selected); return true; }
    Item item; if (!selected_item(item) || !item.entry.directory()) return false;
    if (view == LibraryView::Directory) {
        location_.selected_path = item.entry.relative;
        if (history_.size() >= 256) history_.erase(history_.begin());
        history_.push_back(location_);
    } else history_.clear();
    location_ = {}; location_.request.source = item.source; location_.request.relative = item.entry.relative;
    view = LibraryView::Directory; focus = Focus::List; query.clear(); entries.clear(); load(true); return true;
}
void Browser::back() {
    if (focus != Focus::List) { focus = Focus::List; return; }
    if (view != LibraryView::Directory || history_.empty()) { show_sources(); return; }
    location_ = history_.back(); history_.pop_back(); restore_path_ = location_.selected_path;
    query = location_.request.query; tab = location_.request.tab; sort = location_.request.sort;
    focus = Focus::List; load(false);
}
void Browser::move(int delta) {
    if (loading || entries.empty()) return;
    if (delta > 0 && location_.selected + 1 >= entries.size() && has_next && view == LibraryView::Directory) {
        location_.request.has_before = false; location_.request.has_after = true; location_.request.after = entries.back();
        ++location_.page_index; location_.selected = location_.scroll = 0; load(false); return;
    }
    if (delta < 0 && location_.selected == 0 && location_.page_index > 0 && view == LibraryView::Directory) {
        location_.request.has_after = false; location_.request.has_before = true; location_.request.before = entries.front();
        --location_.page_index; location_.selected = directory_page_size - 1; location_.scroll = directory_page_size - 9; load(false); return;
    }
    if (delta == 1 && location_.selected + 1 >= entries.size()) {
        if (view==LibraryView::Directory && location_.page_index>0) {
            location_.request.has_after=location_.request.has_before=location_.request.from_end=false;
            location_.page_index=location_.selected=location_.scroll=0;load(false);return;
        }
        location_.selected=location_.scroll=0;return;
    }
    if (delta == -1 && location_.selected == 0) {
        if (view==LibraryView::Directory && total>entries.size()) {
            location_.request.has_after=location_.request.has_before=false;location_.request.from_end=true;
            location_.page_index=static_cast<size_t>((total-1)/directory_page_size);select_page_end_=true;load(false);return;
        }
        location_.selected=entries.size()-1;location_.scroll=entries.size()>9?entries.size()-9:0;return;
    }
    auto index = static_cast<int64_t>(location_.selected) + delta;
    location_.selected = static_cast<size_t>(std::max<int64_t>(0, std::min<int64_t>(index, static_cast<int64_t>(entries.size()) - 1)));
    if (location_.selected < location_.scroll) location_.scroll = location_.selected;
    if (location_.selected >= location_.scroll + 9) location_.scroll = location_.selected - 8;
}
void Browser::change_tab(int delta) {
    tab = ((tab + delta) % 4 + 4) % 4;
    if (view == LibraryView::Directory) {
        // A category is rooted in its named source folder. Never filter the
        // previous tab's current folder or carry its paging/search cursors.
        history_.clear(); restore_path_.clear(); query.clear(); entries.clear();
        location_.request.relative = tab_directory(tab); load(true);
    }
    else if (view != LibraryView::Sources) show_sources();
}
void Browser::refresh() { if (view == LibraryView::Directory) { if (selected_entry()) restore_path_ = selected_entry()->relative; load(false); } }
void Browser::search(std::string text) { query = std::move(text); load(true); }
void Browser::set_sort(Sort value) { sort = value; load(true); }
void Browser::set_hidden(bool value) { hidden = value; load(true); }
void Browser::disconnected(const std::string& root) {
    std::string lost=root.empty()?location_.request.source.root:root;
    if (!lost.empty() && std::find(disconnected_events_.begin(), disconnected_events_.end(), lost) == disconnected_events_.end()) disconnected_events_.push_back(lost);
    sources.erase(std::remove_if(sources.begin(),sources.end(),[&](const Source& source){return source.root==lost;}),sources.end());
    monitor_.watch(sources);
    show_sources();status="USB disconnected. Reads stopped; choose Refresh to rescan.";
}
}
