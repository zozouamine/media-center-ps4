#pragma once
namespace mc {
class Osd {
    double last_activity_ = 0;
    bool paused_ = false, menu_ = false, hidden_by_user_ = false;
public:
    static constexpr double timeout = 7.0;
    void started(double now) { last_activity_ = now; paused_ = false; menu_ = false; hidden_by_user_ = false; }
    void activity(double now, bool intentional) { if (intentional) { last_activity_ = now; hidden_by_user_ = false; } }
    void paused(double now, bool paused) { if (paused_ != paused) last_activity_ = now; paused_ = paused; }
    void menu(double now, bool open) { if (menu_ != open) last_activity_ = now; menu_ = open; }
    void hide() { hidden_by_user_ = true; }
    bool hidden_by_user() const { return hidden_by_user_; }
    bool visible(double now) const { return !hidden_by_user_ && (paused_ || menu_ || now - last_activity_ < timeout); }
};
}
