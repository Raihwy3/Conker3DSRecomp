// Native modifiers for Conker 3DS:
// - Skip Intro (upstream MIT adaptation)
// - Separate Audio Volume Sliders (Music / SFX; Voice is in conker_mp3_native.cpp)
// - Hold-to-Skip Any Cutscene
// - Quick Cheats
// - Photo Mode
#include "recomp.h"
#include "conker_options.h"
#include "rt64_3ds.h"
#include "conker_cheats.h"
#include "conker_note_release.h"
#include "librecomp/addresses.hpp"
#include <3ds.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" void func_1501C730(uint8_t*,recomp_context*);
extern "C" void func_1510B690(uint8_t*,recomp_context*);
extern "C" void func_1512C490(uint8_t*,recomp_context*);
extern "C" void conker_enter_photo_mode_ui();
extern "C" void conker_exit_photo_mode_ui();
extern "C" void conker_photo_camera(uint8_t*, recomp_context*);
namespace { conker_audio::NoteReleases note_releases; unsigned release_warnings=0; }
extern "C" void conker_capture_note_release(uint8_t* rdram,recomp_context* ctx) {
    if(!note_releases.capture(rdram,recomp::mem_size,uint32_t(ctx->r25),uint32_t(ctx->r8)) && release_warnings++<8)
        std::fprintf(stderr,"audio: invalid envelope at note start (voice=%08lx sound=%08lx)\n",
            (unsigned long)uint32_t(ctx->r25),(unsigned long)uint32_t(ctx->r8));
}
extern "C" void conker_get_note_release(uint8_t* rdram,recomp_context* ctx) {
    bool captured=false;
    ctx->r6=(gpr)(int32_t)note_releases.release(rdram,recomp::mem_size,uint32_t(ctx->r15),captured);
    ctx->r5=ADD32(ctx->r15,4);
    if(!captured && release_warnings++<8)
        std::fprintf(stderr,"audio: uncached note release (voice=%08lx time=%ld)\n",
            (unsigned long)uint32_t(ctx->r15),(long)(int32_t)ctx->r6);
}
namespace recomp3ds { u32 settings_menu_keys(); }

namespace {
bool bar_requested=false,bar_ready=false;
inline gpr addr(uint32_t a){return (gpr)(int32_t)a;}
inline float rd_f(uint8_t* rdram, uint32_t base, uint32_t off) {
    uint32_t u = (uint32_t)MEM_W(off, addr(base));
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}
inline void wr_f(uint8_t* rdram, uint32_t base, uint32_t off, float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    MEM_W(off, addr(base)) = (int32_t)u;
}

// Voice -> category table (1 = music/CSP, 2 = SFX)
struct VoiceTag { uint32_t voice; uint8_t cat; };
constexpr int kMaxVoices = 64;
VoiceTag g_voices[kMaxVoices]{};
int g_voice_next = 0;

uint8_t tag_for_ra(uint32_t ra) {
    // __n_CSPVoiceHandler (0x10013598..0x10014048) & __n_CSPHandleMIDIMsg (0x1001415C..0x10015044)
    return (ra >= 0x10013598u && ra < 0x10015050u) ? 1 : 2;
}
void set_voice_tag(uint32_t v, uint8_t cat) {
    for (int i = 0; i < kMaxVoices; i++) {
        if (g_voices[i].voice == v) { g_voices[i].cat = cat; return; }
    }
    g_voices[g_voice_next] = { v, cat };
    g_voice_next = (g_voice_next + 1) % kMaxVoices;
}
uint8_t get_voice_tag(uint32_t v, uint32_t ra) {
    for (int i = 0; i < kMaxVoices; i++) {
        if (g_voices[i].voice == v) return g_voices[i].cat;
    }
    return tag_for_ra(ra);
}
int32_t scale_vol(int32_t vol, uint8_t cat) {
    int pct = (cat == 1) ? conker3ds::get("audio_music_vol", 100)
                         : conker3ds::get("audio_sfx_vol", 100);
    if (pct >= 100) return vol;
    if (pct <= 0) return 0;
    int32_t s = (vol * pct) / 100;
    return std::clamp<int32_t>(s, 0, 32767);
}

int g_skip_hold_frames = 0;

struct PhotoState {
    bool active = false;
    float ex = 0, ey = 0, ez = 0;
    float yaw = 0, pitch = 0;
    uint32_t prev_held = 0;
} g_photo;

conker_cheats::State g_cheats;
}
extern "C" void conker_skip_intro_logos(uint8_t* rdram,recomp_context* ctx){
    // Scene loading must be established first. The PC mod also waits ten
    // native logo ticks: requests during its first two ticks can hang.
    if(bar_requested || !conker3ds::get("misc_skip_intro") ||
       MEM_W(0,addr(0x800E0A90))<10 || MEM_W(0,addr(0x800E0A90))>=0x349 ||
       MEM_W(0,addr(0x800BE9F0))!=0x25)return;
    bar_requested=true;
    MEM_B(0,addr(0x8008FE28))=2;MEM_B(0,addr(0x800E0B94))=1;MEM_B(0,addr(0x800D2E40))=0;
    recomp_context call=*ctx;call.r29=ctx->r29-0x40;
    call.r4=6;call.r5=0x1D;call.r6=0;call.r7=0;MEM_W(0x10,call.r29)=1;
    func_1501C730(rdram,&call);MEM_B(0,addr(0x800E0B96))=0xFF;
    std::fprintf(stderr,"modifier: skip intro requested bar\n");
}
extern "C" void conker_skip_intro_loaded(uint8_t* rdram,recomp_context*){
    // Complete the pending transition even if the option was switched off
    // during the load; leaving it half configured would strand the game.
    if(!bar_requested || bar_ready || MEM_W(0,addr(0x800BE9F0))!=0x1D)return;
    bar_ready=true;
    MEM_B(0,addr(0x800D2E40))=0;MEM_B(0,addr(0x800E0B94))=3;
    MEM_B(0,addr(0x8008FD80))=1;MEM_B(0,addr(0x8008FE28))=2;MEM_B(0,addr(0x8008FDA4))=0;
    const uint32_t state=uint32_t(MEM_W(0,addr(0x8008FDD4)));
    if(state>=0x80000000 && state<=0x807FFFC0){
        MEM_B(0x3E,addr(state))=0;MEM_B(0x2B,addr(state))=5;MEM_B(0x2C,addr(state))=5;
    }
    std::fprintf(stderr,"modifier: skip intro bar ready\n");
}

extern "C" void conker_audio_vol_start(uint8_t*, recomp_context* ctx) {
    const uint32_t v = (uint32_t)ctx->r5;
    const uint8_t cat = tag_for_ra((uint32_t)ctx->r31);
    set_voice_tag(v, cat);
    if (conker3ds::get("audio_music_vol", 100) >= 100 && conker3ds::get("audio_sfx_vol", 100) >= 100) return;
    const int16_t vol = (int16_t)(ctx->r7 & 0xFFFF);
    ctx->r7 = (gpr)scale_vol(vol, cat);
}

extern "C" void conker_audio_vol_set(uint8_t*, recomp_context* ctx) {
    if (conker3ds::get("audio_music_vol", 100) >= 100 && conker3ds::get("audio_sfx_vol", 100) >= 100) return;
    const uint32_t v = (uint32_t)ctx->r5;
    const uint8_t cat = get_voice_tag(v, (uint32_t)ctx->r31);
    const int16_t vol = (int16_t)(ctx->r6 & 0xFFFF);
    ctx->r6 = (gpr)scale_vol(vol, cat);
}

extern "C" void conker_cutscene_skip_tick(uint8_t* rdram, recomp_context*) {
    if (!conker3ds::get("misc_skip_cutscene", 0)) {
        g_skip_hold_frames = 0;
        return;
    }
    const bool playing = MEM_BU(0, addr(0x800C35EA)) == 1 || MEM_BU(1, addr(0x800C35EA)) == 1;
    if (!playing) {
        g_skip_hold_frames = 0;
        return;
    }
    const uint32_t held = hidKeysHeld();
    if (held & KEY_B) {
        if (++g_skip_hold_frames >= 15) {
            MEM_B(0, addr(0x800C35C5)) = 0;
            MEM_B(0, addr(0x800C35C4)) = 1;
            g_skip_hold_frames = 0;
        }
    } else {
        g_skip_hold_frames = 0;
    }
}

extern "C" int conker_frame_update_hook(uint8_t* rdram, recomp_context* ctx) {
    // Quick Cheats
    const uint32_t scene = (uint32_t)MEM_W(0, addr(0x800BE9F0));
    conker_cheats::Config cheat_cfg{};
    cheat_cfg.invincible      = conker3ds::get("misc_cheat_invincible", 0) != 0;
    cheat_cfg.inf_lives       = conker3ds::get("misc_cheat_inf_lives", 0) != 0;
    cheat_cfg.max_cash        = conker3ds::get("misc_cheat_max_cash", 0) != 0;
    cheat_cfg.unlock_chapters = conker3ds::get("misc_cheat_unlock_all", 0) != 0;
    g_cheats.apply(rdram, recomp::mem_size, cheat_cfg);

    // Photo Mode optional shortcut (default: D-Pad Right)
    if (scene > 0 && scene < 0x100) {
        static bool prev_photo_combo = false;
        static constexpr u32 kShortcutMasks[] = {
            0, KEY_DRIGHT, KEY_R | KEY_DRIGHT, KEY_L | KEY_DDOWN, KEY_SELECT
        };
        const int bind = conker3ds::get("misc_photo_shortcut", 1);
        const u32 mask = (bind > 0 && bind < 5) ? kShortcutMasks[bind] : 0;
        const u32 masked = recomp3ds::settings_menu_keys();
        const uint32_t held_now = hidKeysHeld() & ~masked;
        const bool combo_down = (mask != 0) && ((held_now & mask) == mask);
        if (combo_down && !prev_photo_combo) {
            if (conker3ds::get("misc_photo_mode", 0)) {
                g_photo.active = false;
                conker_exit_photo_mode_ui();
            } else {
                conker_enter_photo_mode_ui();
            }
        }
        prev_photo_combo = combo_down;
    }

    // Photo Mode world freeze + direct camera update + view matrix rebuild
    if (conker3ds::get("misc_photo_mode", 0) && scene > 0 && scene < 0x100) {
        const uint32_t cam = (uint32_t)MEM_W(0, addr(0x800DBFF0));
        if (cam >= 0x80000000u && cam <= 0x807FFFC0u) {
            conker_photo_camera(rdram, ctx);
            if (conker3ds::get("misc_photo_mode", 0)) {
                recomp_context cam_ctx = *ctx;
                cam_ctx.r4 = (gpr)(int32_t)cam;
                func_1512C490(rdram, &cam_ctx);
                recomp_context cull_ctx = *ctx;
                func_1510B690(rdram, &cull_ctx);
                ctx->r2 = 1;
                return 1;
            }
            return 0;
        }
    }
    g_photo.active = false;
    return 0;
}

extern "C" void conker_photo_camera(uint8_t* rdram, recomp_context*) {
    if (!conker3ds::get("misc_photo_mode", 0)) {
        g_photo.active = false;
        return;
    }
    const uint32_t cam = (uint32_t)MEM_W(0, addr(0x800DBFF0));
    if (cam < 0x80000000u || cam > 0x807FFFC0u) return;

    const u32 masked = recomp3ds::settings_menu_keys();
    const uint32_t held = hidKeysHeld() & ~masked;

    if (!g_photo.active) {
        g_photo.active = true;
        g_photo.ex = rd_f(rdram, cam, 0x2EC);
        g_photo.ey = rd_f(rdram, cam, 0x2F0);
        g_photo.ez = rd_f(rdram, cam, 0x2F4);
        const float dx = rd_f(rdram, cam, 0x2E0) - g_photo.ex;
        const float dy = rd_f(rdram, cam, 0x2E4) - g_photo.ey;
        const float dz = rd_f(rdram, cam, 0x2E8) - g_photo.ez;
        const float horiz = std::sqrt(dx * dx + dz * dz);
        g_photo.yaw = std::atan2(dz, dx);
        g_photo.pitch = std::clamp(std::atan2(dy, horiz > 1e-3f ? horiz : 1e-3f), -1.45f, 1.45f);
        g_photo.prev_held = held;
    }

    const uint32_t just = held & ~g_photo.prev_held;
    g_photo.prev_held = held;

    if (just & KEY_B) {
        g_photo.active = false;
        conker_exit_photo_mode_ui();
        return;
    }
    if (just & KEY_A) {
        rt64_3ds::request_screenshot();
    }

    circlePosition pad{}, cstick{};
    if (masked == 0) {
        hidCircleRead(&pad);
        irrstCstickRead(&cstick);
    }

    auto norm_stick = [](int16_t v) -> float {
        if (v > -20 && v < 20) return 0.0f;
        return std::clamp(v / 150.0f, -1.0f, 1.0f);
    };
    const float mx = norm_stick(pad.dx);
    const float my = norm_stick(pad.dy);
    float rx = norm_stick(cstick.dx);
    float ry = norm_stick(cstick.dy);
    if (held & KEY_DLEFT)  rx -= 0.8f;
    if (held & KEY_DRIGHT) rx += 0.8f;
    if (held & KEY_DUP)    ry += 0.8f;
    if (held & KEY_DDOWN)  ry -= 0.8f;

    g_photo.yaw += rx * 0.045f;
    g_photo.pitch = std::clamp(g_photo.pitch + ry * 0.045f, -1.45f, 1.45f);

    const float cp = std::cos(g_photo.pitch), sp = std::sin(g_photo.pitch);
    const float cy = std::cos(g_photo.yaw),   sy = std::sin(g_photo.yaw);
    const float fx = cp * cy, fy = sp, fz = cp * sy;
    const float sx = -sy,     sz = cy; // right vector in XZ

    const float speed = 12.0f;
    g_photo.ex += (fx * my + sx * mx) * speed;
    g_photo.ey += (fy * my) * speed;
    g_photo.ez += (fz * my + sz * mx) * speed;
    if (held & KEY_R) g_photo.ey += speed;
    if (held & KEY_L) g_photo.ey -= speed;

    const float tx = g_photo.ex + fx * 100.0f;
    const float ty = g_photo.ey + fy * 100.0f;
    const float tz = g_photo.ez + fz * 100.0f;
    wr_f(rdram, cam, 0x2EC, g_photo.ex);
    wr_f(rdram, cam, 0x2F0, g_photo.ey);
    wr_f(rdram, cam, 0x2F4, g_photo.ez);
    wr_f(rdram, cam, 0x2F8, g_photo.ex);
    wr_f(rdram, cam, 0x2FC, g_photo.ey);
    wr_f(rdram, cam, 0x300, g_photo.ez);
    wr_f(rdram, cam, 0x2E0, tx);
    wr_f(rdram, cam, 0x2E4, ty);
    wr_f(rdram, cam, 0x2E8, tz);
    wr_f(rdram, cam, 0x2BC, tx);
    wr_f(rdram, cam, 0x2C0, ty);
    wr_f(rdram, cam, 0x2C4, tz);
}
