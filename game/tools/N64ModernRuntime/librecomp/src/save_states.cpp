// Save states: a whole state of the game, as bytes (see librecomp/save_states.hpp, and
// ultramodern/save_states.hpp for how a save state of a recompiled game works).
//
// The layout (in the program's byte order, little-endian on every platform it's built for):
//   "RCST", the layout's version (u32), the program's fingerprint (u64: which build took it)
//   the threads: their count (u32), then for each, its OSThread (i32), its id (i32), where it waits
//     (its signature, u64, where it is in the game's code, u64, and its depth, u32), whether it has
//     registers (u32), and if so its registers: r0-r31, f0-f31, hi and lo (u64 each), the status
//     register and the float mode (u32)
//   the clock and the timers: the game's clock (u64), osSetTime's offset (i64), the running timers'
//     count (u32) and their OSTimers (i32 each)
//   the VI and the events (ultramodern::save_states::EventState, as it's laid out)
//   the game's memory: its size (u32), then the memory as the program holds it

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "recomp.h"
#include "librecomp/save_states.hpp"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/save_states.hpp"
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"

namespace {
    constexpr char magic[4] = { 'R', 'C', 'S', 'T' };
    constexpr uint32_t layout_version = 3;

    struct Registers {
        uint64_t gpr[32];
        uint64_t fpr[32];
        uint64_t hi, lo;
        uint32_t status_reg;
        uint32_t mips3_float_mode;
    };

    void read_registers(const recomp_context* ctx, Registers& out) {
        const gpr* r = &ctx->r0;
        const fpr* f = &ctx->f0;
        for (int i = 0; i < 32; i++) {
            out.gpr[i] = r[i];
            out.fpr[i] = f[i].u64;
        }
        out.hi = ctx->hi;
        out.lo = ctx->lo;
        out.status_reg = ctx->status_reg;
        out.mips3_float_mode = ctx->mips3_float_mode;
    }

    void write_registers(recomp_context* ctx, const Registers& in) {
        gpr* r = &ctx->r0;
        fpr* f = &ctx->f0;
        for (int i = 0; i < 32; i++) {
            r[i] = in.gpr[i];
            f[i].u64 = in.fpr[i];
        }
        ctx->hi = in.hi;
        ctx->lo = in.lo;
        ctx->status_reg = in.status_reg;
        ctx->mips3_float_mode = (uint8_t)in.mips3_float_mode;
        // f_odd points into the context itself, at the odd float registers' half the float mode uses
        // (as cop0_status_write sets it).
        ctx->f_odd = ctx->mips3_float_mode ? &ctx->f1.u32l : &ctx->f0.u32h;
    }

    struct Writer {
        std::vector<uint8_t>& out;
        template <typename T> void put(const T& value) {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
            out.insert(out.end(), bytes, bytes + sizeof(T));
        }
        void put_bytes(const void* data, size_t size) {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            out.insert(out.end(), bytes, bytes + size);
        }
    };

    struct Reader {
        std::span<const uint8_t> data;
        size_t at = 0;
        bool ok = true;
        template <typename T> T get() {
            T value{};
            if (at + sizeof(T) > data.size()) {
                ok = false;
                return value;
            }
            std::memcpy(&value, data.data() + at, sizeof(T));
            at += sizeof(T);
            return value;
        }
        const uint8_t* get_bytes(size_t size) {
            if (at + size > data.size()) {
                ok = false;
                return nullptr;
            }
            const uint8_t* bytes = data.data() + at;
            at += size;
            return bytes;
        }
    };

    struct SavedThread {
        int32_t thread;
        int32_t id;
        uint64_t signature;
        uint64_t game_signature;
        uint32_t depth;
        bool has_registers;
        Registers registers;
    };

    // This build's fingerprint (ultramodern::save_states::program_fingerprint, with a function of the
    // game library's).
    uint64_t fingerprint() {
        return ultramodern::save_states::program_fingerprint(reinterpret_cast<uintptr_t>(&recomp::save_states::capture));
    }

    struct ParsedState {
        uint64_t fingerprint = 0;
        std::vector<SavedThread> threads;
        ultramodern::save_states::TimerState timers;
        ultramodern::save_states::EventState events;
        const uint8_t* memory = nullptr;
        uint32_t memory_size = 0;
    };

    bool parse(std::span<const uint8_t> data, ParsedState& out, std::string& error) {
        Reader in{ data };
        const uint8_t* head = in.get_bytes(sizeof(magic));
        if (head == nullptr || std::memcmp(head, magic, sizeof(magic)) != 0) {
            error = "it isn't a save state";
            return false;
        }
        const uint32_t version = in.get<uint32_t>();
        if (version != layout_version) {
            error = "it was saved by another version of the program (its layout is " + std::to_string(version) + ")";
            return false;
        }
        out.fingerprint = in.get<uint64_t>();
        const uint32_t thread_count = in.get<uint32_t>();
        for (uint32_t i = 0; i < thread_count && in.ok; i++) {
            SavedThread thread{};
            thread.thread = in.get<int32_t>();
            thread.id = in.get<int32_t>();
            thread.signature = in.get<uint64_t>();
            thread.game_signature = in.get<uint64_t>();
            thread.depth = in.get<uint32_t>();
            thread.has_registers = in.get<uint32_t>() != 0;
            if (thread.has_registers) {
                thread.registers = in.get<Registers>();
            }
            out.threads.push_back(thread);
        }
        out.timers.now = in.get<uint64_t>();
        out.timers.ostime_offset = in.get<int64_t>();
        const uint32_t timer_count = in.get<uint32_t>();
        for (uint32_t i = 0; i < timer_count && in.ok; i++) {
            out.timers.active.push_back(in.get<int32_t>());
        }
        out.events = in.get<ultramodern::save_states::EventState>();
        out.memory_size = in.get<uint32_t>();
        out.memory = in.get_bytes(out.memory_size);
        if (!in.ok || out.memory_size != recomp::save_states::game_memory_size) {
            error = "it's cut short or damaged";
            return false;
        }
        return true;
    }

    // Read a MIPS instruction word from the loaded function in RDRAM / mapped TLB code pages.
    bool read_code(uint8_t* rdram, uint32_t func_vram, uint32_t i, uint32_t& word) {
        uint32_t addr = (func_vram >= 0x10001000u && func_vram < 0x100290D0u)
            ? (func_vram - 0x10000000u + 0x80000000u)
            : func_vram;
        word = (uint32_t)MEM_W(i * 4, (gpr)(int32_t)addr);
        return true;
    }

    struct GameHash {
        uint64_t value = 0xCBF29CE484222325ull;
        void add(uint32_t word) {
            for (int i = 0; i < 4; i++) {
                value ^= word & 0xFF;
                value *= 0x100000001B3ull;
                word >>= 8;
            }
        }
    };

    uint64_t game_signature(uint8_t* rdram, const ultramodern::save_states::ThreadInfo& thread) {
        GameHash hash;
        // How it waits, and the wait's own values (as in its native signature).
        hash.add((uint32_t)thread.kind);
        for (uint32_t arg : thread.args) {
            hash.add(arg);
        }
        if (thread.game_context == nullptr) {
            return hash.value;
        }
        const recomp_context* ctx = static_cast<const recomp_context*>(thread.game_context);
        uint32_t ra = (uint32_t)ctx->r31;
        uint32_t sp = (uint32_t)ctx->r29;
        constexpr uint32_t memory_start = 0x80000000u;
        constexpr uint32_t memory_end = 0x80000000u + recomp::save_states::game_memory_size;
        for (int depth = 0; depth < 64; depth++) {
            hash.add(ra);
            hash.add(sp);
            // The call is two instructions before its return address.
            uint32_t func_vram, func_rom, func_size;
            if (!recomp::overlays::find_loaded_function(ra - 8, func_vram, func_rom, func_size)) {
                break;
            }
            // The prologue, up to the call.
            int32_t frame_size = 0;
            int32_t ra_offset = -1;
            const uint32_t count = std::min<uint32_t>((ra - 8 - func_vram) / 4, 64);
            for (uint32_t i = 0; i < count; i++) {
                uint32_t word;
                if (!read_code(rdram, func_vram, i, word)) {
                    break;
                }
                const int16_t immediate = (int16_t)(word & 0xFFFF);
                if ((word >> 16) == 0x27BD && immediate < 0 && frame_size == 0) {
                    frame_size = -immediate; // addiu sp, sp, -size
                }
                else if ((word >> 16) == 0xAFBF && ra_offset < 0) {
                    ra_offset = immediate; // sw ra, offset(sp)
                }
                else if ((word >> 16) == 0xFFBF && ra_offset < 0) {
                    ra_offset = immediate + 4; // sd ra, offset(sp): its low word, the address
                }
            }
            if (frame_size == 0 || ra_offset < 0) {
                break;
            }
            const uint32_t saved_at = sp + (uint32_t)ra_offset;
            if (saved_at < memory_start || saved_at + 4 > memory_end) {
                break;
            }
            ra = (uint32_t)MEM_W(0, (gpr)(int32_t)saved_at);
            sp += (uint32_t)frame_size;
            if (ra == 0) {
                break;
            }
        }
        return hash.value;
    }

    std::string describe(const char* label, int32_t thread, int32_t id, uint64_t signature, uint64_t game, uint32_t depth) {
        char line[200];
        std::snprintf(line, sizeof(line), "%s thread %d (0x%08X): waits at %016" PRIX64 " (%u frames), in the game's code at %016" PRIX64 "\n",
            label, id, (uint32_t)thread, signature, depth, game);
        return line;
    }
}

bool recomp::save_states::capture(uint8_t* rdram, std::vector<uint8_t>& out, std::string& error) {
    using namespace ultramodern::save_states;
    if (!ultramodern::thread_queue_empty(rdram, ultramodern::running_queue)) {
        error = "a thread is about to run";
        return false;
    }

    out.clear();
    out.reserve(game_memory_size + 4096);
    Writer w{ out };
    w.put_bytes(magic, sizeof(magic));
    w.put(layout_version);
    w.put(fingerprint());

    // The timer thread is held from here on, so no timer runs out (and no timer in game memory
    // changes) while the state is taken.
    lock_timers();
    if (!is_quiet()) {
        unlock_timers();
        error = "the game is busy (an RSP task or an event is on its way)";
        return false;
    }

    const std::vector<ThreadInfo> threads = get_threads(rdram);
    w.put((uint32_t)threads.size());
    for (const ThreadInfo& thread : threads) {
        w.put((int32_t)thread.thread);
        w.put((int32_t)thread.id);
        w.put(thread.signature);
        w.put(game_signature(rdram, thread));
        w.put(thread.depth);
        w.put((uint32_t)(thread.game_context != nullptr ? 1 : 0));
        if (thread.game_context != nullptr) {
            Registers registers{};
            read_registers(static_cast<const recomp_context*>(thread.game_context), registers);
            w.put(registers);
        }
    }

    const TimerState timers = get_timer_state();
    w.put(timers.now);
    w.put(timers.ostime_offset);
    w.put((uint32_t)timers.active.size());
    for (PTR(OSTimer) timer : timers.active) {
        w.put((int32_t)timer);
    }

    w.put(get_event_state(rdram));

    w.put(game_memory_size);
    w.put_bytes(rdram, game_memory_size);

    unlock_timers();
    return true;
}

recomp::save_states::LoadResult recomp::save_states::restore(uint8_t* rdram, std::span<const uint8_t> data, std::string& error) {
    using namespace ultramodern::save_states;
    ParsedState state;
    if (!parse(data, state, error)) {
        return LoadResult::Invalid;
    }
    if (!ultramodern::thread_queue_empty(rdram, ultramodern::running_queue)) {
        error = "a thread is about to run";
        return LoadResult::NotNow;
    }

    // Every thread has to wait where it waited in the state: the same threads, each in the same calls
    // through the game's code, and with registers if it had them (it had started running).
    const bool built_here = state.fingerprint == fingerprint();
    const std::vector<ThreadInfo> threads = get_threads(rdram);
    if (threads.size() != state.threads.size()) {
        error = "the game has " + std::to_string(threads.size()) + " threads, the state " + std::to_string(state.threads.size());
        return LoadResult::NotNow;
    }
    for (const SavedThread& saved : state.threads) {
        const ThreadInfo* current = nullptr;
        for (const ThreadInfo& thread : threads) {
            if (thread.thread == saved.thread) {
                current = &thread;
            }
        }
        if (current == nullptr) {
            error = "thread " + std::to_string(saved.id) + " of the state isn't running";
            return LoadResult::NotNow;
        }
        // In the same build, the native call stack too (a stricter check of the same thing).
        if ((current->game_context != nullptr) != saved.has_registers ||
            game_signature(rdram, *current) != saved.game_signature ||
            (built_here && current->signature != saved.signature)) {
            error = "thread " + std::to_string(saved.id) + " waits somewhere else than in the state";
            return LoadResult::NotNow;
        }
    }

    lock_timers();
    if (!is_quiet()) {
        unlock_timers();
        error = "the game is busy (an RSP task or an event is on its way)";
        return LoadResult::NotNow;
    }

    // The game's memory, then what in it points at the program: each OSThread's native context.
    std::memcpy(rdram, state.memory, state.memory_size);
    for (const SavedThread& saved : state.threads) {
        restore_thread_link(rdram, saved.thread);
    }
    // Each thread's registers, into the context it waits with.
    for (const SavedThread& saved : state.threads) {
        if (!saved.has_registers) {
            continue;
        }
        for (const ThreadInfo& thread : threads) {
            if (thread.thread == saved.thread) {
                write_registers(static_cast<recomp_context*>(thread.game_context), saved.registers);
            }
        }
    }
    set_timer_state(rdram, state.timers);
    set_event_state(rdram, state.events);
    unlock_timers();

    // The events that came in for the state that was replaced.
    drop_external_messages();
    return LoadResult::Loaded;
}

bool recomp::save_states::same_build(std::span<const uint8_t> data) {
    ParsedState state;
    std::string error;
    return parse(data, state, error) && state.fingerprint == fingerprint();
}

std::string recomp::save_states::describe_threads(uint8_t* rdram, std::span<const uint8_t> data) {
    ParsedState state;
    std::string error;
    if (!parse(data, state, error)) {
        return error + "\n";
    }
    std::string out;
    for (const SavedThread& saved : state.threads) {
        out += describe("state's", saved.thread, saved.id, saved.signature, saved.game_signature, saved.depth);
    }
    return out;
}

std::string recomp::save_states::describe_current_threads(uint8_t* rdram) {
    std::string out;
    for (const ultramodern::save_states::ThreadInfo& thread : ultramodern::save_states::get_threads(rdram)) {
        out += describe("game's", thread.thread, thread.id, thread.signature, game_signature(rdram, thread), thread.depth);
    }
    return out;
}
