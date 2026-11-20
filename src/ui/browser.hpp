#pragma once
#include "input/input.hpp"
#include "storage/directory.hpp"

namespace mc {
enum class Focus { List, Tabs, Details };
enum class LibraryView { Sources, Directory, Favorites, LastPlayed, Queue, Index };
struct BrowserLocation {
    PageRequest request;
    size_t selected = 0, scroll = 0, page_index = 0;
    std::string selected_path;
};
class Browser {
    DirectoryWorker worker_;
    SourceMonitor monitor_;
    std::vector<BrowserLocation> history_;
    BrowserLocation location_{};
    std::vector<Item> virtual_items_;
    std::string restore_path_;
    std::vector<std::string> disconnected_events_;
    bool select_page_end_ = false;
    uint64_t request_generation_ = 0;
    void load(bool reset);
public:
    std::vector<Source> sources;
    std::vector<std::string> source_diagnostics;
    std::vector<Entry> entries;
    LibraryView view = LibraryView::Sources;
    Focus focus = Focus::List;
    bool loading = false, details = true, hidden = false, has_next = false;
    uint64_t total = 0;
    int tab = 0;
    Sort sort = Sort::Name;
    std::string status, query;
    size_t selected() const { return location_.selected; }
    size_t scroll() const { return location_.scroll; }
    size_t page_index() const { return location_.page_index; }
    const std::string& directory() const { return location_.request.relative; }
    const Source& source() const { return location_.request.source; }
    const Entry* selected_entry() const;
    bool selected_item(Item&) const;
    bool item_at(size_t index, Item&) const;
    void set_sources(std::vector<Source> list, std::vector<std::string> diagnostics = {});
    void rescan(std::string mount_base, std::vector<std::string> extras);
    bool poll();
    std::vector<std::string> take_disconnected();
    void show_sources();
    void show_items(LibraryView, std::vector<Item> items, std::string title);
    void enter_source(size_t index);
    bool enter_selected_folder();
    void back();
    void move(int delta);
    void change_tab(int delta);
    void refresh();
    void search(std::string text);
    void set_sort(Sort value);
    void set_hidden(bool value);
    void disconnected(const std::string& root = {});
};
}
