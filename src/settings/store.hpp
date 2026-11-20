#pragma once
#include "library/types.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace mc {
struct Settings {
    int volume = 80, slideshow_seconds = 5, repeat = 0;
    bool hidden = false, auto_next = true, shuffle = false;
};
struct SavedState {
    Settings settings;
    std::vector<Item> favorites, last_played, music_queue, video_queue;
};
class StateStore {
    std::string root_, error_;
    int directory_fd_ = -1;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    SavedState pending_{};
    bool quit_ = false, dirty_ = false, clear_ = false, enabled_ = false, source_overlap_ = false;
    void run();
    bool write_state(const SavedState&, std::string& error);
public:
    explicit StateStore(std::string root, const std::vector<Source>& sources = {});
    ~StateStore();
    SavedState read();
    void save(const SavedState&);
    void clear_owned_data();
    void protect_sources(const std::vector<Source>& sources);
    // Recover a directory denied before GoldHEN enabled direct process access.
    // A store disabled because it overlaps a media source is never reopened.
    bool retry_open();
    std::string error();
    const std::string& root() const { return root_; }
};
void toggle_favorite(SavedState&, const Item&);
bool is_favorite(const SavedState&, const Item&);
void record_played(SavedState&, const Item&);
}
