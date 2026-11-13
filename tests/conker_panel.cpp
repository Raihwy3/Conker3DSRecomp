#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <zlib.h>
using u64=uint64_t;
constexpr u64 SYSCLOCK_ARM11=268123480;
u64 fake_tick=SYSCLOCK_ARM11;
u64 svcGetSystemTick(){return fake_tick;}
size_t tinfl_decompress_mem_to_mem(void* out,size_t n,const void* in,size_t len,int){
 z_stream z{};z.next_in=(Bytef*)in;z.avail_in=len;z.next_out=(Bytef*)out;z.avail_out=n;
 inflateInit2(&z,-15);int r=inflate(&z,Z_FINISH);inflateEnd(&z);return r==Z_STREAM_END?z.total_out:size_t(-1);
}
namespace recomp3ds { bool settings_perf_monitor(){return false;} }
bool g_screen_dirty=false,g_conker_menu=false;
uint16_t base[320*240];uint16_t* g_theme_base=base;
constexpr int theme_index(int x,int y){return x*240+239-y;}
constexpr uint16_t theme_rgb(int r,int g,int b){return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);}
#include "../renderer/src/platform/conker_menu_tabs.h"
#include "../renderer/src/platform/conker_console_ui.h"

int main(int argc,char** argv){
    assert(argc==2 && load_conker_ui(argv[1]));
    assert(g_life_icon_ready && g_life_icon_faces==108);
    std::vector<uint16_t> blank(320*240),full(320*240),icons(320*240),hidden(320*240),chapter(320*240),notice(320*240);
    g_conker_options_visible=false;g_conker_chapter_visible=false;g_conker_lives=-1;
    game_background();std::fill(base,base+320*240,0);
    compose_conker_panel(blank.data());
    g_conker_lives=6;g_conker_lives_display=0;compose_conker_panel(full.data());
    g_conker_lives_display=1;compose_conker_panel(icons.data());
    g_conker_lives_display=2;compose_conker_panel(hidden.data());
    assert(hidden==blank && full!=icons && icons!=blank);
    // Icons-only preserves the actual tail artwork and moves it to the left.
    for(int y=4;y<36;++y)for(int x=62;x<230;++x)
        assert(full[theme_index(x,y)]==icons[theme_index(x-50,y)]);
    g_conker_scene=0;g_conker_chapter=0;compose_conker_panel(hidden.data());
    g_conker_chapter_visible=true;compose_conker_panel(chapter.data());assert(chapter!=hidden);
    g_conker_chapter_visible=false;compose_conker_panel(blank.data());assert(blank==hidden);
    const char* text="\x1b[28;0HCapture saved: GFXTASK.BIN";record_game_text(text,strlen(text));
    compose_conker_panel(notice.data());assert(notice!=blank);
    g_conker_gfx_text=false;compose_conker_panel(hidden.data());assert(hidden==blank);
    // HUD visibility settings must not hide the Options interface itself.
    g_conker_menu=true;compose_conker_panel(hidden.data());
    g_conker_lives_display=0;g_conker_chapter_visible=true;compose_conker_panel(full.data());assert(full==hidden && full!=blank);
    // The opening visits Windy with a follow camera. Keep Introduction
    // and hide lives through both cutscene slots and camera lock states.
    std::vector<uint8_t> mem(8*1024*1024);g_conker_rdram=mem.data();
    g_conker_player_camera=[] {return true;};
    auto word=[&](uint32_t off,uint32_t value){memcpy(mem.data()+off,&value,4);};
    auto poll=[&]{fake_tick+=SYSCLOCK_ARM11;update_conker_status();};
    mem[0xE0B94^3]=0;mem[0xD2144^3]=7;word(0xBE9F0,0x18);
    mem[0xC35E8^3]=1;mem[0xC35EA^3]=1;poll();
    assert(g_conker_story_intro && g_conker_scene==191 && g_conker_chapter==0 && g_conker_lives==-1);
    word(0xBE9F0,6);mem[0xC35EA^3]=0;poll();
    assert(g_conker_scene==191 && g_conker_chapter==0 && g_conker_lives==-1 && !g_conker_has_played);
    word(0xBE9F0,0x29);mem[0xC35EB^3]=1;poll();assert(g_conker_story_intro && g_conker_lives==-1);
    mem[0xC35EB^3]=0;word(0xD2DB4,1);poll();assert(g_conker_story_intro && g_conker_lives==-1);
    word(0xD2DB4,0);poll();assert(!g_conker_story_intro && g_conker_has_played && g_conker_lives==6);
    mem[0xC35EA^3]=1;poll();assert(g_conker_lives==-1);
    word(0xBE9F0,0x1D);mem[0xE0B94^3]=3;poll();assert(g_conker_lives==-1 && g_conker_chapter==-1);
    puts("PASS: full/icon/hidden lives, chapter visibility, GFX task notice toggle and Options rendering");
}
