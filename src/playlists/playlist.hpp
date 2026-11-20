#pragma once
#include "library/types.hpp"
#include <random>

namespace mc {
class Playlist {
    std::vector<Item> entries_;
    std::vector<size_t> history_;
    size_t index_ = 0;
    std::mt19937 random_{0x504d4301u};
public:
    void set(std::vector<Item> entries, const Item& current);
    bool next(Item& out, bool automatic, int repeat, bool shuffle);
    bool previous(Item& out);
    const std::vector<Item>& entries() const { return entries_; }
    size_t index() const { return index_; }
};
void enqueue(std::vector<Item>& queue, const Item& item);
}
