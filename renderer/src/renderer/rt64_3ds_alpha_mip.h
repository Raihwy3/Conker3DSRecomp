#pragma once
#include <cstdint>

namespace rt64_3ds {
// One LA8 mip level, in place. Weight intensity by alpha so transparent
// texels do not change the visible colour of a narrow blade or particle.
inline void downsample_alpha_la8(uint16_t* pixels, uint32_t width, uint32_t height) {
    const uint32_t nw = width / 2;
    for (uint32_t y = 0; y < height / 2; y++) for (uint32_t x = 0; x < nw; x++) {
        const uint32_t at = y * 2 * width + x * 2;
        const uint16_t p[4] = { pixels[at], pixels[at + 1], pixels[at + width], pixels[at + width + 1] };
        uint32_t alpha = 0, weighted = 0, intensity = 0;
        for (uint16_t v : p) { alpha += v & 255; weighted += (v >> 8) * (v & 255); intensity += v >> 8; }
        const uint32_t i = alpha ? (weighted + alpha / 2) / alpha : (intensity + 2) / 4;
        pixels[y * nw + x] = (uint16_t)((i << 8) | ((alpha + 2) / 4));
    }
}
}
