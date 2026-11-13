// stderr (and stdout: the runtime's printf is diagnostics too) as whole lines
// to svcOutputDebugString (the emulator's log, 3dslink) and to
// sdmc:/3ds/<Game>/log.txt. newlib hands an unbuffered stream to write() one
// fragment at a time, so lines are assembled here first. The bottom screen's
// console is written directly (screen_printf).
//
// The SD writes happen on a writer thread fed through a ring
// buffer: flushing each line from the thread that logged it stalled the
// renderer 70-100 ms whenever it wrote its periodic report. Lines reach the
// card within ~50 ms, so a crash loses at most that tail; before the writer
// starts and on exit (log_flush) the writes are synchronous.
#include <3ds.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <algorithm>
#include <vector>
#include "miniz/miniz.h"
#include <unistd.h>
#include <sys/reent.h>
#include <sys/iosupport.h>

#include "recomp3ds_internal.h"
#include "recomp3ds.h"
#include "rt64_3ds.h"
#include "conker_menu_tabs.h"
#include "conker_launcher.h"
#include <string>

namespace {

char g_line[512];
int g_len = 0;
FILE* g_file = nullptr;
LightLock g_lock;

// The ring the writer drains (guarded by g_ring_lock, never held over I/O).
constexpr size_t kRing = 64 * 1024;
char g_ring[kRing];
size_t g_head = 0, g_tail = 0;       // write at head, read from tail
uint32_t g_dropped = 0;
LightLock g_ring_lock;
Thread g_writer = nullptr;
volatile bool g_writer_run = false;
// settings.ini's log_level (log_set_level): 2 everything, 1 without the
// renderer's periodic reports and late-frame notes (and the stats line only
// every 10 s, recomp3ds_run.cpp), 0 without the stats and synth lines too:
// boot, warnings and exit. Each line costs a debug-string syscall, a copy
// and its share of the card writes on core 0; the default is 1, 0 on an
// Old 3DS (settings_menu.cpp).
int g_level = 2;

inline bool starts(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

// Only the periodic figures go, by prefix. Everything that appears only
// when something is wrong stays at every level, 0 included: the stall
// watch's `recomp3ds: STALL` / `STALL over`, `recomp3ds: WATCHDOG`, errors.
// A hard freeze leaves its trace through them.
bool dropped_at_level(const char* l) {
    if (g_level >= 2) return false;
    // The renderer's profile (settings.ini's dbg_profile 1 or 3) is the
    // periodic report's indented lines and the frame line it explains: kept
    // at any level once asked for.
    if ((rt64_3ds::settings().dbg_profile & 1) && (starts(l, "rt64-3ds: frame ") || starts(l, "rt64-3ds:   "))) return false;
    if (starts(l, "rt64-3ds: late frame") || starts(l, "rt64-3ds: frame ") || starts(l, "rt64-3ds:   ")) return true;
    if (g_level >= 1) return false;
    return starts(l, "stats:") || starts(l, "dsp-synth ");
}

// When the card write under way began (0: none), for the stall watch: a
// write that never returns stops the log without a word in it.
volatile u64 g_write_since = 0;

void write_file(const char* p, size_t n) {
    if (g_file != nullptr) {
        g_write_since = svcGetSystemTick() | 1;
        fwrite(p, 1, n, g_file);
        fflush(g_file);
        g_write_since = 0;
    }
}

// Moves what the ring holds to the card; returns whether anything was written.
bool drain_ring() {
    static char chunk[8192];
    bool any = false;
    for (;;) {
        LightLock_Lock(&g_ring_lock);
        size_t n = 0;
        while (g_tail != g_head && n < sizeof(chunk)) {
            chunk[n++] = g_ring[g_tail];
            g_tail = (g_tail + 1) % kRing;
        }
        uint32_t dropped = g_dropped;
        g_dropped = 0;
        LightLock_Unlock(&g_ring_lock);
        if (dropped != 0) {
            char note[64];
            int k = snprintf(note, sizeof(note), "[log: %lu bytes dropped]\n", (unsigned long)dropped);
            write_file(note, (size_t)k);
        }
        if (n == 0) return any;
        write_file(chunk, n);
        any = true;
    }
}

// log_sync: what is written goes to the card itself at the next pass (a
// freeze's lines would otherwise be lost with the power-off that ends it).
volatile bool g_sync_request = false;

void writer_thread(void*) {
    while (g_writer_run) {
        drain_ring();
        if (g_sync_request && g_file != nullptr) {
            g_sync_request = false;
            g_write_since = svcGetSystemTick() | 1;
            fsync(fileno(g_file));
            g_write_since = 0;
        }
        svcSleepThread(50 * 1000000ll);
    }
    drain_ring();
}

void queue_line(const char* p, size_t n) {
    if (g_writer == nullptr) {       // before the writer starts: straight out
        write_file(p, n);
        return;
    }
    LightLock_Lock(&g_ring_lock);
    const size_t used = (g_head + kRing - g_tail) % kRing;
    if (used + n >= kRing) {
        g_dropped += (uint32_t)n;
    }
    else {
        for (size_t i = 0; i < n; i++) {
            g_ring[g_head] = p[i];
            g_head = (g_head + 1) % kRing;
        }
    }
    LightLock_Unlock(&g_ring_lock);
}

void flush_line() {
    g_line[g_len] = '\0';
    recomp3ds::autotest_scan_line(g_line);      // test scripts see every line
    if (!dropped_at_level(g_line)) {
        g_line[g_len] = '\n';
        g_line[g_len + 1] = '\0';
        svcOutputDebugString(g_line, g_len + 1);
        queue_line(g_line, (size_t)g_len + 1);
    }
    g_len = 0;
}

ssize_t stderr_write(struct _reent* r, void* fd, const char* ptr, size_t len) {
    (void)r; (void)fd;
    LightLock_Lock(&g_lock);
    for (size_t i = 0; i < len; i++) {
        char c = ptr[i];
        if (c == '\n') {
            flush_line();
        }
        else {
            g_line[g_len++] = c;
            if (g_len >= (int)sizeof(g_line) - 2) {
                flush_line();
            }
        }
    }
    LightLock_Unlock(&g_lock);
    return (ssize_t)len;
}

// libctru's console device, taken before stdout is pointed at the log.
const devoptab_t* g_screen = nullptr;
LightLock g_screen_lock;
volatile bool g_screen_dirty = false;   // the console drew since screen_flush

// Who has the touch screen (screen_bottom_update): the console, writing its
// framebuffer, or the renderer, drawing the game's HUD there with the
// console's text over it; the console then writes g_shadow, a copy of its
// framebuffer the renderer reads (rt64_3ds::set_bottom_console).
PrintConsole* g_console = nullptr;
uint16_t* g_shadow = nullptr;
bool g_on_shadow = false;               // the console writes g_shadow
bool g_releasing = false;               // the renderer was told to stop; waiting for its last copy
uint32_t g_release_ms = 0;
// Conker's status/menu is composed when data changes; no live scene readback.
// Text records and the completed LCD image have separate storage.
bool g_conker_theme = false, g_conker_menu = false;
uint16_t* g_theme_text = nullptr;
uint16_t* g_theme_base = nullptr;
uint16_t* g_theme_output = nullptr;
constexpr int theme_index(int x, int y) { return x * 240 + 239 - y; }
constexpr uint16_t theme_rgb(int r, int g, int b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
#include "conker_console_ui.h"
#include "conker_launcher_ui.h"

constexpr uint32_t kReleaseMs = 150;    // a copy queued before the stop is on the screen by then

uint32_t tick_ms() { return (uint32_t)(svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000)); }

uint16_t* bottom_fb(size_t* bytes) {
    u16 w = 0, h = 0;
    u8* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
    *bytes = (size_t)w * h * 2;         // RGB565, as consoleInit sets it
    return (uint16_t*)fb;
}

// The console back on its framebuffer, with what it wrote meanwhile.
void console_to_fb() {
    size_t bytes = 0;
    uint16_t* fb = bottom_fb(&bytes);
    LightLock_Lock(&g_screen_lock);
    memcpy(fb, g_shadow, bytes);
    g_console->frameBuffer = fb;
    LightLock_Unlock(&g_screen_lock);
    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)fb, bytes);
    g_on_shadow = g_releasing = false;
}

const devoptab_t g_errtab = {
    .name = "recomp3ds_stderr",
    .structSize = 0,
    .write_r = stderr_write,
};

}   // namespace

void recomp3ds::log_init(const char* base_path) {
    LightLock_Init(&g_lock);
    LightLock_Init(&g_ring_lock);
    char path[192];
    snprintf(path, sizeof(path), "%s/log.txt", base_path);
    g_file = fopen(path, "w");
    // 3dslink owns stdout and stderr through its own socket devoptab; leave
    // that alone.
    if (__3dslink_host.s_addr == 0) {
        devoptab_list[STD_ERR] = &g_errtab;
        devoptab_list[STD_OUT] = &g_errtab;
        setvbuf(stderr, nullptr, _IONBF, 0);
        setvbuf(stdout, nullptr, _IONBF, 0);
    }
}

extern "C" int conker_launcher_draw(const ConkerLauncherView* view) {
    if(!view || !g_game_font_ready)return 0;
    if(g_launcher_backdrop.empty()){
        g_launcher_backdrop.resize(320*240);g_launcher_canvas.resize(320*240);
        auto* saved=g_theme_base;const int chapter=g_background_chapter;
        g_theme_base=g_launcher_backdrop.data();g_background_chapter=-1;game_background();
        g_theme_base=saved;g_background_chapter=chapter;
    }
    for(int screen=0;screen<2;++screen){
        const bool top=screen==0;
        launcher_compose(g_launcher_canvas.data(),*view,top);
        u16 w=0,h=0;auto* fb=(uint16_t*)gfxGetFramebuffer(top?GFX_TOP:GFX_BOTTOM,GFX_LEFT,&w,&h);
        if(!fb)continue;
        const int width=top?400:320,offset=(width-320)/2;
        for(int x=0;x<width;++x)for(int y=0;y<240;++y){
            const int sx=x-offset;
            fb[x*240+239-y]=sx>=0 && sx<320?g_launcher_canvas[theme_index(sx,y)]:g_launcher_backdrop[theme_index(x<offset?0:319,y)];
        }
    }
    gfxFlushBuffers();gfxSwapBuffers();return 1;
}
extern "C" void conker_launcher_finish() {
    std::vector<uint16_t>().swap(g_launcher_backdrop);std::vector<uint16_t>().swap(g_launcher_canvas);
}

void recomp3ds::screen_set_conker_theme(bool enabled) { g_conker_theme = enabled; }
void recomp3ds::screen_conker_set_options_button(int visible) { g_conker_options_visible=visible!=0;g_screen_dirty=true; }
void recomp3ds::screen_conker_set_health_display(int enabled) {g_conker_health_visible=enabled!=0;g_screen_dirty=true;}
void recomp3ds::screen_conker_set_money_display(int enabled) {g_conker_money_visible=enabled!=0;g_screen_dirty=true;}
void recomp3ds::screen_conker_set_animated_tails(int enabled) {g_conker_animated_tails=enabled!=0;g_screen_dirty=true;}
void recomp3ds::screen_conker_set_lives_display(int mode) {g_conker_lives_display=std::clamp(mode,0,2);g_screen_dirty=true;}
void recomp3ds::screen_conker_set_chapter_display(int visible) {g_conker_chapter_visible=visible!=0;g_screen_dirty=true;}
void recomp3ds::screen_conker_set_gfx_text(int visible) {g_conker_gfx_text=visible!=0;g_screen_dirty=true;}
bool recomp3ds::screen_conker_options_visible() { return g_conker_options_visible; }
bool recomp3ds::screen_conker_load_rom(const char* path) { return load_conker_ui(path); }
bool recomp3ds::screen_conker_theme() { return g_conker_theme && g_theme_text != nullptr; }
void recomp3ds::screen_conker_menu(bool open) { g_conker_menu = open; g_screen_dirty = true; }
void recomp3ds::screen_conker_set_rdram(const uint8_t* rdram) { g_conker_rdram=rdram; }
void recomp3ds::screen_conker_set_player_camera_query(bool (*active)()) { g_conker_player_camera=active; }
void recomp3ds::screen_conker_update() {
    update_conker_status();
    const u64 tail_tick=svcGetSystemTick()/(SYSCLOCK_ARM11/30);
    if(g_conker_animated_tails && g_conker_lives>0 && g_conker_lives_display!=2 && !g_conker_menu && tail_tick!=g_conker_tail_tick){
        g_conker_tail_tick=tail_tick;g_screen_dirty=true;
    }
    if(g_conker_notice_until && svcGetSystemTick()>g_conker_notice_until) {
        memset(g_game_cells[27],' ',40);g_conker_notice_until=0;g_screen_dirty=true;
    }
}
void recomp3ds::screen_conker_section_title(const char* title) { snprintf(g_conker_section_title,sizeof(g_conker_section_title),"%s",title);g_screen_dirty=true; }

void recomp3ds::screen_init(PrintConsole* console) {
    LightLock_Init(&g_screen_lock);
    g_screen = devoptab_list[STD_OUT];
    g_console = console;
    if (g_conker_theme && console) {
        g_theme_text = (uint16_t*)calloc(320*240,sizeof(uint16_t));
        g_theme_base = (uint16_t*)malloc(320*240*sizeof(uint16_t));
        g_theme_output = (uint16_t*)malloc(320*240*sizeof(uint16_t));
        if (!g_theme_text || !g_theme_base || !g_theme_output) {
            free(g_theme_text);free(g_theme_base);free(g_theme_output);g_theme_text=g_theme_base=g_theme_output=nullptr;
            g_conker_theme=false;
        } else {
            make_conker_panel();
            gfxSetDoubleBuffering(GFX_BOTTOM,true);
            console->frameBuffer=g_theme_text;
            g_screen_dirty=true;
        }
    }
}

void recomp3ds::screen_conker_quit_prompt(bool show,bool restarts) {
    g_conker_quit_prompt=show;g_conker_quit_restarts=restarts;g_screen_dirty=true;
}

void recomp3ds::screen_bottom_update(bool hud) {
    if (g_console == nullptr) return;
    if (screen_conker_theme() && !hud) return;
    if (hud) {
        if (!g_on_shadow) {
            size_t bytes = 0;
            uint16_t* fb = bottom_fb(&bytes);
            if (g_shadow == nullptr) g_shadow = (uint16_t*)malloc(bytes);
            if (g_shadow == nullptr) return;
            LightLock_Lock(&g_screen_lock);
            memcpy(g_shadow, fb, bytes);
            g_console->frameBuffer = g_shadow;
            LightLock_Unlock(&g_screen_lock);
            g_on_shadow = true;
            rt64_3ds::set_bottom_console(g_shadow);
        }
        g_releasing = false;
        rt64_3ds::set_bottom_screen(true);
        return;
    }
    if (!g_on_shadow) return;
    if (!g_releasing) {
        rt64_3ds::set_bottom_screen(false);
        g_releasing = true;
        g_release_ms = tick_ms();
        return;
    }
    // No copy queued since the stop, and the last one long enough ago.
    if (tick_ms() - g_release_ms >= kReleaseMs && rt64_3ds::bottom_idle_ms() >= kReleaseMs) console_to_fb();
}

uint16_t recomp3ds::screen_pixel(int x, int y) {
    if (g_console == nullptr || x < 0 || x >= 320 || y < 0 || y >= 240) return 0;
    return g_console->frameBuffer[x * 240 + (239 - y)];
}

void recomp3ds::screen_take_console(uint32_t timeout_ms) {
    if (g_console == nullptr || !g_on_shadow) return;
    rt64_3ds::set_bottom_screen(false);
    if (!g_releasing) { g_releasing = true; g_release_ms = tick_ms(); }
    const uint32_t t0 = tick_ms();
    while (!(tick_ms() - g_release_ms >= kReleaseMs && rt64_3ds::bottom_idle_ms() >= kReleaseMs) && tick_ms() - t0 < timeout_ms) {
        svcSleepThread(10 * 1000000ll);
    }
    console_to_fb();
}

// One call's text reaches the console whole (escape sequences included),
// whichever thread writes.
void recomp3ds::screen_printf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0 || g_screen == nullptr) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    LightLock_Lock(&g_screen_lock);
    record_game_text(buf,(size_t)n);
    if (!screen_conker_theme()) g_screen->write_r(_REENT, nullptr, buf, (size_t)n);
    g_screen_dirty = true;
    LightLock_Unlock(&g_screen_lock);
    if (g_on_shadow) rt64_3ds::note_bottom_console_changed();
}

// A line of its own at the bottom of the touch screen, in red (empty: clear
// it), written and put on the display from any thread: the stall watch's,
// when the main thread that does both otherwise may be the one stuck.
// Skipped while another thread holds the console.
void recomp3ds::screen_alert(const char* text, int row) {
    if (g_screen == nullptr || LightLock_TryLock(&g_screen_lock) != 0) return;
    char buf[64];
    const int n = snprintf(buf, sizeof(buf), "\x1b[%d;0H\x1b[31m%-39.39s\x1b[0m", row, text);
    record_game_text(buf,(size_t)n);
    if (!screen_conker_theme()) g_screen->write_r(_REENT, nullptr, buf, (size_t)n);
    g_screen_dirty=true;
    LightLock_Unlock(&g_screen_lock);
    if (g_on_shadow) rt64_3ds::note_bottom_console_changed();
    u16 w = 0, h = 0;
    u8* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
    if (screen_conker_theme()) screen_flush();
    else if (fb != nullptr) svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)fb, (u32)w * h * gspGetBytesPerPixel(gfxGetScreenFormat(GFX_BOTTOM)));
}

void recomp3ds::log_sync() { g_sync_request = true; }

uint32_t recomp3ds::log_write_stuck_ms() {
    const u64 since = g_write_since;
    return since != 0 ? (uint32_t)((svcGetSystemTick() - since) / 268123ull) : 0;
}

// The console draws into the bottom framebuffer through the CPU's cache, and
// the display reads memory: what it drew is written back here, with the
// kernel's flush on this core (gfxFlushBuffers asks the GSP module on core
// 1, and the renderer's frame end no longer flushes the whole linear heap).
void recomp3ds::screen_flush() {
    if (!g_screen_dirty) return;
    g_screen_dirty = false;
    if (g_on_shadow) return;            // the renderer reads the copy
    u16 w = 0, h = 0;
    u8* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
    if (fb == nullptr) return;
    const u32 bytes = (u32)w * h * gspGetBytesPerPixel(gfxGetScreenFormat(GFX_BOTTOM));
    if (screen_conker_theme()) {
        LightLock_Lock(&g_screen_lock);
        compose_conker_panel(g_theme_output);
        // Present the completed RGB565 image, then flush and swap the LCD buffer.
        volatile uint16_t* lcd=(volatile uint16_t*)fb;
        for(size_t i=0;i<320*240;++i)lcd[i]=g_theme_output[i];
        LightLock_Unlock(&g_screen_lock);
    }
    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)fb, bytes);
    if (screen_conker_theme()) {
        GSPGPU_FlushDataCache(fb,bytes);
        gfxScreenSwapBuffers(GFX_BOTTOM,false);
    }
}

void recomp3ds::log_set_level(int level) { g_level = level; }
int recomp3ds::log_level() { return g_level; }

void recomp3ds::log_line(const char* s) {
    fprintf(stderr, "%s\n", s);
}

// From here on lines go through the writer thread. It sits just above the
// runtime's VI and timer threads (0x28), and so above the game threads: it
// needs almost no CPU (it mostly waits on the card); below the game it
// starved whenever the game kept core 0 busy, and below the timer thread the
// stall watch's report of that thread spinning never reached the card.
void recomp3ds::log_start_writer() {
    if (g_writer != nullptr || g_file == nullptr) return;
    g_writer_run = true;
    g_writer = threadCreate(writer_thread, nullptr, 16 * 1024, 0x27, 0, false);
}

// Everything logged so far onto the card, and synchronous from then on
// (exit paths). A writer that does not finish within a second (its card
// write stuck) is left alone, and so is the file: draining the ring here
// too would share its chunk buffer and FILE with it, and block this thread
// (the exit) on the same card.
void recomp3ds::log_flush() {
    if (g_writer != nullptr) {
        g_writer_run = false;
        if (R_FAILED(threadJoin(g_writer, 1000000000ull))) {
            svcOutputDebugString("recomp3ds: log writer stuck; log.txt ends here\n", 47);
            return;
        }
        threadFree(g_writer);
        g_writer = nullptr;
    }
    drain_ring();
}
