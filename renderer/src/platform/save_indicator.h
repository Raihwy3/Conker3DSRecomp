#pragma once
#include <atomic>
#include <cstdint>
namespace recomp3ds {
class SaveIndicator {
    std::atomic<uint32_t> serial{0};
    std::atomic<bool> writing{false};
    uint32_t seen=0;
    uint64_t until=0;
public:
    void signal(bool active) {
        writing.store(active,std::memory_order_release);
        if(active)serial.fetch_add(1,std::memory_order_release);
    }
    bool visible(uint64_t now_ms) {
        const uint32_t current=serial.load(std::memory_order_acquire);
        if(current!=seen){seen=current;until=now_ms+1400;}
        return writing.load(std::memory_order_acquire) || now_ms<until;
    }
};
}
