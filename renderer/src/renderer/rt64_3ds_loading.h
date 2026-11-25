#pragma once

#include <cstdint>

namespace rt64_3ds {

// Drives the animated "Loading." / "Loading.." / "Loading..." indicator shown
// while the game is switching levels and has stopped submitting frames.
class LoadingIndicator {
public:
    static constexpr uint64_t kShowDelayMs = 200;
    static constexpr uint64_t kDotStepMs = 350;

    void on_frame_submitted(uint64_t now_ms, bool switching) {
        last_frame_ms_ = now_ms;
        has_frame_ = true;
        latched_switch_ = switching;
        active_ = false;
        active_since_ms_ = 0;
    }

    int poll(uint64_t now_ms, bool enabled, bool switching) {
        if (switching) {
            latched_switch_ = true;
        }
        if (!enabled || !has_frame_ || !latched_switch_) {
            active_ = false;
            return 0;
        }
        if (now_ms < last_frame_ms_ || (now_ms - last_frame_ms_) < kShowDelayMs) {
            active_ = false;
            return 0;
        }
        if (!active_) {
            active_ = true;
            active_since_ms_ = now_ms;
        }
        const uint64_t elapsed = now_ms >= active_since_ms_ ? (now_ms - active_since_ms_) : 0;
        return 1 + (int)((elapsed / kDotStepMs) % 3);
    }

private:
    uint64_t last_frame_ms_ = 0;
    uint64_t active_since_ms_ = 0;
    bool has_frame_ = false;
    bool latched_switch_ = false;
    bool active_ = false;
};

} // namespace rt64_3ds
