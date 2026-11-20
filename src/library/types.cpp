#include "types.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace mc {
std::string extension(const std::string& name) {
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = name.substr(dot + 1);
    for (auto& c : ext) if (static_cast<unsigned char>(c) < 128) c = static_cast<char>(std::tolower(c));
    return ext;
}
Kind classify(const std::string& name) {
    auto e = extension(name);
    for (const char* video : {"avi", "mp4", "mkv", "mov", "webm", "m4v", "mpg", "mpeg", "ts", "m2ts", "mts"})
        if (e == video) return Kind::Video;
    if (e == "wav" || e == "mp3" || e == "flac" || e == "ogg" || e == "aac" || e == "m4a" || e == "opus"
        || e == "wma" || e == "aif" || e == "aiff") return Kind::Music;
    if (e == "jpg" || e == "jpeg" || e == "jpe" || e == "png" || e == "bmp" || e == "webp" || e == "gif"
        || e == "tga" || e == "psd" || e == "hdr" || e == "pic" || e == "pnm" || e == "ppm" || e == "pgm"
        || e == "tif" || e == "tiff" || e == "ico" || e == "heic" || e == "heif" || e == "avif" || e == "jxl") return Kind::Photo;
    return Kind::Other;
}
const char* kind_name(Kind k) {
    switch (k) {
        case Kind::Folder: return "Folder";
        case Kind::Video: return "Video";
        case Kind::Music: return "Music";
        case Kind::Photo: return "Photo";
        case Kind::Source: return "USB source";
        default: return "File";
    }
}
std::string format_size(uint64_t n) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(n); int unit = 0;
    while (value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }
    char out[64];
    std::snprintf(out, sizeof(out), unit ? "%.1f %s" : "%.0f %s", value, units[unit]);
    return out;
}
std::string format_time(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    uint64_t s = static_cast<uint64_t>(seconds); char out[64];
    std::snprintf(out, sizeof(out), "%02llu:%02llu:%02llu", static_cast<unsigned long long>(s / 3600),
                  static_cast<unsigned long long>((s / 60) % 60), static_cast<unsigned long long>(s % 60));
    return out;
}
std::string utf8_encode(uint32_t c) {
    std::string out;
    if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) c = 0xfffd;
    if (c < 128) out += static_cast<char>(c);
    else if (c < 2048) { out += static_cast<char>(0xc0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 63)); }
    else if (c < 65536) { out += static_cast<char>(0xe0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63)); }
    else { out += static_cast<char>(0xf0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 63)); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63)); }
    return out;
}
std::vector<uint32_t> utf8_decode(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 128) { out.push_back(c); ++i; continue; }
        unsigned count = (c >= 0xc2 && c <= 0xdf) ? 2 : (c >= 0xe0 && c <= 0xef) ? 3 : (c >= 0xf0 && c <= 0xf4) ? 4 : 0;
        if (!count || i + count > s.size()) { out.push_back(0xfffd); ++i; continue; }
        uint32_t cp = c & ((1u << (7 - count)) - 1); bool valid = true;
        for (unsigned j = 1; j < count; ++j) { unsigned char b = static_cast<unsigned char>(s[i + j]); if ((b & 0xc0) != 0x80) { valid = false; break; } cp = (cp << 6) | (b & 63); }
        uint32_t minimum = count == 2 ? 0x80 : count == 3 ? 0x800 : 0x10000;
        if (!valid || cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) { out.push_back(0xfffd); ++i; continue; }
        out.push_back(cp); i += count;
    }
    return out;
}
std::string search_key(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    // Directory sorting compares the same names many times. ASCII needs no
    // UTF-8 decoding vector or per-character encoding allocations.
    if (std::all_of(text.begin(), text.end(), [](unsigned char c) { return c < 128; })) {
        for (unsigned char c : text) out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        return out;
    }
    for (auto cp : utf8_decode(text)) {
        if ((cp >= 0x064b && cp <= 0x065f) || cp == 0x0640) continue;
        if (cp == 0x0622 || cp == 0x0623 || cp == 0x0625) cp = 0x0627;
        if (cp == 0x0649) cp = 0x064a;
        if (cp >= 'A' && cp <= 'Z') cp += 'a' - 'A';
        out += utf8_encode(cp);
    }
    return out;
}
bool name_matches(const std::string& name, const std::string& query) {
    return query.empty() || search_key(name).find(search_key(query)) != std::string::npos;
}
bool entry_before(const Entry& a, const Entry& b, Sort sort) {
    if (a.directory() != b.directory()) return a.directory();
    if (sort == Sort::Date && a.modified != b.modified) return a.modified > b.modified;
    if (sort == Sort::Size && a.size != b.size) return a.size > b.size;
    if (sort == Sort::Type && a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    auto ak = search_key(a.name), bk = search_key(b.name);
    if (ak != bk) return ak < bk;
    return a.relative < b.relative;
}
bool kind_in_tab(Kind k, int tab) {
    return (tab >= 0 && tab < 4 && k == Kind::Folder) || tab == 0 || (tab == 1 && k == Kind::Video)
        || (tab == 2 && k == Kind::Music) || (tab == 3 && k == Kind::Photo);
}
const char* tab_directory(int tab) {
    const char* roots[] = {"", "Videos", "Music", "Photos"};
    return tab >= 0 && tab < 4 ? roots[tab] : "";
}
}
