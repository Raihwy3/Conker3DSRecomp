#ifndef __RSP_H__
#define __RSP_H__

#include <cstdio>
#include <cstring>

#include "rsp_vu.hpp"
#include "recomp.h"
#include "ultramodern/ultra64.h"

// TODO: Move these to recomp namespace?

enum class RspExitReason {
    Invalid,
    Broke,
    ImemOverrun,
    UnhandledJumpTarget,
    Unsupported,
    SwapOverlay,
    UnhandledResumeTarget
};

struct RspContext {
    uint32_t      r1,  r2,  r3,  r4,  r5,  r6,  r7,
             r8,  r9,  r10, r11, r12, r13, r14, r15,
             r16, r17, r18, r19, r20, r21, r22, r23,
             r24, r25, r26, r27, r28, r29, r30, r31;
    uint32_t dma_mem_address;
    uint32_t dma_dram_address;
    uint32_t jump_target;
    RSP rsp;
    uint32_t resume_address;
    bool resume_delay;
};

using RspUcodeFunc = RspExitReason(uint8_t* rdram, uint32_t ucode_addr);

alignas(16) extern uint8_t dmem[];
alignas(16) extern uint16_t rspReciprocals[512];
alignas(16) extern uint16_t rspInverseSquareRoots[512];

#define RSP_MEM_B(offset, addr) \
    (*reinterpret_cast<int8_t*>(dmem + (0xFFF & (((offset) + (addr)) ^ 3))))

#define RSP_MEM_BU(offset, addr) \
    (*reinterpret_cast<uint8_t*>(dmem + (0xFFF & (((offset) + (addr)) ^ 3))))

static inline uint32_t RSP_MEM_W_LOAD(uint32_t offset, uint32_t addr) {
    const uint32_t eff = (offset + addr) & 0xFFFu;
    if (__builtin_expect((eff & 3u) == 0, 1)) {
        return *reinterpret_cast<const uint32_t*>(dmem + eff);
    }
    uint32_t out;
    for (int i = 0; i < 4; i++) {
        reinterpret_cast<uint8_t*>(&out)[i ^ 3] = RSP_MEM_BU(offset + i, addr);
    }
    return out;
}

static inline void RSP_MEM_W_STORE(uint32_t offset, uint32_t addr, uint32_t val) {
    const uint32_t eff = (offset + addr) & 0xFFFu;
    if (__builtin_expect((eff & 3u) == 0, 1)) {
        *reinterpret_cast<uint32_t*>(dmem + eff) = val;
        return;
    }
    for (int i = 0; i < 4; i++) {
        RSP_MEM_BU(offset + i, addr) = reinterpret_cast<uint8_t*>(&val)[i ^ 3];
    }
}

static inline uint32_t RSP_MEM_HU_LOAD(uint32_t offset, uint32_t addr) {
    const uint32_t eff = (offset + addr) & 0xFFFu;
    if (__builtin_expect((eff & 1u) == 0, 1)) {
        return *reinterpret_cast<const uint16_t*>(dmem + (eff ^ 2u));
    }
    uint16_t out;
    for (int i = 0; i < 2; i++) {
        reinterpret_cast<uint8_t*>(&out)[(i + 2) ^ 3] = RSP_MEM_BU(offset + i, addr);
    }
    return out;
}

static inline uint32_t RSP_MEM_H_LOAD(uint32_t offset, uint32_t addr) {
    const uint32_t eff = (offset + addr) & 0xFFFu;
    if (__builtin_expect((eff & 1u) == 0, 1)) {
        return *reinterpret_cast<const int16_t*>(dmem + (eff ^ 2u));
    }
    int16_t out;
    for (int i = 0; i < 2; i++) {
        reinterpret_cast<uint8_t*>(&out)[(i + 2) ^ 3] = RSP_MEM_BU(offset + i, addr);
    }
    return out;
}

static inline void RSP_MEM_H_STORE(uint32_t offset, uint32_t addr, uint32_t val) {
    const uint32_t eff = (offset + addr) & 0xFFFu;
    if (__builtin_expect((eff & 1u) == 0, 1)) {
        *reinterpret_cast<uint16_t*>(dmem + (eff ^ 2u)) = static_cast<uint16_t>(val);
        return;
    }
    for (int i = 0; i < 2; i++) {
        RSP_MEM_BU(offset + i, addr) = reinterpret_cast<uint8_t*>(&val)[(i + 2) ^ 3];
    }
}

#define RSP_ADD32(a, b) \
    ((int32_t)((a) + (b)))

#define RSP_SUB32(a, b) \
    ((int32_t)((a) - (b)))

#define RSP_SIGNED(val) \
    ((int32_t)(val))

#define SET_DMA_MEM(mem_addr) dma_mem_address = (mem_addr)
#define SET_DMA_DRAM(dram_addr) dma_dram_address = (dram_addr)
#define DO_DMA_READ(rd_len) dma_rdram_to_dmem(rdram, dma_mem_address, dma_dram_address, (rd_len))
#define DO_DMA_WRITE(wr_len) dma_dmem_to_rdram(rdram, dma_mem_address, dma_dram_address, (wr_len))

// A private snapshot of a task's command list: DMA reads inside it come from the
// snapshot, so a game thread rewriting RDRAM during an asynchronous task can't corrupt it.
struct RspShadow { uint32_t base; uint32_t size; const uint8_t* data; };
extern thread_local RspShadow rsp_shadow;

static inline void dma_rdram_to_dmem(uint8_t* rdram, uint32_t dmem_addr, uint32_t dram_addr, uint32_t rd_len) {
    rd_len += 1; // Read length is inclusive
    dram_addr &= 0xFFFFF8u;
    dmem_addr &= 0xFFFu;
    assert(dmem_addr + rd_len <= 0x1000);
    if (rsp_shadow.data != nullptr && (dmem_addr & 3u) == 0 && dram_addr >= rsp_shadow.base &&
        (dram_addr - rsp_shadow.base) + rd_len <= rsp_shadow.size) {
        std::memcpy(dmem + dmem_addr, rsp_shadow.data + (dram_addr - rsp_shadow.base), rd_len & ~3u);
        for (uint32_t i = rd_len & ~3u; i < rd_len; i++) {
            dmem[(dmem_addr + i) ^ 3u] = rsp_shadow.data[((dram_addr - rsp_shadow.base) + i) ^ 3u];
        }
        return;
    }
    if (__builtin_expect((dmem_addr & 3u) == 0 && (dram_addr + rd_len) <= 0x800000u, 1)) {
        const uint32_t aligned_len = rd_len & ~3u;
        std::memcpy(dmem + dmem_addr, rdram + dram_addr, aligned_len);
        for (uint32_t i = aligned_len; i < rd_len; i++) {
            dmem[(dmem_addr + i) ^ 3u] = rdram[(dram_addr + i) ^ 3u];
        }
        return;
    }
    for (uint32_t i = 0; i < rd_len; i++) {
        RSP_MEM_B(i, dmem_addr) = MEM_B(0, (int64_t)(int32_t)(dram_addr + i + 0x80000000));
    }
}

static inline void dma_dmem_to_rdram(uint8_t* rdram, uint32_t dmem_addr, uint32_t dram_addr, uint32_t wr_len) {
    wr_len += 1; // Write length is inclusive
    dram_addr &= 0xFFFFF8u;
    dmem_addr &= 0xFFFu;
    assert(dmem_addr + wr_len <= 0x1000);
    if (__builtin_expect((dmem_addr & 3u) == 0 && (dram_addr + wr_len) <= 0x800000u, 1)) {
        const uint32_t aligned_len = wr_len & ~3u;
        std::memcpy(rdram + dram_addr, dmem + dmem_addr, aligned_len);
        for (uint32_t i = aligned_len; i < wr_len; i++) {
            rdram[(dram_addr + i) ^ 3u] = dmem[(dmem_addr + i) ^ 3u];
        }
        return;
    }
    for (uint32_t i = 0; i < wr_len; i++) {
        MEM_B(0, (int64_t)(int32_t)(dram_addr + i + 0x80000000)) = RSP_MEM_B(i, dmem_addr);
    }
}

namespace recomp {
    namespace rsp {
        struct callbacks_t {
            using get_rsp_microcode_t = RspUcodeFunc*(const OSTask* task);

            /**
             * Return a function pointer to the corresponding RSP microcode function for the given `task_type`.
             *
             * The full OSTask (`task` parameter) is passed in case the `task_type` number is not enough information to distinguish out the exact microcode function.
             *
             * This function is allowed to return `nullptr` if no microcode matches the specified task. In this case a message will be printed to stderr and the program will exit.
             */
            get_rsp_microcode_t* get_rsp_microcode;
        };

        void set_callbacks(const callbacks_t& callbacks);

        void constants_init();

        bool run_task(uint8_t* rdram, const OSTask* task);
    }
}

#endif
