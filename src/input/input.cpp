#include "input.hpp"
#include <cmath>
#include <algorithm>

namespace mc {
float InputFilter::filter_axis(float x, float y, bool horizontal) {
    if (!std::isfinite(x) || !std::isfinite(y)) return 0;
    const float length = std::sqrt(x*x + y*y);
    if (length <= dead_zone) return 0;
    float value = horizontal ? x : y;
    return std::max(-1.f, std::min(1.f, value));
}
void InputFilter::reset() { previous_ = {}; previous_buttons_ = 0; next_repeat_.fill(0); }
InputEvent InputFilter::update(const RawInput& raw, double now) {
    RawInput input = raw;
    if (!input.connected) { input = {}; input.connected = false; }
    const float lx = filter_axis(input.lx, input.ly, true), ly = filter_axis(input.lx, input.ly, false);
    const float rx = filter_axis(input.rx, input.ry, true), ry = filter_axis(input.rx, input.ry, false);
    uint32_t buttons = input.buttons;
    if (lx <= -dead_zone) buttons |= Left;
    if (lx >= dead_zone) buttons |= Right;
    if (ly <= -dead_zone) buttons |= Up;
    if (ly >= dead_zone) buttons |= Down;
    if (input.lt >= 0.25f) buttons |= L2;
    if (input.rt >= 0.25f) buttons |= R2;
    InputEvent e;
    e.pressed = buttons & ~previous_buttons_;
    e.released = previous_buttons_ & ~buttons;
    e.held = buttons;
    const uint32_t repeat_mask = Up | Down | Left | Right | L2 | R2;
    for (unsigned i = 0; i < 16; ++i) {
        uint32_t mask = 1u << i;
        if (e.pressed & mask) next_repeat_[i] = now + 0.38;
        else if ((buttons & repeat_mask & mask) && now >= next_repeat_[i]) {
            e.repeated |= mask;
            next_repeat_[i] = now + ((mask == L2 || mask == R2) ? 0.25 : 0.12);
        }
    }
    const float plx = filter_axis(previous_.lx, previous_.ly, true), ply = filter_axis(previous_.lx, previous_.ly, false);
    const float prx = filter_axis(previous_.rx, previous_.ry, true), pry = filter_axis(previous_.rx, previous_.ry, false);
    bool stick_changed = std::fabs(lx - plx) >= 0.055f || std::fabs(ly - ply) >= 0.055f
        || std::fabs(rx - prx) >= 0.055f || std::fabs(ry - pry) >= 0.055f;
    bool trigger_changed = (input.lt >= 0.25f && std::fabs(input.lt - previous_.lt) >= 0.06f)
        || (input.rt >= 0.25f && std::fabs(input.rt - previous_.rt) >= 0.06f);
    // Releases alone and polling an unchanged held stick are not fresh activity.
    e.intentional = input.connected && (e.pressed != 0 || stick_changed || trigger_changed || e.repeated != 0);
    previous_ = input; previous_buttons_ = buttons;
    return e;
}
}
