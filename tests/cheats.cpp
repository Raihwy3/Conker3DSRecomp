#include "conker_cheats.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
uint16_t rd_u16(const uint8_t* rd, uint32_t a) {
    uint16_t v = 0;
    std::memcpy(&v, rd + ((a - 0x80000000u) ^ 2u), sizeof(v));
    return v;
}
void wr_u16(uint8_t* rd, uint32_t a, uint16_t v) {
    std::memcpy(rd + ((a - 0x80000000u) ^ 2u), &v, sizeof(v));
}
} // namespace

int main() {
    using namespace conker_cheats;
    std::vector<uint8_t> rd(16 * 1024 * 1024, 0);
    uint8_t* r = rd.data();

    constexpr uint32_t kSaveFlagsBuf = 0x80300000u;
    constexpr uint32_t kSavePtrAddr = 0x800D2E4Cu;
    constexpr uint32_t kOldFlagsAddr = 0x8008FDBCu;
    constexpr uint32_t kStatsMirrorHpAddr = 0x800D2145u;

    wr_u32(r, kSavePtrAddr, kSaveFlagsBuf);
    wr_u16(r, kOldFlagsAddr, 0x0042u);
    wr_u32(r, kCheatFlagsAddr, 0x00000200u);

    State st;
    Config cfg{};
    cfg.unlock_chapters = true;

    for (uint32_t sc : { kSceneLogos, kSceneOpening, kSceneBarMenu, 0x29u }) {
        wr_u32(r, kSceneAddr, sc);
        st.apply(r, rd.size(), cfg);
        assert(rd_u32(r, kCheatFlagsAddr) == (0x00000200u | kUnlockChaptersBit));
        assert(rd_u16(r, kOldFlagsAddr) == 0x0042u);
        for (uint32_t i = 0; i < 27u; ++i) {
            assert(rd_u8(r, kSaveFlagsBuf + i) == 0u);
        }
        assert((rd_u8(r, kSaveFlagsBuf + (0xC9u >> 3)) & (1u << (0xC9u & 7))) == 0u);
        assert((rd_u8(r, kSaveFlagsBuf + (0xCAu >> 3)) & (1u << (0xCAu & 7))) == 0u);
    }

    // Turning unlock_chapters off clears only kUnlockChaptersBit.
    cfg.unlock_chapters = false;
    wr_u32(r, kSceneAddr, kSceneBarMenu);
    st.apply(r, rd.size(), cfg);
    assert(rd_u32(r, kCheatFlagsAddr) == 0x00000200u);

    // Fresh State must not clear a manually entered WELDERSBENCH bit.
    State manual;
    wr_u32(r, kCheatFlagsAddr, 0x00000200u | kUnlockChaptersBit);
    manual.apply(r, rd.size(), Config{});
    assert(rd_u32(r, kCheatFlagsAddr) == (0x00000200u | kUnlockChaptersBit));

    // Menu and intro scenes must not apply stat cheats.
    wr_u8(r, kP1HealthAddr, 3u);
    wr_u8(r, kP1LivesAddr, 3u);
    wr_u8(r, kStatsMirrorHpAddr, 2u);
    wr_s32(r, kP1CashAddr, 150);
    wr_u8(r, kMultiplayerAddr, 0u);

    Config stat_cfg{};
    stat_cfg.invincible = true;
    stat_cfg.inf_lives = true;
    stat_cfg.max_cash = true;
    for (uint32_t sc : { 0x00u, kSceneLogos, kSceneOpening, kSceneBarMenu, 0x100u }) {
        wr_u32(r, kSceneAddr, sc);
        st.apply(r, rd.size(), stat_cfg);
        assert(rd_u8(r, kP1HealthAddr) == 3u);
        assert(rd_u8(r, kP1LivesAddr) == 3u);
        assert(rd_u8(r, kStatsMirrorHpAddr) == 2u);
        assert(rd_s32(r, kP1CashAddr) == 150);
    }

    // In-play scene 0x29: invincible restores non-zero health only, leaves lives/flags untouched.
    wr_u32(r, kSceneAddr, 0x29u);
    Config inv_cfg{};
    inv_cfg.invincible = true;
    st.apply(r, rd.size(), inv_cfg);
    assert(rd_u8(r, kP1HealthAddr) == 6u);
    assert(rd_u8(r, kP1LivesAddr) == 3u);
    assert(rd_u16(r, kOldFlagsAddr) == 0x0042u);

    wr_u8(r, kP1HealthAddr, 0u);
    st.apply(r, rd.size(), inv_cfg);
    assert(rd_u8(r, kP1HealthAddr) == 0u);

    // inf_lives sets 0x800D2144 to at least 10 without touching 0x800D2145.
    Config lives_cfg{};
    lives_cfg.inf_lives = true;
    wr_u8(r, kStatsMirrorHpAddr, 4u);
    st.apply(r, rd.size(), lives_cfg);
    assert(rd_u8(r, kP1LivesAddr) == 10u);
    assert(rd_u8(r, kStatsMirrorHpAddr) == 4u);
    wr_u8(r, kP1LivesAddr, 25u);
    st.apply(r, rd.size(), lives_cfg);
    assert(rd_u8(r, kP1LivesAddr) == 25u);

    // max_cash sets 9999 in story mode, preserves higher values, skips multiplayer.
    Config cash_cfg{};
    cash_cfg.max_cash = true;
    wr_u8(r, kMultiplayerAddr, 0u);
    wr_s32(r, kP1CashAddr, 150);
    st.apply(r, rd.size(), cash_cfg);
    assert(rd_s32(r, kP1CashAddr) == 9999);

    wr_s32(r, kP1CashAddr, 1000000);
    st.apply(r, rd.size(), cash_cfg);
    assert(rd_s32(r, kP1CashAddr) == 1000000);

    wr_u8(r, kMultiplayerAddr, 1u);
    wr_s32(r, kP1CashAddr, 5);
    st.apply(r, rd.size(), cash_cfg);
    assert(rd_s32(r, kP1CashAddr) == 5);

    puts("PASS: cheats preserve D_800D2E4C/D_8008FDBC and apply safely in gameplay");
    return 0;
}
