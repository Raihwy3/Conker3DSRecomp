#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include "ultramodern/ultramodern.hpp"
#include "ultramodern/timeline.hpp"

void dequeue_external_messages(RDRAM_ARG1);
extern std::atomic_bool exited;
std::chrono::microseconds ticks_to_duration(uint64_t ticks);

namespace {
    constexpr uint64_t ticks_per_vi = 46'875'000 / 60;

    std::atomic_bool deterministic = false;
    bool fast = false;
    std::atomic_bool catching_up = false;
    std::atomic_bool pace_from_now = false;
    std::atomic<uint64_t> later_us = 0;
    uint64_t now = 0;
    uint64_t next_vi = 1;
    std::chrono::high_resolution_clock::time_point real_start;

    void pace(uint64_t ticks) {
        using namespace std::chrono_literals;
        if (fast || catching_up.load(std::memory_order_relaxed)) {
            return;
        }
        const auto real_now = std::chrono::high_resolution_clock::now();
        if (pace_from_now.exchange(false)) {
            real_start = real_now - ticks_to_duration(ticks);
        }
        real_start += std::chrono::microseconds{ later_us.exchange(0) };
        const auto target = real_start + ticks_to_duration(ticks);
        if (real_now < target) {
            ultramodern::sleep_until(target);
        }
        else if (real_now - target > 100ms) {
            real_start += real_now - target;
        }
    }
}

void ultramodern::timeline::enable(bool fast_) {
    fast = fast_;
    now = 0;
    next_vi = 1;
    catching_up = false;
    pace_from_now = false;
    later_us = 0;
    deterministic = true;
    ultramodern::timeline_parts::clock_enable();
    ultramodern::timeline_parts::vi_take_over();
    real_start = std::chrono::high_resolution_clock::now();
}

void ultramodern::timeline::disable() {
    if (!deterministic.exchange(false)) {
        return;
    }
    ultramodern::timeline_parts::clock_disable();
    ultramodern::timeline_parts::vi_release();
}

bool ultramodern::timeline::enabled() {
    return deterministic.load(std::memory_order_relaxed);
}

void ultramodern::timeline::set_catching_up(bool on) {
    if (!on && catching_up.exchange(false)) {
        pace_from_now = true;
    }
    catching_up = on;
}

void ultramodern::timeline::pace_later(uint32_t us) {
    later_us += us;
}

uint64_t ultramodern::timeline::delivery_time() {
    return now;
}

void ultramodern::timeline::deliver(RDRAM_ARG bool until_woken, uint64_t max_ticks) {
    dequeue_external_messages(PASS_RDRAM1);
    if (until_woken && !thread_queue_empty(PASS_RDRAM running_queue)) {
        return;
    }
    const uint64_t stop_at = until_woken ? UINT64_MAX : now + max_ticks;
    while (deterministic.load(std::memory_order_relaxed)) {
        while (exited) {
            std::this_thread::sleep_for(std::chrono::seconds{ 1 });
        }
        const uint64_t vi_time = next_vi * ticks_per_vi;
        uint64_t at = vi_time;
        uint64_t deadline = 0;
        if (ultramodern::timeline_parts::timers_next_deadline(PASS_RDRAM deadline) && deadline < at) {
            at = deadline;
        }
        at = std::max(at, now);
        if (at > stop_at) {
            pace(stop_at);
            now = stop_at;
            ultramodern::timeline_parts::clock_advance_to(now);
            return;
        }

        pace(at);
        now = at;
        ultramodern::timeline_parts::clock_advance_to(now);
        const bool vi_due = at == vi_time;
        if (vi_due) {
            ultramodern::timeline_parts::dp_deliver();
        }
        ultramodern::timeline_parts::timers_fire_due(PASS_RDRAM now);
        if (vi_due) {
            ultramodern::timeline_parts::vi_deliver(PASS_RDRAM1);
            next_vi++;
        }
        dequeue_external_messages(PASS_RDRAM1);

        if (until_woken && !thread_queue_empty(PASS_RDRAM running_queue)) {
            return;
        }
    }
}
