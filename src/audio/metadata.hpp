#pragma once
#include "decoder.hpp"

namespace mc {
constexpr size_t artwork_file_limit = 4u * 1024u * 1024u;
constexpr size_t audio_tag_limit = 5u * 1024u * 1024u;
void read_audio_metadata(ReadOnlyFile&, const std::string& extension, AudioInfo&, const std::atomic<bool>& cancel);
void read_vorbis_comment(const char* text, size_t bytes, AudioInfo&, const std::atomic<bool>& cancel);
}
