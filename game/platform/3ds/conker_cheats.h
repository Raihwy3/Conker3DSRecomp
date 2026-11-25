#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace conker_cheats {

constexpr uint32_t kSceneAddr = 0x800BE9F0u;
constexpr uint32_t kSceneLogos = 0x25u;
constexpr uint32_t kSceneOpening = 0x21u;
constexpr uint32_t kSceneBarMenu = 0x1Du;
constexpr uint32_t kMultiplayerAddr = 0x800BE616u;
constexpr uint32_t kCheatFlagsAddr = 0x800E9D00u;
constexpr uint32_t kUnlockChaptersBit = 0x00008000u;
constexpr uint32_t kP1HealthAddr = 0x800CC49Au;
constexpr uint32_t kP1LivesAddr = 0x800D2144u;
constexpr uint32_t kP1CashAddr = 0x800D2148u;

constexpr uint8_t kMaxHealth = 6u;
constexpr uint8_t kMinCheatLives = 10u;
constexpr int32_t kCheatCash = 9999;
constexpr size_t kMinRdramSize = 0x00100000u;

inline uint8_t rd_u8(const uint8_t* rdram, uint32_t a) {
    return rdram[(a - 0x80000000u) ^ 3u];
}

inline void wr_u8(uint8_t* rdram, uint32_t a, uint8_t v) {
    rdram[(a - 0x80000000u) ^ 3u] = v;
}

inline uint32_t rd_u32(const uint8_t* rdram, uint32_t a) {
    uint32_t v = 0;
    std::memcpy(&v, rdram + (a - 0x80000000u), sizeof(v));
    return v;
}

inline void wr_u32(uint8_t* rdram, uint32_t a, uint32_t v) {
    std::memcpy(rdram + (a - 0x80000000u), &v, sizeof(v));
}

inline int32_t rd_s32(const uint8_t* rdram, uint32_t a) {
    int32_t v = 0;
    std::memcpy(&v, rdram + (a - 0x80000000u), sizeof(v));
    return v;
}

inline void wr_s32(uint8_t* rdram, uint32_t a, int32_t v) {
    std::memcpy(rdram + (a - 0x80000000u), &v, sizeof(v));
}

struct Config {
    bool invincible = false;
    bool inf_lives = false;
    bool max_cash = false;
    bool unlock_chapters = false;
};

/** Applies enabled Quick Cheats to N64 RDRAM without corrupting save progress. */
class State {
public:
    void apply(uint8_t* rdram, size_t rdram_size, const Config& cfg) {
        if (!rdram || rdram_size < kMinRdramSize) {
            return;
        }
        const uint32_t scene = rd_u32(rdram, kSceneAddr);
        if (scene == 0u || scene >= 0x100u) {
            return;
        }

        // Toggle only the game's built-in WELDERSBENCH bit (D_800E9D00 & 0x8000)
        // so D_800D2E4C story/script progress flags and saves remain intact.
        if (cfg.unlock_chapters) {
            const uint32_t mask = rd_u32(rdram, kCheatFlagsAddr);
            if ((mask & kUnlockChaptersBit) == 0u) {
                wr_u32(rdram, kCheatFlagsAddr, mask | kUnlockChaptersBit);
            }
        } else if (prev_unlock_) {
            const uint32_t mask = rd_u32(rdram, kCheatFlagsAddr);
            wr_u32(rdram, kCheatFlagsAddr, mask & ~kUnlockChaptersBit);
        }
        prev_unlock_ = cfg.unlock_chapters;

        const bool in_play = scene != kSceneLogos && scene != kSceneOpening && scene != kSceneBarMenu;
        if (!in_play) {
            return;
        }

        if (cfg.invincible) {
            const uint8_t hp = rd_u8(rdram, kP1HealthAddr);
            // Preserve zero health so fatal falls/drowning still transition cleanly.
            if (hp > 0u && hp < kMaxHealth) {
                wr_u8(rdram, kP1HealthAddr, kMaxHealth);
            }
        }
        if (cfg.inf_lives) {
            if (rd_u8(rdram, kP1LivesAddr) < kMinCheatLives) {
                wr_u8(rdram, kP1LivesAddr, kMinCheatLives);
            }
        }
        if (cfg.max_cash && rd_u8(rdram, kMultiplayerAddr) == 0u) {
            if (rd_s32(rdram, kP1CashAddr) < kCheatCash) {
                wr_s32(rdram, kP1CashAddr, kCheatCash);
            }
        }
    }

private:
    bool prev_unlock_ = false;
};

} // namespace conker_cheats
