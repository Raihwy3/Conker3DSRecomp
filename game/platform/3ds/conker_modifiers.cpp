// Native adaptation of upstream Skip Intro by Christopher Conley.
// Upstream MIT licence retained in game/LICENSE. No mod loader is required.
#include "recomp.h"
#include "conker_options.h"
#include <cstdio>
extern "C" void func_1501C730(uint8_t*,recomp_context*);
namespace {
bool bar_requested=false,bar_ready=false;
inline gpr addr(uint32_t a){return (gpr)(int32_t)a;}
}
extern "C" void conker_skip_intro_logos(uint8_t* rdram,recomp_context* ctx){
    // Scene loading must be established first. The PC mod also waits ten
    // native logo ticks: requests during its first two ticks can hang.
    if(bar_requested || !conker3ds::get("misc_skip_intro") ||
       MEM_W(0,addr(0x800E0A90))<10 || MEM_W(0,addr(0x800E0A90))>=0x349 ||
       MEM_W(0,addr(0x800BE9F0))!=0x25)return;
    bar_requested=true;
    MEM_B(0,addr(0x8008FE28))=2;MEM_B(0,addr(0x800E0B94))=1;MEM_B(0,addr(0x800D2E40))=0;
    recomp_context call=*ctx;call.r29=ctx->r29-0x40;
    call.r4=6;call.r5=0x1D;call.r6=0;call.r7=0;MEM_W(0x10,call.r29)=1;
    func_1501C730(rdram,&call);MEM_B(0,addr(0x800E0B96))=0xFF;
    std::fprintf(stderr,"modifier: skip intro requested bar\n");
}
extern "C" void conker_skip_intro_loaded(uint8_t* rdram,recomp_context*){
    // Complete the pending transition even if the option was switched off
    // during the load; leaving it half configured would strand the game.
    if(!bar_requested || bar_ready || MEM_W(0,addr(0x800BE9F0))!=0x1D)return;
    bar_ready=true;
    MEM_B(0,addr(0x800D2E40))=0;MEM_B(0,addr(0x800E0B94))=3;
    MEM_B(0,addr(0x8008FD80))=1;MEM_B(0,addr(0x8008FE28))=2;MEM_B(0,addr(0x8008FDA4))=0;
    const uint32_t state=uint32_t(MEM_W(0,addr(0x8008FDD4)));
    if(state>=0x80000000 && state<=0x807FFFC0){
        MEM_B(0x3E,addr(state))=0;MEM_B(0x2B,addr(state))=5;MEM_B(0x2C,addr(state))=5;
    }
    std::fprintf(stderr,"modifier: skip intro bar ready\n");
}
