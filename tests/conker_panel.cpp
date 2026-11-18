#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
#include "../renderer/src/platform/conker_launcher_ui.h"

int main(int argc,char** argv){
    assert(argc==2 && load_conker_ui(argv[1]));
    assert(g_chocolate_ready && g_tail_frames_ready);
    assert(g_life_icon_ready && g_life_icon_faces==108);
    std::vector<uint8_t> mem(8*1024*1024);g_conker_rdram=mem.data();
    std::vector<uint16_t> blank(320*240),full(320*240),icons(320*240),hidden(320*240),chapter(320*240),notice(320*240);
    g_conker_options_visible=false;g_conker_chapter_visible=false;g_conker_lives=-1;
    game_background();std::fill(base,base+320*240,0);
    compose_conker_panel(blank.data());
    g_conker_lives=6;g_conker_lives_display=0;compose_conker_panel(full.data());
    g_conker_lives_display=1;compose_conker_panel(icons.data());
    g_conker_lives_display=2;compose_conker_panel(hidden.data());
    assert(hidden==blank && full!=icons && icons!=blank);
    // Icons-only preserves the actual tail artwork and moves it to the left.
    for(int y=4;y<36;++y)for(int x=62;x<208;++x)
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
    g_conker_menu=false;
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
    word(0xD2DB4,0);poll();assert(!g_conker_story_intro && g_conker_has_played && g_conker_lives==-1);
    assert(g_conker_health==-1 && g_conker_cash==-1);
    // Health and cash must not depend on whether the tail introduction ran.
    word(0xD2E4C,0x80100000);
    mem[(0xCC2D0+0x1CA)^3]=4;word(0xD2148,1200);poll();
    assert(g_conker_health==4 && g_conker_cash==1200 && g_conker_lives==-1);
    mem[(0x100000+0x19)^3]=4;poll();assert(g_conker_lives==6);
    // Switching save files must not retain the previous file's tail unlock.
    word(0xD2E4C,0x80100100);poll();assert(g_conker_lives==-1 && g_conker_health==4 && g_conker_cash==1200);
    mem[0x8FDA8^3]=0xFF;poll();assert(g_conker_lives==6);
    mem[0x8FDA8^3]=0;poll();assert(g_conker_lives==-1);
    word(0xD2E4C,0xFFFFFFFF);poll();assert(g_conker_lives==-1);
    word(0xD2E4C,0x80100000);poll();assert(g_conker_lives==6);
    word(0xD2148,0);poll();assert(g_conker_cash==-1 && g_conker_health==4 && g_conker_lives==6);
    mem[(0xCC2D0+0x1CA)^3]=0;poll();assert(g_conker_health==-1);
    mem[(0xCC2D0+0x1CA)^3]=4;word(0xD2148,1200);poll();
    g_conker_lives_display=0;g_conker_chapter_visible=true;
    compose_conker_panel(full.data());
    g_conker_health_visible=false;compose_conker_panel(hidden.data());assert(hidden!=full);
    for(int y=190;y<240;++y)for(int x=0;x<210;++x)assert(hidden[theme_index(x,y)]==full[theme_index(x,y)]);
    g_conker_money_visible=false;compose_conker_panel(icons.data());assert(icons!=hidden);
    for(int y=0;y<65;++y)for(int x=210;x<320;++x)assert(icons[theme_index(x,y)]==hidden[theme_index(x,y)]);
    g_conker_health_visible=g_conker_money_visible=true;
    mem[(0xCC2D0+0x1CA)^3]=2;word(0xD2148,0x7D00);poll();
    assert(g_conker_health==2 && g_conker_cash==1000000);
    compose_conker_panel(icons.data());assert(full!=icons);
    g_conker_animated_tails=true;compose_conker_panel(full.data());
    fake_tick+=SYSCLOCK_ARM11/6;compose_conker_panel(icons.data());assert(full!=icons);
    g_conker_animated_tails=false;
    // The first-death cutscene replaces the previous chapter/map title,
    // even before its completion flag has been saved. Resume normal titles
    // when gameplay returns, without keeping First Death on the next map.
    const int training_scene=g_conker_scene;
    mem[(0x100000+0x19)^3]=0;word(0xBE9F0,0x22);mem[0xC35EA^3]=1;poll();
    assert(g_conker_scenes[202].map==0x22 && g_conker_scenes[202].title[0]);
    assert(g_conker_scene==202 && g_conker_chapter==-1);
    assert(g_conker_lives==-1 && g_conker_health==-1 && g_conker_cash==-1);
    poll();assert(g_conker_scene==202);
    mem[(0x100000+0x19)^3]=4;poll();assert(g_conker_scene==202 && g_conker_lives==-1);
    word(0xBE9F0,0x29);mem[0xC35EA^3]=0;poll();
    assert(g_conker_scene==training_scene && g_conker_lives==6);
    mem[0xC35EA^3]=1;poll();assert(g_conker_lives==-1 && g_conker_health==-1 && g_conker_cash==-1);
    word(0xBE9F0,0x1D);mem[0xE0B94^3]=3;poll();assert(g_conker_lives==-1 && g_conker_chapter==-1);
    if(const char* sheet=getenv("CONKER_TAIL_PREVIEW")){
        FILE* f=fopen(sheet,"wb");assert(f);fprintf(f,"P6\n400 160\n255\n");
        for(int y=0;y<160;++y)for(int x=0;x<400;++x){const auto* p=g_tail_frames[(y/40)*10+x/40]+((y%40)*40+x%40)*4;
            uint8_t rgb[]={uint8_t(p[0]*p[3]/255),uint8_t(p[1]*p[3]/255),uint8_t(p[2]*p[3]/255)};fwrite(rgb,1,3,f);}fclose(f);
    }
    if(const char* preview=getenv("CONKER_PANEL_PREVIEW")){
        g_conker_menu=false;g_conker_lives=2;g_conker_health=4;g_conker_cash=1200;
        g_conker_scene=0;g_conker_chapter=0;g_conker_options_visible=true;compose_conker_panel(full.data());
        FILE* f=fopen(preview,"wb");assert(f);fprintf(f,"P6\n320 240\n255\n");
        for(int y=0;y<240;++y)for(int x=0;x<320;++x){uint16_t c=full[theme_index(x,y)];uint8_t rgb[]={uint8_t((c>>11&31)*255/31),uint8_t((c>>5&63)*255/63),uint8_t((c&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);
    }
    if(const char* prefix=getenv("CONKER_LAUNCHER_PREVIEW")){
        g_background_chapter=-1;game_background();g_launcher_backdrop.assign(base,base+320*240);
        ConkerLauncherView v{};v.menu=1;v.selected=2;v.stage="SELECT VERSION";
        v.message="Pick a version below. Your ROM is checked before the game starts.";
        v.detail="SAVES AND SETTINGS ARE SHARED";v.actions="A: SELECT    B: EXIT";
        v.labels[0]="ORIGINAL";v.labels[1]="UNCENSORED";v.labels[2]="TRANSLATIONS";
        v.notes[0]="CBFD.z64";v.notes[1]="CBFDUncensored.z64";v.notes[2]="ES FR PTBR";
        for(int i=0;i<3;++i)v.available[i]=1;
        for(int screen=0;screen<3;++screen){
            if(screen==2){v.menu=0;v.stage="DOWNLOADING UPDATE";v.message="Saving the new launcher...";v.detail="AVAILABLE  v0.1.5";v.actions="B: Cancel";v.done=60;v.total=100;}
            launcher_compose(full.data(),v,screen!=1);
            std::string filename=std::string(prefix)+std::to_string(screen)+".ppm";
            FILE* f=fopen(filename.c_str(),"wb");assert(f);fprintf(f,"P6\n320 240\n255\n");
            for(int y=0;y<240;++y)for(int x=0;x<320;++x){uint16_t c=full[theme_index(x,y)];uint8_t rgb[]={uint8_t((c>>11&31)*255/31),uint8_t((c>>5&63)*255/63),uint8_t((c&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);
        }
    }
    puts("PASS: ROM health/money artwork, live stats, independent health/money toggles, native tail animation, HUD visibility and Options rendering");
}
