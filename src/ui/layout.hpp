#pragma once
#include "media/engine.hpp"
#include <array>

namespace mc::music_layout {
constexpr int back_left=72,back_top=156,back_width=192,back_height=44;
constexpr int controls_left=406,controls_top=754,control_width=168,control_height=116,control_gap=20;
constexpr int control_stride=control_width+control_gap;
constexpr int controls_width=control_stride*6-control_gap;
}

namespace mc::video_layout {
constexpr int overlay_top=826;
constexpr int controls_top=950,control_width=64,control_height=64,control_gap=16;
constexpr int control_stride=control_width+control_gap;
constexpr int controls_width=control_stride*6-control_gap;
constexpr int controls_left=(screen_width-controls_width)/2;
constexpr int back_left=80,back_top=950,back_width=64,back_height=64;
constexpr int aspect_left=1696,aspect_top=950,aspect_width=64,aspect_height=64;
constexpr int options_left=1776,options_top=950,options_width=64,options_height=64;
// Square retains the video aspect shortcut; Stop has the same action as Back.
constexpr std::array<Button,6> buttons{L1,L2,Cross,R2,R1,Circle};
inline bool visible(bool osd,Playback state) {
    return osd || state==Playback::Loading || state==Playback::Error || state==Playback::Ended;
}
inline std::array<bool,6> enabled(const MediaSnapshot& media,size_t playlist_count) {
    bool active=media.state==Playback::Playing || media.state==Playback::Paused || media.state==Playback::Seeking || media.state==Playback::Ended;
    return {playlist_count>1,active && media.seekable,active,active && media.seekable,playlist_count>1,media.state!=Playback::Idle};
}
}
