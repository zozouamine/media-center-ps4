#pragma once
#include "image.hpp"

namespace mc {
std::shared_ptr<Bitmap> decode_ico(const uint8_t*, size_t, const std::atomic<bool>&, std::string& error);
std::shared_ptr<Bitmap> decode_tiff(const uint8_t*, size_t, const std::atomic<bool>&, std::string& error);
}
