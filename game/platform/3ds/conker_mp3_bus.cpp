// Conker's streamed MP3 main bus alongside the existing DSP voice mixer.
// The original n_alAudioFrame still runs players, interleave/save and its clock;
// this replaces only n_alMainBusPull's normal voice/effect mixing.
#include "recomp.h"
#include "dsp_synth.h"
extern "C" void func_151F2E88(uint8_t*, recomp_context*); // mp3_make_samples
extern "C" int conker_mp3_main_bus(uint8_t* rdram, recomp_context* ctx) {
    // Caller is n_alAudioFrame at 0x10019604; +0x34 is this block's nOut.
    const int32_t count = MEM_W(0x34, ctx->r29);
    if (!recomp3ds_synth_retire_stream_block(rdram, count)) return 0;
    const recomp_context saved = *ctx;
    uint32_t end = (uint32_t)ctx->r5;
    // Clear main + aux buses, including the dry/wet accumulators used by MP3.
    MEM_W(0, end) = 0x020004e0u; MEM_W(4, end) = 0x2e0; end += 8;
    MEM_W(0, end) = 0x020007c0u; MEM_W(4, end) = 0x2e0; end += 8;
    if (MEM_W(0, 0x800e0e04u)) {
        // Same scratch frame/command-pointer slot as the retail main bus.
        ctx->r29 = ADD32(saved.r29, -0x60);
        const gpr slot = ADD32(ctx->r29, 0x54);
        MEM_W(0, slot) = end;
        ctx->r4 = 184; ctx->r5 = slot;
        func_151F2E88(rdram, ctx);
        end = (uint32_t)MEM_W(0, slot);
    }
    *ctx = saved;
    ctx->r2 = (gpr)(int32_t)end;
    return 1;
}
