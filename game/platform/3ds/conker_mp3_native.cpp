// Native MP3 decoding for Conker's voice streams.
//
// The game's CPU-side MP3 decoder (overlay func_151F78B4, the decompilation's
// mp3dec_decode_frame) turns one compressed frame into 576 subband samples that
// the RSP microcode then polyphase-synthesises into PCM (opcode 7). On the 3DS
// both halves run on an ARM11: the recompiled decoder is heavy, and the
// recompiled RSP vector code runs in librecomp's scalar (SISD) path on armv6,
// so a talking character can eat most of a core and starve the game thread.
//
// This replaces the decoder with minimp3 (CC0, platform/3ds/minimp3.h), which
// produces the finished PCM directly. The game's stream reader (header, side
// info, bit reservoir, frame scanner, looping/seeking) is untouched: it still
// stages the current frame's header+side info in stream->buffer and hands the
// decoder the main data through mp3main00043dd0 (func_151F7F60). We assemble
// those bytes into one contiguous frame, decode it with minimp3, and write the
// 576 interleaved-free PCM samples the engine expects into the stream's slot.
//
// The RSP polyphase command (opcode 7) then becomes a no-op
// (conker_audio_hle.cpp), because the slot already holds PCM.
#include <cstdint>
#include <cstring>

#include "recomp.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

// The frame reader/helper from the recompiled overlay (decompilation:
// mp3main00043dd0). It DMAs the frame's main data into stream->unk1c and
// returns the offset of that data within unk1c.
extern "C" void func_151F7F60(uint8_t* rdram, recomp_context* ctx);

namespace {

// struct asistream (conkers-bfd-decomp/lib/libultrare/.../mp3/mp3_stream.h).
constexpr uint32_t kMainRing     = 0x001C;   // u8 unk1c[0x2000]: compressed payload
constexpr uint32_t kFrameBuf     = 0x2024;   // u8 buffer[0x40]: header+CRC+side info
constexpr uint32_t kHeaderBytes  = 0x2068;   // 4, or 6 with CRC
constexpr uint32_t kSideInfo     = 0x206C;   // side info bytes (9/17/32)
constexpr uint32_t kSlots        = 0x2070;   // u16 unk2070[6][580]
constexpr uint32_t kSlotIndex    = 0x3BA0;
constexpr uint32_t kMainSize     = 0x3F88;   // frame bytes minus header+side info
constexpr uint32_t kNumChannels  = 0x3F8C;
constexpr uint32_t kFileOffset   = 0x0018;   // offset just past header+side info

constexpr uint32_t kSlotSamples  = 576;      // MPEG-2 Layer III: one granule
constexpr uint32_t kSlotStride   = 580;      // u16 per channel slot
constexpr uint32_t kMaxFrame     = 2048;

mp3dec_t g_dec;
mp3dec_frame_info_t g_info;
bool g_initialised = false;
uint32_t g_next_frame = 0;                   // file offset after the last frame
alignas(16) uint8_t g_frame[kMaxFrame];
alignas(16) int16_t g_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

// RDRAM access in the recompiler's byte-swapped layout (see recomp.h).
inline uint32_t rd32(const uint8_t* rdram, uint32_t addr) {
    return *(const uint32_t*)(rdram + recomp_rdram_offset(addr));
}
inline uint8_t rd8(const uint8_t* rdram, uint32_t addr) {
    return rdram[recomp_rdram_offset(addr) ^ 3];
}
inline void wr16(uint8_t* rdram, uint32_t addr, int16_t v) {
    *(int16_t*)(rdram + recomp_rdram_offset(addr ^ 2)) = v;
}

// The slot layout the RSP command expects (and overwrites): 576 s16 per
// channel, one channel after the next, 580 u16 apart.
void clear_slot(uint8_t* rdram, uint32_t stream, uint32_t slot, uint32_t channels) {
    const uint32_t base = stream + kSlots + slot * kSlotStride * 2;
    for (uint32_t c = 0; c < channels; c++) {
        const uint32_t dst = base + c * kSlotStride * 2;
        for (uint32_t i = 0; i < kSlotSamples; i++) {
            wr16(rdram, dst + i * 2, 0);
        }
    }
}

} // namespace

// Replaces func_151F78B4 (mp3dec_decode_frame). Runs with the caller's
// recomp_context; func_151F7F60 is a normal recompiled call.
extern "C" void conker_mp3_decode_frame(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t stream = (uint32_t)ctx->r4;

    // Read everything before func_151F7F60 clobbers the context registers.
    const uint32_t slot       = rd32(rdram, stream + kSlotIndex);
    const uint32_t channels   = rd32(rdram, stream + kNumChannels) == 2 ? 2 : 1;
    const uint32_t main_len   = rd32(rdram, stream + kMainSize);
    const uint32_t header_len = rd32(rdram, stream + kHeaderBytes) + rd32(rdram, stream + kSideInfo);

    // The game's read_frame consumed header+CRC+side info, so unk18 now points
    // just past the header, i.e. at this frame's main data.
    const uint32_t frame_start = rd32(rdram, stream + kFileOffset) - header_len;

    // Feed the frame's main data through the game's DMA/ring code so its file
    // offset, ring bookkeeping and ROM side effects stay exactly as before.
    ctx->r4 = (gpr)stream;
    func_151F7F60(rdram, ctx);
    const uint32_t main_start = (uint32_t)ctx->r2;

    // mp3main00043dd0 returns 0 when the ring was empty before this DMA (the
    // stream's priming frame), and the original decoder skips that frame. The
    // same frame still has to reach minimp3 so its main data seeds the
    // bit reservoir for the frame after it.
    const bool first = main_start == 0;

    if (header_len > kMaxFrame || main_len > kMaxFrame - header_len) {
        ctx->r2 = 0;
        return;
    }

    // header (+CRC) + side info, then this frame's main data: one MP3 frame.
    const uint32_t buf = stream + kFrameBuf;
    for (uint32_t i = 0; i < header_len; i++) g_frame[i] = rd8(rdram, buf + i);
    const uint32_t main = stream + kMainRing + main_start;
    for (uint32_t i = 0; i < main_len; i++) g_frame[header_len + i] = rd8(rdram, main + i);
    const uint32_t frame_bytes = header_len + main_len;

    // A seek or a new file restarts minimp3; otherwise its bit reservoir and
    // synthesis state carry across frames, just like the game's decoder.
    if (!g_initialised || frame_start != g_next_frame) {
        mp3dec_init(&g_dec);
        g_initialised = true;
    }

    const int samples = mp3dec_decode_frame(&g_dec, g_frame, (int)frame_bytes, g_pcm, &g_info);
    g_next_frame = frame_start + frame_bytes;

    // A frame whose bit reservoir is not complete yet (the first frame after a
    // seek) decodes to nothing. The original decoder still reports success
    // then and the mixer plays whatever the slot held, so hand it silence.
    if (samples <= 0) {
        clear_slot(rdram, stream, slot, channels);
        ctx->r2 = first ? 0 : 1;
        return;
    }

    // minimp3 may find a different channel count in a malformed frame; follow
    // the game's, which decides how many slots the mixer reads.
    const uint32_t mp3_channels = g_info.channels > 0 ? (uint32_t)g_info.channels : 1;
    const uint32_t count = (uint32_t)samples < kSlotSamples ? (uint32_t)samples : kSlotSamples;
    const uint32_t base = stream + kSlots + slot * kSlotStride * 2;
    for (uint32_t c = 0; c < channels; c++) {
        const uint32_t src = c < mp3_channels ? c : mp3_channels - 1;
        const uint32_t dst = base + c * kSlotStride * 2;
        for (uint32_t i = 0; i < count; i++) {
            wr16(rdram, dst + i * 2, g_pcm[i * mp3_channels + src]);
        }
        for (uint32_t i = count; i < kSlotSamples; i++) {
            wr16(rdram, dst + i * 2, 0);
        }
    }

    ctx->r2 = first ? 0 : 1;
}
