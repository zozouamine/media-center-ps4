#pragma once
#include <cstdint>
#include <array>

namespace mc {
enum Button : uint32_t {
    Up = 1u << 0, Down = 1u << 1, Left = 1u << 2, Right = 1u << 3,
    Cross = 1u << 4, Circle = 1u << 5, Triangle = 1u << 6, Square = 1u << 7,
    L1 = 1u << 8, R1 = 1u << 9, L2 = 1u << 10, R2 = 1u << 11,
    Options = 1u << 12, L3 = 1u << 13, R3 = 1u << 14, Touch = 1u << 15
};
struct RawInput {
    uint32_t buttons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
    bool connected = true;
    // Native computer preview only; PS4 continues to use controller input.
    int pointer_x = -1, pointer_y = -1, pointer_clicks = 0;
    bool pointer_pressed = false;
};
struct InputEvent {
    uint32_t pressed = 0, released = 0, repeated = 0, held = 0;
    bool intentional = false;
    bool has(Button b) const { return ((pressed | repeated) & b) != 0; }
    bool edge(Button b) const { return (pressed & b) != 0; }
};
class InputFilter {
    RawInput previous_{};
    uint32_t previous_buttons_ = 0;
    std::array<double, 16> next_repeat_{};
    static float filter_axis(float x, float y, bool horizontal);
public:
    static constexpr float dead_zone = 0.24f;
    InputEvent update(const RawInput& input, double monotonic_seconds);
    void reset();
};
}
