#include "playlist.hpp"
#include <algorithm>

namespace mc {
void Playlist::set(std::vector<Item> entries, const Item& current) {
    if (entries.size() > 256) entries.resize(256);
    entries_ = std::move(entries); history_.clear();
    auto it = std::find(entries_.begin(), entries_.end(), current);
    if (it == entries_.end()) { if (entries_.size() >= 256) entries_.pop_back(); entries_.push_back(current); index_ = entries_.size() - 1; }
    else index_ = static_cast<size_t>(it - entries_.begin());
}
bool Playlist::next(Item& out, bool automatic, int repeat, bool shuffle) {
    if (entries_.empty()) return false;
    if (automatic && repeat == 1) { out = entries_[index_]; return true; }
    size_t next = index_;
    if (shuffle && entries_.size() > 1) { std::uniform_int_distribution<size_t> dist(0, entries_.size() - 2); next = dist(random_); if (next >= index_) ++next; }
    else if (index_ + 1 < entries_.size()) next = index_ + 1;
    else if (repeat == 2 || !automatic) next = 0;
    else return false;
    if (history_.size() >= 256) history_.erase(history_.begin()); history_.push_back(index_);
    index_ = next; out = entries_[index_]; return true;
}
bool Playlist::previous(Item& out) {
    if (entries_.empty()) return false;
    if (!history_.empty()) { index_ = history_.back(); history_.pop_back(); }
    else if (index_ > 0) --index_;
    else index_ = entries_.size() - 1;
    out = entries_[index_]; return true;
}
void enqueue(std::vector<Item>& queue, const Item& item) {
    if (queue.size() < 256 && std::find(queue.begin(), queue.end(), item) == queue.end()) queue.push_back(item);
}
}
