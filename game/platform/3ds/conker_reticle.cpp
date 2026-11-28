#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include "recomp.h"
#include "conker.hpp"
#include "conker_options.h"
#include "netplay/netplay_uds_3ds.h"
#include "rt64_3ds.h"
namespace {
using Clock=std::chrono::steady_clock;
std::atomic<int64_t> last{0};std::atomic<bool> shown{false},raised{false};
float readf(uint8_t* rdram,gpr p,int o){uint32_t v=MEM_W(o,p);float f;memcpy(&f,&v,4);return f;}
void note(uint8_t* rdram,gpr cam,bool raise){
 const bool netplay = conker_netplay::is_active();
 const int want_port = netplay ? conker_netplay::local_port() : 0;
 if((int)MEM_BU(0x23D,cam)!=want_port)return;
 uint32_t ptr=MEM_W(0,(gpr)(int32_t)0x800BE628);
 if(ptr<0x80000000 || ptr>0x807ffe80)return;
 gpr v=(gpr)(int32_t)(ptr + (uint32_t)want_port * 0x180u);
 float f=readf(rdram,v,0x74),base=readf(rdram,v,0x6c);
 const bool full_cam = readf(rdram,v,0x2c)<=4 && readf(rdram,v,0x30)>=286 && readf(rdram,v,0x24)<=4 && readf(rdram,v,0x28)>=210;
 const bool zoomed = (f > 0 && base > 0 && f < base - 0.5f);
 shown = !zoomed && (netplay || full_cam);
 raised=raise;last=Clock::now().time_since_epoch().count();
}
}
void conker::reticle::aim_frame(uint8_t* r,gpr c){note(r,c,false);}
void conker::reticle::look_frame(uint8_t* rdram,gpr c,uint32_t state){
 const bool mp = MEM_BU(0, (gpr)(int32_t)0x800BE616) != 0;
 if(state==2 || state==10 || (mp && state!=0))note(rdram,c,!mp);
}
extern "C" void conker_3ds_reticle_draw(uint8_t* rdram,recomp_context* ctx){
 (void)rdram; (void)ctx;
 auto since=Clock::now().time_since_epoch().count()-last.load();
 const int style=conker3ds::get("aim_reticle",0);
 if(style<=0 || style>7 || !shown || since<0 || Clock::duration(since)>=std::chrono::milliseconds(150)){
  rt64_3ds::set_reticle(false,0,255,255,255,0,false);
  return;
 }
 int opacity=conker3ds::get("aim_reticle_opacity",100);
 if(opacity<10)opacity=10; if(opacity>100)opacity=100;
 const uint8_t alpha=(uint8_t)((opacity*255+50)/100);
 static constexpr uint8_t kPalette[6][3]={{255,255,255},{64,255,64},{255,176,32},{255,48,48},{64,160,255},{192,96,255}};
 int col_idx=conker3ds::get("aim_reticle_color",0);
 if(col_idx<0 || col_idx>=6)col_idx=0;
 rt64_3ds::set_reticle(true,style,kPalette[col_idx][0],kPalette[col_idx][1],kPalette[col_idx][2],alpha,raised.load());
}
