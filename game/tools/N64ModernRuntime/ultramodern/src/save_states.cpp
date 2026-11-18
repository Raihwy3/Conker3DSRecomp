// Save states: the game's threads, where each one waits, and the idle moment (see save_states.hpp for
// how a save state of a recompiled game works).

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/save_states.hpp"

#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <Windows.h>
#elif defined(__APPLE__)
#   include <execinfo.h>
#   include <mach-o/dyld.h>
#   include <mach-o/loader.h>
#   include <cstring>
#elif defined(__3DS__)
#   include <unwind.h>
extern "C" {
    extern char __start__[];
    extern char __end__[];
}
#else
#   include <execinfo.h>
#   include <link.h>
#endif

namespace {
    // --- Where a thread waits: its native call stack, as offsets into the program ---

    // The program's own code, the range a frame has to be in to count. The other frames (the C
    // runtime's start of a thread, the system's) are left out: their addresses change from one run to
    // the next, while the program's offsets don't (for the same build).
    struct ImageRange {
        uintptr_t start = 0;
        uintptr_t end = 0;
    };

#if defined(_WIN32)
    ImageRange find_image_range() {
        // The program itself (ultramodern is linked into it).
        HMODULE module = GetModuleHandleW(nullptr);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
        ImageRange range;
        range.start = reinterpret_cast<uintptr_t>(module);
        range.end = range.start + nt->OptionalHeader.SizeOfImage;
        return range;
    }
#elif defined(__APPLE__)
    ImageRange find_image_range() {
        // Image 0 is the program. Its range is its segments' (but __PAGEZERO), moved by its slide.
        const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(0));
        const intptr_t slide = _dyld_get_image_vmaddr_slide(0);
        ImageRange range{ UINTPTR_MAX, 0 };
        const uint8_t* command = reinterpret_cast<const uint8_t*>(header + 1);
        for (uint32_t i = 0; i < header->ncmds; i++) {
            const auto* load = reinterpret_cast<const load_command*>(command);
            if (load->cmd == LC_SEGMENT_64) {
                const auto* segment = reinterpret_cast<const segment_command_64*>(load);
                if (std::strcmp(segment->segname, "__PAGEZERO") != 0) {
                    range.start = std::min<uintptr_t>(range.start, segment->vmaddr + slide);
                    range.end = std::max<uintptr_t>(range.end, segment->vmaddr + segment->vmsize + slide);
                }
            }
            command += load->cmdsize;
        }
        return range;
    }
#elif defined(__3DS__)
    ImageRange find_image_range() {
        return { reinterpret_cast<uintptr_t>(__start__), reinterpret_cast<uintptr_t>(__end__) };
    }
#else
    ImageRange find_image_range() {
        // The first object dl_iterate_phdr reports is the program. Its range is its loaded segments'.
        ImageRange range{ UINTPTR_MAX, 0 };
        dl_iterate_phdr([](dl_phdr_info* info, size_t, void* data) -> int {
            auto* out = static_cast<ImageRange*>(data);
            for (int i = 0; i < info->dlpi_phnum; i++) {
                const auto& segment = info->dlpi_phdr[i];
                if (segment.p_type == PT_LOAD) {
                    out->start = std::min<uintptr_t>(out->start, info->dlpi_addr + segment.p_vaddr);
                    out->end = std::max<uintptr_t>(out->end, info->dlpi_addr + segment.p_vaddr + segment.p_memsz);
                }
            }
            return 1; // Only the first.
        }, &range);
        return range;
    }
#endif

    const ImageRange& image_range() {
        static const ImageRange range = find_image_range();
        return range;
    }

    // FNV-1a over the frames' offsets.
    struct Hash {
        uint64_t value = 0xCBF29CE484222325ull;
        uint32_t count = 0;
        void add(uintptr_t frame) {
            const ImageRange& range = image_range();
            if (frame < range.start || frame >= range.end) {
                return;
            }
            uint64_t offset = frame - range.start;
            for (int i = 0; i < 8; i++) {
                value ^= offset & 0xFF;
                value *= 0x100000001B3ull;
                offset >>= 8;
            }
            count++;
        }
    };

    // The calling thread's native call stack, hashed.
    void capture_signature(uint64_t& signature, uint32_t& depth) {
        Hash hash;
#if defined(_WIN32)
        // RtlCaptureStackBackTrace walks the stack with the program's unwind data, so it works
        // without frame pointers. Up to 62 frames at a time.
        constexpr ULONG batch = 62;
        void* frames[batch];
        ULONG skip = 1;
        while (skip < 2048) {
            const USHORT count = RtlCaptureStackBackTrace(skip, batch, frames, nullptr);
            for (USHORT i = 0; i < count; i++) {
                hash.add(reinterpret_cast<uintptr_t>(frames[i]));
            }
            if (count < batch) {
                break;
            }
            skip += count;
        }
#elif defined(__3DS__)
        struct UnwindState {
            Hash* hash;
            int skip;
        } state{ &hash, 1 };
        _Unwind_Backtrace([](struct _Unwind_Context* ctx, void* arg) -> _Unwind_Reason_Code {
            auto* s = static_cast<UnwindState*>(arg);
            const uintptr_t ip = _Unwind_GetIP(ctx);
            if (ip == 0) {
                return _URC_END_OF_STACK;
            }
            if (s->skip > 0) {
                s->skip--;
            } else {
                s->hash->add(ip);
            }
            return s->hash->count >= 64 ? _URC_END_OF_STACK : _URC_NO_REASON;
        }, &state);
#else
        void* frames[1024];
        const int count = backtrace(frames, 1024);
        for (int i = 1; i < count; i++) {
            hash.add(reinterpret_cast<uintptr_t>(frames[i]));
        }
#endif
        signature = hash.value;
        depth = hash.count;
    }

    // --- The game's threads ---

    std::mutex threads_mutex;
    std::vector<UltraThreadContext*> game_threads;

    std::atomic<ultramodern::save_states::safe_point_callback_t> safe_point_callback = nullptr;
    std::atomic<bool> safe_points_wanted = false;
    std::atomic<int32_t> tasks_in_flight = 0;
}

thread_local UltraThreadContext* current_thread_context = nullptr;

void ultramodern::save_states::register_thread(UltraThreadContext* context, PTR(OSThread) thread) {
    context->self = thread;
    std::lock_guard lock{ threads_mutex };
    game_threads.push_back(context);
}

void ultramodern::save_states::unregister_thread(UltraThreadContext* context) {
    std::lock_guard lock{ threads_mutex };
    std::erase(game_threads, context);
}

uint64_t ultramodern::save_states::program_fingerprint(uintptr_t extra) {
    // Where some of the runtime's functions are in the program (as offsets, like the signatures), the
    // program's size, and a place the caller adds: a build that changes the code nearly always moves one.
    Hash hash;
    hash.add(reinterpret_cast<uintptr_t>(&ultramodern::save_states::on_idle));
    hash.add(reinterpret_cast<uintptr_t>(&ultramodern::run_next_thread_and_wait));
    hash.add(reinterpret_cast<uintptr_t>(&osRecvMesg));
    hash.add(extra);
    const ImageRange& range = image_range();
    uint64_t size = range.end - range.start;
    for (int i = 0; i < 8; i++) {
        hash.value ^= size & 0xFF;
        hash.value *= 0x100000001B3ull;
        size >>= 8;
    }
    return hash.value;
}

void ultramodern::save_states::set_current_thread(UltraThreadContext* context) {
    current_thread_context = context;
}

void ultramodern::save_states::record_wait(UltraThreadContext* context, WaitKind kind) {
    capture_signature(context->wait_signature, context->wait_depth);
    // A message queue wait (note_wait, just before) tells its own kind and values.
    if (context->noted_kind != 0) {
        kind = (WaitKind)context->noted_kind;
    }
    context->wait_kind = (uint32_t)kind;
    for (int i = 0; i < 3; i++) {
        context->wait_args[i] = context->noted_args[i];
        context->noted_args[i] = 0;
    }
    context->noted_kind = 0;
    // With the kind and the values, which the native frames hold as it waits.
    uint32_t values[4] = { context->wait_kind, context->wait_args[0], context->wait_args[1], context->wait_args[2] };
    for (uint32_t value : values) {
        for (int i = 0; i < 4; i++) {
            context->wait_signature ^= value & 0xFF;
            context->wait_signature *= 0x100000001B3ull;
            value >>= 8;
        }
    }
}

void ultramodern::save_states::note_wait(WaitKind kind, uint32_t a, uint32_t b, uint32_t c) {
    if (current_thread_context != nullptr) {
        current_thread_context->noted_kind = (uint32_t)kind;
        current_thread_context->noted_args[0] = a;
        current_thread_context->noted_args[1] = b;
        current_thread_context->noted_args[2] = c;
    }
}

void ultramodern::save_states::set_thread_game_context(void* game_context) {
    if (current_thread_context != nullptr) {
        current_thread_context->game_context = game_context;
    }
}

std::vector<ultramodern::save_states::ThreadInfo> ultramodern::save_states::get_threads(RDRAM_ARG1) {
    std::vector<ThreadInfo> out;
    std::lock_guard lock{ threads_mutex };
    for (UltraThreadContext* context : game_threads) {
        OSThread* thread = TO_PTR(OSThread, context->self);
        // A thread being destroyed (its OSThread no longer has its context) is left out.
        if (thread->context != context) {
            continue;
        }
        out.push_back({ context->self, thread->id, context->game_context, context->wait_signature, context->wait_depth,
            (WaitKind)context->wait_kind, { context->wait_args[0], context->wait_args[1], context->wait_args[2] } });
    }
    return out;
}

void ultramodern::save_states::restore_thread_link(RDRAM_ARG PTR(OSThread) thread_) {
    std::lock_guard lock{ threads_mutex };
    for (UltraThreadContext* context : game_threads) {
        if (context->self == thread_) {
            TO_PTR(OSThread, thread_)->context = context;
            return;
        }
    }
}

void ultramodern::save_states::set_safe_point_callback(safe_point_callback_t callback) {
    safe_point_callback = callback;
}

void ultramodern::save_states::set_safe_points_wanted(bool wanted) {
    safe_points_wanted = wanted;
}

void ultramodern::save_states::on_receive(RDRAM_ARG PTR(OSMesgQueue) mq, PTR(OSMesg) msg, int32_t flags) {
    const safe_point_callback_t callback = safe_point_callback.load();
    if (callback == nullptr || !safe_points_wanted.load() || current_thread_context == nullptr) {
        return;
    }
    // Every other thread waits only with none ready to run.
    if (!thread_queue_empty(PASS_RDRAM running_queue)) {
        return;
    }
    // The running thread is here, in osRecvMesg with these values (held in its native frames).
    note_wait(WaitKind::ReceiveRunning, (uint32_t)mq, (uint32_t)msg, (uint32_t)flags);
    record_wait(current_thread_context, WaitKind::ReceiveRunning);
    callback(PASS_RDRAM1);
}

void ultramodern::save_states::on_idle(RDRAM_ARG1) {
    const safe_point_callback_t callback = safe_point_callback.load();
    if (callback == nullptr || !safe_points_wanted.load()) {
        return;
    }
    // The idle thread "waits" here, in pause_self.
    if (current_thread_context != nullptr) {
        record_wait(current_thread_context, WaitKind::Idle);
    }
    callback(PASS_RDRAM1);
}

bool ultramodern::save_states::is_quiet() {
    // In this order: a task's last message is sent before it counts as finished, so with none in flight,
    // any message it sent is either still waiting below or was handed to the game.
    if (tasks_in_flight.load() != 0) {
        return false;
    }
    return external_message_count() == 0 && timer_actions_pending() == 0;
}

void ultramodern::save_states::task_submitted() {
    tasks_in_flight.fetch_add(1);
}

void ultramodern::save_states::task_finished() {
    tasks_in_flight.fetch_sub(1);
}
