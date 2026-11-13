// Conker's original UI font and OPTIONS sprite, decoded from the selected ROM.
// Included by log.cpp inside its private namespace; no artwork is embedded here.
struct ConkerGlyph { int w=0,h=0,dx=0,dy=0; uint8_t pixels[32*32]{}; };
ConkerGlyph g_game_glyphs[95];
uint8_t g_game_map[95]{};
uint8_t g_options_art[96*32*4]{};
bool g_game_font_ready=false,g_options_ready=false;
bool g_conker_options_visible=true;
int g_conker_lives_display=0; // Full, icons only, hidden.
bool g_conker_chapter_visible=true,g_conker_gfx_text=true;
char g_conker_section_title[48]{};
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
struct ChapterSurface { int id,w,h,bits; };
// Verified bank-04 terrain consumers (map, segment 0):
// 41 bark; 6 mossy rock; 11 masonry; 12 barn boards; 53 cavern wall;
// 2 carved stone; 60 castle masonry; 4 weathered metal; 57 bank masonry.
// Menu surface: map 29 pub floor (resource 439, horizontal faces at y=0).
constexpr ChapterSurface kChapterSurfaces[10]={{2714,64,32,4},{1455,32,64,8},
    {217,64,64,4},{3511,64,32,4},{45,64,32,4},{3990,64,64,4},
    {1502,32,64,8},{4506,64,64,4},{853,32,32,8},{439,32,64,8}};
uint16_t g_chapter_surfaces[10][64*64]{};
bool g_chapter_surface_ready[10]{};
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
        if(!g.w || !g.h || g.w>32 || g.h>32 || size<8 || at+size>font.size()) {good=false;break;}
        for(size_t p=at+8;p<at+size;++p) {
            const int count=(font[p]&15)+1;
            if(n+count>(size_t)(g.w*g.h)) {good=false;break;}
            for(int j=0;j<count;++j)g.pixels[n++]=font[p]&0xF0;
        }
        if(n!=(size_t)(g.w*g.h))good=false;
        at+=size;
    }
    if(good) {
        memcpy(g_game_map,data.data()+0x2E10,95);
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
            if(id>=2035 && id<=2037){target=g_options_art;target_width=96;tile=id-2035;}
            for(int c=0;c<9;++c) {const int base=c==3?barn:chapters[c];
                if(id>=base && id<base+3){target=g_chapter_art[c];target_width=96;tile=id-base;}}
            for(int d=0;d<10;++d)if(id==digits[d])target=g_life_digits[d];
            for(int c=0;c<10;++c)if(id==kChapterSurfaces[c].id) {
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
            if(text[end]=='m') {a=atoi(params);g_text_ink=a==33?1:a==31?2:0;}
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
    if(c>='a'&&c<='z')c-=32;
    for(int i=0;i<95;++i)if(g_game_map[i]==c)return i;
    return -1;
}
int game_text_width(const char* s) {
    int w=0;for(;*s;++s){int k=game_glyph((unsigned char)*s);w+=*s==' '?4:k>=0?g_game_glyphs[k].w+g_game_glyphs[k].dx:0;}return w;
}
void game_blend(uint16_t* dest,int x,int y,int r,int g,int b,int alpha) {
    if(x<0||x>=320||y<0||y>=240||alpha<=0)return;
    uint16_t& d=dest[theme_index(x,y)];int dr=((d>>11)&31)*255/31,dg=((d>>5)&63)*255/63,db=(d&31)*255/31;
    d=theme_rgb((r*alpha+dr*(255-alpha))/255,(g*alpha+dg*(255-alpha))/255,(b*alpha+db*(255-alpha))/255);
}
void game_text(uint16_t* dest,const char* s,int x,int y,int maxw,uint16_t color,bool center=false) {
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
void game_options(uint16_t* dest,int x,int y) {
    if(!g_options_ready){game_text(dest,"OPTIONS",160,y+8,250,theme_rgb(255,220,80),true);return;}
    for(int yy=0;yy<32;++yy)for(int xx=0;xx<96;++xx){const auto* p=g_options_art+(yy*96+xx)*4;game_blend(dest,x+xx,y+yy,p[0],p[1],p[2],p[3]);}
}
void make_conker_panel() {memset(g_theme_base,0,320*240*2);memset(g_game_cells,' ',sizeof(g_game_cells));}
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
    if(g_conker_story_intro){
        chosen=191;chapter=0;displayed_lives=-1;
    }else if(!in_game || !g_conker_has_played){
        chosen=-1;chapter=-1;displayed_lives=-1;
    }else{
        if(scripted || (!player_camera && map!=g_conker_play_map))displayed_lives=-1;
        if(!controlled && map!=g_conker_play_map){chosen=g_conker_scene;chapter=g_conker_chapter;}
    }
    if(map!=g_conker_map || displayed_lives!=g_conker_lives || chosen!=g_conker_scene || chapter!=g_conker_chapter)g_screen_dirty=true;
    g_conker_map=map;g_conker_lives=displayed_lives;g_conker_scene=chosen;g_conker_chapter=chapter;
}
void game_background() {
    const int chapter=9; // Keep the pub floor throughout menus and gameplay.
    if(g_background_chapter==chapter)return;
    g_background_chapter=chapter;
    const auto& t=kChapterSurfaces[chapter];
    // Blur the repeating tile once per chapter, wrapping samples at its
    // edges so neighbouring copies stay seamless. UI glyphs stay sharp.
    static uint16_t horizontal[64*64], blurred[64*64];
    constexpr int weights[9]={1,2,3,4,5,4,3,2,1};
    for(int pass=0;pass<2;++pass)for(int y=0;y<t.h;++y)for(int x=0;x<t.w;++x) {
        int r=0,g=0,b=0;
        for(int k=-4;k<=4;++k) {
            const int sx=pass?x:(x+k+t.w)%t.w, sy=pass?(y+k+t.h)%t.h:y;
            const uint16_t p=pass?horizontal[sy*t.w+sx]:g_chapter_surfaces[chapter][sy*t.w+sx];
            const int w=weights[k+4];r+=((p>>11)&31)*w;g+=((p>>5)&63)*w;b+=(p&31)*w;
        }
        const uint16_t p=theme_rgb(r*255/(25*31),g*255/(25*63),b*255/(25*31));
        (pass?blurred:horizontal)[y*t.w+x]=p;
    }
    for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
        const uint16_t p=g_chapter_surface_ready[chapter]?blurred[((y/2)%t.h)*t.w+(x/2)%t.w]:0;
        // Smooth fades to black at the four edges, computed only when
        // the cached backdrop changes. UI text is composited afterwards.
        const float fx=std::min(1.0f,std::min(x,319-x)/38.0f);
        const float fy=std::min(1.0f,std::min(y,239-y)/26.0f);
        const float sx=fx*fx*(3-2*fx),sy=fy*fy*(3-2*fy);
        const int light=int(80*sx*sy);
        g_theme_base[theme_index(x,y)]=theme_rgb(((p>>11)&31)*light/31,((p>>5)&63)*light/63,(p&31)*light/31);
    }
}
void game_sprite(uint16_t* dest,const uint8_t* image,int w,int h,int x,int y) {
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){const auto* p=image+(yy*w+xx)*4;game_blend(dest,x+xx,y+yy,p[0],p[1],p[2],p[3]);}
}
// One ROM-derived tail for each spare life. Crop transparent margins so
// normal counts sit beside LIVES; large counts wrap within the header.
void game_lives(uint16_t* dest,uint16_t colour) {
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
    const int count=std::clamp(g_conker_lives,0,99),sw=right-left,sh=bottom-top;
    const int start_x=g_conker_lives_display==1?12:62;
    const int available=308-start_x;
    int height=32,width=1,gap=3,columns=1;
    for(;height>=8;--height){
        width=std::max(1,sw*height/sh);gap=std::max(2,height/8);
        columns=std::max(1,available/(width+gap));const int rows=(count+columns-1)/columns;
        if(rows*height+(rows-1)*3<=64)break;
    }
    for(int i=0;i<count;++i){
        const int x=start_x+(i%columns)*(width+gap),y=4+(i/columns)*(height+3);
        for(int yy=0;yy<height;++yy)for(int xx=0;xx<width;++xx){
            const auto* p=g_life_icon+((top+yy*sh/height)*kLifeIconSize+left+xx*sw/width)*4;
            game_blend(dest,x+xx,y+yy,p[0],p[1],p[2],p[3]);
        }
    }
}
void compose_conker_panel(uint16_t* dest) {
    static char previous_notice[41]{};
    char notice[41];game_line(notice,27);
    if(strcmp(previous_notice,notice)){strcpy(previous_notice,notice);g_conker_notice_until=notice[0]?svcGetSystemTick()+SYSCLOCK_ARM11*4:0;}
    game_background();const int dim=255;
    for(int i=0;i<320*240;++i){auto c=g_theme_base[i];dest[i]=theme_rgb(((c>>11)&31)*dim/31,((c>>5)&63)*dim/63,(c&31)*dim/31);}
    const uint16_t white=theme_rgb(255,255,255),yellow=theme_rgb(255,223,70),red=theme_rgb(255,110,95);
    char line[41];
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
        if(g_conker_chapter>=0) {if(g_conker_chapter_visible)game_sprite(dest,g_chapter_art[g_conker_chapter],96,32,112,84);}
        if(g_conker_chapter_visible && g_conker_scene>=0)game_text(dest,g_conker_scenes[g_conker_scene].title,160,126,292,white,true);
        if(g_conker_options_visible)game_options(dest,220,204);
    }else{
        game_options(dest,112,2);game_text(dest,"BACK",8,16,64,yellow);
        game_text(dest,g_conker_section_title,222,16,90,white);
        for(int tab=0;tab<conker_menu::count;++tab){
            const bool active=g_game_inks[5][tab*conker_menu::cell_width]==1;
            game_text(dest,conker_menu::labels[tab],tab*64+32,42,60,active?yellow:white,true);
            if(active)for(int x=tab*64+4;x<tab*64+60;++x)game_blend(dest,x,57,255,223,70,150);
        }
        for(int slot=0;slot<7;++slot){
            const int row=7+slot*2, y=60+slot*20;
            game_line(line,row,2,21);if(!line[0])continue;
            const bool selected=g_game_cells[row][0]=='>';
            // Larger touch rows, a restrained gold selection and separate
            // value controls keep Conker's font/artwork as the visual focus.
            if(selected)for(int yy=y;yy<y+19;++yy)for(int xx=8;xx<316;++xx)
                game_blend(dest,xx,yy,112,82,24,55);
            if(selected)for(int yy=y+2;yy<y+17;++yy)for(int xx=9;xx<12;++xx)game_blend(dest,xx,yy,255,223,70,190);
            game_text(dest,line,17,y+4,172,selected?yellow:white);
            game_line(line,row,24,13);game_text(dest,line,194,y+4,82,selected?yellow:white);
            for(int yy=y+3;yy<y+17;++yy){game_blend(dest,279,yy,255,223,70,40);game_blend(dest,299,yy,255,223,70,40);}
            if(strcmp(line,"OPEN"))game_text(dest,"<",283,y+4,12,yellow);
            game_text(dest,">",304,y+4,12,yellow);
        }
        game_line(line,21);game_text(dest,line,8,208,304,white);
    }
    // FPS is optional, and belongs in the footer rather than a terminal header.
    if(recomp3ds::settings_perf_monitor()) {game_line(line,1);game_text(dest,line,8,g_conker_menu?224:180,304,white);}
    for(int row=22;row<30;++row){if(row==27 && (!g_conker_gfx_text || svcGetSystemTick()>g_conker_notice_until))continue;game_line(line,row);if(line[0])game_text(dest,line,4,row*8-1,312,g_game_inks[row][0]==2?red:white);}
}
