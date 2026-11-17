// Mouse camera for keyboard and mouse players: a free orbit camera, called from hooks
// in conker.toml.
//
// RecompFrontend's Mouse Sensitivity option (on the Mouse & Gyro tab, look_aim.cpp). Above 0, the cursor is
// captured while the game is played (and released in the menus), and
// recompinput::get_mouse_deltas() gives the mouse's movement since the game last read
// its controllers, scaled by that sensitivity.
//
// The follow camera (struct108: gObjects[0].camera for player 1, D_800DBFF0 the one
// being played) looks at a point (+0x2BC) above its pivot at Conker's feet (+0x2A4),
// from an eye kept at a horizontal distance (+0x374) and height (+0x344) from the
// pivot. It doesn't keep its angle: every frame func_15125330 works it out (+0x37C)
// from where the eye is, and Conker's movement is relative to it. So once the mouse
// moves, the eye the camera wants (+0x2F8) is placed each frame from our own yaw and
// pitch around the look-at point, and the rest of the game follows. The camera stays
// where the mouse leaves it.
//
// Walls: the eye is placed as the game's camera collision (func_1512BB10) starts, the
// way the C-buttons' turning places it earlier in the same update (func_15122C5C). The
// collision moves the camera from where it was drawn last frame (+0x304) toward that eye
// and stops it at walls, sliding along them, and leaves the result in +0x2F8. The view
// (func_151284C4, in func_1512C490) then draws from there (+0x2EC). So the orbit stops at
// walls the way the game's own camera does.
//
// The orbit only runs where the C-buttons turn the camera (func_1512D390 ran this
// frame) and not in the look mode (func_15120158: hold R, aiming), so cutscenes, special
// cameras and aiming are the game's. Nor in the bank's slow motion leaps from a B pad, which
// keep this camera but where the mouse moves the crosshair instead (issue #85, look_aim.cpp). Turning with C-left or C-right (or the stick) alone
// hands the camera back to the game until the mouse moves again. Turning with both, the
// orbit takes the C-buttons' turn too (issue #60: the stick stopped turning the camera while
// the mouse, or gyro sent as the mouse, moved). The Mouse: Turn the Camera setting
// (look_aim.cpp) turns the orbit off.
//
// Right Stick: Free Camera (look_aim.cpp, issue #65) turns the orbit with player 1's right
// stick too, as a modern third-person camera: left and right turn it, up and down tilt it, at
// a rate (degrees per second) the stick's tilt sets. While the orbit could run last frame,
// the controller's input leaves the right stick off the C-buttons (frontend.cpp), so the
// game's own turning doesn't fight it. Elsewhere (R-Look, aiming, cutscenes, other cameras)
// the stick is the C-buttons as usual. The distance follows the game's own (C-Down zooms)
// until the scroll wheel sets one.
//
// Looking up, the eye swings down under the look-at point only as far as it can stay above
// Conker's feet and outside him. Tilted further, the eye stays there and the view aims up
// from it instead, to 80 degrees above the horizon (conker_mouse_camera_aim, issue #70):
// otherwise the view always looked at the look-at point, and beside anything tall its top
// stayed out of view.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <SDL.h>

#include "recomp.h"
#include "recompinput/input_state.h"

#include "conker.hpp"
#if defined(__arm__) && !defined(CONKER_RT64)
#include "recomp3ds_internal.h"
#include "rt64_3ds.h"
extern "C" bool conker_3ds_free_camera_enabled();
extern "C" bool conker_3ds_first_person_enabled();
extern "C" bool conker_3ds_shoulder_swap_held();
extern "C" bool conker_3ds_recenter_held();
extern "C" bool conker_3ds_photo_mode_active();
#endif

// The game's floor height under a point (game_13BB20.c), called by floor_below.
extern "C" void func_1510E8BC(uint8_t* rdram, recomp_context* ctx);
// The game's camera wall/floor collision (func_1512BB10), also used to probe shoulder clearance.
extern "C" void func_1512BB10(uint8_t* rdram, recomp_context* ctx);
// Angle helpers (func_1512523C, func_15125330) used to sync skybox pitch/yaw to the OTS view.
extern "C" void func_1512523C(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_15125330(uint8_t* rdram, recomp_context* ctx);

namespace {
    // Degrees per pixel of mouse movement at 100% sensitivity.
    constexpr float degrees_per_pixel = 0.2f;
    constexpr float degrees_to_radians = 3.14159265358979f / 180.0f;
    // How far the camera may look up or down: pitch is the eye's angle above the
    // look-at point. Below it, the eye comes in toward Conker's feet (see ground_clearance),
    // as far as body_clearance lets it.
    constexpr float min_pitch = -70.0f * degrees_to_radians;
    constexpr float max_pitch = 75.0f * degrees_to_radians;
    // Looking up (the eye below the look-at point), the camera comes in closer so the eye
    // stays this share of the look-at point's height above Conker's feet (the pivot), the
    // way modern third-person cameras slide in along the ground. Otherwise the eye went into
    // the floor, the collision pushed it back out each frame and the view shook (issue #70).
    constexpr float ground_clearance = 0.13f;
    // Coming in, the eye stays at least this far from the look-at point across the ground
    // (horizontally), outside Conker's body. With ground_clearance it sets how far the camera
    // can tilt to look up: past that, it couldn't stay both above his feet and outside him.
    constexpr float body_clearance = 90.0f;
    // Tilting up past where the eye stops (the lowest pitch), the view aims up from the eye, up to
    // this angle above the horizon (issue #70: to see the top of anything tall from beside it).
    constexpr float highest_view = 80.0f * degrees_to_radians;
    constexpr uint32_t current_camera = 0x800DBFF0; // D_800DBFF0
    // Scroll wheel zoom: each notch scales the distance by this, between the nearest and
    // farthest of the game's own camera distances (D_800A34B0: the controller's four,
    // each a horizontal distance and a height from the pivot, 530 x 400 the farthest).
    constexpr float zoom_step = 1.12f;
    constexpr uint32_t camera_distances = 0x800A34B0; // D_800A34B0, 4 x { horizontal, height }
    constexpr int camera_distance_count = 4;
    // When the camera is behind the eye the orbit wants (held by a wall, or freed after one),
    // it's sent this fraction of the way each frame, and a gap this many units larger than the
    // orbit's own move counts as behind. The collision only takes the C-buttons' camera a
    // little way each frame; one long move from where a wall held it jumped about.
    constexpr float catch_up = 0.3f;
    // Where the game pulls its own camera in (tight spots, and for a frame here and there as its
    // collision meets a wall), the orbit's distance eases in toward it by this share a frame, keeps
    // it tight_hold frames after the game lets go, then eases back out by tight_release a frame.
    // Following the game's distance as it was, the orbit jumped in and out every other frame (#70).
    constexpr float tight_ease = 0.35f;
    constexpr float tight_release = 0.12f;
    constexpr int tight_hold = 15;
    // Easing back out no faster than this a frame: from where a wall held it far in (the distance wanted is far
    // further), a share of the way sent the eye straight back into the wall, which caught it again (issue #70).
    constexpr float release_step = 6.0f;
    // When the game's wall collision stops the orbit's eye short of where it was placed, the orbit comes in to
    // where it was held (its distance from the look-at point) plus wall_margin, for tight_hold frames, as the
    // game's own camera does. Placed far into the wall instead (the distance it wants is beyond it), the eye was
    // stopped at one of two points along the wall in turn, each start giving the other, and the view shook
    // (issue #70: a wall behind Conker).
    constexpr float wall_margin = 10.0f;
    constexpr float wall_held_slack = 1.0f;
    // At rest against something (the collision held or lifted the eye last frame) with nothing the orbit goes by
    // changed since (the look-at point, the turn and tilt, the distance wanted), the eye stays where it was drawn:
    // moved from there to there, the collision has nothing to change. Its answers otherwise differ with where the
    // camera starts, and at a wall, or more so where walls and the floor meet, the view shook with nothing moving
    // (issue #70). It's kept until something changes.
    constexpr float rest_move = 0.5f;
    constexpr float rest_turn = 1e-4f;
    // The game's own distance (which the orbit's follows) wavers by a unit or two from frame to frame, still.
    constexpr float rest_distance = 5.0f;
    // Touching something (the collision held or lifted the eye), the eye drawn goes only this share of the way
    // from where it was last frame to where the collision left it. Walls, ledges and corners can each answer a
    // little differently from frame to frame however the eye is placed (the start it's moved from changes the
    // end): smoothed, an eye sent back and forth between two places moves a few units instead (issue #70). In
    // the open it's drawn where it's placed.
    constexpr float contact_smooth = 0.35f;
    // And for this many frames after, so an eye let go of (behind where it was placed, smoothed) doesn't jump the
    // rest of the way at once.
    constexpr int contact_after = 8;
    // When the game's collision meets the floor, it lifts the eye to 40 above it (func_1512BB10,
    // the floor from func_1510E8BC), straight up. Near the ground the orbit's eye then bobbed: lifted,
    // eased back down, lifted again; and so high a floor kept it from looking up (issue #70). For the
    // orbit's eye the lift is to this much above the floor instead, and the orbit places its eye
    // no lower than that above the floor under it, asked of the game each frame (floor_below), so
    // it doesn't sink into it.
    constexpr float game_floor_lift = 40.0f;
    constexpr float floor_clearance = 20.0f;
    // floor_below: what func_1510E8BC gives when it finds no floor (D_800A3690, its lowest).
    constexpr float no_floor = -9000.0f;
    constexpr float behind_slack = 8.0f;
    // The right stick at full tilt, at 100% Camera: Turning Speed: degrees per second turned and
    // tilted. Inside the deadzone it's still; past it, the tilt is rescaled from 0 and squared, for
    // fine control near the middle.
    constexpr float stick_yaw_speed = 150.0f;
    constexpr float stick_pitch_speed = 90.0f;
    constexpr float stick_deadzone = 0.2f;
    // A longer gap between two frames' camera updates (a pause, loading) counts as this long.
    constexpr float max_frame_seconds = 0.1f;

    // Set at the end of each camera update, read by the input thread (frontend.cpp).
    std::atomic<bool> stick_owns_camera = false;
    // Set at the end of each camera update: it was the normal camera.
    std::atomic<bool> was_normal_camera{false};

    // Scroll wheel notches since the view last read them (SDL event watch: the
    // frontend's own event loop consumes the events).
    std::atomic<int> wheel_notches = 0;
    int SDLCALL watch_wheel(void*, SDL_Event* event) {
        if (event->type == SDL_MOUSEWHEEL) {
            wheel_notches.fetch_add(event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y);
        }
        return 1;
    }

    struct Orbit {
        bool engaged = false;
        bool follow_camera_ran = false; // func_1512D390 ran since the last view
        bool look_mode_ran = false;     // func_15120158 (hold R, aiming) ran since the last view
        bool c_turning = false;         // C-left or C-right held when func_1512D390 ran
        bool turned = false;            // the mouse and wheel were read since the last view
        bool moved = false;             // and they moved
        bool has_target = false;        // target and next_target hold eyes the orbit wanted
        float target[3] = {};           // the eye the orbit wanted last frame
        float next_target[3] = {};      // this frame's, kept as target once the frame ends
        float yaw = 0.0f;               // radians, the eye's direction from the look-at point
        float pitch = 0.0f;
        float aim = 0.0f;               // radians the view aims up past the lowest pitch (0: at the look-at point)
        float wanted = 0.0f;            // the distance from the look-at point
        bool wheel_zoomed = false;      // the scroll wheel set it (else it follows the game's)
        bool has_stick_time = false;    // stick_time holds the last read of the stick
        float tight = 0.0f;             // the game's pulled-in distance kept (0: none)
        int tight_frames = 0;           // frames left to keep it
        float shown = 0.0f;             // the distance the orbit places the eye at, eased
        float wall = 0.0f;              // the distance a wall held the eye at (0: none)
        float floor = no_floor;         // the floor found under the eye this frame (floor_below)
        bool contact = false;           // the collision held or lifted the eye last frame
        int smooth_frames = 0;          // frames left to smooth the eye drawn (contact_smooth)
        bool resting = false;           // the eye is kept where it was drawn (rest_move)
        bool has_last = false;          // last_* hold last frame's look-at point, turn, tilt and distance wanted
        float last_look[3] = {}, last_yaw = 0.0f, last_pitch = 0.0f, last_wanted = 0.0f;
        int wall_frames = 0;            // frames left to keep to it
        bool placed = false;            // the orbit placed the eye this frame, at placed_eye
        float placed_eye[3] = {};
        float jump_tilt = 0.0f;
        float jump_lift = 0.0f;
        float jump_pull = 0.0f;
        float melee_pull = 0.0f;
        int melee_hold = 0;
        bool has_ots_pivot = false;
        float ots_pivot[3] = {};
        bool ots_left_shoulder = false;
        bool prev_swap_held = false;
        float ots_shoulder_x = 34.0f;
        float ots_shoulder_up = 24.0f;
        bool ots_probing = false;
        bool ots_probe_has_sample = false;
        int ots_probe_cooldown = 0;
        float ots_clear_side = 34.0f;
        float ots_clear_up = 24.0f;
        float ots_probe_x = 0.0f, ots_probe_y = 0.0f, ots_probe_z = 0.0f;
        float ots_probe_yaw = 0.0f, ots_probe_up = 24.0f;
        bool ots_probe_left = false;
        float smooth_stick_yaw = 0.0f;
        float smooth_stick_pitch = 0.0f;
        bool has_cstick_time = false;
        std::chrono::steady_clock::time_point last_cstick_time;
        bool has_ots_time = false;
        std::chrono::steady_clock::time_point last_ots_time;
        float ots_dt = 1.0f / 30.0f;
        bool prev_recenter_held = false;
        float recenter_timer = 0.0f;
        float walk_time = 0.0f;
        float stop_grace = 0.0f;
        bool walk_zoomed_out = false;
        float walk_pull = 0.0f;
        bool has_char_pos = false;
        float prev_char_x = 0.0f;
        float prev_char_y = 0.0f;
        float prev_char_z = 0.0f;
        float peak_air = 0.0f;
        float land_dip = 0.0f;
        float lead_x = 0.0f;
        float lead_z = 0.0f;
        float idle_time = 0.0f;
        float idle_pull = 0.0f;
        float slope_pitch = 0.0f;
        std::chrono::steady_clock::time_point stick_time;
    } orbit;

    float read_float(uint8_t* rdram, gpr base, int32_t offset);
    void write_float(uint8_t* rdram, gpr base, int32_t offset, float value);

    // The floor's height below (x, from_y, z), or no_floor: func_1510E8BC as func_1512BB10 calls it
    // to lift its eye off the floor ($a2 the result's address, the point and the camera's last eye
    // height on the stack). Called from a hook in func_1512BB10 (ctx its context), with a stack of
    // its own below that function's.
    float floor_below(uint8_t* rdram, recomp_context* ctx, gpr camera, float x, float from_y, float z) {
        recomp_context call = *ctx;
        call.r29 = ADD32(ctx->r29, -0x80);
        const gpr sp = call.r29;
        const gpr result = ADD32(sp, 0x40);
        write_float(rdram, result, 0, -10000.0f);
        MEM_W(0x10, sp) = 0;
        MEM_W(0x14, sp) = 0;
        write_float(rdram, sp, 0x18, x);
        write_float(rdram, sp, 0x1C, from_y);
        write_float(rdram, sp, 0x20, z);
        MEM_W(0x24, sp) = MEM_W(0x308, camera);
        MEM_W(0x28, sp) = 0;
        MEM_W(0x2C, sp) = 0;
        write_float(rdram, sp, 0x30, -10000.0f);
        write_float(rdram, sp, 0x34, from_y);
        MEM_W(0x38, sp) = 1;
        call.r4 = 0;
        call.r5 = 0;
        call.r6 = result;
        call.r7 = 0;
        func_1510E8BC(rdram, &call);
        return read_float(rdram, result, 0);
    }

    float read_float(uint8_t* rdram, gpr base, int32_t offset) {
        uint32_t word = (uint32_t)MEM_W(offset, base);
        float value;
        std::memcpy(&value, &word, sizeof(value));
        return value;
    }

    void write_float(uint8_t* rdram, gpr base, int32_t offset, float value) {
        uint32_t word;
        std::memcpy(&word, &value, sizeof(word));
        MEM_W(offset, base) = (int32_t)word;
    }

    // The yaw of the eye at camera + offset around the look-at point (cx, cz), as orbit.yaw.
    float eye_yaw(uint8_t* rdram, gpr camera, int32_t offset, float cx, float cz) {
        return std::atan2(read_float(rdram, camera, offset + 8) - cz, read_float(rdram, camera, offset) - cx);
    }

    bool mouse_turns_camera() {
#if defined(CONKER_RT64)
        return conker::look_aim::mouse_turns_camera();
#elif defined(__arm__)
        return false; // the 3DS uses stick/touch axes, not a desktop mouse
#else
        return true;
#endif
    }

    bool stick_turns_camera_setting() {
#if defined(CONKER_RT64)
        return conker::look_aim::stick_free_camera();
#elif defined(__arm__)
        return conker_3ds_free_camera_enabled();
#else
        return false;
#endif
    }

    bool first_person_setting() {
#if defined(__arm__) && !defined(CONKER_RT64)
        return conker_3ds_first_person_enabled();
#else
        return false;
#endif
    }

    bool cutscene_or_intro_now(uint8_t* rdram) {
        // Active cutscene slot (D_800C35EA[0..1], same as cutscene_aspect.cpp & model_detail.cpp)
        // plus pre-play logos (0x25), chainsaw opening (0x21), and pub startup/menu (0x1D).
        if (MEM_BU(0, (gpr)(int32_t)0x800C35EA) != 0 || MEM_BU(1, (gpr)(int32_t)0x800C35EA) != 0) {
            return true;
        }
        const uint32_t scene = (uint32_t)MEM_W(0, (gpr)(int32_t)0x800BE9F0);
        return scene == 0x25 || scene == 0x21 || scene == 0x1D;
    }

    bool shoulder_swap_held() {
#if defined(__arm__) && !defined(CONKER_RT64)
        return conker_3ds_shoulder_swap_held();
#else
        return false;
#endif
    }

    bool recenter_held() {
#if defined(__arm__) && !defined(CONKER_RT64)
        return conker_3ds_recenter_held();
#else
        return false;
#endif
    }

    bool photo_mode_active() {
#if defined(__arm__) && !defined(CONKER_RT64)
        return conker_3ds_photo_mode_active();
#else
        return false;
#endif
    }

    // Convert a 30fps per-frame lerp factor k to a frame-rate-independent factor for dt seconds.
    float ots_alpha(float k30, float dt) {
        return 1.0f - std::pow(1.0f - k30, std::clamp(dt * 30.0f, 0.05f, 3.0f));
    }

    // The orbit's turn (yaw) and tilt (pitch) from player 1's right stick since the last read, in
    // radians: its tilt past the deadzone, at Camera: Turning Speed and Invert Turning, for the time
    // since the last read. Right turns the way the mouse moving right does; up tilts to look up.
    void read_stick(float& yaw, float& pitch) {
        yaw = 0.0f;
        pitch = 0.0f;
        const auto now = std::chrono::steady_clock::now();
        const float seconds = orbit.has_stick_time ?
            std::min(std::chrono::duration<float>(now - orbit.stick_time).count(), max_frame_seconds) : 0.0f;
        orbit.stick_time = now;
        orbit.has_stick_time = true;
#if defined(CONKER_RT64)
        std::array<SDL_GameController*, 4> controllers{};
        if (conker::frontend::port_controllers(controllers) < 1 || controllers[0] == nullptr) {
            return;
        }
        const float x = SDL_GameControllerGetAxis(controllers[0], SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
        const float y = SDL_GameControllerGetAxis(controllers[0], SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
        const float tilt = std::sqrt(x * x + y * y);
        if (tilt <= stick_deadzone) {
            return;
        }
        const float scaled = std::min((tilt - stick_deadzone) / (1.0f - stick_deadzone), 1.0f);
        const float strength = scaled * scaled / tilt;
        bool invert_x = false, invert_y = false;
        conker::look_aim::free_camera_invert(invert_x, invert_y);
        const float speed = conker::look_aim::camera_turn_speed() * seconds * degrees_to_radians;
        yaw = x * strength * stick_yaw_speed * speed * (invert_x ? -1.0f : 1.0f);
        pitch = y * strength * stick_pitch_speed * speed * (invert_y ? -1.0f : 1.0f);
#elif defined(__arm__)
        float x=0,y=0;
        recomp3ds::input_get_right_stick(&x,&y);
        // The platform already applies a radial deadzone. Up is positive
        // here; the PC camera takes SDL's positive-down pitch convention.
        const float raw_mag = std::min(1.0f, std::hypot(x, y));
        const bool ots = first_person_setting();
        const float strength = ots
            ? (raw_mag > 0.0f ? std::pow(raw_mag, 0.65f) : 0.0f)
            : raw_mag;
        bool ix=false,iy=false;conker::look_aim::free_camera_invert(ix,iy);
        const float speed=conker::look_aim::camera_turn_speed();
        float vx = x * strength * stick_yaw_speed * degrees_to_radians * speed * (ix ? -1.0f : 1.0f);
        float vy = -y * strength * stick_pitch_speed * degrees_to_radians * speed * (iy ? -1.0f : 1.0f);
        if (ots) {
            const float alpha = (raw_mag > 0.01f) ? 0.45f : 0.60f;
            orbit.smooth_stick_yaw += (vx - orbit.smooth_stick_yaw) * alpha;
            orbit.smooth_stick_pitch += (vy - orbit.smooth_stick_pitch) * alpha;
            vx = (raw_mag == 0.0f && std::fabs(orbit.smooth_stick_yaw) < 1e-4f) ? 0.0f : orbit.smooth_stick_yaw;
            vy = (raw_mag == 0.0f && std::fabs(orbit.smooth_stick_pitch) < 1e-4f) ? 0.0f : orbit.smooth_stick_pitch;
        }
        yaw = vx * seconds;
        pitch = vy * seconds;
#endif
    }
}

bool conker::mouse_camera::stick_turns_camera() {
    return stick_owns_camera.load(std::memory_order_relaxed);
}

bool conker::mouse_camera::normal_camera() {
    return was_normal_camera;
}

bool conker::mouse_camera::first_person_active() {
    return first_person_setting() && orbit.engaged;
}

namespace {
    // The bank's slow motion leaps (issue #85, look_aim.cpp): the camera follows from behind Conker and
    // 115 units above him (logged: about 295 behind, 21 degrees down), so seen straight from behind he
    // stands in the middle of the view, over what he's aiming at. In a leap the eye the camera wants is
    // raised by leap_lift_height, eased in as the leap starts and out as it ends (leap_lift_ease of the
    // way a camera update), so the view looks down past him.
    //
    // Only the view is raised: its eye (+0x2EC) as it's copied from the eye the camera wants
    // (conker_leap_view_lift, in func_1512C490), and lowered back as the next update starts
    // (conker_leap_view_restore, func_15122C5C), before that copies the view's eye back into the eye wanted
    // (+0x2F8) for the update to start from. The game's camera never sees it. Tried first: the eye wanted
    // raised, whole every frame, stacked up from frame to frame (logged: the camera climbed to the ceiling,
    // 70 degrees down); its change alone, the game eased it back within half a second; the height the
    // camera keeps above his feet (+0x344) raised, in a leap the game doesn't go by it (no higher at all).
    constexpr float leap_lift_height = 110.0f;
    constexpr float leap_lift_ease = 0.08f;
    float leap_lift = 0.0f;
    // What the view was raised by, and which camera's, to lower it back.
    float leap_lift_applied = 0.0f;
    gpr leap_lift_camera = 0;
}

// func_1512D390 (the C-buttons' turning), before its last restore: $s0 is the camera.
// Marks that the follow camera is running this frame, and whether C-left or C-right is held
// (+0x36C points at the buttons held).
extern "C" void conker_mouse_camera_follow(uint8_t* rdram, recomp_context* ctx) {
    const gpr camera = ctx->r16;
    if ((uint32_t)camera != (uint32_t)MEM_W(0, (gpr)(int32_t)current_camera)) {
        return;
    }
    orbit.follow_camera_ran = true;
    const gpr buttons = (gpr)(int32_t)MEM_W(0x36C, camera);
    orbit.c_turning = ((uint32_t)MEM_HU(0, buttons) & 0x3) != 0;
}

// func_15120158 (the look mode: hold R, and aiming such as the slingshot on a B pad), after
// its first instruction. The mouse aims there (look_aim.cpp), so the orbit leaves the
// camera to it: otherwise both turned with the mouse, and the view ran ahead of the aim.
extern "C" void conker_mouse_camera_look_mode(uint8_t* rdram, recomp_context* ctx) {
    orbit.look_mode_ran = true;
}

// func_1512BB10 (the camera's collision), after its first instruction: $a0 is the camera.
// Places the eye the orbit wants, for the collision to move the camera toward. The game
// calls it a second time in some frames (camera +0x23C set): the mouse and wheel are read
// only the first time, and the second places the same eye. The C-buttons' turning has
// placed the eye the game wants by now (+0x2F8), turned from last frame's (+0x304).
extern "C" void conker_mouse_camera_collide(uint8_t* rdram, recomp_context* ctx) {
    if (orbit.ots_probing) {
        return;
    }
    const gpr camera = ctx->r4;
    if ((uint32_t)camera != (uint32_t)MEM_W(0, (gpr)(int32_t)current_camera)) {
        return;
    }
    const bool use_mouse = mouse_turns_camera();
    const bool use_stick = stick_turns_camera_setting();
    const bool fp = first_person_setting();
    // Nor in the bank's slow motion leaps, where the mouse moves the crosshair instead (issue #85, look_aim.cpp),
    // nor during cutscenes or the startup pub/intro sequence when Over shoulder is enabled.
    if (!orbit.follow_camera_ran || orbit.look_mode_ran || conker::look_aim::leaping_now() ||
        photo_mode_active() || (fp && cutscene_or_intro_now(rdram)) || (!use_mouse && !use_stick)) {
        orbit.engaged = false;
        orbit.aim = 0.0f;
        orbit.resting = false;
        orbit.has_last = false;
        orbit.smooth_frames = 0;
        orbit.has_stick_time = false;
        orbit.has_ots_time = false;
        orbit.jump_tilt = 0.0f;
        orbit.jump_lift = 0.0f;
        orbit.jump_pull = 0.0f;
        orbit.melee_pull = 0.0f;
        orbit.melee_hold = 0;
        orbit.has_ots_pivot = false;
        orbit.ots_shoulder_x = orbit.ots_left_shoulder ? -34.0f : 34.0f;
        orbit.ots_shoulder_up = 24.0f;
        orbit.smooth_stick_yaw = 0.0f;
        orbit.smooth_stick_pitch = 0.0f;
        orbit.has_cstick_time = false;
        wheel_notches = 0;
        return;
    }

    float mouse_x = 0.0f, mouse_y = 0.0f;
    float stick_yaw = 0.0f, stick_pitch = 0.0f;
    int notches = 0;
    const bool first = !orbit.turned;
    if (first) {
        if (use_mouse) {
            recompinput::get_mouse_deltas(&mouse_x, &mouse_y);
            notches = wheel_notches.exchange(0);
        }
        else {
            wheel_notches = 0;
        }
        if (use_stick) {
            read_stick(stick_yaw, stick_pitch);
        }
        orbit.turned = true;
        orbit.moved = mouse_x != 0.0f || mouse_y != 0.0f || notches != 0 || stick_yaw != 0.0f || stick_pitch != 0.0f;
        orbit.wheel_zoomed |= notches != 0;
    }
    // The C-buttons alone turn the game's camera (unless in Over shoulder).
    if (!fp && orbit.c_turning && !orbit.moved) {
        orbit.engaged = false;
        orbit.jump_tilt = 0.0f;
        orbit.has_cstick_time = false;
        orbit.aim = 0.0f;
        return;
    }

    const float cx = read_float(rdram, camera, 0x2BC);
    const float cy = read_float(rdram, camera, 0x2C0);
    const float cz = read_float(rdram, camera, 0x2C4);
    // The distance the game keeps: its eye's horizontal distance and height from the
    // pivot, measured from the look-at point.
    const float horizontal = read_float(rdram, camera, 0x374);
    const float height = read_float(rdram, camera, 0x344) - (cy - read_float(rdram, camera, 0x2A8));
    const float wanted_distance = std::sqrt(horizontal * horizontal + height * height);

    if (!orbit.engaged) {
        if (!orbit.moved && !fp) {
            return;
        }
        // Take over from where the game's camera was drawn.
        const float ex = read_float(rdram, camera, 0x2EC) - cx;
        const float ey = read_float(rdram, camera, 0x2F0) - cy;
        const float ez = read_float(rdram, camera, 0x2F4) - cz;
        orbit.shown = std::sqrt(ex * ex + ey * ey + ez * ez);
        orbit.tight = 0.0f;
        orbit.tight_frames = 0;
        orbit.wall = 0.0f;
        orbit.wall_frames = 0;
        orbit.yaw = std::atan2(ez, ex);
        orbit.pitch = fp ? (8.0f * degrees_to_radians) : std::atan2(ey, std::sqrt(ex * ex + ez * ez));
        if (orbit.wanted == 0.0f) {
            orbit.wanted = wanted_distance;
        }
        orbit.engaged = true;
    }

    // Until the scroll wheel sets a distance, the orbit keeps the game's, so C-Down zooms it.
    if (fp) {
        if (first) {
            const bool swap_held = shoulder_swap_held();
            if (swap_held && !orbit.prev_swap_held) {
                orbit.ots_left_shoulder = !orbit.ots_left_shoulder;
            }
            orbit.prev_swap_held = swap_held;

            const auto now = std::chrono::steady_clock::now();
            float dt = 1.0f / 30.0f;
            if (orbit.has_ots_time) {
                dt = std::clamp(std::chrono::duration<float>(now - orbit.last_ots_time).count(), 1.0f / 120.0f, 0.10f);
            }
            orbit.last_ots_time = now;
            orbit.has_ots_time = true;
            orbit.ots_dt = dt;

            if (!orbit.has_cstick_time || stick_yaw != 0.0f || stick_pitch != 0.0f || mouse_x != 0.0f || mouse_y != 0.0f) {
                orbit.last_cstick_time = now;
                orbit.has_cstick_time = true;
            }
            else if (std::chrono::duration<float>(now - orbit.last_cstick_time).count() > 1.8f) {
                const uint32_t pad_addr = (uint32_t)MEM_W(0x36C, camera);
                if (pad_addr >= 0x80000000u && pad_addr <= 0x807FFFF0u) {
                    const gpr pad = (gpr)(int32_t)pad_addr;
                    const float lx = std::clamp((float)MEM_B(2, pad) / 80.0f, -1.0f, 1.0f);
                    const float ly = std::clamp((float)MEM_B(3, pad) / 80.0f, -1.0f, 1.0f);
                    if (std::fabs(lx) > 0.15f && ly >= -0.35f) {
                        const float curve = lx * std::fabs(lx);
                        orbit.yaw += curve * (105.0f * degrees_to_radians) * dt;
                    }
                }
            }
            float target_jump_tilt = 0.0f;
            float target_jump_lift = 0.0f;
            float target_jump_pull = 0.0f;
            float jump_cam_lift = 0.0f;
            bool melee_active = false;
            bool char_moved = false;
            float air_now = 0.0f;
            float target_lead_x = 0.0f;
            float target_lead_z = 0.0f;
            float slope_vy = 0.0f;
            float behind_yaw = orbit.yaw;
            bool has_behind_yaw = false;
            const uint32_t char_addr = (uint32_t)MEM_W(0x3D0, camera);
            if (char_addr >= 0x80000000u && char_addr <= 0x807FFFC0u) {
                const gpr character = (gpr)(int32_t)char_addr;
                const float char_x = read_float(rdram, character, 0x14);
                const float char_y = read_float(rdram, character, 0x18);
                const float char_z = read_float(rdram, character, 0x1C);
                if (orbit.has_char_pos) {
                    const float dx_c = char_x - orbit.prev_char_x;
                    const float dz_c = char_z - orbit.prev_char_z;
                    const float step_xz = std::hypot(dx_c, dz_c);
                    const float speed_xz = step_xz / dt;
                    char_moved = speed_xz > 18.0f;
                    if (char_moved && step_xz > 0.001f) {
                        const float lead_mag = std::clamp(speed_xz / 120.0f, 0.0f, 1.0f) * 8.0f;
                        target_lead_x = (dx_c / step_xz) * lead_mag;
                        target_lead_z = (dz_c / step_xz) * lead_mag;
                        slope_vy = (char_y - orbit.prev_char_y) / dt;
                    }
                }
                orbit.prev_char_x = char_x;
                orbit.prev_char_y = char_y;
                orbit.prev_char_z = char_z;
                orbit.has_char_pos = true;

                // func_15048FC8 subtracts 90 deg at 0x15049074, so 0=-Z, 90=-X, 180=+Z, 270=+X (CCW).
                // Behind-Conker eye direction in orbit.yaw (0=+X, 90=+Z, CW) is atan2(-cos(yaw), -sin(yaw)).
                const float char_yaw_rad = read_float(rdram, character, 0x40) * degrees_to_radians;
                behind_yaw = std::atan2(-std::cos(char_yaw_rad), -std::sin(char_yaw_rad));
                has_behind_yaw = true;

                const float char_floor = floor_below(rdram, ctx, camera, char_x, char_y + 10.0f, char_z);
                const float air_floor = (char_floor > no_floor && char_y - char_floor > 5.0f)
                    ? (char_y - char_floor - 5.0f)
                    : 0.0f;
                const float air_cam = (char_y - cy > 5.0f) ? (char_y - cy - 5.0f) : 0.0f;
                air_now = std::max(air_floor, air_cam);
                if (air_now > 0.0f) {
                    target_jump_tilt = std::clamp(air_now * 0.0020f, 0.0f, 7.5f * degrees_to_radians);
                    target_jump_lift = std::clamp(air_now * 0.75f, 0.0f, 44.0f);
                    target_jump_pull = std::clamp(air_now * 0.40f, 0.0f, 32.0f);
                }
                if (char_y > cy + 2.0f) {
                    jump_cam_lift = std::clamp((char_y - cy - 2.0f) * 0.72f, 0.0f, 38.0f);
                }
                // Conker's current action/animation ID (set by func_1505E0C4 at character+0x84).
                // B-button melee/pan swings trigger states 0x33, 0x46, 0x1DC, 0x354, 0x355.
                const uint16_t action_state = MEM_HU(0x84, character);
                melee_active = action_state == 0x33 || action_state == 0x46 || action_state == 0x1DC ||
                    action_state == 0x354 || action_state == 0x355;
            }
            else {
                orbit.has_char_pos = false;
            }

            // Landing impact spring: brief downward dip when touching down from a fall/jump.
            if (air_now > 8.0f) {
                orbit.peak_air = std::max(orbit.peak_air, air_now);
            }
            else if (air_now <= 2.0f && orbit.peak_air > 14.0f) {
                orbit.land_dip = std::clamp(orbit.peak_air * 0.14f, 1.5f, 5.5f);
                orbit.peak_air = 0.0f;
            }
            orbit.land_dip *= (1.0f - ots_alpha(0.24f, dt));
            if (orbit.land_dip < 0.05f) {
                orbit.land_dip = 0.0f;
            }

            // Movement look-ahead lead.
            orbit.lead_x += (target_lead_x - orbit.lead_x) * ots_alpha(0.12f, dt);
            orbit.lead_z += (target_lead_z - orbit.lead_z) * ots_alpha(0.12f, dt);

            // Idle close-up framing after 4.5s of stillness with no input.
            const bool any_input = char_moved || stick_yaw != 0.0f || stick_pitch != 0.0f ||
                mouse_x != 0.0f || mouse_y != 0.0f || air_now > 2.0f || melee_active;
            if (any_input) {
                orbit.idle_time = 0.0f;
            }
            else {
                orbit.idle_time += dt;
            }
            const float target_idle_pull = (orbit.idle_time >= 4.5f && orbit.aim <= 0.0f) ? -12.0f : 0.0f;
            orbit.idle_pull += (target_idle_pull - orbit.idle_pull) * ots_alpha(0.06f, dt);

            // Bounded slope pitch assist: tilt slightly down on descents (~7 deg max) to keep the ground in view.
            const float target_slope_pitch = (char_moved && air_now <= 2.0f && slope_vy < -22.0f && orbit.aim <= 0.0f)
                ? std::clamp((-slope_vy - 22.0f) * 0.0012f, 0.0f, 0.12f)
                : 0.0f;
            orbit.slope_pitch += (target_slope_pitch - orbit.slope_pitch) * ots_alpha(0.08f, dt);

            if (char_moved) {
                orbit.walk_time += dt;
                orbit.stop_grace = 0.0f;
                if (orbit.walk_time >= 3.0f) {
                    orbit.walk_zoomed_out = true;
                }
            }
            else {
                orbit.stop_grace += dt;
                if (orbit.stop_grace >= 0.15f) {
                    orbit.walk_time = 0.0f;
                    orbit.walk_zoomed_out = false;
                }
            }
            const float target_walk_pull = (orbit.walk_zoomed_out && orbit.aim <= 0.0f) ? 28.0f : 0.0f;
            orbit.walk_pull += (target_walk_pull - orbit.walk_pull) * ots_alpha(0.08f, dt);

            const bool rec_held = recenter_held();
            if (rec_held && !orbit.prev_recenter_held) {
                orbit.recenter_timer = 0.45f;
            }
            orbit.prev_recenter_held = rec_held;
            if (stick_yaw != 0.0f || stick_pitch != 0.0f || mouse_x != 0.0f || mouse_y != 0.0f) {
                orbit.recenter_timer = 0.0f;
            }
            if (has_behind_yaw && (rec_held || orbit.recenter_timer > 0.0f)) {
                constexpr float ots_two_pi = 2.0f * 3.14159265358979f;
                const float dyaw = std::remainder(behind_yaw - orbit.yaw, ots_two_pi);
                orbit.yaw += dyaw * ots_alpha(0.32f, dt);
                orbit.pitch += (0.10f - orbit.pitch) * ots_alpha(0.24f, dt);
                orbit.recenter_timer = std::max(0.0f, orbit.recenter_timer - dt);
            }
            orbit.jump_tilt += (target_jump_tilt - orbit.jump_tilt) * ots_alpha(target_jump_tilt > orbit.jump_tilt ? 0.18f : 0.10f, dt);
            orbit.jump_lift += (target_jump_lift - orbit.jump_lift) * ots_alpha(target_jump_lift > orbit.jump_lift ? 0.24f : 0.12f, dt);
            orbit.jump_pull += (target_jump_pull - orbit.jump_pull) * ots_alpha(target_jump_pull > orbit.jump_pull ? 0.20f : 0.11f, dt);
            if (melee_active) {
                orbit.melee_hold = 14;
            }
            else if (orbit.melee_hold > 0) {
                orbit.melee_hold--;
            }
            const float target_melee_pull = (orbit.melee_hold > 0) ? 48.0f : 0.0f;
            orbit.melee_pull += (target_melee_pull - orbit.melee_pull) * ots_alpha(target_melee_pull > orbit.melee_pull ? 0.28f : 0.10f, dt);

            const float target_cy = cy + jump_cam_lift;
            const float jump_dist = orbit.has_ots_pivot
                ? std::hypot(cx - orbit.ots_pivot[0], std::hypot(target_cy - orbit.ots_pivot[1], cz - orbit.ots_pivot[2]))
                : 1000.0f;
            if (!orbit.has_ots_pivot || jump_dist > 90.0f) {
                orbit.ots_pivot[0] = cx;
                orbit.ots_pivot[1] = target_cy;
                orbit.ots_pivot[2] = cz;
                orbit.has_ots_pivot = true;
            }
            else {
                orbit.ots_pivot[0] += (cx - orbit.ots_pivot[0]) * ots_alpha(0.50f, dt);
                orbit.ots_pivot[1] += (target_cy - orbit.ots_pivot[1]) * ots_alpha(target_cy > orbit.ots_pivot[1] ? 0.42f : 0.28f, dt);
                orbit.ots_pivot[2] += (cz - orbit.ots_pivot[2]) * ots_alpha(0.50f, dt);
                orbit.ots_pivot[0] = std::clamp(orbit.ots_pivot[0], cx - 16.0f, cx + 16.0f);
                orbit.ots_pivot[1] = std::clamp(orbit.ots_pivot[1], target_cy - 18.0f, target_cy + 18.0f);
                orbit.ots_pivot[2] = std::clamp(orbit.ots_pivot[2], cz - 16.0f, cz + 16.0f);
            }
        }
        // Pitch-based craning: tuck closer when looking up, slightly tighter when looking down.
        const float pitch_dist = (orbit.pitch < 0.0f)
            ? (162.0f + std::max(orbit.pitch, -1.0f) * 45.0f)
            : (162.0f - std::min(orbit.pitch, 1.0f) * 16.0f);
        orbit.wanted = pitch_dist + orbit.jump_pull + orbit.melee_pull + orbit.walk_pull + orbit.idle_pull;
    }
    else {
        orbit.jump_tilt = 0.0f;
        orbit.jump_lift = 0.0f;
        orbit.jump_pull = 0.0f;
        orbit.melee_pull = 0.0f;
        orbit.melee_hold = 0;
        orbit.walk_time = 0.0f;
        orbit.stop_grace = 0.0f;
        orbit.walk_zoomed_out = false;
        orbit.walk_pull = 0.0f;
        orbit.has_char_pos = false;
        orbit.peak_air = 0.0f;
        orbit.land_dip = 0.0f;
        orbit.lead_x = 0.0f;
        orbit.lead_z = 0.0f;
        orbit.idle_time = 0.0f;
        orbit.idle_pull = 0.0f;
        orbit.slope_pitch = 0.0f;
        orbit.recenter_timer = 0.0f;
        orbit.has_ots_pivot = false;
        orbit.ots_shoulder_x = orbit.ots_left_shoulder ? -34.0f : 34.0f;
        orbit.ots_shoulder_up = 24.0f;
        orbit.smooth_stick_yaw = 0.0f;
        orbit.smooth_stick_pitch = 0.0f;
        orbit.has_cstick_time = false;
        if (!orbit.wheel_zoomed) {
            orbit.wanted = wanted_distance;
        }
    }

    // The controller's nearest and farthest distances from the look-at point.
    const float look_height = cy - read_float(rdram, camera, 0x2A8);
    float nearest = 0.0f, farthest = 0.0f;
    for (int i = 0; i < camera_distance_count; i++) {
        const gpr preset = (gpr)(int32_t)(camera_distances + i * 8);
        const float h = read_float(rdram, preset, 0), v = read_float(rdram, preset, 4) - look_height;
        const float d = std::sqrt(h * h + v * v);
        nearest = (i == 0) ? d : std::min(nearest, d);
        farthest = (i == 0) ? d : std::max(farthest, d);
    }
    if (!fp) {
        orbit.wanted = std::clamp(orbit.wanted * std::pow(zoom_step, (float)-notches), nearest, farthest);
    }
    orbit.yaw += mouse_x * degrees_per_pixel * degrees_to_radians + stick_yaw;
    if (first && orbit.c_turning) {
        // The C-buttons' turn this frame: as far as the game turned its own eye.
        constexpr float two_pi = 2.0f * 3.14159265358979f;
        orbit.yaw += std::remainder(eye_yaw(rdram, camera, 0x2F8, cx, cz) - eye_yaw(rdram, camera, 0x304, cx, cz), two_pi);
    }
    // Looking up: the eye no further than it can stay above Conker's feet and outside him (issue #70).
    // Tilting further up than that, the eye stays there and the view aims up instead (orbit.aim, see
    // conker_mouse_camera_aim), as Ocarina of Time's free camera does: from close to a tower its top
    // was out of view, as the eye always looked at the look-at point. Tilting back down takes the aim
    // off first, then the eye comes back up.
    const float active_body_clearance = fp ? 72.0f : body_clearance;
    const float drop = std::max(look_height * (1.0f - ground_clearance), 0.0f);
    const float lowest_pitch = std::max(min_pitch, -std::atan2(drop, active_body_clearance));
    const float tilt = orbit.pitch - orbit.aim + mouse_y * degrees_per_pixel * degrees_to_radians + stick_pitch;
    if (tilt >= lowest_pitch) {
        orbit.pitch = std::min(tilt, max_pitch);
        orbit.aim = 0.0f;
    }
    else {
        orbit.pitch = lowest_pitch;
        // No more aim than takes the view to highest_view (the eye looks up -lowest_pitch already).
        orbit.aim = std::clamp(lowest_pitch - tilt, 0.0f, std::max(highest_view + lowest_pitch, 0.0f));
    }

    // The eye's direction from the look-at point.
    const float effective_pitch = fp ? std::min(orbit.pitch + orbit.slope_pitch, max_pitch) : orbit.pitch;
    const float dx = std::cos(effective_pitch) * std::cos(orbit.yaw);
    const float dy = std::sin(effective_pitch);
    const float dz = std::cos(effective_pitch) * std::sin(orbit.yaw);

    // At rest: nothing the orbit goes by changed since last frame, against something (rest_move).
    if (first) {
        const bool unchanged = orbit.has_last && (notches == 0) &&
            (std::fabs(cx - orbit.last_look[0]) < rest_move) && (std::fabs(cy - orbit.last_look[1]) < rest_move) && (std::fabs(cz - orbit.last_look[2]) < rest_move) &&
            (std::fabs(orbit.yaw - orbit.last_yaw) < rest_turn) && (std::fabs(orbit.pitch - orbit.last_pitch) < rest_turn) && (std::fabs(orbit.wanted - orbit.last_wanted) < rest_distance);
        orbit.resting = unchanged && (orbit.resting || orbit.contact);
        orbit.last_look[0] = cx; orbit.last_look[1] = cy; orbit.last_look[2] = cz;
        orbit.last_yaw = orbit.yaw; orbit.last_pitch = orbit.pitch; orbit.last_wanted = orbit.wanted;
        orbit.has_last = true;
    }

    // The distance the orbit eases toward (only the first time a frame, see tight_ease), held while at rest.
    if (first && !orbit.resting) {
        // Where the game pulls its own camera in closer than the controller can (tight spots),
        // so does the orbit.
        if (wanted_distance < nearest) {
            orbit.tight = wanted_distance;
            orbit.tight_frames = tight_hold;
        }
        else if (orbit.tight_frames > 0) {
            orbit.tight_frames--;
        }
        else {
            orbit.tight = 0.0f;
        }
        float goal = (orbit.tight > 0.0f) ? std::min(orbit.wanted, orbit.tight) : orbit.wanted;
        // No further than just beyond a wall that held the eye.
        if (orbit.wall_frames > 0) {
            orbit.wall_frames--;
            goal = std::min(goal, orbit.wall + wall_margin);
        }
        // Looking up, in close enough that the eye stays above Conker's feet, but not into him
        // (issue #70). Eased too: far out, a small tilt here moves the eye a long way in.
        if (dy < 0.0f && -dy * goal > drop) {
            goal = std::min(goal, std::max(drop / -dy, active_body_clearance / std::cos(orbit.pitch)));
        }
        if (orbit.shown <= 0.0f) {
            orbit.shown = goal;
        }
        orbit.shown += (goal < orbit.shown) ? (goal - orbit.shown) * tight_ease : std::min((goal - orbit.shown) * tight_release, release_step);
    }
    const float zoomed = orbit.shown;
    const float px = (fp && orbit.has_ots_pivot) ? orbit.ots_pivot[0] : cx;
    const float py = (fp && orbit.has_ots_pivot) ? orbit.ots_pivot[1] : cy;
    const float pz = (fp && orbit.has_ots_pivot) ? orbit.ots_pivot[2] : cz;
    float target[3] = { px + dx * zoomed, py + dy * zoomed, pz + dz * zoomed };
    // Above the floor under it (floor_clearance), looked for down from the look-at point. func_1510E8BC can
    // answer with a surface above where it looked from: with a wall behind Conker the eye is behind or in it,
    // and it gives the wall's top. That isn't ground under the camera: going by it lifted the eye high up the
    // wall, and dropped it back each time the eye crossed the wall's edge (the jitter of issue #70).
    const float search_from = std::max(target[1], cy);
    const float floor = floor_below(rdram, ctx, camera, target[0], search_from, target[2]);
    orbit.floor = (floor > no_floor && floor <= search_from) ? floor : no_floor;
    if (floor > no_floor && floor <= search_from) {
        target[1] = std::max(target[1], floor + floor_clearance);
    }

    // The collision moves the camera from last frame's eye (+0x304). If that's further from
    // the eye wanted now than the orbit itself moved since last frame, the camera is behind:
    // ease it there rather than sending it the whole way at once.
    float gap = 0.0f, moved = 0.0f;
    float from[3];
    for (int i = 0; i < 3; i++) {
        from[i] = read_float(rdram, camera, 0x304 + i * 4);
        gap += (target[i] - from[i]) * (target[i] - from[i]);
        const float step = orbit.has_target ? (target[i] - orbit.target[i]) : 0.0f;
        moved += step * step;
    }
    const bool behind = orbit.has_target && (std::sqrt(gap) > std::sqrt(moved) + behind_slack);
    for (int i = 0; i < 3; i++) {
        const float eye = orbit.resting ? from[i] : (behind ? from[i] + (target[i] - from[i]) * catch_up : target[i]);
        orbit.placed_eye[i] = eye;
        orbit.placed = true;
        write_float(rdram, camera, 0x2F8 + i * 4, eye);
        orbit.next_target[i] = target[i];
    }
}

// func_151284C4 (builds the view), after its first instruction: $a0 is the camera. The
// frame's camera update is done: start over for the next one.
extern "C" void conker_mouse_camera(uint8_t* rdram, recomp_context* ctx) {
    const gpr camera = ctx->r4;
    if ((uint32_t)camera != (uint32_t)MEM_W(0, (gpr)(int32_t)current_camera)) {
        return;
    }
    if (!orbit.follow_camera_ran || orbit.look_mode_ran || photo_mode_active() || (first_person_setting() && cutscene_or_intro_now(rdram))) {
        orbit.engaged = false;
        orbit.jump_tilt = 0.0f;
        orbit.jump_lift = 0.0f;
        orbit.jump_pull = 0.0f;
        orbit.melee_pull = 0.0f;
        orbit.melee_hold = 0;
        orbit.has_ots_pivot = false;
        orbit.ots_shoulder_x = orbit.ots_left_shoulder ? -34.0f : 34.0f;
        orbit.ots_shoulder_up = 24.0f;
        orbit.smooth_stick_yaw = 0.0f;
        orbit.smooth_stick_pitch = 0.0f;
        orbit.has_cstick_time = false;
        orbit.aim = 0.0f;
    }
#if defined(__arm__) && !defined(CONKER_RT64)
    rt64_3ds::set_first_person_head_cull(false);
#endif
    // The game's collision lifted the orbit's eye off the floor (to game_floor_lift above it): to floor_clearance
    // above it instead, unless the orbit placed it higher than that. It's the floor's lift when the eye was lifted
    // straight up, or to just game_floor_lift above the floor found under it (a wall may move it across as well).
    if (orbit.engaged && orbit.placed) {
        const float ex = read_float(rdram, camera, 0x2F8), ey = read_float(rdram, camera, 0x2FC), ez = read_float(rdram, camera, 0x300);
        orbit.contact = (std::fabs(ex - orbit.placed_eye[0]) + std::fabs(ey - orbit.placed_eye[1]) + std::fabs(ez - orbit.placed_eye[2])) > rest_move;
        const bool straight_up = std::fabs(ex - orbit.placed_eye[0]) < 1.5f && std::fabs(ez - orbit.placed_eye[2]) < 1.5f;
        const bool to_floor = (orbit.floor > no_floor) && (std::fabs(ey - (orbit.floor + game_floor_lift)) < 2.0f);
        if ((straight_up || to_floor) && ey > orbit.placed_eye[1] + 1.0f) {
            write_float(rdram, camera, 0x2FC, std::max(orbit.placed_eye[1], ey - game_floor_lift + floor_clearance));
        }
        // Held across by a wall: how far from the look-at point, to come in to (wall_margin).
        if (std::hypot(ex - orbit.placed_eye[0], ez - orbit.placed_eye[2]) > wall_held_slack) {
            const float lx = read_float(rdram, camera, 0x2BC), ly = read_float(rdram, camera, 0x2C0), lz = read_float(rdram, camera, 0x2C4);
            orbit.wall = std::sqrt((ex - lx) * (ex - lx) + (ey - ly) * (ey - ly) + (ez - lz) * (ez - lz));
            orbit.wall_frames = tight_hold;
        }
        // Touching, or just let go: part of the way from last frame's eye (+0x304) to this one (contact_smooth).
        if (orbit.contact) {
            orbit.smooth_frames = contact_after;
        }
        else if (orbit.smooth_frames > 0) {
            orbit.smooth_frames--;
        }
        if (orbit.smooth_frames > 0) {
            for (int i = 0; i < 3; i++) {
                const float was = read_float(rdram, camera, 0x304 + i * 4);
                const float now = read_float(rdram, camera, 0x2F8 + i * 4);
                write_float(rdram, camera, 0x2F8 + i * 4, was + (now - was) * contact_smooth);
            }
        }
        if (first_person_setting()) {
            const float cx_now = read_float(rdram, camera, 0x2F8);
            const float cy_now = read_float(rdram, camera, 0x2FC);
            const float cz_now = read_float(rdram, camera, 0x300);
            const float squeeze = orbit.ots_probe_has_sample
                ? std::clamp(1.0f - std::fabs(orbit.ots_clear_side) / 14.0f, 0.0f, 1.0f)
                : 0.0f;
            const float desired_up = std::max(6.0f, 24.0f + orbit.jump_lift + squeeze * 9.0f - orbit.land_dip);
            const float dpos = std::fabs(cx_now - orbit.ots_probe_x) + std::fabs(cy_now - orbit.ots_probe_y) + std::fabs(cz_now - orbit.ots_probe_z);
            const float dyaw = std::fabs(orbit.yaw - orbit.ots_probe_yaw);
            const bool urgent = !orbit.ots_probe_has_sample || (orbit.ots_left_shoulder != orbit.ots_probe_left) || dpos > 12.0f || dyaw > 0.12f;
            const bool moved = dpos > 0.5f || dyaw > 0.005f || std::fabs(desired_up - orbit.ots_probe_up) > 0.5f;
            if (urgent || (moved && orbit.ots_probe_cooldown <= 0)) {
                const float rx = std::sin(orbit.yaw);
                const float rz = -std::cos(orbit.yaw);
                const float dir_sign = orbit.ots_left_shoulder ? -1.0f : 1.0f;
                const int32_t save_target[3] = { MEM_W(0x2F8, camera), MEM_W(0x2FC, camera), MEM_W(0x300, camera) };
                const int32_t save_from[3] = { MEM_W(0x304, camera), MEM_W(0x308, camera), MEM_W(0x30C, camera) };
                const int32_t save_26c = MEM_W(0x26C, camera);
                const int32_t save_360 = MEM_W(0x360, camera);
                const int32_t save_3e8 = MEM_BU(0x3E8, camera);
                const int32_t save_5f0 = MEM_W(0x5F0, camera);
                const int32_t save_94c = MEM_W(0x94C, camera);
                const int32_t save_950 = MEM_W(0x950, camera);
                const int32_t save_954 = MEM_W(0x954, camera);
                const int32_t save_958 = MEM_W(0x958, camera);
                const int32_t save_95c = MEM_W(0x95C, camera);
                const int32_t save_960 = MEM_W(0x960, camera);

                write_float(rdram, camera, 0x304, cx_now);
                write_float(rdram, camera, 0x308, cy_now);
                write_float(rdram, camera, 0x30C, cz_now);
                write_float(rdram, camera, 0x2F8, cx_now + rx * (dir_sign * 40.0f));
                write_float(rdram, camera, 0x2FC, cy_now + desired_up + 4.0f);
                write_float(rdram, camera, 0x300, cz_now + rz * (dir_sign * 40.0f));
                recomp_context call = *ctx;
                call.r4 = camera;
                orbit.ots_probing = true;
                func_1512BB10(rdram, &call);
                orbit.ots_probing = false;

                const float hx = read_float(rdram, camera, 0x2F8);
                const float hy = read_float(rdram, camera, 0x2FC);
                const float hz = read_float(rdram, camera, 0x300);
                const float side_proj = ((hx - cx_now) * rx + (hz - cz_now) * rz) * dir_sign;
                orbit.ots_clear_side = dir_sign * std::clamp(side_proj - 6.0f, 0.0f, 34.0f);
                orbit.ots_clear_up = std::clamp((hy - cy_now) - 4.0f, 6.0f, desired_up);
                orbit.ots_probe_has_sample = true;
                orbit.ots_probe_cooldown = 1;
                orbit.ots_probe_x = cx_now;
                orbit.ots_probe_y = cy_now;
                orbit.ots_probe_z = cz_now;
                orbit.ots_probe_yaw = orbit.yaw;
                orbit.ots_probe_up = desired_up;
                orbit.ots_probe_left = orbit.ots_left_shoulder;

                MEM_W(0x2F8, camera) = save_target[0];
                MEM_W(0x2FC, camera) = save_target[1];
                MEM_W(0x300, camera) = save_target[2];
                MEM_W(0x304, camera) = save_from[0];
                MEM_W(0x308, camera) = save_from[1];
                MEM_W(0x30C, camera) = save_from[2];
                MEM_W(0x26C, camera) = save_26c;
                MEM_W(0x360, camera) = save_360;
                MEM_B(0x3E8, camera) = save_3e8;
                MEM_W(0x5F0, camera) = save_5f0;
                MEM_W(0x94C, camera) = save_94c;
                MEM_W(0x950, camera) = save_950;
                MEM_W(0x954, camera) = save_954;
                MEM_W(0x958, camera) = save_958;
                MEM_W(0x95C, camera) = save_95c;
                MEM_W(0x960, camera) = save_960;
            }
            else if (orbit.ots_probe_cooldown > 0) {
                orbit.ots_probe_cooldown--;
            }
            const float target_up = std::min(orbit.ots_clear_up, desired_up);
            orbit.ots_shoulder_x += (orbit.ots_clear_side - orbit.ots_shoulder_x) * ots_alpha(std::fabs(orbit.ots_clear_side) < std::fabs(orbit.ots_shoulder_x) ? 0.88f : 0.18f, orbit.ots_dt);
            orbit.ots_shoulder_up += (target_up - orbit.ots_shoulder_up) * ots_alpha(target_up < orbit.ots_shoulder_up ? 0.85f : 0.22f, orbit.ots_dt);
        }
        else {
            orbit.ots_probe_has_sample = false;
        }
    }
    orbit.placed = false;
    // For the controller's input until the next update: whether the stick turns this camera.
    was_normal_camera = orbit.follow_camera_ran && !orbit.look_mode_ran;
    stick_owns_camera = was_normal_camera && stick_turns_camera_setting();
    // The eye wanted this frame, to tell next frame how far the orbit itself moved.
    orbit.has_target = orbit.engaged;
    std::memcpy(orbit.target, orbit.next_target, sizeof(orbit.target));
    orbit.follow_camera_ran = false;
    orbit.look_mode_ran = false;
    orbit.c_turning = false;
    orbit.turned = false;
    orbit.moved = false;
}

// func_1512C490 (builds the view, called by func_151284C4), before 0x1512C640: $s0 is the camera,
// and the view's look-at point (+0x2E0) and eye (+0x2EC) have just been copied from the look-at
// point (+0x2BC) and the eye the collision left (+0x2F8). The view (func_15128CB0) is made from
// these two. Tilted up past the lowest pitch (orbit.aim), the view's look-at point is raised so
// the view aims that much higher from the same eye, its direction across the ground the same:
// Conker drops toward the bottom of the screen and what's above comes into view (issue #70). Only
// the view's copy changes: the look-at point the game follows with, the eye and its collision,
// and Conker's movement (by the eye's direction across the ground) are as they were.
extern "C" void conker_mouse_camera_aim(uint8_t* rdram, recomp_context* ctx) {
    const gpr camera = ctx->r16;
    if (!orbit.engaged || photo_mode_active() || (first_person_setting() && cutscene_or_intro_now(rdram)) || orbit.aim <= 0.0f || (uint32_t)camera != (uint32_t)MEM_W(0, (gpr)(int32_t)current_camera)) {
        return;
    }
    // The eye from where it's copied (+0x2F8): the copy's z (+0x2F4) is stored after this, in the branch's delay slot.
    const float ex = read_float(rdram, camera, 0x2F8), ey = read_float(rdram, camera, 0x2FC), ez = read_float(rdram, camera, 0x300);
    const float lx = read_float(rdram, camera, 0x2E0) - ex, ly = read_float(rdram, camera, 0x2E4) - ey, lz = read_float(rdram, camera, 0x2E8) - ez;
    const float across = std::sqrt(lx * lx + lz * lz);
    if (across < 1.0f) {
        return;
    }
    const float distance = std::sqrt(across * across + ly * ly);
    const float view = std::min(std::atan2(ly, across) + orbit.aim, highest_view);
    const float scale = distance * std::cos(view) / across;
    write_float(rdram, camera, 0x2E0, ex + lx * scale);
    write_float(rdram, camera, 0x2E4, ey + distance * std::sin(view));
    write_float(rdram, camera, 0x2E8, ez + lz * scale);
}

// func_1512C490 at 0x1512C848, right before jal 0x150491EC: $a0 ($sp+0xEC) is the
// final view eye, $a1 ($sp+0xE0) is the final view look-at, and $s0 is the camera.
// In First person mode, replace only those stack vectors so all game camera state
// (+0x2A4, +0x2BC, +0x2F8, +0x304) stays untouched.
extern "C" void conker_first_person_view(uint8_t* rdram, recomp_context* ctx) {
    const gpr camera = ctx->r16;
    if (!first_person_setting() || !orbit.engaged || photo_mode_active() || cutscene_or_intro_now(rdram) ||
        (uint32_t)camera != (uint32_t)MEM_W(0, (gpr)(int32_t)current_camera)) {
        return;
    }
    // Over-the-shoulder offset: wall-cleared left/right & up offset (plus jump lift),
    // slight horizon convergence, damped pivot follow, and smooth jump-tilt.
    const float rx = std::sin(orbit.yaw);
    const float rz = -std::cos(orbit.yaw);
    const float shoulder_x = orbit.ots_shoulder_x;
    const float shoulder_up = orbit.ots_shoulder_up;
    const float base_up = std::min(shoulder_up, 24.0f);
    const float jump_up = std::max(shoulder_up - 24.0f, 0.0f);
    const float look_x = shoulder_x * 0.50f;
    const float look_up = base_up * 0.92f + jump_up * 0.45f;
    const float piv_dx = orbit.has_ots_pivot ? (orbit.ots_pivot[0] - read_float(rdram, camera, 0x2BC)) : 0.0f;
    const float piv_dy = orbit.has_ots_pivot ? (orbit.ots_pivot[1] - read_float(rdram, camera, 0x2C0)) : 0.0f;
    const float piv_dz = orbit.has_ots_pivot ? (orbit.ots_pivot[2] - read_float(rdram, camera, 0x2C4)) : 0.0f;
    const gpr eye_ptr = ctx->r4;
    const gpr look_ptr = ctx->r5;
    const float ex = read_float(rdram, eye_ptr, 0) + rx * shoulder_x;
    const float ey = read_float(rdram, eye_ptr, 4) + shoulder_up;
    const float ez = read_float(rdram, eye_ptr, 8) + rz * shoulder_x;
    float lx = read_float(rdram, look_ptr, 0) + piv_dx + rx * look_x + orbit.lead_x;
    float ly = read_float(rdram, look_ptr, 4) + piv_dy + look_up;
    float lz = read_float(rdram, look_ptr, 8) + piv_dz + rz * look_x + orbit.lead_z;
    if (orbit.jump_tilt > 0.0001f) {
        const float dx = lx - ex;
        const float dy = ly - ey;
        const float dz = lz - ez;
        const float horiz = std::sqrt(dx * dx + dz * dz);
        if (horiz > 0.001f) {
            const float dist = std::sqrt(horiz * horiz + dy * dy);
            const float p = std::atan2(dy, horiz);
            const float p_new = std::max(p - orbit.jump_tilt, -80.0f * degrees_to_radians);
            const float scale = (dist * std::cos(p_new)) / horiz;
            lx = ex + dx * scale;
            ly = ey + dist * std::sin(p_new);
            lz = ez + dz * scale;
        }
    }
    write_float(rdram, eye_ptr, 0, ex);
    write_float(rdram, eye_ptr, 4, ey);
    write_float(rdram, eye_ptr, 8, ez);
    write_float(rdram, look_ptr, 0, lx);
    write_float(rdram, look_ptr, 4, ly);
    write_float(rdram, look_ptr, 8, lz);

    // Sync the camera's skybox pitch (+0x388) and yaw (+0x37C/+0x380) to the OTS view vector
    // so the skybox stays locked to the 3D horizon instead of drifting with OTS offsets/pivots.
    const float saved_look[3] = {
        read_float(rdram, camera, 0x2BC),
        read_float(rdram, camera, 0x2C0),
        read_float(rdram, camera, 0x2C4),
    };
    const float saved_eye[3] = {
        read_float(rdram, camera, 0x2F8),
        read_float(rdram, camera, 0x2FC),
        read_float(rdram, camera, 0x300),
    };
    write_float(rdram, camera, 0x2BC, lx);
    write_float(rdram, camera, 0x2C0, ly);
    write_float(rdram, camera, 0x2C4, lz);
    write_float(rdram, camera, 0x2F8, ex);
    write_float(rdram, camera, 0x2FC, ey);
    write_float(rdram, camera, 0x300, ez);
    recomp_context call = *ctx;
    call.r29 = ADD32(ctx->r29, -0x40);
    call.r4 = camera;
    func_1512523C(rdram, &call);
    call.r4 = camera;
    func_15125330(rdram, &call);
    write_float(rdram, camera, 0x2BC, saved_look[0]);
    write_float(rdram, camera, 0x2C0, saved_look[1]);
    write_float(rdram, camera, 0x2C4, saved_look[2]);
    write_float(rdram, camera, 0x2F8, saved_eye[0]);
    write_float(rdram, camera, 0x2FC, saved_eye[1]);
    write_float(rdram, camera, 0x300, saved_eye[2]);
    float yaw_380 = read_float(rdram, camera, 0x37C) - read_float(rdram, camera, 0x5E8);
    if (yaw_380 < 0.0f) {
        yaw_380 += 360.0f;
    }
    else if (yaw_380 >= 360.0f) {
        yaw_380 -= 360.0f;
    }
    write_float(rdram, camera, 0x380, yaw_380);
    write_float(rdram, camera, 0x3A0, yaw_380 * degrees_to_radians);
}

// func_151236D0 before its first instruction: skip water crossing state and splash side effects
// while the Over-the-shoulder clearance probe calls func_1512BB10.
extern "C" bool conker_camera_water_skip() {
    return orbit.ots_probing;
}

// func_151236D0 before 0x151237D0 (c.le.s $f10, $f8: $f10 = camera +0x2FC, $f8 = water +0x360):
// when Over-the-shoulder is active, test the actual OTS eye height (+ ots_shoulder_up) so the
// underwater blue tint and splash only trigger when the rendered camera itself enters water.
extern "C" void conker_camera_water_eye(uint8_t* rdram, recomp_context* ctx) {
    const gpr camera = ctx->r5;
    if (!first_person_setting() || !orbit.engaged || !orbit.follow_camera_ran ||
        orbit.look_mode_ran || conker::look_aim::leaping_now() || photo_mode_active() || cutscene_or_intro_now(rdram) ||
        (uint32_t)camera != (uint32_t)MEM_W(0, (gpr)(int32_t)current_camera)) {
        return;
    }
    ctx->f10.fl += orbit.ots_shoulder_up;
}

// func_15122C5C (the follow camera's update), after it keeps $a0 in $s0 (0x15122C6C): the view's eye raised
// for a leap last frame (conker_leap_view_lift) is lowered back before it's copied into the eye wanted, and
// the lift eases toward this frame's (issue #85, see leap_lift_height).
extern "C" void conker_leap_view_restore(uint8_t* rdram, recomp_context* ctx) {
    const gpr camera = ctx->r16;
    if ((uint32_t)camera != (uint32_t)MEM_W(0, (gpr)(int32_t)current_camera)) {
        return;
    }
    if (leap_lift_applied != 0.0f && camera == leap_lift_camera) {
        write_float(rdram, camera, 0x2F0, read_float(rdram, camera, 0x2F0) - leap_lift_applied);
    }
    leap_lift_applied = 0.0f;
    const float target = conker::look_aim::leaping_now() ? leap_lift_height : 0.0f;
    leap_lift += (target - leap_lift) * leap_lift_ease;
    if (std::fabs(leap_lift - target) < 0.5f) {
        leap_lift = target;
    }
}

// func_1512C490 ($s0 the camera), before 0x1512C640: the view's eye has been copied from the eye
// wanted, its y (+0x2F0) stored. In a leap it's raised (issue #85), and lowered back as the next update starts.
extern "C" void conker_leap_view_lift(uint8_t* rdram, recomp_context* ctx) {
    const gpr camera = ctx->r16;
    if (leap_lift == 0.0f || (uint32_t)camera != (uint32_t)MEM_W(0, (gpr)(int32_t)current_camera)) {
        return;
    }
    write_float(rdram, camera, 0x2F0, read_float(rdram, camera, 0x2F0) + leap_lift);
    leap_lift_applied += leap_lift;
    leap_lift_camera = camera;
}

// frontend.cpp, once SDL is up: listen for the scroll wheel.
void conker_mouse_camera_init() {
    SDL_AddEventWatch(watch_wheel, nullptr);
}
