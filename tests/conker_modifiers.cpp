#include "recomp.h"
#include "conker_options.h"
#include "conker_menu_tabs.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
static int requests=0;
extern "C" void func_1501C730(uint8_t* rdram,recomp_context* ctx){
    assert(ctx->r4==6 && ctx->r5==0x1D && ctx->r6==0 && ctx->r7==0 && MEM_W(0x10,ctx->r29)==1);++requests;
    ctx->r4=12345; // A nested game call must not corrupt the caller context.
}
extern "C" void conker_skip_intro_logos(uint8_t*,recomp_context*);
extern "C" void conker_skip_intro_loaded(uint8_t*,recomp_context*);
int main(){
    for(int x=0;x<320;++x)assert(conker_menu::at_x(x)==x/64);
    assert(conker_menu::wrap(-1)==4 && conker_menu::wrap(5)==0);
    std::vector<uint8_t> memory(16*1024*1024);uint8_t* rdram=memory.data();
    auto a=[](uint32_t p){return (gpr)(int32_t)p;};
    recomp_context ctx{};ctx.r29=a(0x80700000);ctx.r4=99;
    MEM_W(0,a(0x800BE9F0))=0x25;MEM_W(0,a(0x800E0A90))=20;
    conker_skip_intro_logos(rdram,&ctx);assert(requests==0);
    conker3ds::set("misc_skip_intro",1);MEM_W(0,a(0x800E0A90))=2;
    conker_skip_intro_logos(rdram,&ctx);assert(requests==0);
    MEM_W(0,a(0x800E0A90))=0x349;conker_skip_intro_logos(rdram,&ctx);assert(requests==0);
    MEM_W(0,a(0x800E0A90))=10;conker_skip_intro_logos(rdram,&ctx);
    assert(requests==1 && ctx.r4==99 && ctx.r29==a(0x80700000));
    conker_skip_intro_logos(rdram,&ctx);assert(requests==1);
    MEM_W(0,a(0x800BE9F0))=0x21;conker_skip_intro_loaded(rdram,&ctx);assert(MEM_BU(0,a(0x800E0B94))==1);
    MEM_W(0,a(0x800BE9F0))=0x1D;MEM_W(0,a(0x8008FDD4))=int32_t(0x80200000);
    conker_skip_intro_loaded(rdram,&ctx);assert(MEM_BU(0,a(0x800E0B94))==3 && MEM_BU(0x2B,a(0x80200000))==5);
    puts("PASS: five tabs; disabled/armed/one-shot intro skip, nested-call preservation and bar setup");
}
