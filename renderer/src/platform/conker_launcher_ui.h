// Copyright (c) 2026 Raihwy3. MIT.
// Software startup panels using only artwork/font decoded from the user's ROM.
std::vector<uint16_t> g_launcher_backdrop, g_launcher_canvas;
void launcher_rect(uint16_t* out,int x,int y,int w,int h,int r,int g,int b,int alpha=255) {
    for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)game_blend(out,xx,yy,r,g,b,alpha);
}
void launcher_panel(uint16_t* out,int x,int y,int w,int h) {
    launcher_rect(out,x,y,w,h,24,16,8,150);
    launcher_rect(out,x,y,w,1,194,145,60,180);
    launcher_rect(out,x,y+h-1,w,1,96,63,25,180);
}
void launcher_wrap(uint16_t* out,const char* text,int x,int y,int width,int lines,uint16_t colour) {
    if(!text)return;
    char line[128]{};int used=0;
    while(*text && lines>0){
        if(*text=='\n'){game_text(out,line,x,y,width,colour);used=0;line[0]=0;y+=18;--lines;++text;continue;}
        while(*text==' ' && !used)++text;
        if(!*text)break;
        const char* start=text;while(*text && *text!=' ' && *text!='\n')++text;
        const int n=int(text-start);
        if(used && (used+n+1>=int(sizeof(line)) || game_text_width((std::string(line)+" "+std::string(start,n)).c_str())>width)){
            game_text(out,line,x,y,width,colour);used=0;line[0]=0;y+=18;if(!--lines)break;
        }
        if(used)line[used++]=' ';
        for(int i=0;i<n && used<int(sizeof(line))-1;++i)line[used++]=start[i]>=32 && start[i]<127?start[i]:' ';
        line[used]=0;if(*text==' ')++text;
    }
    if(used && lines>0)game_text(out,line,x,y,width,colour);
}
void launcher_footer(uint16_t* out) {
    const auto gold=theme_rgb(235,192,94);
    launcher_rect(out,16,211,288,1,168,125,55,150);
    game_text(out,"CONKER: 3DS RECOMP  " CONKER_LAUNCHER_VERSION,160,220,286,gold,true);
}
void launcher_compose(uint16_t* out,const ConkerLauncherView& v,bool top) {
    memcpy(out,g_launcher_backdrop.data(),320*240*2);
    const auto gold=theme_rgb(255,218,105),white=theme_rgb(245,235,212),muted=theme_rgb(188,172,143);
    if(top){
        game_text(out,"CONKER:",160,14,280,gold,true);
        game_text(out,"3DS RECOMP",160,39,280,white,true);
        launcher_panel(out,16,75,288,120);
        game_text(out,v.stage?v.stage:"GETTING READY",160,87,260,gold,true);
        launcher_wrap(out,v.message,28,115,264,3,white);
        if(v.detail)game_text(out,v.detail,160,174,262,muted,true);
    }else if(v.menu==2){
        game_text(out,"GETTING READY",160,18,280,gold,true);
        launcher_panel(out,16,64,288,122);
        game_text(out,"STARTING GAME",160,79,260,white,true);
        launcher_wrap(out,v.message,28,110,264,3,muted);
        if(v.detail)game_text(out,v.detail,160,161,260,gold,true);
    }else if(v.menu){
        game_text(out,v.stage,160,14,286,gold,true);
        game_text(out,"CHOOSE YOUR GAME",160,39,280,muted,true);
        for(int i=0;i<3;++i){const int y=64+i*32;const bool selected=v.selected==i;
            launcher_panel(out,12,y,296,30);
            if(selected){launcher_rect(out,13,y+1,294,28,149,104,34,80);launcher_rect(out,12,y,3,30,255,211,93);}
            game_text(out,v.labels[i]?v.labels[i]:"",24,y+2,265,v.available[i]?white:muted);
            game_text(out,v.notes[i]?v.notes[i]:"",24,y+17,265,muted);
        }
        game_text(out,v.actions?v.actions:"A: SELECT    B: BACK",160,184,288,gold,true);
    }else{
        game_text(out,"LAUNCHER UPDATE",160,18,280,gold,true);
        launcher_panel(out,16,54,288,102);
        game_text(out,"INSTALLED  " CONKER_LAUNCHER_VERSION,160,66,260,white,true);
        launcher_wrap(out,v.detail?v.detail:v.message,28,96,264,3,muted);
        if(v.actions){
            launcher_panel(out,16,160,288,31);
            game_text(out,v.actions,160,170,272,gold,true);
        }
    }
    if(top && v.total){
        const unsigned percent=std::min(100u,unsigned(uint64_t(v.done)*100/v.total));
        launcher_rect(out,28,158,264,7,18,12,6);
        launcher_rect(out,29,159,262*percent/100,5,216,166,66);
    }
    launcher_footer(out);
}
