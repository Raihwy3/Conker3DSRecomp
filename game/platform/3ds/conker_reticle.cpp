#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include "recomp.h"
#include "conker.hpp"
#include "conker_options.h"
namespace {
using Clock=std::chrono::steady_clock;
std::atomic<int64_t> last{0};std::atomic<bool> shown{false},raised{false};
float readf(uint8_t* rdram,gpr p,int o){uint32_t v=MEM_W(o,p);float f;memcpy(&f,&v,4);return f;}
void note(uint8_t* rdram,gpr cam,bool raise){
 if(MEM_BU(0x23D,cam)!=0)return;
 uint32_t ptr=MEM_W(0,(gpr)(int32_t)0x800BE628);
 if(ptr<0x80000000 || ptr>0x807ffe80)return;
 gpr v=(gpr)(int32_t)ptr;
 float f=readf(rdram,v,0x74),base=readf(rdram,v,0x6c);
 shown=!(f>0 && base>0 && f<base-.5f) && readf(rdram,v,0x2c)<=4 && readf(rdram,v,0x30)>=286 && readf(rdram,v,0x24)<=4 && readf(rdram,v,0x28)>=210;
 raised=raise;last=Clock::now().time_since_epoch().count();
}
void cmd(uint8_t* rdram,gpr& dl,uint32_t a,uint32_t b){MEM_W(0,dl)=a;MEM_W(4,dl)=b;dl+=8;}
}
void conker::reticle::aim_frame(uint8_t* r,gpr c){note(r,c,false);}
void conker::reticle::look_frame(uint8_t* r,gpr c,uint32_t state){if(state==2 || state==10)note(r,c,true);}
extern "C" void conker_3ds_reticle_draw(uint8_t* rdram,recomp_context* ctx){
 auto since=Clock::now().time_since_epoch().count()-last.load();
 const int style=conker3ds::get("aim_reticle",0);
 if(style<=0 || style>7 || !shown || since<0 || Clock::duration(since)>=std::chrono::milliseconds(80))return;
 int w=MEM_W(0,(gpr)(int32_t)0x800BE620),h=MEM_W(0,(gpr)(int32_t)0x800BE624);
 if(w<=0 || w>1024 || h<=0 || h>1024)return;
 static uint32_t slot=0;uint32_t address=0x00F12000+(slot++%16)*0x800;gpr dl=(gpr)(int32_t)(address|0x80000000);
 cmd(rdram,dl,0xE7000000,0);cmd(rdram,dl,0xED000000,((w*4)<<12)|(h*4));
 cmd(rdram,dl,0xE3000A01,0x00000000); // G_CYC_1CYCLE
 cmd(rdram,dl,0xE200001F,0x00504240); // G_RM_XLU_SURF
 cmd(rdram,dl,0xFCFFFFFF,0xFFFDF6FB); // G_CC_PRIMITIVE
 auto rect=[&](int x0,int y0,int x1,int y1){
  if(x1<=0||y1<=0||x0>=w||y0>=h)return;
  if(x0<0)x0=0; if(y0<0)y0=0; if(x1>w)x1=w; if(y1>h)y1=h;
  cmd(rdram,dl,0xF6000000|((x1*4)<<12)|(y1*4),((x0*4)<<12)|(y0*4));
 };
 auto shape_px=[&](int x,int y)->bool{
  int ax=x<0?-x:x,ay=y<0?-y:y,r=x*x+y*y;
  if(style==1)return ax<=1 && ay<=1;
  if(style==2 || style==3 || style==6){
   bool arms=(!y && ax>=3 && ax<=7) || (!x && ay>=3 && ay<=7 && (style!=6 || y>0));
   return arms || (style==3 && !x && !y);
  }
  if(style==4 || style==5)return (r>=20 && r<=36) || (style==5 && !x && !y);
  if(style==7)return ax+ay>=5 && ax+ay<=6;
  return false;
 };
 auto outline_px=[&](int x,int y)->bool{
  if(shape_px(x,y))return false;
  for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)if((dx||dy)&&shape_px(x+dx,y+dy))return true;
  return false;
 };
 const int cx=w/2,cy=int(h*(raised?.30f:.50f));
 int opacity=conker3ds::get("aim_reticle_opacity",100);
 if(opacity<10)opacity=10; if(opacity>100)opacity=100;
 const uint32_t alpha=(uint32_t)((opacity*255+50)/100);
 static constexpr uint8_t kPalette[6][3]={{255,255,255},{64,255,64},{255,176,32},{255,48,48},{64,160,255},{192,96,255}};
 int col_idx=conker3ds::get("aim_reticle_color",0);
 if(col_idx<0 || col_idx>=6)col_idx=0;
 auto emit_pass=[&](auto&& pred,uint32_t rgba){
  cmd(rdram,dl,0xFA000000,rgba);
  for(int y=-8;y<=8;y++){
   int x=-8;
   while(x<=8){
    if(!pred(x,y)){x++;continue;}
    int x1=x+1;while(x1<=8 && pred(x1,y))x1++;
    rect(cx+x,cy+y,cx+x1,cy+y+1);
    x=x1;
   }
  }
 };
 emit_pass(outline_px,alpha);
 const uint32_t prim=((uint32_t)kPalette[col_idx][0]<<24)|((uint32_t)kPalette[col_idx][1]<<16)|((uint32_t)kPalette[col_idx][2]<<8)|alpha;
 emit_pass(shape_px,prim);
 cmd(rdram,dl,0xDF000000,0);gpr out=(gpr)(int32_t)MEM_W(0,ctx->r17);cmd(rdram,out,0xDE000000,address);MEM_W(0,ctx->r17)=out;
}
