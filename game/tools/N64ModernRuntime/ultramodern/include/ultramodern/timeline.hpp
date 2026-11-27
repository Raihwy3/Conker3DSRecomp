#ifndef __ULTRAMODERN_TIMELINE_HPP__
#define __ULTRAMODERN_TIMELINE_HPP__

#include <cstdint>

#include "ultra64.h"

// Deterministic virtual timeline for local wireless / lockstep multiplayer.
// When enabled, VIs (60 Hz), timers, and DP completions are delivered at
// deterministic idle delivery points instead of from real-time host threads.
namespace ultramodern {
    namespace timeline {
        void enable(bool fast);
        void disable();
        bool enabled();
        void set_catching_up(bool on);
        void pace_later(uint32_t us);
        uint64_t delivery_time();
        void deliver(RDRAM_ARG bool until_woken, uint64_t max_ticks);
    }

    namespace timeline_parts {
        // timer.cpp
        void clock_enable();
        void clock_disable();
        void clock_advance_to(uint64_t ticks);
        bool timers_next_deadline(RDRAM_ARG uint64_t& deadline);
        void timers_fire_due(RDRAM_ARG uint64_t ticks);
        // events.cpp
        void vi_take_over();
        void vi_release();
        void vi_deliver(RDRAM_ARG1);
        void dp_deliver();
    }
}

#endif
