#include "rt64_3ds_tev.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

C3D_TexEnv g_env[6]{};
int g_buf_update_rgb = 0;
int g_buf_update_a = 0;
uint32_t g_buf_color = 0;
int g_get_texenv_calls = 0;
int g_buf_update_calls = 0;
int g_buf_color_calls = 0;

void reset_mock_counters() {
    g_get_texenv_calls = 0;
    g_buf_update_calls = 0;
    g_buf_color_calls = 0;
}

struct Vec4 {
    float r, g, b, a;
};

float clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

Vec4 clamp01(Vec4 v) {
    return { clamp01(v.r), clamp01(v.g), clamp01(v.b), clamp01(v.a) };
}

Vec4 unpack_rgba(const uint8_t c[4]) {
    return { c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f };
}

Vec4 unpack_u32(uint32_t u) {
    return { (u & 0xFFu) / 255.0f, ((u >> 8) & 0xFFu) / 255.0f, ((u >> 16) & 0xFFu) / 255.0f, ((u >> 24) & 0xFFu) / 255.0f };
}

// Encodes G_SETCOMBINE words (cc_w0 = w0 & 0xFFFFFF, cc_w1 = w1).
struct CombineDesc {
    uint32_t a0, b0, c0, d0, Aa0, Ab0, Ac0, Ad0;
    uint32_t a1, b1, c1, d1, Aa1, Ab1, Ac1, Ad1;
};

void encode_combine(const CombineDesc& c, uint32_t& w0, uint32_t& w1) {
    w0 = ((c.a0 & 0xF) << 20) | ((c.c0 & 0x1F) << 15) | ((c.Aa0 & 7) << 12) | ((c.Ac0 & 7) << 9) |
         ((c.a1 & 0xF) << 5) | (c.c1 & 0x1F);
    w1 = ((c.b0 & 0xF) << 28) | ((c.b1 & 0xF) << 24) | ((c.Aa1 & 7) << 21) | ((c.Ac1 & 7) << 18) |
         ((c.d0 & 7) << 15) | ((c.Ab0 & 7) << 12) | ((c.Ad0 & 7) << 9) |
         ((c.d1 & 7) << 6) | ((c.Ab1 & 7) << 3) | (c.Ad1 & 7);
}

int func_args(uint16_t func) {
    switch (func) {
        case GPU_REPLACE: return 1;
        case GPU_MODULATE:
        case GPU_ADD:
        case GPU_ADD_SIGNED:
        case GPU_SUBTRACT:
        case GPU_DOT3_RGB:
        case GPU_DOT3_RGBA:
            return 2;
        case GPU_INTERPOLATE:
        case GPU_MULTIPLY_ADD:
        case GPU_ADD_MULTIPLY:
            return 3;
        default:
            assert(false && "unknown combine func");
            return 3;
    }
}

Vec4 eval_func(uint16_t func_rgb, uint16_t func_a, const Vec4 srgb[3], const float sa[3]) {
    auto eval_c = [](uint16_t f, float a, float b, float c) -> float {
        switch (f) {
            case GPU_REPLACE: return clamp01(a);
            case GPU_MODULATE: return clamp01(a * b);
            case GPU_ADD: return clamp01(a + b);
            case GPU_ADD_SIGNED: return clamp01(a + b - 0.5f);
            case GPU_INTERPOLATE: return clamp01(a * c + b * (1.0f - c));
            case GPU_SUBTRACT: return clamp01(a - b);
            case GPU_MULTIPLY_ADD: return clamp01(a * b + c);
            case GPU_ADD_MULTIPLY: return clamp01((a + b) * c);
            default: assert(false); return 0.0f;
        }
    };
    return {
        eval_c(func_rgb, srgb[0].r, srgb[1].r, srgb[2].r),
        eval_c(func_rgb, srgb[0].g, srgb[1].g, srgb[2].g),
        eval_c(func_rgb, srgb[0].b, srgb[1].b, srgb[2].b),
        eval_c(func_a, sa[0], sa[1], sa[2]),
    };
}

// Simulates the PICA200 6-stage TEV pipeline as configured by apply_tev.
Vec4 sim_pica(Vec4 tex0, Vec4 tex1, Vec4 shade, bool rtt_tex0_fmt4 = false) {
    auto read_src = [&](uint16_t src_id, Vec4 prev, Vec4 comb_buf, uint32_t stage_const) -> Vec4 {
        switch (src_id) {
            case GPU_PRIMARY_COLOR: return shade;
            case GPU_TEXTURE0: return rtt_tex0_fmt4 ? Vec4{ tex0.r, tex0.r, tex0.r, 1.0f } : tex0;
            case GPU_TEXTURE1: return tex1;
            case GPU_PREVIOUS_BUFFER:
                assert(comb_buf.r >= -1.0f && "Stage 0 read undefined GPU_PREVIOUS_BUFFER!");
                return comb_buf;
            case GPU_CONSTANT: return unpack_u32(stage_const);
            case GPU_PREVIOUS:
                assert(prev.r >= -1.0f && "Stage 0 read undefined GPU_PREVIOUS!");
                return prev;
            default:
                assert(false && "unexpected GPU_TEVSRC");
                return {};
        }
    };
    auto apply_rgb_op = [](Vec4 v, uint32_t op) -> Vec4 {
        switch (op) {
            case GPU_TEVOP_RGB_SRC_COLOR: return v;
            case GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR: return { 1.0f - v.r, 1.0f - v.g, 1.0f - v.b, 1.0f };
            case GPU_TEVOP_RGB_SRC_ALPHA: return { v.a, v.a, v.a, v.a };
            case GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA: return { 1.0f - v.a, 1.0f - v.a, 1.0f - v.a, 1.0f };
            case GPU_TEVOP_RGB_SRC_R: return { v.r, v.r, v.r, v.r };
            case GPU_TEVOP_RGB_ONE_MINUS_SRC_R: return { 1.0f - v.r, 1.0f - v.r, 1.0f - v.r, 1.0f };
            default: assert(false); return {};
        }
    };
    auto apply_a_op = [](Vec4 v, uint32_t op) -> float {
        switch (op) {
            case GPU_TEVOP_A_SRC_ALPHA: return v.a;
            case GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA: return 1.0f - v.a;
            case GPU_TEVOP_A_SRC_R: return v.r;
            case GPU_TEVOP_A_ONE_MINUS_SRC_R: return 1.0f - v.r;
            default: assert(false); return 0.0f;
        }
    };

    Vec4 prev{ -999.0f, -999.0f, -999.0f, -999.0f };
    Vec4 comb_buf{ -999.0f, -999.0f, -999.0f, -999.0f };
    Vec4 next_buf = unpack_u32(g_buf_color);

    for (int s = 0; s < 6; s++) {
        // GPUREG_TEXENV_UPDATE_BUFFER: bits 0..3 update next_buf when entering
        // stages 1..4 from the immediately preceding stage's output (prev).
        if (s >= 1 && s <= 4) {
            if (g_buf_update_rgb & (1u << (s - 1))) {
                next_buf.r = prev.r; next_buf.g = prev.g; next_buf.b = prev.b;
            }
            if (g_buf_update_a & (1u << (s - 1))) {
                next_buf.a = prev.a;
            }
        }
        const C3D_TexEnv& e = g_env[s];
        const int nrgb = func_args(e.funcRgb);
        const int na = func_args(e.funcAlpha);
        Vec4 srgb[3]{};
        float sa[3]{};
        for (int i = 0; i < nrgb; i++) {
            const uint16_t src = (e.srcRgb >> (i * 4)) & 0xF;
            const uint32_t op = (e.opRgb >> (i * 4)) & 0xF;
            srgb[i] = apply_rgb_op(read_src(src, prev, comb_buf, e.color), op);
        }
        for (int i = 0; i < na; i++) {
            const uint16_t src = (e.srcAlpha >> (i * 4)) & 0xF;
            const uint32_t op = (e.opAlpha >> (i * 4)) & 0xF;
            sa[i] = apply_a_op(read_src(src, prev, comb_buf, e.color), op);
        }
        prev = eval_func(e.funcRgb, e.funcAlpha, srgb, sa);
        comb_buf = next_buf;
    }
    return prev;
}

Vec4 eval_rdp(const rt64_3ds::DrawRecord& d, const CombineDesc& cc, Vec4 tex0, Vec4 tex1, Vec4 shade) {
    const Vec4 prim = unpack_rgba(d.prim);
    const Vec4 env = unpack_rgba(d.env);
    const Vec4 fog = unpack_rgba(d.fog);
    const Vec4 zero{ 0, 0, 0, 0 }, one{ 1, 1, 1, 1 };
    const float k5 = (d.convert_k5 > 255 ? 255 : d.convert_k5) / 255.0f;
    const Vec4 k5v{ k5, k5, k5, k5 };
    const uint32_t cyc = (d.othermode_h >> 20) & 3;
    if (rt64_3ds::lod_trilerp(d.cc_w0, d.cc_w1, d.othermode_h)) tex1 = tex0;

    auto rgb_a = [&](uint32_t v, Vec4 comb) -> Vec4 {
        switch (v) { case 0: return comb; case 1: return tex0; case 2: return tex1; case 3: return prim;
                     case 4: return shade; case 5: return env; case 6: return one; default: return zero; }
    };
    auto rgb_b = [&](uint32_t v, Vec4 comb) -> Vec4 {
        switch (v) { case 0: return comb; case 1: return tex0; case 2: return tex1; case 3: return prim;
                     case 4: return shade; case 5: return env; default: return zero; }
    };
    auto rgb_c = [&](uint32_t v, Vec4 comb) -> Vec4 {
        switch (v) { case 0: return comb; case 1: return tex0; case 2: return tex1; case 3: return prim;
                     case 4: return shade; case 5: return env;
                     case 7: return { comb.a, comb.a, comb.a, comb.a };
                     case 8: return { tex0.a, tex0.a, tex0.a, tex0.a };
                     case 9: return { tex1.a, tex1.a, tex1.a, tex1.a };
                     case 10: return { prim.a, prim.a, prim.a, prim.a };
                     case 11: return { shade.a, shade.a, shade.a, shade.a };
                     case 12: return { env.a, env.a, env.a, env.a };
                     case 15: return k5v;
                     default: return zero; }
    };
    auto rgb_d = [&](uint32_t v, Vec4 comb) -> Vec4 {
        switch (v) { case 0: return comb; case 1: return tex0; case 2: return tex1; case 3: return prim;
                     case 4: return shade; case 5: return env; case 6: return one; default: return zero; }
    };
    auto alp_abd = [&](uint32_t v, Vec4 comb) -> float {
        switch (v) { case 0: return comb.a; case 1: return tex0.a; case 2: return tex1.a; case 3: return prim.a;
                     case 4: return shade.a; case 5: return env.a; case 6: return 1.0f; default: return 0.0f; }
    };
    auto alp_c = [&](uint32_t v) -> float {
        switch (v) { case 1: return tex0.a; case 2: return tex1.a; case 3: return prim.a;
                     case 4: return shade.a; case 5: return env.a; default: return 0.0f; }
    };

    auto run_cycle = [&](uint32_t va, uint32_t vb, uint32_t vc, uint32_t vd,
                         uint32_t vAa, uint32_t vAb, uint32_t vAc, uint32_t vAd, Vec4 comb) -> Vec4 {
        Vec4 A = rgb_a(va, comb), B = rgb_b(vb, comb), C = rgb_c(vc, comb), D = rgb_d(vd, comb);
        float Aa = alp_abd(vAa, comb), Ab = alp_abd(vAb, comb), Ac = alp_c(vAc), Ad = alp_abd(vAd, comb);
        return clamp01(Vec4{
            (A.r - B.r) * C.r + D.r,
            (A.g - B.g) * C.g + D.g,
            (A.b - B.b) * C.b + D.b,
            (Aa - Ab) * Ac + Ad,
        });
    };

    Vec4 out = run_cycle(cc.a0, cc.b0, cc.c0, cc.d0, cc.Aa0, cc.Ab0, cc.Ac0, cc.Ad0, zero);
    if (cyc == 1) {
        out = run_cycle(cc.a1, cc.b1, cc.c1, cc.d1, cc.Aa1, cc.Ab1, cc.Ac1, cc.Ad1, out);
        if ((d.geometry_mode & 0x00010000) && ((d.othermode_l >> 30) & 3) == 3 && ((d.othermode_l >> 26) & 3) == 2) {
            out.r = clamp01(fog.r * shade.a + out.r * (1.0f - shade.a));
            out.g = clamp01(fog.g * shade.a + out.g * (1.0f - shade.a));
            out.b = clamp01(fog.b * shade.a + out.b * (1.0f - shade.a));
        }
    }
    return out;
}

bool close4(Vec4 a, Vec4 b, float tol = 0.012f) {
    return std::fabs(a.r - b.r) <= tol && std::fabs(a.g - b.g) <= tol &&
           std::fabs(a.b - b.b) <= tol && std::fabs(a.a - b.a) <= tol;
}

void run_case(const char* name, rt64_3ds::DrawRecord d, const CombineDesc& cc,
              Vec4 tex0, Vec4 tex1, Vec4 shade, bool rtt_tex0_fmt4 = false) {
    encode_combine(cc, d.cc_w0, d.cc_w1);
    rt64_3ds::TevPlan plan;
    rt64_3ds::plan_tev(d, plan);
    assert(plan.fallbacks == 0 && "unexpected fallback in plan_tev");
    rt64_3ds::apply_tev(plan);
    const Vec4 got = sim_pica(tex0, tex1, shade, rtt_tex0_fmt4);
    const Vec4 exp = eval_rdp(d, cc, tex0, tex1, shade);
    if (!close4(got, exp)) {
        std::fprintf(stderr, "FAIL [%s]: got=(%.3f, %.3f, %.3f, %.3f) exp=(%.3f, %.3f, %.3f, %.3f) stages=%d upd=(%x,%x)\n",
                     name, got.r, got.g, got.b, got.a, exp.r, exp.g, exp.b, exp.a,
                     plan.stages, plan.buf_update_rgb, plan.buf_update_a);
        std::abort();
    }
    std::printf("PASS %-46s stages=%d upd=(%x,%x) rgb=(%.2f,%.2f,%.2f,%.2f)\n",
                name, plan.stages, plan.buf_update_rgb, plan.buf_update_a, got.r, got.g, got.b, got.a);
}

rt64_3ds::DrawRecord make_2cyc(std::array<uint8_t, 4> prim, std::array<uint8_t, 4> env,
                               uint32_t omh = (1u << 20), uint32_t oml = 0x04D12078u, uint32_t gm = 0x200405u) {
    rt64_3ds::DrawRecord d{};
    d.othermode_h = omh;
    d.othermode_l = oml;
    d.geometry_mode = gm;
    for (int i = 0; i < 4; i++) { d.prim[i] = prim[i]; d.env[i] = env[i]; d.fog[i] = 0xFF; }
    d.tex[0].valid = true;
    d.tex[1].valid = true;
    return d;
}

}   // namespace

C3D_TexEnv* C3D_GetTexEnv(int id) {
    g_get_texenv_calls++;
    return &g_env[id];
}
void C3D_TexEnvBufUpdate(int mode, int mask) {
    g_buf_update_calls++;
    if (mode & C3D_RGB) g_buf_update_rgb = mask;
    if (mode & C3D_Alpha) g_buf_update_a = mask;
}
void C3D_TexEnvBufColor(u32 color) {
    g_buf_color_calls++;
    g_buf_color = color;
}

int main() {
    // CC IDs: COMB=0, TEX0=1, TEX1=2, PRIM=3, SHAD=4, ENV=5, 1=6, 0=31(A)/15(B)/31(C)/7(D)
    // Alpha ABD: CMBA=0, TX0A=1, TX1A=2, PRMA=3, SHDA=4, ENVA=5, 1=6, 0=7
    // Alpha C:   LODF=0, TX0A=1, TX1A=2, PRMA=3, SHDA=4, ENVA=5, PLOD=6, 0=7
    constexpr uint32_t Z_A = 15, Z_B = 15, Z_C = 31, Z_D = 7, AZ = 7, ACZ = 7;

    // 1. Conker mipmapped object combiner:
    //    C0: (TEX1-TEX0)*LODF+TEX0, A:(TX1A-TX0A)*LODF+TX0A
    //    C1: (SHAD-ENV)*COMB+PRIM,  A:(CMBA-0)*ENVA+0
    const CombineDesc c_mip = {
        2, 1, 13, 1,   2, 1, 0, 1,
        4, 5, 0, 3,    0, AZ, 5, AZ,
    };
    const uint32_t omh_mip = (1u << 20) | 0x10000u; // 2-cycle + G_TL_LOD
    const Vec4 hoodie{ 0.0f, 0.45f, 0.78f, 1.0f };
    const Vec4 fur{ 0.92f, 0.45f, 0.08f, 0.85f };
    const Vec4 lit{ 0.88f, 0.82f, 0.76f, 0.65f };

    run_case("mip above water (hoodie)", make_2cyc({0,0,0,0}, {0,0,0,255}, omh_mip), c_mip, hoodie, hoodie, lit);
    run_case("mip brown slurry underwater (hoodie)", make_2cyc({45,20,0,0}, {0,30,110,255}, omh_mip), c_mip, hoodie, hoodie, lit);
    run_case("mip brown slurry underwater (fur)", make_2cyc({45,20,0,0}, {0,30,110,255}, omh_mip), c_mip, fur, fur, lit);
    run_case("mip blue water underwater (hoodie)", make_2cyc({0,15,60,0}, {120,40,0,255}, omh_mip), c_mip, hoodie, hoodie, lit);
    run_case("mip semi-transparent underwater", make_2cyc({45,20,0,0}, {0,30,110,128}, omh_mip), c_mip, hoodie, hoodie, lit);
    {
        auto df = make_2cyc({45,20,0,0}, {0,30,110,255}, omh_mip, 0xC8102078u, 0x210405u);
        df.fog[0] = 80; df.fog[1] = 50; df.fog[2] = 20; df.fog[3] = 255;
        run_case("mip brown slurry + G_FOG", df, c_mip, fur, fur, lit);
    }

    // 2. Conker unmipmapped object combiner:
    //    C0: (0-0)*0+TEX0, A:(TX0A-0)*SHDA+0
    //    C1: (SHAD-ENV)*COMB+PRIM, A:(CMBA-0)*ENVA+0
    const CombineDesc c_unmip = {
        Z_A, Z_B, Z_C, 1,   1, AZ, 4, AZ,
        4, 5, 0, 3,         0, AZ, 5, AZ,
    };
    run_case("unmip above water", make_2cyc({0,0,0,0}, {0,0,0,255}), c_unmip, hoodie, fur, lit);
    run_case("unmip brown slurry underwater", make_2cyc({45,20,0,0}, {0,30,110,255}), c_unmip, hoodie, fur, lit);
    run_case("unmip semi-transparent underwater", make_2cyc({45,20,0,0}, {0,30,110,128}), c_unmip, fur, fur, lit);
    {
        auto df = make_2cyc({45,20,0,0}, {0,30,110,255}, (1u << 20), 0xC8102078u, 0x210405u);
        df.fog[0] = 75; df.fog[1] = 40; df.fog[2] = 15; df.fog[3] = 255;
        run_case("unmip brown slurry + G_FOG", df, c_unmip, hoodie, fur, lit);
    }

    // 3. Conker untextured object combiner:
    //    C0: (0-0)*0+0, A:(0-0)*0+SHDA
    //    C1: (SHAD-ENV)*K5+PRIM, A:(CMBA-0)*ENVA+0
    const CombineDesc c_untex = {
        Z_A, Z_B, Z_C, Z_D,   AZ, AZ, ACZ, 4,
        4, 5, 15, 3,          0, AZ, 5, AZ,
    };
    {
        auto du = make_2cyc({45,20,0,0}, {0,30,110,220});
        du.convert_k5 = 200;
        run_case("untextured underwater (K5)", du, c_untex, hoodie, fur, lit);
    }

    // 4. Conker table [2F] and [50] with (1 - B) * C + COMB:
    const CombineDesc c_2f = {
        1, Z_B, 5, Z_D,   AZ, AZ, ACZ, 1,
        6, 5, 3, 0,       AZ, AZ, ACZ, 0,
    };
    const CombineDesc c_50 = {
        1, Z_B, 3, Z_D,   1, AZ, 3, AZ,
        6, 1, 5, 0,       AZ, AZ, ACZ, 0,
    };
    run_case("conker table [2F] (1-ENV)*PRIM+COMB", make_2cyc({180,90,40,255}, {60,120,200,255}), c_2f, fur, hoodie, lit);
    run_case("conker table [50] (1-TEX0)*ENV+COMB", make_2cyc({180,90,40,255}, {60,120,200,255}), c_50, fur, hoodie, lit);

    // 5. Complex Cycle 0 + 2-op Cycle 1 combiners exercising buffer updates and delay_a:
    const Vec4 t0{ 0.72f, 0.55f, 0.35f, 0.80f };
    const Vec4 t1{ 0.50f, 0.82f, 0.60f, 0.65f };
    const Vec4 sh{ 0.90f, 0.75f, 0.60f, 0.70f };
    const CombineDesc c_save_c = {
        1, Z_B, 4, Z_D,   1, AZ, 4, AZ,
        3, 5, 0, 4,       0, AZ, 5, AZ,   // C1: (PRIM-ENV)*COMB+SHAD, A: CMBA*ENVA
    };
    run_case("save_rgb via c1 (COMB factor)", make_2cyc({230,200,180,255}, {60,40,30,220}), c_save_c, t0, t1, sh);

    const CombineDesc c_save_d = {
        1, Z_B, 4, Z_D,   1, AZ, 4, AZ,
        3, 5, 2, 0,       0, AZ, 5, AZ,   // C1: (PRIM-ENV)*TEX1+COMB, A: CMBA*ENVA
    };
    run_case("save_rgb via d1 (COMB addend)", make_2cyc({230,200,180,255}, {60,40,30,220}), c_save_d, t0, t1, sh);

    const CombineDesc c_shift_save = {
        3, 5, 1, 5,       3, 5, 1, 5,     // C0: (PRIM-ENV)*TEX0+ENV (stage[0] uses buf)
        4, 5, 0, 3,       0, AZ, 5, AZ,   // C1: (SHAD-ENV)*COMB+PRIM
    };
    run_case("reads_buffer(stage0) shift + save_rgb", make_2cyc({210,160,100,220}, {40,30,80,180}), c_shift_save, t0, t1, sh);

    const CombineDesc c_delay_a = {
        1, Z_B, 4, Z_D,   1, AZ, 4, AZ,
        3, 5, 7, 4,       3, AZ, 0, AZ,   // C1: (PRIM-ENV)*CMBA+SHAD, A: PRMA*CMBA (1-op A)
    };
    run_case("delay_a cross-channel hazard", make_2cyc({230,200,180,210}, {60,40,30,100}), c_delay_a, t0, t1, sh);

    const CombineDesc c_save_a = {
        1, Z_B, 4, Z_D,   1, AZ, 4, AZ,
        3, 5, 2, 4,       3, 5, 0, 4,     // C1: (PRIM-ENV)*TEX1+SHAD, A: (PRMA-ENVA)*CMBA+SHDA
    };
    run_case("save_a (2-op Cycle 1 alpha)", make_2cyc({230,200,180,220}, {60,40,30,80}), c_save_a, t0, t1, sh);

    const CombineDesc c_save_both = {
        1, Z_B, 4, Z_D,   1, AZ, 4, AZ,
        3, 5, 7, 4,       3, 5, 1, 0,     // C1: 2-op RGB reads CMBA + 2-op Alpha reads CMBA
    };
    run_case("save_a read by 2-op RGB & Alpha", make_2cyc({230,200,180,220}, {60,40,30,80}), c_save_both, t0, t1, sh);

    // 6. Intensity RTT combined with ONE_MINUS:
    const CombineDesc c_rtt_inv = {
        6, 1, 3, Z_D,     6, 1, 3, AZ,    // C0: (1-TEX0)*PRIM
        Z_A, Z_B, Z_C, 0, AZ, AZ, ACZ, 0, // C1: COMB
    };
    {
        auto dr = make_2cyc({180,140,100,210}, {0,0,0,255});
        dr.tex[0].valid = true;
        dr.tex[0].rtt = 0;
        dr.tex[0].fmt = 4;
        const Vec4 rtt_in{ 0.35f, 0.10f, 0.90f, 0.25f };
        const Vec4 rtt_rdp{ rtt_in.r, rtt_in.r, rtt_in.r, rtt_in.r };
        encode_combine(c_rtt_inv, dr.cc_w0, dr.cc_w1);
        rt64_3ds::TevPlan plan;
        rt64_3ds::plan_tev(dr, plan);
        assert(plan.fallbacks == 0);
        rt64_3ds::apply_tev(plan);
        const Vec4 got = sim_pica(rtt_in, t1, sh, true);
        const Vec4 exp = eval_rdp(dr, c_rtt_inv, rtt_rdp, t1, sh);
        assert(close4(got, exp));
        std::printf("PASS %-46s stages=%d upd=(%x,%x) rgb=(%.2f,%.2f,%.2f,%.2f)\n",
                    "intensity RTT (fmt=4) with ONE_MINUS", plan.stages, plan.buf_update_rgb, plan.buf_update_a,
                    got.r, got.g, got.b, got.a);
    }

    // 7. Delta apply_tev(plan, prev) sequence:
    {
        auto make_1cyc_prim = [&](std::array<uint8_t, 4> prim, uint32_t omh = 0) {
            rt64_3ds::DrawRecord d{};
            d.othermode_h = omh;
            d.othermode_l = 0x04D12078u;
            d.geometry_mode = 0x200405u;
            for (int i = 0; i < 4; i++) { d.prim[i] = prim[i]; d.env[i] = 255; d.fog[i] = 0xFF; }
            d.tex[0].valid = true;
            d.tex[1].valid = true;
            return d;
        };
        const CombineDesc c_1cyc_mod = {
            1, Z_B, 3, Z_D,   1, AZ, 3, AZ,
            Z_A, Z_B, Z_C, 0, AZ, AZ, ACZ, 0,
        };
        auto d1a = make_1cyc_prim({200, 180, 140, 220});
        encode_combine(c_1cyc_mod, d1a.cc_w0, d1a.cc_w1);
        rt64_3ds::TevPlan p1a;
        rt64_3ds::plan_tev(d1a, p1a);
        assert(p1a.stages == 1);

        reset_mock_counters();
        rt64_3ds::apply_tev(p1a, nullptr);
        assert(g_get_texenv_calls == 6 && g_buf_update_calls == 2 && g_buf_color_calls == 1);
        assert(close4(sim_pica(t0, t1, sh), eval_rdp(d1a, c_1cyc_mod, t0, t1, sh)));

        // Different prim color on stage 0 only -> 1 TEV stage update, 0 buf calls.
        auto d1b = make_1cyc_prim({120, 200, 90, 180});
        encode_combine(c_1cyc_mod, d1b.cc_w0, d1b.cc_w1);
        rt64_3ds::TevPlan p1b;
        rt64_3ds::plan_tev(d1b, p1b);
        assert(p1b.stages == 1);

        reset_mock_counters();
        rt64_3ds::apply_tev(p1b, &p1a);
        assert(g_get_texenv_calls == 1 && g_buf_update_calls == 0 && g_buf_color_calls == 0);
        assert(close4(sim_pica(t0, t1, sh), eval_rdp(d1b, c_1cyc_mod, t0, t1, sh)));

        // Transition to 4-stage save_rgb plan -> buf_update_rgb updated.
        auto duw = make_2cyc({210, 160, 100, 220}, {40, 30, 80, 180});
        encode_combine(c_shift_save, duw.cc_w0, duw.cc_w1);
        rt64_3ds::TevPlan puw;
        rt64_3ds::plan_tev(duw, puw);
        assert(puw.stages == 4 && puw.buf_update_rgb != 0);

        reset_mock_counters();
        rt64_3ds::apply_tev(puw, &p1b);
        assert(g_buf_update_rgb == puw.buf_update_rgb);
        assert(close4(sim_pica(t0, t1, sh), eval_rdp(duw, c_shift_save, t0, t1, sh)));

        // Transition back to 1-stage plan -> stages 0..3 touched (stages 1..3 reset, 4..5 untouched) and buf_update_rgb cleared.
        reset_mock_counters();
        rt64_3ds::apply_tev(p1b, &puw);
        assert(g_get_texenv_calls == 4 && g_buf_update_rgb == 0);
        assert(close4(sim_pica(t0, t1, sh), eval_rdp(d1b, c_1cyc_mod, t0, t1, sh)));

        // Same plan again -> 0 calls emitted.
        reset_mock_counters();
        rt64_3ds::apply_tev(p1b, &p1b);
        assert(g_get_texenv_calls == 0 && g_buf_update_calls == 0 && g_buf_color_calls == 0);
        std::puts("PASS delta apply_tev(plan, prev) stage & buffer diffing");
    }

    std::puts("ALL TEV COMBINER TESTS PASSED");
    return 0;
}
