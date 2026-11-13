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
 if(!conker::reticle::enabled() || !shown || since<0 || Clock::duration(since)>=std::chrono::milliseconds(80))return;
 int w=MEM_W(0,(gpr)(int32_t)0x800BE620),h=MEM_W(0,(gpr)(int32_t)0x800BE624);
 if(w<=0 || w>1024 || h<=0 || h>1024)return;
 static uint32_t slot=0;uint32_t address=0x00F12000+(slot++%32)*0x400;gpr dl=(gpr)(int32_t)(address|0x80000000);
 cmd(rdram,dl,0xE7000000,0);cmd(rdram,dl,0xED000000,((w*4)<<12)|(h*4));
 cmd(rdram,dl,0xE3000A01,0x00300000);cmd(rdram,dl,0xF7000000,0x07c107c1);
 auto dot=[&](int x,int y,int size){cmd(rdram,dl,0xF6000000|(((x+size-1)*4)<<12)|((y+size-1)*4),((x*4)<<12)|(y*4));};
 const int cx=w/2,cy=int(h*(raised?.30f:.50f));const float radius=h*.05f;
 for(int i=0;i<24;++i){float a=i*6.2831853f/24;dot(cx+int(std::cos(a)*radius),cy+int(std::sin(a)*radius),2);}dot(cx-1,cy-1,3);
 cmd(rdram,dl,0xDF000000,0);gpr out=(gpr)(int32_t)MEM_W(0,ctx->r17);cmd(rdram,out,0xDE000000,address);MEM_W(0,ctx->r17)=out;
}
