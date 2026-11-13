#include "conker_rom_choices.h"
#include "conker_launcher.h"
// Conker's Bad Fur Day: Recompiled, Nintendo 3DS entry point.
#include <cinttypes>
#include <atomic>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <3ds.h>

extern "C" {
#include <3ds/types.h>
#include <3ds/services/hid.h>
}

#include <SDL.h>

#include "recomp3ds.h"
#include "librecomp/rsp.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/game.hpp"
#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "conker.hpp"
#include "conker_options.h"
#include "conker_audio_hle.h"

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
RspExitReason conker_audio_ucode(uint8_t* rdram, uint32_t ucode_addr);

static std::atomic<bool> g_free_camera{false};
extern "C" bool conker_3ds_free_camera_enabled() { return g_free_camera.load(); }

extern "C" void conker_stat_change(uint8_t*, recomp_context*) {}

// 400x240 top screen window handle for widescreen.cpp's SDL_GetWindowSize.
static SDL_Window dummy_window{};
SDL_Window* window = &dummy_window;

bool conker::model_detail::always_highest() {
    return conker3ds::get("model_highest")!=0;
}

ultramodern::input::connected_device_info_t conker::get_connected_device_info(int controller_num) {
    if (controller_num == 0) {
        return { ultramodern::input::Device::Controller, ultramodern::input::Pak::None };
    }
    return { ultramodern::input::Device::None, ultramodern::input::Pak::None };
}

namespace {

using namespace recomp3ds;

const ButtonMap kButtons[] = {
    { KEY_A, N64_A },
    { KEY_B, N64_B },
    { KEY_L | KEY_ZL, N64_Z },
    { KEY_R, N64_R },
    { KEY_ZR, N64_L },
    { KEY_START, N64_START },
    { KEY_X | KEY_DUP, N64_CUP },
    { KEY_Y | KEY_DDOWN, N64_CDOWN },
    { KEY_DLEFT, N64_CLEFT },
    { KEY_DRIGHT, N64_CRIGHT },
};

const rt64_3ds::StereoRule kStereoRules[] = {
    { 0, UINT32_MAX, rt64_3ds::ProjKind::Perspective, rt64_3ds::StereoClass::World },
};

const MenuDefault kDefaults[] = {
    { "separation", 50 },
    { "convergence_tenths", 100 },
    { "stereo_ghost_contrast", 100 },
    { "stick_deadzone", 15 },
    { "cstick_deadzone", 20 },
    { "frame_cap", 2 },
    { "perf_monitor", 0 },
};

int g_touch_mode = 1;
int g_face_camera_value=0;
const char* const kFaceCameraNames[]={"Off","On"};
void apply_face_camera(int v){recomp3ds::input_set_face_camera(v!=0);}
int g_options_button = 1;
int g_animated_tails=0,g_health_display=1,g_money_display=1;
int g_lives_display=0,g_chapter_display=1,g_gfx_text=1;
const char* const kLivesDisplay[]={"Full","Icons only","Hidden"};
const char* const kOptionsButtonNames[] = { "Off", "On" };
int g_free_camera_value = 0;
const char* const kFreeCameraNames[] = { "Original", "Free orbit" };
void apply_free_camera(int v) { g_free_camera.store(v!=0); }
const char* const kTouchModes[] = { "Movement", "Look" };

const char* const kOffOn[]={"Off","On"};
const char* const kInvert[]={"None","X","Y","Both"};
const char* const kResponse[]={"Smooth","Direct"};
const char* const kDetail[]={"Original","Highest"};
const char* const kCutscene[]={"Expanded","4:3"};
int value_camera_turn_speed=100;
void apply_camera_turn_speed(int v){conker3ds::set("camera_turn_speed",v);}
int value_camera_invert_turning=0;
void apply_camera_invert_turning(int v){conker3ds::set("camera_invert_turning",v);}
int value_camera_field_of_view_degrees=50;
void apply_camera_field_of_view_degrees(int v){conker3ds::set("camera_field_of_view_degrees",v);}
int value_look_stick_response=0;
void apply_look_stick_response(int v){conker3ds::set("look_stick_response",v);}
int value_look_stick_invert=2;
void apply_look_stick_invert(int v){conker3ds::set("look_stick_invert",v);}
int value_aim_swap_sticks=0;
void apply_aim_swap_sticks(int v){conker3ds::set("aim_swap_sticks",v);}
int value_aim_lock_on=1;
void apply_aim_lock_on(int v){conker3ds::set("aim_lock_on",v);}
int value_aim_reticle=0;
void apply_aim_reticle(int v){conker3ds::set("aim_reticle",v);}
int value_gyro_enabled=0;
void apply_gyro_enabled(int v){conker3ds::set("gyro_enabled",v);recomp3ds::input_set_gyro(v!=0);}
int value_gyro_sensitivity=100;
void apply_gyro_sensitivity(int v){conker3ds::set("gyro_sensitivity",v);}
int value_look_gyro_response=0;
void apply_look_gyro_response(int v){conker3ds::set("look_gyro_response",v);}
int value_look_gyro_invert=0;
void apply_look_gyro_invert(int v){conker3ds::set("look_gyro_invert",v);}
int value_cutscene_4x3=0;
void apply_cutscene_4x3(int v){conker3ds::set("cutscene_4x3",v);}
int value_model_highest=0;
void apply_model_highest(int v){conker3ds::set("model_highest",v);}
int g_skip_intro=0;
void apply_skip_intro(int v){conker3ds::set("misc_skip_intro",v);}
const MenuOption kOptions[] = {
    {"misc_skip_intro","Skip intro",0,1,&g_skip_intro,kOffOn,apply_skip_intro,PageGame},
    {"face_camera","Face-button camera",0,1,&g_face_camera_value,kFaceCameraNames,apply_face_camera,PageControls},
    {"bottom_health","Chocolate health",0,1,&g_health_display,kOffOn,screen_conker_set_health_display,PageGame},
    {"bottom_money","Money display",0,1,&g_money_display,kOffOn,screen_conker_set_money_display,PageGame},
    {"bottom_animated_tails","Animated tails",0,1,&g_animated_tails,kOffOn,screen_conker_set_animated_tails,PageGame},
    {"bottom_lives","Lives display",0,2,&g_lives_display,kLivesDisplay,screen_conker_set_lives_display,PageGame},
    {"bottom_chapter","Chapter text",0,1,&g_chapter_display,kOffOn,screen_conker_set_chapter_display,PageGame},
    {"gfx_task_text","GFX task text",0,1,&g_gfx_text,kOffOn,screen_conker_set_gfx_text,PageGame},
    { "options_button", "Options button", 0, 1, &g_options_button, kOptionsButtonNames,
      screen_conker_set_options_button, PageGame },
    { "free_camera", "Camera", 0, 1, &g_free_camera_value, kFreeCameraNames,
      apply_free_camera, PageControls },
    { "touch_stick", "Touchscreen", 0, 1, &g_touch_mode, kTouchModes,
      input_set_touch_stick_mode, PageControls },
    {"camera_turn_speed","Camera speed",50,300,&value_camera_turn_speed,nullptr,apply_camera_turn_speed,PageControls,5,"%"},
    {"camera_invert_turning","Camera inversion",0,3,&value_camera_invert_turning,kInvert,apply_camera_invert_turning,PageControls},
    {"camera_field_of_view_degrees","Field of view",35,80,&value_camera_field_of_view_degrees,nullptr,apply_camera_field_of_view_degrees,PageControls},
    {"look_stick_response","Aim response",0,1,&value_look_stick_response,kResponse,apply_look_stick_response,PageControls},
    {"look_stick_invert","Aim inversion",0,3,&value_look_stick_invert,kInvert,apply_look_stick_invert,PageControls},
    {"aim_swap_sticks","Swap aim sticks",0,1,&value_aim_swap_sticks,kOffOn,apply_aim_swap_sticks,PageControls},
    {"aim_lock_on","Aim lock-on",0,1,&value_aim_lock_on,kOffOn,apply_aim_lock_on,PageControls},
    {"aim_reticle","Aiming reticle",0,1,&value_aim_reticle,kOffOn,apply_aim_reticle,PageControls},
    {"gyro_enabled","Gyro aiming",0,1,&value_gyro_enabled,kOffOn,apply_gyro_enabled,PageControls},
    {"gyro_sensitivity","Gyro sensitivity",0,400,&value_gyro_sensitivity,nullptr,apply_gyro_sensitivity,PageControls,5,"%"},
    {"look_gyro_response","Gyro response",0,1,&value_look_gyro_response,kResponse,apply_look_gyro_response,PageControls},
    {"look_gyro_invert","Gyro inversion",0,3,&value_look_gyro_invert,kInvert,apply_look_gyro_invert,PageControls},
    {"cutscene_4x3","Cutscene framing",0,1,&value_cutscene_4x3,kCutscene,apply_cutscene_4x3,PageGame},
    {"model_highest","Conker detail",0,1,&value_model_highest,kDetail,apply_model_highest,PageGame},
};

void set_fr_mode(recomp_context* ctx) {
    constexpr uint32_t STATUS_FR = 0x04000000;
    cop0_status_write(ctx, ctx->status_reg | STATUS_FR);
}

void on_thread_create(uint8_t*, recomp_context* ctx) {
    set_fr_mode(ctx);
}

static uint8_t* g_rdram = nullptr;

void conker_on_init_3ds(uint8_t* rdram, recomp_context* ctx) {
    g_rdram = rdram;
    recomp3ds::screen_conker_set_rdram(rdram);
    conker::register_tlb_mapped_code();
    conker::map_tlb_code_pages(rdram);

    // Conker's scheduler (func_100049E0) relies on one-shot timer messages
    // (msg=3 for audio RSP yield/start, msg=6 for osContStartReadData) never
    // being dropped when the 8-slot scheduler queue 0x8003B218 fills with VI
    // retraces during long asset decompressions. Restore ultramodern's default
    // requeue_timer = true (which recomp3ds_run.cpp disables by default).
    ultramodern::set_message_queue_control(ultramodern::MessageQueueControl{});

    constexpr int32_t osCicId = 0x80000310;
    MEM_W(osCicId, 0) = 6105;

    constexpr int32_t osRunningThread = 0x8002BE00;
    ultramodern::set_running_thread_variable(osRunningThread);
}

void register_conker_overlays_and_exports() {
    conker::register_overlays();
    conker::register_mod_exports();
}

static RspExitReason conker_hybrid_audio_ucode(uint8_t* rdram, uint32_t ucode_addr) {
    return conker::audio_hle_run(rdram, ucode_addr);
}

RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    if (task->t.type == M_AUDTASK) {
        // Native MP3 decoding already wrote PCM into the stream slots.
        // Running the original MP3 overlay would synthesise that PCM again.
        return conker_hybrid_audio_ucode;
    }
    std::fprintf(stderr, "Unknown RSP task: %" PRIu32 "\n", task->t.type);
    return nullptr;
}
} // namespace

// Choose a file in place: the original ROM, EEPROM and settings are never renamed.
// ROM hashes distinguish the retail game and the supported patched files.
void conker_set_rom_path(const char*);
extern "C" int conker_update_startup(PrintConsole*, PrintConsole*);

static int g_launcher_art_rom=-1;
static int choose_rom() {
    using namespace conker_rom_choices;
    bool present[count]{};
    struct stat st;
    for(int i=0;i<count;++i){char path[192];snprintf(path,sizeof(path),"sdmc:/3ds/ConkerRecomp/%s",choices[i].filename);
        present[i]=stat(path,&st)==0 && S_ISREG(st.st_mode);}
    const bool any_translation=present[2] || present[3] || present[4];
    bool translations=false;
    int selected=present[0]?0:present[1]?1:2;
    // libctru retains the selected console pointer beyond this function.
    static PrintConsole top{}, bottom{};
    consoleInit(GFX_TOP, &top);
    consoleInit(GFX_BOTTOM, &bottom);
    // Use the available ROM only for launcher artwork; the chosen version is
    // loaded again before running, so translations keep their own font.
    for(int i=0;i<count;++i)if(present[i]){
        char path[192];snprintf(path,sizeof(path),"sdmc:/3ds/ConkerRecomp/%s",choices[i].filename);
        if(recomp3ds::screen_conker_load_rom(path)){g_launcher_art_rom=i;break;}
    }
    if (!conker_update_startup(&top, &bottom)){conker_launcher_finish();return -1;}
    bool redraw = true;
    int result = -1;
    // Drain the launch button before accepting a choice.
    bool released = false;
    while (aptMainLoop()) {
        hidScanInput();
        const u32 down = hidKeysDown();
        if (!(hidKeysHeld() & (KEY_A | KEY_START | KEY_B | KEY_TOUCH))) released = true;
        if(released && (down & (KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT))){
            selected=wrap(selected,(down&(KEY_UP|KEY_LEFT))?-1:1);redraw=true;
        }
        bool accept=released && (down&(KEY_A|KEY_START));
        if(released && (down&KEY_TOUCH)){
            touchPosition touch;hidTouchRead(&touch);
            const int row=touch_row(touch.py);
            if(row>=0){selected=row;redraw=true;accept=true;}
        }
        if(accept){
            if(!translations && selected==2){translations=true;selected=0;redraw=true;}
            else {const int index=rom_index(translations,selected);if(present[index]){result=index;break;}}
        }
        if(released && (down&KEY_B)){
            if(translations){translations=false;selected=2;redraw=true;}
            else break;
        }
        if (redraw) {
            ConkerLauncherView view{};
            view.menu=1;view.selected=selected;
            view.stage=translations?"TRANSLATIONS":"SELECT VERSION";
            view.message="Pick a version below. Your ROM is checked before the game starts.";
            view.detail="SAVES AND SETTINGS ARE SHARED";
            view.actions=translations?"A: START    B: BACK":"A: SELECT    B: EXIT";
            for(int row=0;row<3;++row){
                const bool folder=!translations && row==2;
                const int index=rom_index(translations,row);
                view.labels[row]=folder?"TRANSLATIONS":choices[index].label;
                view.available[row]=folder?any_translation:present[index];
                view.notes[row]=folder?"ES FR PTBR":present[index]?choices[index].filename:"ROM NOT FOUND";
            }
            if(conker_launcher_draw(&view)){redraw=false;gspWaitForVBlank();continue;}
            consoleSelect(&top); consoleClear();
            printf("\x1b[3;4HConker: 3DS Recomp\n\n   Choose your ROM version.\n\n   Files are checked before the game starts.\n\n   All versions share your saves and settings.");
            if(translations)printf("\x1b[15;4HTranslations\n\n   CBFDfr.z64   - French\n   CBFDes.z64   - Spanish\n   CBFDptbr.z64 - Brazilian Portuguese");
            consoleSelect(&bottom);consoleClear();
            printf("\x1b[3;3H%s",translations?"TRANSLATIONS":"SELECT VERSION");
            for(int row=0;row<3;++row){
                const bool folder=!translations && row==2;
                const int index=rom_index(translations,row);
                const bool exists=folder?any_translation:present[index];
                const char* label=folder?"Translations >":choices[index].label;
                printf("\x1b[%d;3H%s %s%s",9+row*4,selected==row?">":" ",label,exists?"":" (missing)");
            }
            if(!translations)printf("\x1b[19;5HES FR PTBR");
            printf("\x1b[24;3HD-pad: choose   A/START: launch\n\n  Touch to select. B: %s",translations?"back":"exit");
            redraw = false;
        }
        gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
    }
    // Do not pass the selection press through to the game's intro/menu.
    while (result >= 0 && aptMainLoop()) {
        hidScanInput();
        if (!(hidKeysHeld() & (KEY_A | KEY_START | KEY_TOUCH))) break;
        gspWaitForVBlank();
    }
    if(result>=0){
        ConkerLauncherView view{};view.stage="STARTING GAME";
        view.message="Checking your ROM and preparing the game.";
        view.detail=choices[result].label;view.actions="PLEASE WAIT";
        // Use the same panel for loading, rather than the update heading.
        view.menu=2;view.selected=-1;
        conker_launcher_draw(&view);gspWaitForVBlank();
    }
    conker_launcher_finish();
    return result;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    recomp3ds::screen_set_conker_theme(true);
    recomp3ds::screen_conker_set_player_camera_query(conker::mouse_camera::normal_camera);
    recomp3ds::input_set_right_stick_owner([] { return conker::look_aim::swap_sticks_now(0) || (conker_3ds_free_camera_enabled() && conker::mouse_camera::stick_turns_camera()); });
    recomp3ds::input_set_transform([](uint16_t* b,float* x,float* y){
        if(!conker::look_aim::swap_sticks_now(0))return;
        const float move_x=*x,move_y=*y;
        recomp3ds::input_get_right_stick(x,y);
        // Physical C-buttons stay mapped. The left stick supplies movement.
        const bool turns=conker::look_aim::z_turns_aim(0) && (*b & recomp3ds::N64_Z);
        if(!turns){if(move_x<-.35f)*b|=recomp3ds::N64_CLEFT;if(move_x>.35f)*b|=recomp3ds::N64_CRIGHT;}
        if(move_y<-.35f)*b|=recomp3ds::N64_CDOWN;if(move_y>.35f)*b|=recomp3ds::N64_CUP;
    });
    recomp3ds::GameDesc desc{};
    desc.game_id = u8"CBFD";
    desc.sd_dir = "ConkerRecomp";
    desc.mods_folder = false;
    desc.console_video_options = true;
    desc.prepare_launch = [](recomp3ds::GameDesc& game) {
        const int version = choose_rom();
        if (version < 0) return false;
        const auto& choice=conker_rom_choices::choices[version];
        game.entry.rom_hash=choice.hash;
        const char* name=choice.filename;
        game.entry.rom_filename=std::u8string(reinterpret_cast<const char8_t*>(name));
        char path[192];
        snprintf(path,sizeof(path),"sdmc:/3ds/ConkerRecomp/%s",(const char*)game.entry.rom_filename.c_str());
        conker_set_rom_path(path);
        if (version!=g_launcher_art_rom && !recomp3ds::screen_conker_load_rom(path)) recomp3ds::screen_set_conker_theme(false);
        return true;
    };
    recomp::Version::from_string(CONKER_LAUNCHER_VERSION, desc.project_version);

    desc.rom_new3ds = recomp3ds::GameDesc::RomPlace::Stream;
    desc.rom_old3ds = recomp3ds::GameDesc::RomPlace::Stream;
    desc.rom_cache_mb = 24;
    desc.rom_block_kb = 256;
    desc.stall_frame_timeout_s = 15;

    desc.entry = recomp::GameEntry{
        .rom_hash = conker::roms::us_rom_hash,
        .internal_name = "CONKER BFD",
        .display_name = "Conker: 3DS Recomp",
        .game_id = u8"CBFD",
        .mod_game_id = "conker",
        .save_type = recomp::SaveType::Eep16k,
        .thumbnail_bytes = {},
        .is_enabled = true,
        .decompression_routine = conker::decompress_rom,
        .has_compressed_code = true,
        .entrypoint_address = (gpr)(int32_t)0x80001000u,
        .entrypoint = recomp_entrypoint,
        .thread_create_callback = on_thread_create,
        .on_init_callback = conker_on_init_3ds,
    };

    desc.rom_hint = "Use a US retail, Uncensored or translation ROM.\nFiles: CBFD.z64 / CBFDUncensored.z64\nCBFDfr.z64 / CBFDes.z64 / CBFDptbr.z64";
    desc.rsp.get_rsp_microcode = get_rsp_microcode;
    desc.register_overlays = register_conker_overlays_and_exports;

    desc.render.game_name = "Conker: 3DS Recomp";
    desc.render.gbi = rt64_3ds::Gbi::F3DEX2CBFD;
    desc.render.vi_width = 292;
    desc.render.vi_height = 216;
    // Untagged perspective cameras are the world, including intro models.
    // Offscreen shadow targets are rendered once without an eye offset.
    desc.render.rules = kStereoRules;
    desc.render.rule_count = sizeof(kStereoRules) / sizeof(kStereoRules[0]);
    desc.render.unclassified_persp = rt64_3ds::StereoClass::World;
    desc.render.unclassified_ortho = rt64_3ds::StereoClass::Hud;
    desc.render.no_near_clip = true;
    desc.render.render_to_texture = true;
    desc.render.sample_drawn_frame = true;
    desc.render.fix_rect_lr = true;
    desc.render.stretch_tint_quads = true;
    desc.render.merge_rect_slices = true;
    desc.render.scissor_edge_slack = 4;
    desc.render.unify_world_depth = false;
    desc.render.crosshair_row = false;
    desc.render.force_lod_branch = false;
    desc.render.screen_filters = true;
    desc.render.frame_interpolation = true;
    desc.render.double_eyes = true;
    desc.render.early_drawn = true;
    desc.render.eye_depth16 = true;
    desc.render.intensity_textures16 = true;
    desc.render.alpha_mask_mips = true;
    desc.render.fill_clear = true;
    desc.render.decal_window = true;
    desc.render.max_decal_bias = 0.0005f;

    desc.two_cores_game_on_core0 = true;
    desc.two_cores_read_on_game_core = true;
    // Ordinary voices remain on the DSP. The native main-bus hook builds
    // only Conker's separate MP3 stream into the AI output channel.
    desc.synth = recomp3ds::SynthFlavor::NAudio;
    desc.synth.cpu_stream = true;
    desc.synth.synth_global = 0x8002BA44;
    desc.synth.param_samples = 0x1C;
    desc.synth.cur_samples = 0x20;
    desc.synth.param_list = 0x40;
    desc.synth.aux_bus = 0x48;
    desc.synth.lame_list = 0x14;
    // Conker's actual n_audio ABI, verified against StartVoiceParams and the
    // envelope mixer: extra filters, virtual-voice fields and linked aux buses.
    desc.synth.pvoice_offset = 0x90;
    desc.synth.start_t_stack = 36;
    desc.synth.voice_unity = 0x1e;
    desc.synth.pvoice_control_bias = 8;
    desc.synth.aux_bus_stride = 0x44;
    desc.synth.aux_bus_count = 0x50;
    desc.audio_async = true;
    desc.audio_margin_frames = 4;
    desc.button_map = kButtons;
    desc.button_map_count = sizeof(kButtons) / sizeof(kButtons[0]);
    desc.menu_options = kOptions;
    desc.menu_option_count = sizeof(kOptions)/sizeof(kOptions[0]);
    desc.menu_defaults = kDefaults;
    desc.menu_default_count = sizeof(kDefaults) / sizeof(kDefaults[0]);
    desc.old3ds_frame_cap = 0;

#ifdef CONKER_NULL_RENDERER
    desc.null_renderer = true;
#endif

    return recomp3ds::run(desc);
}
