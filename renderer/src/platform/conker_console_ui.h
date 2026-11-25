// Conker's original UI font and OPTIONS sprite, decoded from the selected ROM.
// Included by log.cpp inside its private namespace; no artwork is embedded here.
#include "conker_launcher.h"
struct ConkerGlyph { int w=0,h=0,dx=0,dy=0; uint8_t pixels[32*32]{}; };
ConkerGlyph g_game_glyphs[95];
uint8_t g_game_map[95]{};
int8_t g_game_glyph_lut[256]{};
uint8_t g_options_art[96*32*4]{};
bool g_game_font_ready=false,g_options_ready=false;
bool g_conker_options_visible=true;
bool g_conker_animated_tails=true,g_conker_health_visible=true,g_conker_money_visible=true;
int g_conker_health=-1,g_conker_cash=-1;
u64 g_conker_tail_tick=0;
uint8_t g_money_art[32*32*4]{};
int g_conker_lives_display=1; // Full, icons only, hidden.
bool g_conker_chapter_visible=true,g_conker_gfx_text=false;
char g_conker_section_title[48]{};
char g_conker_subnote[128]{};
char g_game_cells[30][40];
uint8_t g_game_inks[30][40]{};
int g_text_x=0,g_text_y=0,g_text_ink=0;
const uint8_t* g_conker_rdram=nullptr;
bool (*g_conker_player_camera)()=nullptr;
bool g_conker_has_played=false,g_conker_story_intro=false;
bool g_conker_quit_prompt=false, g_conker_quit_restarts=false;
u64 g_conker_notice_until=0;
int g_conker_play_map=-1;
int g_background_chapter=-1;
int g_conker_bg_style=0;
struct ChapterSurface { int id,w,h,bits; };
// Verified bank-04 terrain consumers (map, segment 0):
// 41 bark; 6 mossy rock; 11 masonry; 12 barn boards; 53 cavern wall;
// 2 carved stone; 60 castle masonry; 4 weathered metal; 57 bank masonry.
// Menu surface: map 29 pub floor (resource 439, horizontal faces at y=0).
constexpr ChapterSurface kChapterSurfaces[12]={{2714,64,32,4},{1455,32,64,8},
    {217,64,64,4},{3511,64,32,4},{45,64,32,4},{3990,64,64,4},
    {1502,32,64,8},{4506,64,64,4},{853,32,32,8},{439,32,64,8},
    {1501,64,64,4},{1503,64,64,4}};
uint16_t g_chapter_surfaces[12][64*64]{};
bool g_chapter_surface_ready[12]{};
struct ConkerScene { char title[72]{}; uint8_t map=0; };
ConkerScene g_conker_scenes[204];
uint8_t g_chapter_art[9][96*32*4]{};
uint8_t g_life_digits[10][32*32*4]{};
int g_conker_scene=-1, g_conker_map=-1, g_conker_lives=-1, g_conker_chapter=-1;
uint8_t g_conker_bits[26]{};
u64 g_conker_poll=0;

uint32_t ui_word(const uint8_t* p) { return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3]; }
std::vector<uint8_t> ui_read(FILE* f,size_t offset,size_t size) {
    std::vector<uint8_t> out(size);
    if (fseek(f,(long)offset,SEEK_SET) || fread(out.data(),1,size,f)!=size) out.clear();
    return out;
}
std::vector<uint8_t> ui_unzip(const std::vector<uint8_t>& src,size_t limit) {
    if(src.size()<5) return {};
    const size_t n=ui_word(src.data());
    if(!n || n>limit) return {};
    std::vector<uint8_t> out(n);
    if(tinfl_decompress_mem_to_mem(out.data(),n,src.data()+4,src.size()-4,0)!=n) return {};
    return out;
}
#include "conker_lives_icon.h"

bool load_conker_ui(const char* path) {
    g_game_font_ready=g_options_ready=false;
    FILE* f=fopen(path,"rb"); if(!f) return false;
    auto font=ui_read(f,0x40F10,0x1540);
    auto data=ui_unzip(ui_read(f,0x42450+0x145ED8,0x15A388-0x145ED8),1<<20);
    bool good=font.size()==0x1540 && data.size()>=0xF200+4507*2;
    size_t at=0;
    for(int k=0;good && k<95;++k) {
        if(at+8>font.size()) {good=false;break;}
        auto& g=g_game_glyphs[k];g.w=font[at];g.h=font[at+1];g.dx=font[at+2];g.dy=font[at+3];
        size_t size=ui_word(font.data()+at+4),n=0;
        if(g.w>32 || g.h>32 || size<8 || at+size>font.size()) {good=false;break;}
        for(size_t p=at+8;p<at+size;++p) {
            const int count=(font[p]&15)+1;
            // Some translations leave zero-width glyphs or padding runs
            // after a glyph's bitmap. Decode only its declared pixel area.
            for(int j=0;j<count && n<(size_t)(g.w*g.h);++j)g.pixels[n++]=font[p]&0xF0;
        }
        if(n!=(size_t)(g.w*g.h))good=false;
        at+=size;
    }
    if(good) {
        memcpy(g_game_map,data.data()+0x2E10,95);
        memset(g_game_glyph_lut,-1,sizeof(g_game_glyph_lut));
        for(int i=94;i>=0;--i) {
            unsigned char mc=g_game_map[i];
            g_game_glyph_lut[mc]=(int8_t)i;
            if(mc>='A'&&mc<='Z')g_game_glyph_lut[mc+32]=(int8_t)i;
        }
        g_game_font_ready=true;
        for (int i=0;i<204;++i) {
            const uint8_t* rec=data.data()+0x4910+i*20;
            const uint32_t ptr=ui_word(rec);
            g_conker_scenes[i].map=(ui_word(rec+8)>>16)&255;
            if(ptr>=0x80082B20 && ptr-0x80082B20<data.size()) {
                const size_t off=ptr-0x80082B20;
                for(size_t j=0;j<71 && off+j<data.size() && data[off+j];++j)
                    g_conker_scenes[i].title[j]=(char)data[off+j];
            }
        }
        constexpr int chapters[9]={2108,2126,2102,2129,2117,2123,2120,2111,2105};
        // Barn Boys' resource comes from its native sprite-selector record.
        const int barn=ui_word(data.data()+0x800859E0-0x80082B20+68*8+4);
        constexpr int digits[10]={2226,2192,2219,2216,2178,2177,2210,2209,2176,2191};
        size_t offset=0x1A37E0; bool artwork=true;
        for(int id=0;id<=4506;++id) {
            const size_t n=(size_t(data[0xF200+id*2])<<8)|data[0xF201+id*2];
            uint8_t* target=nullptr;int target_width=32, tile=0;
            if(id==2038)target=g_money_art;
            if(id>=2035 && id<=2037){target=g_options_art;target_width=96;tile=id-2035;}
            for(int c=0;c<9;++c) {const int base=c==3?barn:chapters[c];
                if(id>=base && id<base+3){target=g_chapter_art[c];target_width=96;tile=id-base;}}
            for(int d=0;d<10;++d)if(id==digits[d])target=g_life_digits[d];
            for(int c=0;c<12;++c)if(id==kChapterSurfaces[c].id) {
                const auto& t=kChapterSurfaces[c];
                auto raw=ui_unzip(ui_read(f,offset,n),65536);
                const size_t palette_bytes=t.bits==4?32:512;
                const int stride=t.w*t.bits/8;
                if(raw.size()<size_t(stride*t.h)+palette_bytes)continue;
                const uint8_t* pal=raw.data()+raw.size()-palette_bytes;
                for(int y=0;y<t.h;++y)for(int x=0;x<t.w;++x) {
                    // Native TMEM odd-row swap; the model preview's origin is bottom-left.
                    const int sy=t.h-1-y, byte=(x*t.bits/8)^((sy&1)?4:0);
                    int index=raw[sy*stride+byte];
                    if(t.bits==4)index=(index>>((x&1)?0:4))&15;
                    const uint16_t rgba=(uint16_t(pal[index*2])<<8)|pal[index*2+1];
                    g_chapter_surfaces[c][y*t.w+x]=theme_rgb(((rgba>>11)&31)*255/31,((rgba>>6)&31)*255/31,((rgba>>1)&31)*255/31);
                }
                g_chapter_surface_ready[c]=true;
            }
            if(target) {
                auto rgba=ui_unzip(ui_read(f,offset,n),32*32*4);
                if(rgba.size()!=32*32*4){artwork=false;offset+=n;continue;}
                for(int y=0;y<32;++y)for(int x=0;x<32;++x)for(int c=0;c<4;++c) {
                    const int src=y*128+((x*4+c)^((y&1)?8:0));
                    target[(y*target_width+tile*32+x)*4+c]=rgba[src];
                }
            }
            offset+=n;
        }
        g_options_ready=artwork;
        load_conker_life_icon(f,data);
    }
    fclose(f);return good;
}
void record_game_text(const char* text,size_t count) {
    if(!g_game_font_ready)return;
    for(size_t i=0;i<count;++i) {
        unsigned char c=text[i];
        if(c==27 && i+1<count && text[i+1]=='[') {
            size_t end=i+2;while(end<count && !((text[end]>='A'&&text[end]<='Z')||(text[end]>='a'&&text[end]<='z')))++end;
            if(end>=count)break;
            int a=0,b=0;char params[40]{};const size_t len=std::min(end-i-2,sizeof(params)-1);memcpy(params,text+i+2,len);
            if(text[end]=='H' || text[end]=='f') {sscanf(params,"%d;%d",&a,&b);g_text_y=std::clamp(a-1,0,29);g_text_x=std::clamp(b-1,0,39);}
            if(text[end]=='m') {a=atoi(params);g_text_ink=a==33?1:a==31?2:a==90?3:0;}
            if(text[end]=='J' && atoi(params)==2)memset(g_game_cells,' ',sizeof(g_game_cells));
            i=end;continue;
        }
        if(c=='\r'){g_text_x=0;continue;}
        if(c=='\n'){g_text_x=0;g_text_y=std::min(g_text_y+1,29);continue;}
        if(c<32)continue;
        if(g_text_x>=40){g_text_x=0;g_text_y=std::min(g_text_y+1,29);}
        g_game_cells[g_text_y][g_text_x]=(char)c;g_game_inks[g_text_y][g_text_x]=g_text_ink;++g_text_x;
    }
}
int game_glyph(unsigned char c) {
    return g_game_glyph_lut[c];
}
int game_text_width(const char* s) {
    int w=0;for(;*s;++s){int k=game_glyph((unsigned char)*s);w+=*s==' '?4:k>=0?g_game_glyphs[k].w+g_game_glyphs[k].dx:0;}return w;
}
void game_blend(uint8_t* dest,int x,int y,int r,int g,int b,int alpha) {
    if(x<0||x>=320||y<0||y>=240||alpha<=0)return;
    uint8_t* d=&dest[(size_t)theme_index(x,y)*3];
    const int inv=255-alpha;
    d[0]=(uint8_t)((b*alpha+d[0]*inv)/255);
    d[1]=(uint8_t)((g*alpha+d[1]*inv)/255);
    d[2]=(uint8_t)((r*alpha+d[2]*inv)/255);
}
void game_text(uint8_t* dest,const char* s,int x,int y,int maxw,uint16_t color,bool center=false) {
    const int width=game_text_width(s);if(!width)return;
    const float scale=std::min(1.0f,(float)maxw/width);
    if(center)x-=int(width*scale)/2;
    const int r=((color>>11)&31)*255/31,g=((color>>5)&63)*255/63,b=(color&31)*255/31;
    for(int pass=0;pass<2;++pass){float cursor=(float)x;
        for(const unsigned char* p=(const unsigned char*)s;*p;++p){int k=game_glyph(*p);if(*p==' '){cursor+=4*scale;continue;}if(k<0)continue;
            const auto& glyph=g_game_glyphs[k];const int dw=std::max(1,(int)(glyph.w*scale)),dh=std::max(1,(int)(glyph.h*scale));
            for(int yy=0;yy<dh;++yy)for(int xx=0;xx<dw;++xx){int a=glyph.pixels[(yy*glyph.h/dh)*glyph.w+xx*glyph.w/dw];
                game_blend(dest,int(cursor+glyph.dx*scale)+xx+(pass==0),y+int(glyph.dy*scale)+yy+(pass==0),pass?r:0,pass?g:0,pass?b:0,a);}
            cursor+=(glyph.w+glyph.dx)*scale;
        }
    }
}
void game_line(char* out,int row,int start=0,int count=40) {
    memcpy(out,g_game_cells[row]+start,count);out[count]=0;
    while(count && out[count-1]==' ')out[--count]=0;
}
void game_options(uint8_t* dest,int x,int y) {
    if(!g_options_ready){game_text(dest,"OPTIONS",160,y+8,250,theme_rgb(255,220,80),true);return;}
    for(int yy=0;yy<32;++yy)for(int xx=0;xx<96;++xx){const auto* p=g_options_art+(yy*96+xx)*4;game_blend(dest,x+xx,y+yy,p[0],p[1],p[2],p[3]);}
}
void make_conker_panel() {memset(g_theme_base,0,320*240*3);memset(g_game_cells,' ',sizeof(g_game_cells));}
int chapter_for_scene(int i) {
    if(i<0 || i>=204)return -1;
    if(i<10 || i==191 || i==203)return 0;
    if(i<43 || (i>=192 && i<=194))return 1;
    if(i<69)return 3;
    if(i<87)return 2;
    if(i<117 || i==130 || i==131)return 5;
    if(i<138)return 4;
    if(i<166)return 7;
    if(i<175)return 6;
    if(i<183 || i==195 || i==199)return 8;
    return -1;
}
uint32_t game_mem_word(uint32_t a) { uint32_t v;memcpy(&v,g_conker_rdram+(a-0x80000000),4);return v; }
void update_conker_status() {
    if(!g_conker_rdram || !g_game_font_ready)return;
    const u64 now=svcGetSystemTick();if(now-g_conker_poll<SYSCLOCK_ARM11/4)return;g_conker_poll=now;
    // The native game's player stat 3 includes the current life; its HUD
    // (151EB7B0) subtracts one before displaying the spare-life count.
    const int map=(int)game_mem_word(0x800BE9F0);
    // The follow-camera also runs in scripted intro shots. Phase 0 is
    // the native play loop (151DDC20); phases 1/3/11 are opening/menu.
    const int phase=g_conker_rdram[0xE0B94^3];
    const bool in_game=phase==0 && map>=0 && map<0x44 && map!=0x25 && map!=0x21 && map!=0x1D;
    const int lives=in_game?std::clamp((int)(int8_t)g_conker_rdram[(0xD2144)^3]-1,0,99):-1;
    const uint32_t save=game_mem_word(0x800D2E4C);
    int chosen=map==g_conker_map?g_conker_scene:-1;
    if(chosen<0 || chosen>=204 || g_conker_scenes[chosen].map!=map)chosen=-1;
    for(int i=0;in_game && i<204;++i) {
        if(!g_conker_scenes[i].title[0] || g_conker_scenes[i].map!=map)continue;
        bool reached=false,just=false;
        if(save>=0x80000000 && save<=0x807FFFE6) {
            const uint8_t bits=g_conker_rdram[((save-0x80000000)+(i>>3))^3];
            reached=(bits>>(i&7))&1;just=reached && !(g_conker_bits[i>>3]&(1<<(i&7)));
        }
        if(chosen<0 || just)chosen=i;
    }
    if(save>=0x80000000 && save<=0x807FFFE6)
        for(int i=0;i<26;++i)g_conker_bits[i]=g_conker_rdram[((save-0x80000000)+i)^3];
    int chapter=chapter_for_scene(chosen);
    int displayed_lives=lives;
    const bool menu_map=map==0x25 || map==0x21 || map==0x1D;
    const bool scripted=g_conker_rdram[0xC35EA^3]!=0 || g_conker_rdram[0xC35EB^3]!=0 || game_mem_word(0x800D2DB4)!=0;
    const bool player_camera=g_conker_player_camera && g_conker_player_camera();
    const bool controlled=in_game && player_camera && !scripted;
    // ROM scene 191 is Hungover / Introduction: cutscene 1 starts on map
    // 0x18 and visits other maps. Their follow cameras do not mean play.
    if(menu_map){g_conker_story_intro=false;g_conker_has_played=false;g_conker_play_map=-1;}
    if(map==0x18 && g_conker_rdram[0xC35E8^3]==1 && g_conker_rdram[0xC35EA^3]!=0)
        g_conker_story_intro=true;
    if(g_conker_story_intro && map==0x29 && controlled)g_conker_story_intro=false;
    if(controlled && !g_conker_story_intro){g_conker_has_played=true;g_conker_play_map=map;}
    // First Death has its own map and scene title. Unlike ordinary scripted
    // cutaways, it must not keep the title from the map where Conker died.
    if(in_game && map==0x22){
        chosen=202;chapter=chapter_for_scene(chosen);displayed_lives=-1;
    }else if(g_conker_story_intro){
        chosen=191;chapter=0;displayed_lives=-1;
    }else if(!in_game || !g_conker_has_played){
        chosen=-1;chapter=-1;displayed_lives=-1;
    }else{
        if(scripted || (!player_camera && map!=g_conker_play_map))displayed_lives=-1;
        if(!controlled && map!=g_conker_play_map){chosen=g_conker_scene;chapter=g_conker_chapter;}
    }
    // The native player record stores chocolate health at +0x1CA;
    // stat 6 is the story cash word. Match the game's million-dollar sentinel.
    const bool show_stats=displayed_lives>=0;
    // Native pause menu 151EB080: spare lives become visible after the
    // First Death introduction (save scene 202), or in its special mode.
    // Read the active save each poll; never carry unlocks between files.
    const bool valid_save=save>=0x80000000 && save<=0x807FFFE6;
    const bool tails_unlocked=valid_save &&
        (g_conker_rdram[((save-0x80000000)+0x19)^3]&4);
    const bool special_mode=int8_t(g_conker_rdram[0x8FDA8^3])<0;
    if(!tails_unlocked && !special_mode)displayed_lives=-1;
    // The chocolate renderer (150900F0) draws the native health count,
    // rather than consulting a separate permanent unlock flag. Money in
    // 151EB554 is likewise omitted when the current total is zero.
    const int native_health=std::clamp(int(g_conker_rdram[(0xCC2D0+0x1CA)^3]),0,6);
    const int health=show_stats && native_health>0?native_health:-1;
    int cash=show_stats?std::clamp(int(int32_t(game_mem_word(0x800D2148))),0,9999999):-1;
    if(cash==0x7D00)cash=1000000;
    if(cash==0)cash=-1;
    if(health!=g_conker_health || cash!=g_conker_cash)g_screen_dirty=true;
    g_conker_health=health;g_conker_cash=cash;
    if(map!=g_conker_map || displayed_lives!=g_conker_lives || chosen!=g_conker_scene || chapter!=g_conker_chapter)g_screen_dirty=true;
    g_conker_map=map;g_conker_lives=displayed_lives;g_conker_scene=chosen;g_conker_chapter=chapter;
}
void game_background() {
    int target=9;
    switch(g_conker_bg_style) {
        case 1: target=6; break;
        case 2: target=10; break;
        case 3: target=11; break;
        case 4: target=2; break;
        case 5: target=8; break;
        case 6: target=12; break;
        default: target=9; break;
    }
    if(g_background_chapter==target)return;
    g_background_chapter=target;
    if(target==12) {
        for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
            const float fx=std::min(1.0f,std::min(x,319-x)/38.0f);
            const float fy=std::min(1.0f,std::min(y,239-y)/26.0f);
            const float sx=fx*fx*(3.f-2.f*fx), sy=fy*fy*(3.f-2.f*fy);
            uint8_t* dst=&g_theme_base[(size_t)theme_index(x,y)*3];
            dst[0]=(uint8_t)(38.f*sx*sy+0.5f);
            dst[1]=(uint8_t)(32.f*sx*sy+0.5f);
            dst[2]=(uint8_t)(28.f*sx*sy+0.5f);
        }
        return;
    }
    const int chapter=target;
    if(!g_chapter_surface_ready[chapter]) {
        memset(g_theme_base,0,320*240*3);
        return;
    }
    const auto& t=kChapterSurfaces[chapter];
    // Blur the repeating tile in float precision (no intermediate RGB565
    // truncation), wrapping samples at its edges so copies stay seamless.
    std::vector<float> horiz((size_t)t.w*t.h*3), blurred((size_t)t.w*t.h*3);
    constexpr float weights[9]={1.f/25.f,2.f/25.f,3.f/25.f,4.f/25.f,5.f/25.f,4.f/25.f,3.f/25.f,2.f/25.f,1.f/25.f};
    for(int y=0;y<t.h;++y)for(int x=0;x<t.w;++x) {
        float r=0.f,g=0.f,b=0.f;
        for(int k=-4;k<=4;++k) {
            const int sx=(x+k+t.w)%t.w;
            const uint16_t p=g_chapter_surfaces[chapter][y*t.w+sx];
            const float w=weights[k+4];
            r+=((p>>11)&31)*(w/31.f);
            g+=((p>>5)&63)*(w/63.f);
            b+=(p&31)*(w/31.f);
        }
        float* dst=&horiz[(size_t)(y*t.w+x)*3];
        dst[0]=r;dst[1]=g;dst[2]=b;
    }
    for(int y=0;y<t.h;++y)for(int x=0;x<t.w;++x) {
        float r=0.f,g=0.f,b=0.f;
        for(int k=-4;k<=4;++k) {
            const int sy=(y+k+t.h)%t.h;
            const float* src=&horiz[(size_t)(sy*t.w+x)*3];
            const float w=weights[k+4];
            r+=src[0]*w;g+=src[1]*w;b+=src[2]*w;
        }
        float* dst=&blurred[(size_t)(y*t.w+x)*3];
        dst[0]=r;dst[1]=g;dst[2]=b;
    }
    for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
        const float u=(x+0.5f)*0.5f-0.5f;
        const float v=(y+0.5f)*0.5f-0.5f;
        const int ix0=(int)(u+(float)t.w)-t.w;
        const int iy0=(int)(v+(float)t.h)-t.h;
        const float tx=u-(float)ix0, ty=v-(float)iy0;
        const int x0=(ix0+t.w)%t.w, x1=(x0+1)%t.w;
        const int y0=(iy0+t.h)%t.h, y1=(y0+1)%t.h;
        const float* p00=&blurred[(size_t)(y0*t.w+x0)*3];
        const float* p10=&blurred[(size_t)(y0*t.w+x1)*3];
        const float* p01=&blurred[(size_t)(y1*t.w+x0)*3];
        const float* p11=&blurred[(size_t)(y1*t.w+x1)*3];
        const float w00=(1.f-tx)*(1.f-ty), w10=tx*(1.f-ty), w01=(1.f-tx)*ty, w11=tx*ty;
        const float cr=p00[0]*w00+p10[0]*w10+p01[0]*w01+p11[0]*w11;
        const float cg=p00[1]*w00+p10[1]*w10+p01[1]*w01+p11[1]*w11;
        const float cb=p00[2]*w00+p10[2]*w10+p01[2]*w01+p11[2]*w11;
        const float fx=std::min(1.0f,std::min(x,319-x)/38.0f);
        const float fy=std::min(1.0f,std::min(y,239-y)/26.0f);
        const float sx=fx*fx*(3.f-2.f*fx), sy=fy*fy*(3.f-2.f*fy);
        const float shade=100.f*sx*sy;
        uint8_t* dst=&g_theme_base[(size_t)theme_index(x,y)*3];
        dst[0]=(uint8_t)std::clamp((int)(cb*shade+0.5f),0,255);
        dst[1]=(uint8_t)std::clamp((int)(cg*shade+0.5f),0,255);
        dst[2]=(uint8_t)std::clamp((int)(cr*shade+0.5f),0,255);
    }
}
void game_sprite(uint8_t* dest,const uint8_t* image,int w,int h,int x,int y) {
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){const auto* p=image+(yy*w+xx)*4;game_blend(dest,x+xx,y+yy,p[0],p[1],p[2],p[3]);}
}
// One ROM-derived tail for each spare life. Crop transparent margins so
// normal counts sit beside LIVES; large counts wrap within the header.
void game_lives(uint8_t* dest,uint16_t colour) {
    if(g_conker_lives_display==2)return;
    if(g_conker_lives_display==0)game_text(dest,"LIVES",12,14,46,colour);
    if(!g_life_icon_ready || g_conker_lives<=0)return;
    static int left=0,right=0,top=0,bottom=0;
    if(right==0){
        left=top=kLifeIconSize;
        for(int y=0;y<kLifeIconSize;++y)for(int x=0;x<kLifeIconSize;++x)
            if(g_life_icon[(y*kLifeIconSize+x)*4+3]){
                left=std::min(left,x);right=std::max(right,x+1);top=std::min(top,y);bottom=std::max(bottom,y+1);
            }
    }
    if(right<=left || bottom<=top)return;
    const bool animated=g_conker_animated_tails && g_tail_frames_ready;
    const int crop_left=animated?g_tail_crop[0]:left,crop_top=animated?g_tail_crop[2]:top;
    const int count=std::clamp(g_conker_lives,0,99),sw=animated?g_tail_crop[1]-g_tail_crop[0]:right-left,sh=animated?g_tail_crop[3]-g_tail_crop[2]:bottom-top;
    const int start_x=g_conker_lives_display==1?12:62;
    const int available=208-start_x;
    int height=32,width=1,gap=3,columns=1;
    for(;height>=8;--height){
        width=std::max(1,sw*height/sh);gap=std::max(2,height/8);
        columns=std::max(1,available/(width+gap));const int rows=(count+columns-1)/columns;
        if(rows*height+(rows-1)*3<=64)break;
    }
    for(int i=0;i<count;++i){
        const int x=start_x+(i%columns)*(width+gap),y=4+(i/columns)*(height+3);
        const uint8_t* image=g_life_icon;
        if(animated){
            const int frame=(svcGetSystemTick()/(SYSCLOCK_ARM11/30)+i*2)%kTailFrames;
            image=g_tail_frames[frame];
        }
        for(int yy=0;yy<height;++yy)for(int xx=0;xx<width;++xx){
            const auto* p=image+((crop_top+yy*sh/height)*kLifeIconSize+crop_left+xx*sw/width)*4;
            game_blend(dest,x+xx,y+yy,p[0],p[1],p[2],p[3]);
        }
    }
}
// Original pause-menu artwork, placed in the bottom screen's corners.
void game_scaled_sprite(uint8_t* dest,const uint8_t* image,int source_w,int source_h,int x,int y,int w,int h,int alpha=255) {
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){const auto* p=image+((yy*source_h/h)*source_w+xx*source_w/w)*4;
        game_blend(dest,x+xx,y+yy,p[0],p[1],p[2],p[3]*alpha/255);}
}
void game_health_money(uint8_t* dest) {
    if(g_conker_health_visible && g_conker_health>=0 && g_chocolate_ready)
        for(int i=0;i<6;++i)game_scaled_sprite(dest,g_chocolate_icon,kLifeIconSize,kLifeIconSize,
            224+(i%3)*27,5+(i/3)*26,26,26,i<g_conker_health?255:48);
    if(g_conker_money_visible && g_conker_cash>=0){
        game_sprite(dest,g_money_art,32,32,8,199);
        char text[16];snprintf(text,sizeof(text),"%d",g_conker_cash);
        int x=39;for(const char* c=text;*c;++c,x+=19)
            game_scaled_sprite(dest,g_life_digits[*c-'0'],32,32,x,201,24,24);
    }
}
void game_rect(uint8_t* dest,int x,int y,int w,int h,int r,int g,int b,int a) {
    for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)game_blend(dest,xx,yy,r,g,b,a);
}
void game_box(uint8_t* dest,int x,int y,int w,int h,int r,int g,int b,int a) {
    for(int xx=x;xx<x+w;++xx){game_blend(dest,xx,y,r,g,b,a);game_blend(dest,xx,y+h-1,r,g,b,a);}
    for(int yy=y+1;yy<y+h-1;++yy){game_blend(dest,x,yy,r,g,b,a);game_blend(dest,x+w-1,yy,r,g,b,a);}
}
enum class AchTier { Bronze, Silver, Gold };
struct AchStyle {
    int wr, wg, wb;       // trophy highlight
    int yr, yg, yb;       // trophy body
    int ar, ag, ab;       // trophy shadow
    int hi_r, hi_g, hi_b; // border/highlight
    int lo_r, lo_g, lo_b; // border shadow
    int tx_r, tx_g, tx_b; // text accent
    int bg_r, bg_g, bg_b; // selected row fill (unlocked)
    int lk_r, lk_g, lk_b; // selected row fill (locked)
};
inline AchTier conker_ach_tier(const char* title) {
    if(!title) return AchTier::Silver;
    if(!strcmp(title,"Bad Fur Day")) return AchTier::Bronze;
    if(!strcmp(title,"What a Day") || !strcmp(title,"Every Chapter")) return AchTier::Gold;
    return AchTier::Silver;
}
inline const AchStyle& conker_ach_style(AchTier tier) {
    static constexpr AchStyle kBronze{ 255,200,152, 205,127,50,  138,74,24,  216,134,72,  132,72,30,  224,146,80, 108,60,26,  64,42,26 };
    static constexpr AchStyle kSilver{ 255,255,255, 204,212,224, 128,138,152, 224,232,242, 130,140,154, 218,226,238, 84,96,114, 48,56,68 };
    static constexpr AchStyle kGold  { 255,250,190, 255,214,56,  196,130,22,  255,223,70,  175,118,24, 255,223,70, 125,88,22,  68,58,36 };
    return (tier==AchTier::Bronze)?kBronze:((tier==AchTier::Silver)?kSilver:kGold);
}
inline constexpr const char* kTrophy16[16]={
    "....OOOOOOOO....",
    "...OYYYYYYYYO...",
    ".OOYWYYYYYYAYOO.",
    "OYOYYWYYYYYAYOYO",
    "OYOYYWYYYYYAYOYO",
    ".OOYYYYYYYYAAOO.",
    "...OOYYYYAAOO...",
    "....OYYYYAAO....",
    ".....OYYAAO.....",
    "......OYAO......",
    "......OYAO......",
    ".....OYYYYO.....",
    "....OYYYYAAO....",
    "...OAAAAAAAAO...",
    "...OOOOOOOOOO...",
    "................"
};
void game_ach_icon(uint8_t* dest,int x,int y,bool unlocked,AchTier tier=AchTier::Gold) {
    if(unlocked){
        const AchStyle& s=conker_ach_style(tier);
        for(int yy=0;yy<15;++yy)for(int xx=0;xx<16;++xx){
            const char c=kTrophy16[yy][xx];
            if(c=='O')game_blend(dest,x+xx,y+yy,18,10,4,255);
            else if(c=='W')game_blend(dest,x+xx,y+yy,s.wr,s.wg,s.wb,255);
            else if(c=='Y')game_blend(dest,x+xx,y+yy,s.yr,s.yg,s.yb,255);
            else if(c=='A')game_blend(dest,x+xx,y+yy,s.ar,s.ag,s.ab,255);
        }
        return;
    }
    static const char* const kLock[12]={
        "............",
        "....SSSS....",
        "...S....S...",
        "...S....S...",
        "..BBBBBBBB..",
        "..BBBBBBBB..",
        "..BBBKKBBB..",
        "..BBBKKBBB..",
        "..BBBBBBBB..",
        "..BBBBBBBB..",
        "............",
        "............"
    };
    for(int yy=0;yy<12;++yy)for(int xx=0;xx<12;++xx){
        const char c=kLock[yy][xx];
        if(c=='S')game_blend(dest,x+2+xx,y+2+yy,155,162,172,220);
        else if(c=='B')game_blend(dest,x+2+xx,y+2+yy,105,112,122,220);
        else if(c=='K')game_blend(dest,x+2+xx,y+2+yy,35,38,44,240);
    }
}
void compose_conker_panel(uint8_t* dest) {
    static char previous_notice[41]{};
    char notice[41];game_line(notice,27);
    if(strcmp(previous_notice,notice)){strcpy(previous_notice,notice);g_conker_notice_until=notice[0]?svcGetSystemTick()+SYSCLOCK_ARM11*4:0;}
    game_background();
    memcpy(dest,g_theme_base,320*240*3);
    const uint16_t white=theme_rgb(255,255,255),yellow=theme_rgb(255,223,70),red=theme_rgb(255,110,95),muted=theme_rgb(160,166,174);
    char line[41];
    if(!g_conker_rdram && g_game_font_ready){
        game_text(dest,"CONKER: 3DS RECOMP",160,24,288,yellow,true);
        game_text(dest,CONKER_LAUNCHER_VERSION,160,49,280,white,true);
        game_text(dest,"GETTING READY",160,99,280,yellow,true);
        game_text(dest,"PREPARING THE GAME",160,126,280,white,true);
        game_text(dest,"YOUR ADVENTURE STARTS SOON",160,155,286,white,true);
        return;
    }
    if(g_conker_quit_prompt) {
        game_text(dest,"QUIT GAME?",160,68,290,yellow,true);
        game_text(dest,g_conker_quit_restarts?"QUITTING RESTARTS THE CONSOLE":"RETURN TO THE LAUNCHER",160,104,292,white,true);
        game_text(dest,"QUIT",80,158,120,yellow,true);
        game_text(dest,"RESUME",240,158,120,white,true);
        game_text(dest,"A",80,184,100,white,true);
        game_text(dest,"B",240,184,100,white,true);
        return;
    }
    if(!g_conker_menu){
        if(g_conker_lives>=0)game_lives(dest,white);
        game_health_money(dest);
        if(g_conker_chapter>=0) {if(g_conker_chapter_visible)game_sprite(dest,g_chapter_art[g_conker_chapter],96,32,112,84);}
        if(g_conker_chapter_visible && g_conker_scene>=0)game_text(dest,g_conker_scenes[g_conker_scene].title,160,126,292,white,true);
        if(g_conker_options_visible)game_options(dest,220,204);
    }else{
        game_options(dest,112,2);game_text(dest,"BACK",8,16,64,yellow);
        game_text(dest,g_conker_section_title,222,16,90,white);
        for(int tab=0;tab<conker_menu::count;++tab){
            const bool active=g_game_inks[5][tab*conker_menu::cell_width]==1;
            const int x0=conker_menu::tab_x0[tab], w=conker_menu::tab_w[tab];
            game_text(dest,conker_menu::labels[tab],x0+w/2,42,w-6,active?yellow:white,true);
            if(active)for(int x=x0+4;x<x0+w-4;++x)game_blend(dest,x,57,255,223,70,150);
        }
        const bool ach_page=!strcmp(g_conker_section_title,"Trophies");
        AchTier selected_tier=AchTier::Gold;
        for(int slot=0;slot<7;++slot){
            const int row=7+slot*2, y=60+slot*20;
            game_line(line,row,2,21);if(!line[0])continue;
            const bool selected=g_game_cells[row][0]=='>';
            char val[24]{};game_line(val,row,24,13);
            const bool unlocked=!strcmp(val,"UNLOCKED");
            const bool readonly=unlocked || !strcmp(val,"LOCKED");
            if(ach_page && readonly){
                const AchTier tier=conker_ach_tier(line);
                const AchStyle& st=conker_ach_style(tier);
                const uint16_t tier_col=theme_rgb(st.tx_r,st.tx_g,st.tx_b);
                if(selected){
                    selected_tier=tier;
                    game_rect(dest,8,y,304,18,unlocked?st.bg_r:st.lk_r,unlocked?st.bg_g:st.lk_g,unlocked?st.bg_b:st.lk_b,120);
                    game_box(dest,8,y,304,18,st.hi_r,st.hi_g,st.hi_b,210);
                }else if(unlocked){
                    game_rect(dest,8,y,304,18,st.bg_r*60/100,st.bg_g*60/100,st.bg_b*60/100,90);
                    game_box(dest,8,y,304,18,st.hi_r*75/100,st.hi_g*75/100,st.hi_b*75/100,130);
                }else{
                    game_rect(dest,8,y,304,18,22,26,32,95);
                    game_box(dest,8,y,304,18,76,82,92,115);
                }
                game_ach_icon(dest,11,y+2,unlocked,tier);
                game_text(dest,line,29,y+4,206,selected?tier_col:(unlocked?white:muted));
                game_rect(dest,240,y+2,68,14,unlocked?st.bg_r:38,unlocked?st.bg_g:42,unlocked?st.bg_b:48,145);
                game_text(dest,unlocked?"EARNED":"LOCKED",274,y+4,62,unlocked?tier_col:muted,true);
            }else{
                const AchStyle& sv=conker_ach_style(AchTier::Silver);
                const AchStyle& gd=conker_ach_style(AchTier::Gold);
                const bool disabled=g_game_inks[row][2]==3;
                if(disabled){
                    if(selected){
                        game_rect(dest,8,y,304,18,sv.lk_r,sv.lk_g,sv.lk_b,120);
                        game_box(dest,8,y,304,18,gd.hi_r,gd.hi_g,gd.hi_b,180);
                    }else{
                        game_rect(dest,8,y,304,18,22,26,32,95);
                        game_box(dest,8,y,304,18,76,82,92,115);
                    }
                }else if(selected){
                    game_rect(dest,8,y,304,18,gd.bg_r,gd.bg_g,gd.bg_b,120);
                    game_box(dest,8,y,304,18,gd.hi_r,gd.hi_g,gd.hi_b,210);
                    game_rect(dest,9,y+2,3,14,255,223,70,220);
                }else{
                    game_rect(dest,8,y,304,18,sv.bg_r*60/100,sv.bg_g*60/100,sv.bg_b*60/100,90);
                    game_box(dest,8,y,304,18,sv.hi_r*75/100,sv.hi_g*75/100,sv.hi_b*75/100,130);
                }
                const uint16_t text_col=disabled?muted:(selected?yellow:white);
                game_text(dest,line,17,y+4,172,text_col);
                game_text(dest,val,194,y+4,80,text_col);
                if(!readonly){
                    const bool is_folder=!strcmp(val,"OPEN");
                    if(!disabled){
                        if(!is_folder){
                            game_rect(dest,277,y+2,17,14,255,223,70,35);
                            game_text(dest,"<",283,y+4,12,yellow);
                        }
                        game_rect(dest,295,y+2,15,14,255,223,70,35);
                        game_text(dest,">",300,y+4,12,yellow);
                    }else{
                        game_text(dest,"<",283,y+4,12,muted);
                        game_text(dest,">",300,y+4,12,muted);
                    }
                }
            }
        }
        game_line(line,21);
        int ach_u=0,ach_t=0;
        if(ach_page && sscanf(line,"Unlocked: %d / %d",&ach_u,&ach_t)==2 && ach_t>0){
            char prog[24];snprintf(prog,sizeof(prog),"%d / %d EARNED",ach_u,ach_t);
            game_text(dest,prog,8,203,116,yellow);
            game_rect(dest,130,206,120,8,20,24,30,150);
            game_box(dest,130,206,120,8,190,155,55,160);
            const int fill=(116*ach_u)/ach_t;
            if(fill>0)game_rect(dest,132,208,fill,4,255,212,64,230);
            if(strstr(line,"^ more"))game_text(dest,"^",264,203,16,white);
            if(strstr(line,"v more"))game_text(dest,"v",288,203,16,white);
        }else{
            game_text(dest,line,8,208,304,white);
        }
        if(g_conker_subnote[0]){
            if(ach_page){
                const AchStyle& st=conker_ach_style(selected_tier);
                const uint16_t desc_col=theme_rgb(st.tx_r,st.tx_g,st.tx_b);
                game_rect(dest,8,218,304,19,18,22,28,150);
                game_box(dest,8,218,304,19,st.hi_r*70/100,st.hi_g*70/100,st.hi_b*70/100,155);
                const bool hidden=!strcmp(g_conker_subnote,"???");
                game_text(dest,g_conker_subnote,14,222,292,hidden?muted:desc_col);
            }else{
                game_rect(dest,8,218,304,19,18,22,28,150);
                game_box(dest,8,218,304,19,76,82,92,130);
                game_text(dest,g_conker_subnote,14,222,292,yellow);
            }
        }
    }
    // FPS is optional, and belongs in the footer rather than a terminal header.
    if(recomp3ds::settings_perf_monitor() && (!g_conker_menu || !g_conker_subnote[0])) {game_line(line,1);game_text(dest,line,8,g_conker_menu?224:180,304,white);}
    const bool ach_toast=!strncmp(notice,"Achievement unlocked: ",22) && svcGetSystemTick()<=g_conker_notice_until;
    if(ach_toast){
        const AchTier tier=conker_ach_tier(notice+22);
        const AchStyle& st=conker_ach_style(tier);
        const uint16_t hdr=theme_rgb(st.tx_r,st.tx_g,st.tx_b);
        game_rect(dest,20,202,280,32,26,22,16,210);
        game_box(dest,20,202,280,32,st.hi_r,st.hi_g,st.hi_b,235);
        game_ach_icon(dest,26,210,true,tier);
        game_text(dest,"ACHIEVEMENT UNLOCKED",46,205,244,hdr);
        game_text(dest,notice+22,46,218,244,white);
    }
    for(int row=22;row<30;++row){if(row==27 && (ach_toast || !g_conker_gfx_text || svcGetSystemTick()>g_conker_notice_until))continue;game_line(line,row);if(line[0])game_text(dest,line,4,row*8-1,312,g_game_inks[row][0]==2?red:white);}
}
bool render_conker_toast_rgba8(const char* text,uint32_t* out,int& out_w,int& out_h) {
    if(!g_game_font_ready || !text || !*text)return false;
    constexpr int W=184,H=34;
    out_w=W;out_h=H;
    uint8_t buf[W*H*4]{};
    auto set_px=[&](int x,int y,int r,int g,int b,int a){
        if((unsigned)x>=(unsigned)W || (unsigned)y>=(unsigned)H)return;
        uint8_t* p=buf+(y*W+x)*4;p[0]=(uint8_t)r;p[1]=(uint8_t)g;p[2]=(uint8_t)b;p[3]=(uint8_t)a;
    };
    auto blend_px=[&](int x,int y,int r,int g,int b,int a){
        if((unsigned)x>=(unsigned)W || (unsigned)y>=(unsigned)H || a<=0)return;
        uint8_t* p=buf+(y*W+x)*4;
        p[0]=(uint8_t)((r*a+p[0]*(255-a))/255);
        p[1]=(uint8_t)((g*a+p[1]*(255-a))/255);
        p[2]=(uint8_t)((b*a+p[2]*(255-a))/255);
        p[3]=(uint8_t)std::max((int)p[3],a);
    };
    const bool is_ach=!strncmp(text,"Achievement unlocked: ",22);
    const AchTier tier=is_ach?conker_ach_tier(text+22):AchTier::Gold;
    const AchStyle& st=conker_ach_style(tier);
    static uint8_t toast_pub[64*32*3]{};
    static bool toast_pub_ready=false;
    if(!toast_pub_ready && g_chapter_surface_ready[9]){
        constexpr int tw=32,th=64;
        for(int y=0;y<th;++y)for(int x=0;x<tw;++x){
            int sr=0,sg=0,sb=0;
            for(int ky=-1;ky<=1;++ky)for(int kx=-1;kx<=1;++kx){
                const int w=(kx==0?2:1)*(ky==0?2:1);
                const uint16_t p=g_chapter_surfaces[9][((y+ky+th)%th)*tw+((x+kx+tw)%tw)];
                sr+=(((p>>11)&31)*255/31)*w;
                sg+=(((p>>5)&63)*255/63)*w;
                sb+=((p&31)*255/31)*w;
            }
            const int idx=(y*tw+x)*3;
            toast_pub[idx+0]=(uint8_t)(sr/16);
            toast_pub[idx+1]=(uint8_t)(sg/16);
            toast_pub[idx+2]=(uint8_t)(sb/16);
        }
        toast_pub_ready=true;
    }
    for(int y=0;y<H;++y){
        for(int x=0;x<W;++x){
            const int cx=x<3?3-x:(x>=W-3?x-(W-4):0);
            const int cy=y<3?3-y:(y>=H-3?y-(H-4):0);
            if(cx+cy>4)continue;
            const int d=std::min({x,y,W-1-x,H-1-y});
            if(d==0 || cx+cy==4){set_px(x,y,16,10,5,245);continue;}
            if(d<=2){
                const bool hi=(x<W-3 && y<H-3 && (x<=2 || y<=2));
                if(is_ach)set_px(x,y,hi?st.hi_r:st.lo_r,hi?st.hi_g:st.lo_g,hi?st.hi_b:st.lo_b,245);
                else set_px(x,y,hi?190:115,hi?170:100,hi?140:80,240);
                continue;
            }
            if(d==3){set_px(x,y,24,14,6,238);continue;}
            const float fx=std::min(1.0f,std::min(x-3,W-4-x)/22.0f);
            const float fy=std::min(1.0f,std::min(y-3,H-4-y)/10.0f);
            const float vig=0.55f+0.45f*(fx*fx*(3-2*fx))*(fy*fy*(3-2*fy));
            int r=44,g=28,b=14;
            if(toast_pub_ready){
                const int idx=((y&63)*32+(x&31))*3;
                r=int(toast_pub[idx+0]*0.44f*vig);
                g=int(toast_pub[idx+1]*0.44f*vig);
                b=int(toast_pub[idx+2]*0.44f*vig);
            }else{
                r=int((46-(y/4))*vig);g=int((30-(y/5))*vig);b=int((15-(y/8))*vig);
            }
            set_px(x,y,r,g,b,242);
        }
    }
    if(is_ach){
        for(int ty=0;ty<16;++ty){
            for(int tx=0;tx<16;++tx){
                const char c=kTrophy16[ty][tx];
                if(c=='O')set_px(7+tx,9+ty,18,10,4,255);
                else if(c=='W')set_px(7+tx,9+ty,st.wr,st.wg,st.wb,255);
                else if(c=='Y')set_px(7+tx,9+ty,st.yr,st.yg,st.yb,255);
                else if(c=='A')set_px(7+tx,9+ty,st.ar,st.ag,st.ab,255);
            }
        }
    }
    auto draw_n64=[&](const char* s,int x0,int y0,int maxw,int r,int g,int b){
        const int width=game_text_width(s);if(!width)return;
        const float scale=std::min(0.85f,(float)maxw/width);
        for(int pass=0;pass<2;++pass){
            float cursor=(float)x0;
            for(const unsigned char* p=(const unsigned char*)s;*p;++p){
                int k=game_glyph(*p);if(*p==' '){cursor+=4*scale;continue;}if(k<0)continue;
                const auto& glyph=g_game_glyphs[k];
                const int dw=std::max(1,(int)(glyph.w*scale)),dh=std::max(1,(int)(glyph.h*scale));
                for(int yy=0;yy<dh;++yy)for(int xx=0;xx<dw;++xx){
                    const int a=glyph.pixels[(yy*glyph.h/dh)*glyph.w+xx*glyph.w/dw];
                    blend_px(int(cursor+glyph.dx*scale)+xx+(pass==0),y0+int(glyph.dy*scale)+yy+(pass==0),pass?r:0,pass?g:0,pass?b:0,a);
                }
                cursor+=(glyph.w+glyph.dx)*scale;
            }
        }
    };
    if(is_ach){
        draw_n64("ACHIEVEMENT UNLOCKED",27,5,150,st.tx_r,st.tx_g,st.tx_b);
        draw_n64(text+22,27,18,150,255,250,232);
    }else{
        draw_n64(text,8,11,168,255,250,232);
    }
    for(int i=0;i<W*H;++i){
        const uint8_t* p=buf+i*4;
        out[i]=((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3];
    }
    return true;
}
