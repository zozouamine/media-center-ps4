#include "metadata.hpp"
#include <algorithm>
#include <cstring>

namespace mc {
namespace {
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t le32(const uint8_t* p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]; }
uint32_t syncsafe(const uint8_t* p) { return uint32_t(p[0] & 127) << 21 | uint32_t(p[1] & 127) << 14 | uint32_t(p[2] & 127) << 7 | (p[3] & 127); }
bool is_syncsafe(const uint8_t* p) { return !((p[0] | p[1] | p[2] | p[3]) & 128); }
std::vector<uint8_t> remove_unsynchronization(const uint8_t* p, size_t bytes) {
    std::vector<uint8_t> out; out.reserve(bytes);
    for (size_t i = 0; i < bytes; ++i) { out.push_back(p[i]); if (p[i] == 255 && i + 1 < bytes && p[i+1] == 0) ++i; }
    return out;
}
std::string text_frame(const uint8_t* p, size_t bytes) {
    if (bytes < 2) return {};
    unsigned encoding = *p++; --bytes; bytes = std::min<size_t>(bytes, 4096);
    std::string result;
    if (encoding == 3) result.assign(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), bytes));
    else if (encoding == 0) for (size_t i = 0; i < bytes && p[i]; ++i) result += utf8_encode(p[i]);
    else if (encoding == 1 || encoding == 2) {
        bool little = false; size_t i = 0;
        if (bytes >= 2 && p[0] == 255 && p[1] == 254) { little = true; i = 2; }
        else if (bytes >= 2 && p[0] == 254 && p[1] == 255) i = 2;
        auto unit = [&](size_t at) { return little ? uint32_t(p[at]) | uint32_t(p[at+1]) << 8 : uint32_t(p[at]) << 8 | p[at+1]; };
        while (i + 1 < bytes) {
            uint32_t cp = unit(i); i += 2; if (!cp) break;
            if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < bytes) {
                auto low = unit(i);
                if (low >= 0xdc00 && low <= 0xdfff) { cp = 0x10000 + ((cp-0xd800) << 10) + low-0xdc00; i += 2; }
            }
            result += utf8_encode(cp);
        }
    }
    return result.substr(0, 4096);
}
void artwork(const uint8_t* p, size_t bytes, uint32_t type, AudioInfo& info, const std::atomic<bool>& cancel) {
    if (cancel || !bytes || bytes > artwork_file_limit || (info.artwork && (type != 3 || info.artwork_type == 3))) return;
    std::string ignored;
    auto bitmap = decode_bitmap(p, bytes, 4u * 1024u * 1024u, ignored);
    if (bitmap && !cancel) {
        bitmap = resize_bitmap(bitmap, 640);
        if (bitmap) { info.artwork = std::move(bitmap); info.artwork_type = type; }
    }
}
void apic(const uint8_t* p, size_t bytes, bool version2, AudioInfo& info, const std::atomic<bool>& cancel) {
    if (bytes < 5 || p[0] > 3) return;
    unsigned encoding = p[0]; size_t cursor = 1;
    if (version2) cursor += 3;
    else {
        size_t length = strnlen(reinterpret_cast<const char*>(p+cursor), bytes-cursor);
        if (!length || length > 255 || length >= bytes-cursor || (length == 3 && !std::memcmp(p+cursor, "-->", 3))) return;
        cursor += length + 1;
    }
    if (cursor >= bytes) return;
    uint32_t type = p[cursor++]; bool terminated = false;
    if (encoding == 1 || encoding == 2) {
        while (cursor + 1 < bytes) {
            if (!p[cursor] && !p[cursor+1]) { cursor += 2; terminated = true; break; }
            cursor += 2;
        }
    } else {
        size_t length = strnlen(reinterpret_cast<const char*>(p+cursor), bytes-cursor);
        if (length < bytes-cursor) { cursor += length + 1; terminated = true; }
    }
    if (terminated && cursor < bytes) artwork(p+cursor, bytes-cursor, type, info, cancel);
}
void id3_tag(const uint8_t* data, size_t bytes, AudioInfo& info, const std::atomic<bool>& cancel) {
    if (bytes < 10 || std::memcmp(data, "ID3", 3) || data[3] < 2 || data[3] > 4 || !is_syncsafe(data+6)) return;
    unsigned version = data[3]; size_t length = syncsafe(data+6);
    if (length > audio_tag_limit || length > bytes-10 || (version == 2 && (data[5] & 64))) return;
    const uint8_t* tag = data+10; std::vector<uint8_t> unsynchronized;
    if (version < 4 && (data[5] & 128)) { unsynchronized = remove_unsynchronization(tag, length); tag = unsynchronized.data(); length = unsynchronized.size(); }
    size_t cursor = 0;
    if (version >= 3 && (data[5] & 64)) {
        if (length < 4) return;
        uint64_t extended = version == 4 ? syncsafe(tag) : uint64_t(be32(tag)) + 4;
        if (extended < 6 || extended > length) return;
        cursor = static_cast<size_t>(extended);
    }
    size_t header = version == 2 ? 6 : 10; unsigned frames = 0;
    while (!cancel && length-cursor >= header && frames++ < 512) {
        const uint8_t* frame = tag+cursor; if (!frame[0]) break;
        size_t size = version == 2 ? (uint32_t(frame[3]) << 16 | uint32_t(frame[4]) << 8 | frame[5]) : version == 4 ? syncsafe(frame+4) : be32(frame+4);
        if (!size || size > length-cursor-header || (version == 4 && !is_syncsafe(frame+4))) break;
        const uint8_t* body = frame+header; size_t body_size = size;
        uint8_t flags = version == 2 ? 0 : frame[9]; cursor += header+size;
        if ((version == 3 && (flags & 0xc0)) || (version == 4 && (flags & 0x0c))) continue;
        size_t prefix = (version == 3 && (flags & 0x20)) || (version == 4 && (flags & 0x40)) ? 1 : 0;
        if (version == 4 && (flags & 1)) prefix += 4;
        if (prefix >= body_size) continue; body += prefix; body_size -= prefix;
        std::vector<uint8_t> unescaped;
        if (version == 4 && ((flags & 2) || (data[5] & 128))) { unescaped = remove_unsynchronization(body, body_size); body = unescaped.data(); body_size = unescaped.size(); }
        size_t id_size = version == 2 ? 3 : 4;
        std::string id(reinterpret_cast<const char*>(frame), id_size);
        if (id == "TIT2" || id == "TT2") info.title = text_frame(body, body_size);
        else if (id == "TPE1" || id == "TP1") info.artist = text_frame(body, body_size);
        else if (id == "TALB" || id == "TAL") info.album = text_frame(body, body_size);
        else if (id == "APIC" || id == "PIC") apic(body, body_size, version == 2, info, cancel);
    }
}
void picture_block(const uint8_t* data, size_t bytes, AudioInfo& info, const std::atomic<bool>& cancel) {
    if (bytes < 32 || bytes > audio_tag_limit) return;
    size_t cursor = 0;
    auto integer = [&](uint32_t& out) { if (bytes-cursor < 4) return false; out = be32(data+cursor); cursor += 4; return true; };
    auto skip = [&](uint32_t count) { if (count > bytes-cursor) return false; cursor += count; return true; };
    uint32_t type = 0, mime = 0, description = 0, value = 0, size = 0;
    if (!integer(type) || !integer(mime) || mime > bytes-cursor) return;
    if (mime == 3 && !std::memcmp(data+cursor, "-->", 3)) return;
    if (!skip(mime) || !integer(description) || !skip(description)) return;
    for (int i = 0; i < 4; ++i) if (!integer(value)) return;
    if (!integer(size) || size > bytes-cursor) return;
    artwork(data+cursor, size, type, info, cancel);
}
std::vector<uint8_t> base64(const char* text, size_t bytes) {
    if (bytes > ((audio_tag_limit + 2) / 3) * 4 + 1024) return {};
    std::vector<uint8_t> out; out.reserve(std::min<size_t>(audio_tag_limit, bytes / 4 * 3));
    unsigned accumulator = 0, bits = 0, padding = 0;
    for (size_t i = 0; i < bytes; ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        if (c == '=') { if (++padding > 2) return {}; continue; }
        if (padding) return {};
        int value = c >= 'A' && c <= 'Z' ? c-'A' : c >= 'a' && c <= 'z' ? c-'a'+26 : c >= '0' && c <= '9' ? c-'0'+52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (value < 0) return {};
        accumulator = (accumulator << 6) | static_cast<unsigned>(value); bits += 6;
        if (bits >= 8) { bits -= 8; if (out.size() == audio_tag_limit) return {}; out.push_back(static_cast<uint8_t>(accumulator >> bits)); }
    }
    return out;
}
void comments(const uint8_t* data, size_t bytes, AudioInfo& info, const std::atomic<bool>& cancel) {
    if (bytes < 8) return;
    size_t cursor = 4; uint32_t vendor = le32(data);
    if (vendor > bytes-cursor) return; cursor += vendor;
    if (bytes-cursor < 4) return;
    uint32_t count = le32(data+cursor); cursor += 4;
    for (uint32_t i = 0; i < std::min<uint32_t>(count, 512) && !cancel; ++i) {
        if (bytes-cursor < 4) return;
        uint32_t length = le32(data+cursor); cursor += 4;
        if (length > bytes-cursor) return;
        read_vorbis_comment(reinterpret_cast<const char*>(data+cursor), length, info, cancel); cursor += length;
    }
}
void flac(ReadOnlyFile& file, AudioInfo& info, const std::atomic<bool>& cancel) {
    uint8_t magic[4]; if (!file.read_at(0, magic, 4) || std::memcmp(magic, "fLaC", 4)) return;
    uint64_t cursor = 4;
    for (unsigned blocks = 0; blocks < 256 && !cancel; ++blocks) {
        uint8_t h[4]; if (!file.read_at(cursor, h, 4)) return; cursor += 4;
        uint32_t length = uint32_t(h[1]) << 16 | uint32_t(h[2]) << 8 | h[3]; unsigned type = h[0] & 127;
        if (length > file.size()-cursor || cursor+length > 8u*1024u*1024u) return;
        if ((type == 4 || type == 6) && length && length <= audio_tag_limit) {
            std::vector<uint8_t> body(length); if (!file.read_at(cursor, body.data(), length)) return;
            if (type == 6) picture_block(body.data(), body.size(), info, cancel); else comments(body.data(), body.size(), info, cancel);
        }
        cursor += length; if (h[0] & 128) return;
    }
}
void riff_info(const std::vector<uint8_t>& body, AudioInfo& info) {
    if (body.size() < 4 || std::memcmp(body.data(), "INFO", 4)) return;
    for (size_t cursor = 4; body.size()-cursor >= 8;) {
        const uint8_t* chunk = body.data()+cursor; uint32_t length = le32(chunk+4); cursor += 8;
        if (length > body.size()-cursor) return;
        std::string value(reinterpret_cast<const char*>(body.data()+cursor), strnlen(reinterpret_cast<const char*>(body.data()+cursor), std::min<size_t>(length, 4096)));
        if (!std::memcmp(chunk, "INAM", 4)) info.title = value; else if (!std::memcmp(chunk, "IART", 4)) info.artist = value; else if (!std::memcmp(chunk, "IPRD", 4)) info.album = value;
        uint64_t padded = uint64_t(length)+(length & 1); if (padded > body.size()-cursor) return; cursor += static_cast<size_t>(padded);
    }
}
void wav(ReadOnlyFile& file, AudioInfo& info, const std::atomic<bool>& cancel) {
    uint8_t h[12]; if (!file.read_at(0, h, 12) || std::memcmp(h, "RIFF", 4) || std::memcmp(h+8, "WAVE", 4)) return;
    uint64_t end = std::min<uint64_t>(file.size(), uint64_t(le32(h+4))+8), cursor = 12;
    for (unsigned chunks = 0; chunks < 512 && cursor <= end && end-cursor >= 8 && !cancel; ++chunks) {
        uint8_t chunk[8]; if (!file.read_at(cursor, chunk, 8)) return; cursor += 8;
        uint32_t length = le32(chunk+4); if (length > end-cursor) return;
        bool tag = !std::memcmp(chunk, "id3 ", 4) || !std::memcmp(chunk, "ID3 ", 4);
        if ((tag || !std::memcmp(chunk, "LIST", 4)) && length && length <= audio_tag_limit+10) {
            std::vector<uint8_t> body(length); if (!file.read_at(cursor, body.data(), length)) return;
            if (tag) id3_tag(body.data(), body.size(), info, cancel); else riff_info(body, info);
        }
        uint64_t padded = uint64_t(length)+(length & 1); if (padded > end-cursor) return; cursor += padded;
    }
}
}
void read_vorbis_comment(const char* text, size_t bytes, AudioInfo& info, const std::atomic<bool>& cancel) {
    if (!text || !bytes || cancel) return;
    const char* equals = static_cast<const char*>(std::memchr(text, '=', std::min<size_t>(bytes, 64)));
    if (!equals) return;
    size_t key_length = static_cast<size_t>(equals-text), length = bytes-key_length-1;
    std::string key = search_key(std::string(text, key_length)); const char* value = equals+1;
    if (key == "metadata_block_picture") { auto decoded = base64(value, length); if (!decoded.empty()) picture_block(decoded.data(), decoded.size(), info, cancel); }
    else if (length <= 4096) {
        if (key == "title") info.title.assign(value, length); else if (key == "artist") info.artist.assign(value, length); else if (key == "album") info.album.assign(value, length);
    }
}
void read_audio_metadata(ReadOnlyFile& file, const std::string& ext, AudioInfo& info, const std::atomic<bool>& cancel) {
    if (ext == "flac") flac(file, info, cancel);
    else if (ext == "wav") wav(file, info, cancel);
    else if (ext == "mp3") {
        uint8_t h[10]; if (!file.read_at(0, h, 10) || std::memcmp(h, "ID3", 3) || !is_syncsafe(h+6)) return;
        size_t length = syncsafe(h+6); if (length > audio_tag_limit || length > file.size()-10) return;
        std::vector<uint8_t> tag(length+10); if (!file.read_at(0, tag.data(), tag.size())) return;
        id3_tag(tag.data(), tag.size(), info, cancel);
    }
}
}
