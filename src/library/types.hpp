#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mc {
enum class Kind { Folder, Video, Music, Photo, Other, Source };
enum class Sort { Name, Date, Size, Type };
struct Source {
    std::string root, label;
    uint64_t device = 0, inode = 0;
    uint64_t total_bytes = 0, available_bytes = 0;
    bool space_known = false, volume_label_known = false;
    std::string filesystem, mount_name;
};
struct Entry {
    std::string name, relative;
    Kind kind = Kind::Other;
    uint64_t size = 0;
    int64_t modified = 0;
    bool directory() const { return kind == Kind::Folder || kind == Kind::Source; }
};
struct Item {
    Source source;
    Entry entry;
    bool operator==(const Item& other) const {
        return source.root == other.source.root && entry.relative == other.entry.relative;
    }
};
Kind classify(const std::string& name);
const char* kind_name(Kind kind);
std::string extension(const std::string& name);
std::string format_size(uint64_t bytes);
std::string format_time(double seconds);
std::string utf8_encode(uint32_t cp);
std::vector<uint32_t> utf8_decode(const std::string& text);
std::string search_key(const std::string& text);
bool name_matches(const std::string& name, const std::string& query);
bool entry_before(const Entry& a, const Entry& b, Sort sort);
bool kind_in_tab(Kind kind, int tab);
const char* tab_directory(int tab);
}
