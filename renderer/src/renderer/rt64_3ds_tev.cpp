// Combiner planner. Each RDP cycle computes (A - B) * C + D per channel; the
// PICA offers REPLACE, MODULATE, ADD, SUBTRACT, INTERPOLATE and
// MULTIPLY_ADD per stage with one constant colour per stage, plus one more
// the whole plan shares: the combiner buffer's colour, read as
// PREVIOUS_BUFFER while no stage updates the buffer. (PRIM - ENV) * TEXEL0
// + ENV, a common text and HUD combiner, needs both in one stage. Common
// forms map to one stage; the general form takes two (SUBTRACT then
// MULTIPLY_ADD on PREVIOUS). Sources the PICA lacks (noise, LOD fractions,
// key/K4 constants) become constants. The buffer only reads as its
// colour from the second stage on (the first stage's PREVIOUS_BUFFER is
// zero), so a plan whose first stage needs it starts one stage later.
#include "rt64_3ds_tev.h"

#include <cstring>

namespace rt64_3ds {

namespace {

enum Src : uint8_t { S_COMBINED, S_TEX0, S_TEX1, S_PRIM, S_SHADE, S_ENV, S_ONE, S_ZERO,
                     S_COMBINED_A, S_TEX0_A, S_TEX1_A, S_PRIM_A, S_SHADE_A, S_ENV_A,
                     S_LODFRAC, S_PRIMLODFRAC, S_HALF, S_K5, S_BUF, S_BUF_A };

Src map_rgb_a(uint32_t v) {   // A input
    switch (v) { case 0: return S_COMBINED; case 1: return S_TEX0; case 2: return S_TEX1; case 3: return S_PRIM; case 4: return S_SHADE;
                 case 5: return S_ENV; case 6: return S_ONE; case 7: return S_HALF; default: return S_ZERO; }
}
Src map_rgb_b(uint32_t v) {
    switch (v) { case 0: return S_COMBINED; case 1: return S_TEX0; case 2: return S_TEX1; case 3: return S_PRIM; case 4: return S_SHADE;
                 case 5: return S_ENV; case 6: return S_HALF; case 7: return S_HALF; default: return S_ZERO; }
}
Src map_rgb_c(uint32_t v) {
    switch (v) { case 0: return S_COMBINED; case 1: return S_TEX0; case 2: return S_TEX1; case 3: return S_PRIM; case 4: return S_SHADE;
                 case 5: return S_ENV; case 6: return S_HALF; case 7: return S_COMBINED_A; case 8: return S_TEX0_A; case 9: return S_TEX1_A;
                 case 10: return S_PRIM_A; case 11: return S_SHADE_A; case 12: return S_ENV_A; case 13: return S_LODFRAC;
                 case 14: return S_PRIMLODFRAC; case 15: return S_K5; default: return S_ZERO; }
}
Src map_rgb_d(uint32_t v) {
    switch (v) { case 0: return S_COMBINED; case 1: return S_TEX0; case 2: return S_TEX1; case 3: return S_PRIM; case 4: return S_SHADE;
                 case 5: return S_ENV; case 6: return S_ONE; default: return S_ZERO; }
}
Src map_a_abd(uint32_t v) {
    switch (v) { case 0: return S_COMBINED_A; case 1: return S_TEX0_A; case 2: return S_TEX1_A; case 3: return S_PRIM_A; case 4: return S_SHADE_A;
                 case 5: return S_ENV_A; case 6: return S_ONE; default: return S_ZERO; }
}
Src map_a_c(uint32_t v) {
    switch (v) { case 0: return S_LODFRAC; case 1: return S_TEX0_A; case 2: return S_TEX1_A; case 3: return S_PRIM_A; case 4: return S_SHADE_A;
                 case 5: return S_ENV_A; case 6: return S_PRIMLODFRAC; default: return S_ZERO; }
}

bool is_const(Src s) { return s == S_PRIM || s == S_ENV || s == S_ONE || s == S_ZERO || s == S_PRIM_A || s == S_ENV_A || s == S_LODFRAC || s == S_PRIMLODFRAC || s == S_HALF || s == S_K5; }

// Which constant colour a source needs (0 none, 1 prim, 2 env, 3 white, 4 black, 5 lod, 6 primlod, 7 half).
int const_id(Src s) {
    switch (s) { case S_PRIM: case S_PRIM_A: return 1; case S_ENV: case S_ENV_A: return 2; case S_ONE: return 3; case S_ZERO: return 4;
                 case S_LODFRAC: return 5; case S_PRIMLODFRAC: return 6; case S_HALF: return 7; case S_K5: return 8; default: return 0; }
}

struct Op {                       // one channel's operation on up to 3 sources
    GPU_COMBINEFUNC func;
    Src s[3];
    int n;
    bool inv[3] = { false, false, false };
};

// Plans one channel of one cycle into 1 or 2 ops.
int plan_channel(Src a, Src b, Src c, Src d, Op* ops) {
    if (c == S_ZERO || a == b) {                     // result = d
        ops[0] = { GPU_REPLACE, { d, d, d }, 1 };
        return 1;
    }
    if (b == S_ZERO) {
        if (c == S_ONE && d == S_ZERO) { ops[0] = { GPU_REPLACE, { a, a, a }, 1 }; return 1; }
        if (d == S_ZERO) { ops[0] = { GPU_MODULATE, { a, c, c }, 2 }; return 1; }
        if (c == S_ONE) { ops[0] = { GPU_ADD, { a, d, d }, 2 }; return 1; }
        ops[0] = { GPU_MULTIPLY_ADD, { a, c, d }, 3 };
        return 1;
    }
    if (d == b) {                                    // (a - b) * c + b = lerp(b, a, c)
        ops[0] = { GPU_INTERPOLATE, { a, b, c }, 3 };
        return 1;
    }
    if (a == S_ONE) {                                // (1 - b) * c + d via ONE_MINUS operand
        if (c == S_ONE && d == S_ZERO) { ops[0] = { GPU_REPLACE, { b, b, b }, 1, { true, false, false } }; return 1; }
        if (d == S_ZERO) { ops[0] = { GPU_MODULATE, { b, c, c }, 2, { true, false, false } }; return 1; }
        if (c == S_ONE) { ops[0] = { GPU_ADD, { b, d, d }, 2, { true, false, false } }; return 1; }
        ops[0] = { GPU_MULTIPLY_ADD, { b, c, d }, 3, { true, false, false } };
        return 1;
    }
    ops[0] = { GPU_SUBTRACT, { a, b, b }, 2 };
    if (d == S_ZERO) { ops[1] = { GPU_MODULATE, { S_COMBINED, c, c }, 2 }; }
    else { ops[1] = { GPU_MULTIPLY_ADD, { S_COMBINED, c, d }, 3 }; }
    return 2;
}

struct Consts { uint32_t prim, env, lod, primlod, k5; };

uint32_t const_value(int id, const Consts& k) {
    switch (id) { case 1: return k.prim; case 2: return k.env; case 3: return 0xFFFFFFFFu; case 4: return 0x00000000u;
                  case 5: return k.lod; case 6: return k.primlod; case 7: return 0x80808080u; case 8: return k.k5; default: return 0; }
}

// A primitive or environment input whose value is all zero or all one in
// the channels it is read as becomes the plain ZERO or ONE: fewer constants
// per stage (often none), and simpler ops.
Src fold_const(Src s, bool alpha_channel, const Consts& k) {
    uint32_t v;
    bool alpha_part;
    switch (s) {
        case S_K5: v = k.k5; alpha_part = false; break;
        case S_PRIM: v = k.prim; alpha_part = alpha_channel; break;
        case S_ENV: v = k.env; alpha_part = alpha_channel; break;
        case S_PRIM_A: v = k.prim; alpha_part = true; break;
        case S_ENV_A: v = k.env; alpha_part = true; break;
        default: return s;
    }
    const uint32_t part = alpha_part ? (v >> 24) : (v & 0x00FFFFFFu);
    if (part == 0) return S_ZERO;
    if (part == (alpha_part ? 0xFFu : 0x00FFFFFFu)) return S_ONE;
    return s;
}

GPU_TEVSRC tev_src(Src s, bool& alpha_operand) {
    alpha_operand = false;
    switch (s) {
        case S_COMBINED: return GPU_PREVIOUS;
        case S_COMBINED_A: alpha_operand = true; return GPU_PREVIOUS;
        case S_TEX0: return GPU_TEXTURE0;
        case S_TEX0_A: alpha_operand = true; return GPU_TEXTURE0;
        case S_TEX1: return GPU_TEXTURE1;
        case S_TEX1_A: alpha_operand = true; return GPU_TEXTURE1;
        case S_SHADE: return GPU_PRIMARY_COLOR;
        case S_SHADE_A: alpha_operand = true; return GPU_PRIMARY_COLOR;
        case S_BUF: return GPU_PREVIOUS_BUFFER;
        case S_BUF_A: alpha_operand = true; return GPU_PREVIOUS_BUFFER;
        case S_PRIM_A: case S_ENV_A: alpha_operand = true; return GPU_CONSTANT;
        default: return GPU_CONSTANT;
    }
}

// A constant colour as the stages read it: the RGB part (an RGB operand
// reading colour) and the alpha part (an alpha operand, or an RGB operand
// reading alpha) can hold different colours' parts.
struct ConstSlot {
    bool has_rgb = false, has_a = false;
    bool locked_rgb = false, locked_a = false;
    uint32_t rgb = 0;
    uint8_t a = 0;
    bool fits(bool alpha, uint32_t v) const {
        if (alpha) return !locked_a && (!has_a || a == (uint8_t)(v >> 24));
        return !locked_rgb && (!has_rgb || rgb == (v & 0x00FFFFFFu));
    }
    void take(bool alpha, uint32_t v) { if (alpha) { has_a = true; a = (uint8_t)(v >> 24); } else { has_rgb = true; rgb = v & 0x00FFFFFFu; } }
    void lock(bool alpha) { if (alpha) locked_a = true; else locked_rgb = true; }
    uint32_t value() const { return rgb | ((uint32_t)a << 24); }
};

// Fills a stage from an rgb op and an alpha op (either may be null = pass
// PREVIOUS through). Each constant goes to the stage's constant, else to
// the plan's buffer colour; only when both hold other colours is it
// counted as a fallback (and reads the stage constant).
void build_stage(TevStage& st, const Op* rgb, const Op* alpha, const Consts& k, ConstSlot& buf, int& fallbacks) {
    ConstSlot own;
    auto place = [&](Src s, bool alpha_part) -> GPU_TEVSRC {
        uint32_t v = const_value(const_id(s), k);
        if (own.fits(alpha_part, v)) { own.take(alpha_part, v); return GPU_CONSTANT; }
        if (buf.fits(alpha_part, v)) { buf.take(alpha_part, v); return GPU_PREVIOUS_BUFFER; }
        fallbacks++;
        return GPU_CONSTANT;
    };

    for (int i = 0; i < 3; i++) {
        st.src_rgb[i] = GPU_PREVIOUS; st.op_rgb[i] = GPU_TEVOP_RGB_SRC_COLOR;
        st.src_a[i] = GPU_PREVIOUS; st.op_a[i] = GPU_TEVOP_A_SRC_ALPHA;
    }
    if (rgb) {
        st.func_rgb = rgb->func;
        for (int i = 0; i < rgb->n; i++) {
            bool ao;
            st.src_rgb[i] = tev_src(rgb->s[i], ao);
            st.op_rgb[i] = ao ? (rgb->inv[i] ? GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA : GPU_TEVOP_RGB_SRC_ALPHA)
                              : (rgb->inv[i] ? GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR : GPU_TEVOP_RGB_SRC_COLOR);
            if (st.src_rgb[i] == GPU_CONSTANT) st.src_rgb[i] = place(rgb->s[i], ao);
        }
    }
    else st.func_rgb = GPU_REPLACE;
    if (alpha) {
        st.func_a = alpha->func;
        for (int i = 0; i < alpha->n; i++) {
            bool ao;
            st.src_a[i] = tev_src(alpha->s[i], ao);
            st.op_a[i] = alpha->inv[i] ? GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA : GPU_TEVOP_A_SRC_ALPHA;
            if (st.src_a[i] == GPU_CONSTANT) st.src_a[i] = place(alpha->s[i], true);
        }
    }
    else st.func_a = GPU_REPLACE;
    st.constant = own.value();
}

}   // namespace

static void plan_tev_raw(const DrawRecord& d, TevPlan& plan) {
    plan = TevPlan{};
    ConstSlot buf;
    Consts k;
    k.prim = (uint32_t)d.prim[0] | ((uint32_t)d.prim[1] << 8) | ((uint32_t)d.prim[2] << 16) | ((uint32_t)d.prim[3] << 24);
    k.env = (uint32_t)d.env[0] | ((uint32_t)d.env[1] << 8) | ((uint32_t)d.env[2] << 16) | ((uint32_t)d.env[3] << 24);
    const uint32_t k5 = d.convert_k5 > 255 ? 255 : d.convert_k5;
    k.k5 = k5 * 0x01010101u;
    k.lod = 0x00000000u;
    uint32_t pl = d.prim_lod_frac;
    k.primlod = pl | (pl << 8) | (pl << 16) | (pl << 24);

    uint32_t cyc = (d.othermode_h >> 20) & 3;
    if (cyc == 3) {
        // Fill mode: the fill colour (parked in prim). Rectangles in 1- and
        // 2-cycle mode go through the combiner like triangles.
        TevStage& st = plan.stage[plan.stages++];
        Op rgb = { GPU_REPLACE, { S_PRIM, S_PRIM, S_PRIM }, 1 }, a = { GPU_REPLACE, { S_PRIM_A, S_PRIM_A, S_PRIM_A }, 1 };
        build_stage(st, &rgb, &a, k, buf, plan.fallbacks);
        plan.combiner_stages = plan.stages;
        return;
    }
    if (cyc == 2) {   // copy: texel straight through
        TevStage& st = plan.stage[plan.stages++];
        Op rgb = { GPU_REPLACE, { S_TEX0, S_TEX0, S_TEX0 }, 1 }, a = { GPU_REPLACE, { S_TEX0_A, S_TEX0_A, S_TEX0_A }, 1 };
        build_stage(st, &rgb, &a, k, buf, plan.fallbacks);
        plan.combiner_stages = plan.stages;
        return;
    }

    uint32_t w0 = d.cc_w0, w1 = d.cc_w1;
    Src a0 = map_rgb_a((w0 >> 20) & 0xF), c0 = map_rgb_c((w0 >> 15) & 0x1F), Aa0 = map_a_abd((w0 >> 12) & 7), Ac0 = map_a_c((w0 >> 9) & 7);
    Src a1 = map_rgb_a((w0 >> 5) & 0xF), c1 = map_rgb_c(w0 & 0x1F);
    Src b0 = map_rgb_b((w1 >> 28) & 0xF), b1 = map_rgb_b((w1 >> 24) & 0xF);
    Src Aa1 = map_a_abd((w1 >> 21) & 7), Ac1 = map_a_c((w1 >> 18) & 7);
    Src d0 = map_rgb_d((w1 >> 15) & 7), Ab0 = map_a_abd((w1 >> 12) & 7), Ad0 = map_a_abd((w1 >> 9) & 7);
    Src d1 = map_rgb_d((w1 >> 6) & 7), Ab1 = map_a_abd((w1 >> 3) & 7), Ad1 = map_a_abd(w1 & 7);
    if (lod_trilerp(w0, w1, d.othermode_h)) {
        // The PICA blends the mip levels: TEXEL1 reads as TEXEL0, and the
        // lerp between them leaves TEXEL0.
        for (Src* s : { &a0, &b0, &c0, &d0, &Aa0, &Ab0, &Ac0, &Ad0 }) {
            if (*s == S_TEX1) *s = S_TEX0;
            else if (*s == S_TEX1_A) *s = S_TEX0_A;
        }
    }

    int cycles = (cyc == 1) ? 2 : 1;
    auto fr = [&](Src s) { return fold_const(s, false, k); };
    auto fa = [&](Src s) { return fold_const(s, true, k); };
    // In cycle 0 there is no PREVIOUS: a COMBINED input reads as zero on real
    // hardware; the PICA's stage-0 PREVIOUS is undefined, so fold it before
    // planning cycle 0.
    auto no_comb = [](Src s) { return (s == S_COMBINED || s == S_COMBINED_A) ? S_ZERO : s; };

    Op rgb0[2], alpha0[2];
    int nr0 = plan_channel(no_comb(fr(a0)), no_comb(fr(b0)), no_comb(fr(c0)), no_comb(fr(d0)), rgb0);
    int na0 = plan_channel(no_comb(fa(Aa0)), no_comb(fa(Ab0)), no_comb(fa(Ac0)), no_comb(fa(Ad0)), alpha0);

    if (cycles == 1) {
        int n0 = nr0 > na0 ? nr0 : na0;
        for (int i = 0; i < n0 && plan.stages < 6; i++) {
            TevStage& st = plan.stage[plan.stages++];
            const Op* r = i < nr0 ? &rgb0[i] : nullptr;
            const Op* a = i < na0 ? &alpha0[i] : nullptr;
            // When one channel finishes early its later stages pass PREVIOUS.
            build_stage(st, r, a, k, buf, plan.fallbacks);
        }
    }
    else {
        // Collapse trivial Cycle 0 pass-throughs (e.g. TEX0 or 0/1) directly
        // into Cycle 1 so Cycle 1 stages don't clobber or depend on Cycle 0.
        auto is_pass = [](int n, const Op* ops) {
            if (n != 1 || ops[0].func != GPU_REPLACE || ops[0].inv[0]) return false;
            const Src s = ops[0].s[0];
            return !is_const(s) || s == S_ZERO || s == S_ONE;
        };
        const bool pass_rgb = is_pass(nr0, rgb0);
        const bool pass_a = is_pass(na0, alpha0);
        auto sub_rgb = [&](Src s) {
            if (pass_rgb && s == S_COMBINED) return rgb0[0].s[0];
            if (pass_a && s == S_COMBINED_A) return alpha0[0].s[0];
            return s;
        };
        auto sub_a = [&](Src s) {
            return (pass_a && s == S_COMBINED_A) ? alpha0[0].s[0] : s;
        };

        Op rgb1[2], alpha1[2];
        int nr1 = plan_channel(fr(sub_rgb(a1)), fr(sub_rgb(b1)), fr(sub_rgb(c1)), fr(sub_rgb(d1)), rgb1);
        int na1 = plan_channel(fa(sub_a(Aa1)), fa(sub_a(Ab1)), fa(sub_a(Ac1)), fa(sub_a(Ad1)), alpha1);

        // Ops[1].s[0] in a 2-op cycle always reads ops[0] (GPU_PREVIOUS),
        // never Cycle 0's output; check Cycle 0 references from start_j.
        auto op_reads = [](const Op& op, int start_j, Src target) {
            for (int j = start_j; j < op.n; j++) if (op.s[j] == target) return true;
            return false;
        };
        const bool c0_rgb_live = op_reads(rgb1[0], 0, S_COMBINED) || (nr1 == 2 && op_reads(rgb1[1], 1, S_COMBINED));
        const bool c0_a_live = op_reads(rgb1[0], 0, S_COMBINED_A) || (nr1 == 2 && op_reads(rgb1[1], 1, S_COMBINED_A)) ||
                               op_reads(alpha1[0], 0, S_COMBINED_A) || (na1 == 2 && op_reads(alpha1[1], 1, S_COMBINED_A));
        if (!c0_rgb_live) nr0 = 0;
        if (!c0_a_live) na0 = 0;

        const int n0 = nr0 > na0 ? nr0 : na0;
        for (int i = 0; i < n0 && plan.stages < 6; i++) {
            TevStage& st = plan.stage[plan.stages++];
            Op pass_rgb_op = { GPU_REPLACE, { S_SHADE, S_SHADE, S_SHADE }, 1 };
            Op pass_a_op = { GPU_REPLACE, { S_SHADE_A, S_SHADE_A, S_SHADE_A }, 1 };
            const Op* r = i < nr0 ? &rgb0[i] : (i == 0 ? &pass_rgb_op : nullptr);
            const Op* a = i < na0 ? &alpha0[i] : (i == 0 ? &pass_a_op : nullptr);
            build_stage(st, r, a, k, buf, plan.fallbacks);
        }

        // Preserve Cycle 0's result across a 2-op Cycle 1 stage when Cycle 1's
        // first op overwrites GPU_PREVIOUS before its second op reads Cycle 0.
        const bool delay_a = (nr1 == 2 && na1 == 1 && op_reads(rgb1[1], 1, S_COMBINED_A));
        const bool save_rgb = (nr1 == 2 && op_reads(rgb1[1], 1, S_COMBINED));
        const bool save_a = (na1 == 2 && op_reads(alpha1[1], 1, S_COMBINED_A)) ||
                            (nr1 == 2 && na1 == 2 && op_reads(rgb1[1], 1, S_COMBINED_A));
        const int c0_end = plan.stages - 1;
        if (c0_end >= 0 && c0_end < 4) {
            if (save_rgb) {
                plan.buf_update_rgb |= (uint8_t)(1u << c0_end);
                for (int j = 1; j < rgb1[1].n; j++) if (rgb1[1].s[j] == S_COMBINED) rgb1[1].s[j] = S_BUF;
            }
            if (save_a) {
                plan.buf_update_a |= (uint8_t)(1u << c0_end);
                if (na1 == 2) {
                    for (int j = 1; j < alpha1[1].n; j++) if (alpha1[1].s[j] == S_COMBINED_A) alpha1[1].s[j] = S_BUF_A;
                }
                if (nr1 == 2) {
                    for (int j = 1; j < rgb1[1].n; j++) if (rgb1[1].s[j] == S_COMBINED_A) rgb1[1].s[j] = S_BUF_A;
                }
            }
        }

        const int n1 = nr1 > na1 ? nr1 : na1;
        for (int i = 0; i < n1 && plan.stages < 6; i++) {
            // Starting at stage c0_end + 2 (i == 1), updated buffer channels hold
            // Cycle 0's output instead of buffer_color.
            if (i == 1) {
                if (save_rgb) buf.lock(false);
                if (save_a) buf.lock(true);
            }
            TevStage& st = plan.stage[plan.stages++];
            const Op* r = i < nr1 ? &rgb1[i] : nullptr;
            const Op* a = delay_a ? (i == 1 ? &alpha1[0] : nullptr) : (i < na1 ? &alpha1[i] : nullptr);
            build_stage(st, r, a, k, buf, plan.fallbacks);
        }
    }

    // The first stage's PREVIOUS_BUFFER reads zero, not the buffer colour
    // (the buffer takes the colour one stage late): move the plan down a
    // stage behind one that does nothing the plan reads.
    auto reads_buffer = [](const TevStage& st) {
        for (int i = 0; i < 3; i++) if (st.src_rgb[i] == GPU_PREVIOUS_BUFFER || st.src_a[i] == GPU_PREVIOUS_BUFFER) return true;
        return false;
    };
    if (plan.stages > 0 && reads_buffer(plan.stage[0])) {
        if (plan.stages < 6) {
            for (int i = plan.stages; i > 0; i--) plan.stage[i] = plan.stage[i - 1];
            plan.stages++;
            TevStage& st = plan.stage[0];
            Op pass = { GPU_REPLACE, { S_SHADE, S_SHADE, S_SHADE }, 1 };
            Op pass_a = { GPU_REPLACE, { S_SHADE_A, S_SHADE_A, S_SHADE_A }, 1 };
            ConstSlot none;
            build_stage(st, &pass, &pass_a, k, none, plan.fallbacks);
            plan.buf_update_rgb = (uint8_t)((plan.buf_update_rgb << 1) & 0xFu);
            plan.buf_update_a = (uint8_t)((plan.buf_update_a << 1) & 0xFu);
        }
        else plan.fallbacks++;
    }

    plan.combiner_stages = plan.stages;

    // Fog: the blender mixes the fog colour by the shade alpha (fog factor).
    if ((d.geometry_mode & 0x00010000) && cycles == 2 && ((d.othermode_l >> 30) & 3) == 3 && ((d.othermode_l >> 26) & 3) == 2 && plan.stages < 6) {
        TevStage& st = plan.stage[plan.stages++];
        Op rgb = { GPU_INTERPOLATE, { S_PRIM, S_COMBINED, S_SHADE_A }, 3 };   // src0 * src2 + src1 * (1 - src2)
        Consts kf = k;
        kf.prim = (uint32_t)d.fog[0] | ((uint32_t)d.fog[1] << 8) | ((uint32_t)d.fog[2] << 16) | ((uint32_t)d.fog[3] << 24);
        build_stage(st, &rgb, nullptr, kf, buf, plan.fallbacks);
    }
    if (plan.stages == 0) {
        TevStage& st = plan.stage[plan.stages++];
        Op rgb = { GPU_REPLACE, { S_SHADE, S_SHADE, S_SHADE }, 1 }, a = { GPU_REPLACE, { S_SHADE_A, S_SHADE_A, S_SHADE_A }, 1 };
        build_stage(st, &rgb, &a, k, buf, plan.fallbacks);
    }

    // Blend factor: the PICA blends by the fragment's alpha, the blender
    // may use the fog colour's alpha (DK64's water and glass) or the shade
    // alpha instead. Put that factor in the output alpha when nothing
    // compares the combiner's alpha.
    const uint32_t l = d.othermode_l;
    const bool alpha_compared = (l & 3) != 0 || (l & 0x3000) != 0;
    if ((l & 0x4000) && !alpha_compared && plan.stages < 6) {
        auto sel = [&](int cy, int shift) { return (l >> (cy == 0 ? shift + 2 : shift)) & 3; };   // cycle 0 fields sit 2 bits higher
        auto is_blend_in_mem = [&](int cy) { return sel(cy, 28) == 0 && sel(cy, 20) == 1 && sel(cy, 16) == 0; };   // IN*A + MEM*(1-A)
        auto is_pass = [&](int cy) { return sel(cy, 28) == 0 && sel(cy, 24) == 3 && sel(cy, 20) == 0 && sel(cy, 16) == 2; };  // IN*0 + IN*1
        int src = -1;              // 0 fog alpha, 1 combiner alpha * fog alpha, 2 shade alpha
        int last = (cyc == 1) ? 1 : 0;
        if (is_blend_in_mem(last)) {
            uint32_t a = sel(last, 24);
            bool first_pass = (cyc != 1) || is_pass(0);
            bool first_in_a = (cyc == 1) && is_blend_in_mem(0) && sel(0, 24) == 0;
            if (a == 1 && first_pass) src = 0;
            else if (a == 1 && first_in_a) src = 1;
            else if (a == 2 && first_pass) src = 2;
        }
        // A constant blended over memory: P the fog or blend colour, M MEM,
        // B 1-A (Dinosaur Planet's fog haze, FOG * SHADE_A + MEM * (1 -
        // SHADE_A), drawn with a combiner that puts out white). The fragment
        // becomes that colour with the blender's A as its alpha; blended by
        // source alpha, the PICA gives the RDP's result. Without this the
        // combiner's white went on whole: white strips on the ground.
        auto is_const_in_mem = [&](int cy) { const uint32_t p = sel(cy, 28); return (p == 2 || p == 3) && sel(cy, 20) == 1 && sel(cy, 16) == 0; };
        const bool first_pass = (cyc != 1) || is_pass(0);
        if (src < 0 && is_const_in_mem(last) && first_pass && !(sel(last, 28) == 2 && sel(last, 24) == 1)) {
            const uint32_t p = sel(last, 28), a = sel(last, 24);
            TevStage& st = plan.stage[plan.stages++];
            Op rgb = { GPU_REPLACE, { S_PRIM, S_PRIM, S_PRIM }, 1 };
            Op op_a;
            if (a == 0) op_a = { GPU_REPLACE, { S_COMBINED_A, S_COMBINED_A, S_COMBINED_A }, 1 };      // the combiner's alpha
            else if (a == 1) op_a = { GPU_REPLACE, { S_PRIM_A, S_PRIM_A, S_PRIM_A }, 1 };        // the fog colour's
            else if (a == 2) op_a = { GPU_REPLACE, { S_SHADE_A, S_SHADE_A, S_SHADE_A }, 1 };     // shade (the fog factor under G_FOG)
            else op_a = { GPU_REPLACE, { S_ZERO, S_ZERO, S_ZERO }, 1 };
            Consts kf = k;   // the colour rides in the stage constant
            const uint8_t* c = p == 3 ? d.fog : d.blend;
            kf.prim = (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
            build_stage(st, &rgb, &op_a, kf, buf, plan.fallbacks);
        }
        else if (src >= 0) {
            TevStage& st = plan.stage[plan.stages++];
            Op rgb = { GPU_REPLACE, { S_COMBINED, S_COMBINED, S_COMBINED }, 1 };
            Op a;
            if (src == 0) a = { GPU_REPLACE, { S_PRIM_A, S_PRIM_A, S_PRIM_A }, 1 };
            else if (src == 1) a = { GPU_MODULATE, { S_COMBINED_A, S_PRIM_A, S_PRIM_A }, 2 };
            else a = { GPU_REPLACE, { S_SHADE_A, S_SHADE_A, S_SHADE_A }, 1 };
            Consts kf = k;   // the fog colour rides in the stage constant
            kf.prim = (uint32_t)d.fog[0] | ((uint32_t)d.fog[1] << 8) | ((uint32_t)d.fog[2] << 16) | ((uint32_t)d.fog[3] << 24);
            build_stage(st, &rgb, &a, kf, buf, plan.fallbacks);
        }
    }
    plan.buffer_color = buf.value();
}

// An N64 intensity render target supplies I to both colour and alpha.
// Our offscreen targets are RGBA8, where the silhouette's alpha can remain
// opaque even in its black background. Read intensity from red instead.
void plan_tev(const DrawRecord& d, TevPlan& plan) {
    plan_tev_raw(d, plan);
    for (int t = 0; t < 2; t++) {
        const TexDesc& tex = d.tex[t];
        if (!tex.valid || tex.rtt < 0 || tex.fmt != 4) continue;
        const GPU_TEVSRC src = t == 0 ? GPU_TEXTURE0 : GPU_TEXTURE1;
        for (int i = 0; i < plan.stages; i++) {
            TevStage& st = plan.stage[i];
            for (int k = 0; k < 3; k++) {
                if (st.src_rgb[k] == src) {
                    if (st.op_rgb[k] == GPU_TEVOP_RGB_SRC_ALPHA || st.op_rgb[k] == GPU_TEVOP_RGB_SRC_COLOR) st.op_rgb[k] = GPU_TEVOP_RGB_SRC_R;
                    else if (st.op_rgb[k] == GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA || st.op_rgb[k] == GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR) st.op_rgb[k] = GPU_TEVOP_RGB_ONE_MINUS_SRC_R;
                }
                if (st.src_a[k] == src) {
                    if (st.op_a[k] == GPU_TEVOP_A_SRC_ALPHA) st.op_a[k] = GPU_TEVOP_A_SRC_R;
                    else if (st.op_a[k] == GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA) st.op_a[k] = GPU_TEVOP_A_ONE_MINUS_SRC_R;
                }
            }
        }
    }
}

void apply_tev(const TevPlan& plan, const TevPlan* prev) {
    const bool full = (prev == nullptr || prev->stages < 0);
    // The buffer colour is the plan's second constant; updated stages latch
    // into it two stages later for PREVIOUS_BUFFER reads.
    if (full || plan.buf_update_rgb != prev->buf_update_rgb)
        C3D_TexEnvBufUpdate(C3D_RGB, plan.buf_update_rgb);
    if (full || plan.buf_update_a != prev->buf_update_a)
        C3D_TexEnvBufUpdate(C3D_Alpha, plan.buf_update_a);
    if (full || plan.buffer_color != prev->buffer_color)
        C3D_TexEnvBufColor(plan.buffer_color);
    for (int i = 0; i < 6; i++) {
        if (i < plan.stages) {
            const TevStage& st = plan.stage[i];
            if (!full && i < prev->stages && st == prev->stage[i]) continue;
            C3D_TexEnv* env = C3D_GetTexEnv(i);
            C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env, C3D_RGB, st.src_rgb[0], st.src_rgb[1], st.src_rgb[2]);
            C3D_TexEnvSrc(env, C3D_Alpha, st.src_a[0], st.src_a[1], st.src_a[2]);
            C3D_TexEnvOpRgb(env, st.op_rgb[0], st.op_rgb[1], st.op_rgb[2]);
            C3D_TexEnvOpAlpha(env, st.op_a[0], st.op_a[1], st.op_a[2]);
            C3D_TexEnvFunc(env, C3D_RGB, st.func_rgb);
            C3D_TexEnvFunc(env, C3D_Alpha, st.func_a);
            C3D_TexEnvColor(env, st.constant);
        }
        else if (full || i < prev->stages) {
            C3D_TexEnv* env = C3D_GetTexEnv(i);
            C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        }
    }
}

}   // namespace rt64_3ds
