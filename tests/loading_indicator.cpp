#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

#include "rt64_3ds_loading.h"

using u64 = uint64_t;
constexpr u64 SYSCLOCK_ARM11 = 268123480;
u64 fake_tick = SYSCLOCK_ARM11;
u64 svcGetSystemTick() { return fake_tick; }

size_t tinfl_decompress_mem_to_mem(void* out, size_t n, const void* in, size_t len, int) {
    z_stream z{};
    z.next_in = (Bytef*)in;
    z.avail_in = (uInt)len;
    z.next_out = (Bytef*)out;
    z.avail_out = (uInt)n;
    inflateInit2(&z, -15);
    const int r = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    return r == Z_STREAM_END ? z.total_out : size_t(-1);
}

namespace recomp3ds {
bool settings_perf_monitor() { return false; }
}

bool g_screen_dirty = false, g_conker_menu = false;
uint8_t base[320 * 240 * 3];
uint8_t* g_theme_base = base;
constexpr int theme_index(int x, int y) { return x * 240 + 239 - y; }
constexpr uint16_t theme_rgb(int r, int g, int b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

#include "conker_menu_tabs.h"
#include "conker_console_ui.h"

static void test_state_machine() {
    rt64_3ds::LoadingIndicator ind;

    // Boot before first frame: never shows even if switching is true.
    assert(ind.poll(0, true, true) == 0);
    assert(ind.poll(500, true, true) == 0);

    // Normal gameplay with a long idle gap (1000 ms) while not switching.
    ind.on_frame_submitted(1000, false);
    assert(ind.poll(1000, true, false) == 0);
    assert(ind.poll(2000, true, false) == 0);

    // Fast level switch finishing before 200 ms: no flicker.
    ind.on_frame_submitted(3000, true);
    assert(ind.poll(3150, true, false) == 0);
    ind.on_frame_submitted(3150, false);
    assert(ind.poll(3500, true, false) == 0);

    // Slow level switch: dot progression 0 (<200ms) -> 1 -> 2 -> 3 -> 1 -> 2 -> 3.
    ind.on_frame_submitted(4000, true);
    assert(ind.poll(4199, true, true) == 0);
    assert(ind.poll(4200, true, true) == 1);
    assert(ind.poll(4549, true, true) == 1);
    assert(ind.poll(4550, true, true) == 2);
    assert(ind.poll(4899, true, true) == 2);
    assert(ind.poll(4900, true, true) == 3);
    assert(ind.poll(5249, true, true) == 3);
    assert(ind.poll(5250, true, true) == 1);
    assert(ind.poll(5600, true, true) == 2);
    assert(ind.poll(5950, true, true) == 3);

    // Latching: continues animating after switching flag clears until a real frame arrives.
    assert(ind.poll(6300, true, false) == 1);
    assert(ind.poll(6650, true, false) == 2);
    ind.on_frame_submitted(6700, false);
    assert(ind.poll(6700, true, false) == 0);
    assert(ind.poll(8000, true, false) == 0);

    // Switching rising mid-gap also latches properly.
    ind.on_frame_submitted(9000, false);
    assert(ind.poll(9050, true, true) == 0);
    assert(ind.poll(9200, true, true) == 1);
    assert(ind.poll(9550, true, false) == 2);

    // Toggle disabled: always hidden, and enabling mid-switch restarts cleanly at 1 dot.
    ind.on_frame_submitted(10000, true);
    assert(ind.poll(10300, false, true) == 0);
    assert(ind.poll(10600, false, false) == 0);
    assert(ind.poll(10600, true, false) == 1);
    assert(ind.poll(10950, true, false) == 2);
    assert(ind.poll(11300, true, false) == 3);
}

static bool has_bright_pixel(const uint32_t* img, int w, int x0, int x1, int y0, int y1) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const uint32_t px = img[y * w + x];
            const int r = (int)((px >> 24) & 0xFF);
            const int g = (int)((px >> 16) & 0xFF);
            const int b = (int)((px >> 8) & 0xFF);
            if (r > 180 || g > 180 || b > 180) return true;
        }
    }
    return false;
}

static void test_rom_renders(const char* rom_path) {
    assert(load_conker_ui(rom_path));

    uint32_t toast[256 * 64]{};
    const char* cases[] = {
        "Achievement unlocked: Bad Fur Day",
        "Achievement unlocked: Things That Go Bump",
        "Achievement unlocked: Every Chapter",
        "Save state saved to slot 1",
    };
    for (int i = 0; i < 4; ++i) {
        int w = 0, h = 0;
        std::memset(toast, 0, sizeof(toast));
        assert(render_conker_toast_rgba8(cases[i], toast, w, h));
        assert(w == 212 && h == 40);
        assert(w <= 256 && h <= 64);
        assert((toast[0] & 0xFFu) == 0u);
        assert((toast[(h / 2) * w + (w / 2)] & 0xFFu) >= 240u);
        if (i < 3) {
            assert(has_bright_pixel(toast, w, 30, w - 6, 6, 16));
            assert(has_bright_pixel(toast, w, 30, w - 6, 22, 33));
        } else {
            assert(has_bright_pixel(toast, w, 10, w - 6, 14, 27));
        }
    }

    uint32_t load[3][128 * 32]{};
    int lw[3]{}, lh[3]{};
    for (int d = 1; d <= 3; ++d) {
        assert(render_conker_loading_rgba8(d, load[d - 1], lw[d - 1], lh[d - 1]));
        const int w = lw[d - 1], h = lh[d - 1];
        assert(w == 92 && h == 22);
        assert(w <= 128 && h <= 32);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int cx = x < 2 ? 2 - x : (x >= w - 2 ? x - (w - 3) : 0);
                const int cy = y < 2 ? 2 - y : (y >= h - 2 ? y - (h - 3) : 0);
                const uint32_t a = load[d - 1][y * w + x] & 0xFFu;
                assert(a == (cx + cy > 2 ? 0u : 255u));
            }
        }
        assert(has_bright_pixel(load[d - 1], w, 9, 65, 5, 16));
        assert(has_bright_pixel(load[d - 1], w, 66, w - 5, 5, 16));
    }

    // "Loading" word region (x < 66) must be pixel-identical across all dot frames.
    for (int y = 0; y < 22; ++y) {
        for (int x = 0; x < 66; ++x) {
            assert(load[0][y * 92 + x] == load[1][y * 92 + x]);
            assert(load[1][y * 92 + x] == load[2][y * 92 + x]);
        }
    }

    // Dots region (x >= 66) must differ across 1, 2, and 3 dots.
    auto dots_equal = [&](int a, int b) {
        for (int y = 0; y < 22; ++y) {
            for (int x = 66; x < 92; ++x) {
                if (load[a][y * 92 + x] != load[b][y * 92 + x]) return false;
            }
        }
        return true;
    };
    assert(!dots_equal(0, 1));
    assert(!dots_equal(1, 2));
    assert(!dots_equal(0, 2));
}

int main(int argc, char** argv) {
    test_state_machine();
    if (argc >= 2) {
        test_rom_renders(argv[1]);
    }
    std::puts("PASS loading indicator state machine, toast rendering, and animated loading frames");
    return 0;
}
