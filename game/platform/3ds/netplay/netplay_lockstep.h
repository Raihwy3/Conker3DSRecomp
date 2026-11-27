#pragma once

#include <array>
#include <cstdint>
#include "netplay_packets.h"

namespace conker_netplay {

// Pure deterministic lockstep input buffer (host-testable, no 3DS dependency).
// Each console owns one port (0..player_count-1), queues its own pad at
// `current_read + input_delay`, broadcasts a sliding window of up to
// `kInputWindow` recent local pads, and advances `current_read` once all
// active ports have delivered inputs for `current_read`.
class LockstepEngine {
public:
    static constexpr size_t kRingSize = 64;

    void start(int local_port, int player_count, int input_delay) {
        local_port_ = local_port;
        player_count_ = player_count;
        input_delay_ = input_delay < 0 ? 0 : (input_delay > 8 ? 8 : input_delay);
        active_mask_ = (uint8_t)((1u << player_count_) - 1u);
        current_read_ = 0;
        next_local_write_ = (uint32_t)input_delay_;
        active_ = true;

        for (int p = 0; p < kMaxPlayers; ++p) {
            for (size_t s = 0; s < kRingSize; ++s) {
                ring_[p][s] = Slot{};
            }
            // Pre-fill initial delay reads (0..input_delay-1) with neutral pads on every peer.
            for (int r = 0; r < input_delay_; ++r) {
                store_slot(p, (uint32_t)r, Pad{});
            }
        }
    }

    void stop() { active_ = false; }
    bool active() const { return active_; }
    int local_port() const { return local_port_; }
    int player_count() const { return player_count_; }
    int input_delay() const { return input_delay_; }
    uint32_t current_read() const { return current_read_; }
    uint8_t active_mask() const { return active_mask_; }

    // Record local input for the upcoming read (`current_read + input_delay`)
    // and build the redundant sliding-window InputsPacket to broadcast.
    InputsPacket push_local_input(const Pad& local_pad) {
        const uint32_t target = current_read_ + (uint32_t)input_delay_;
        store_slot(local_port_, target, local_pad);
        if (target + 1 > next_local_write_) {
            next_local_write_ = target + 1;
        }
        return build_local_packet();
    }

    // Build a retransmission packet of the latest local inputs (used while stalled waiting for a peer).
    InputsPacket build_local_packet() const {
        InputsPacket pkt{};
        pkt.port = (uint8_t)local_port_;
        const uint32_t end = next_local_write_;
        const uint32_t count = end < (uint32_t)kInputWindow ? end : (uint32_t)kInputWindow;
        const uint32_t first = end - count;
        pkt.first_read = first;
        pkt.count = (uint8_t)count;
        for (uint32_t i = 0; i < count; ++i) {
            Pad pad{};
            if (load_slot(local_port_, first + i, pad)) {
                pkt.pads[i] = pad;
            }
        }
        return pkt;
    }

    // Apply an incoming InputsPacket from a remote peer.
    void on_remote_inputs(const InputsPacket& pkt) {
        if (!active_ || pkt.port >= (uint8_t)player_count_ || pkt.port == (uint8_t)local_port_) {
            return;
        }
        for (uint8_t i = 0; i < pkt.count; ++i) {
            const uint32_t r = pkt.first_read + i;
            if (r + kRingSize <= current_read_) continue; // too old
            if (r >= current_read_ + kRingSize) continue; // too far ahead
            store_slot(pkt.port, r, pkt.pads[i]);
        }
    }

    void mark_peer_left(int port) {
        if (port >= 0 && port < kMaxPlayers && port != local_port_) {
            active_mask_ &= (uint8_t)~(1u << port);
        }
    }

    // Returns bitmask of ports we are still waiting on for `current_read_`.
    uint8_t missing_mask() const {
        uint8_t missing = 0;
        for (int p = 0; p < player_count_; ++p) {
            if (!(active_mask_ & (1u << p))) continue;
            Pad dummy{};
            if (!load_slot(p, current_read_, dummy)) {
                missing |= (uint8_t)(1u << p);
            }
        }
        return missing;
    }

    bool ready_to_advance() const { return active_ && missing_mask() == 0; }

    // Consume inputs for `current_read_` into `out_pads[0..kMaxPlayers-1]` and advance `current_read_`.
    bool pop_frame(Pad out_pads[kMaxPlayers]) {
        if (!ready_to_advance()) return false;
        for (int p = 0; p < kMaxPlayers; ++p) {
            if (p < player_count_ && (active_mask_ & (1u << p))) {
                Pad pad{};
                load_slot(p, current_read_, pad);
                out_pads[p] = pad;
            } else {
                out_pads[p] = Pad{};
                if (p >= player_count_) out_pads[p].err_no = 0x08; // CONT_NO_RESPONSE_ERROR
            }
        }
        current_read_++;
        return true;
    }

private:
    struct Slot {
        uint32_t read = UINT32_MAX;
        Pad pad{};
    };

    void store_slot(int port, uint32_t read, const Pad& pad) {
        Slot& s = ring_[port][read % kRingSize];
        s.read = read;
        s.pad = pad;
    }

    bool load_slot(int port, uint32_t read, Pad& out) const {
        const Slot& s = ring_[port][read % kRingSize];
        if (s.read != read) return false;
        out = s.pad;
        return true;
    }

    bool active_ = false;
    int local_port_ = 0;
    int player_count_ = 1;
    int input_delay_ = kDefaultInputDelay;
    uint8_t active_mask_ = 1;
    uint32_t current_read_ = 0;
    uint32_t next_local_write_ = 0;
    Slot ring_[kMaxPlayers][kRingSize]{};
};

} // namespace conker_netplay
