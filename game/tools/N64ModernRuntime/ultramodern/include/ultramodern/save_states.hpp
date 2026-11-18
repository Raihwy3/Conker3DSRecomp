#ifndef __ULTRAMODERN_SAVE_STATES_HPP__
#define __ULTRAMODERN_SAVE_STATES_HPP__

// Save states: the parts of a save state that ultramodern holds.
//
// Why a recompiled game is hard to save: in an emulator the whole console is data (memory and the
// CPU's registers), so a save state is a copy of it. Here each of the game's threads runs as a real
// thread of the program, and where a thread is in the game's code (which function called which) is
// held on that thread's own native stack, which can't be written to a file or rebuilt from one.
//
// How it's done anyway: the game's threads take turns (only one runs at a time, as on the N64). A
// thread waiting at the same place in the same calls each time is in a known state there, whatever
// its native stack holds: its registers (the recompiled code's context, which the game library owns)
// and the game's memory are all that change between one wait and the next. So a state is taken at a
// safe point, a moment when every game thread but the one running waits, and the running one is at a
// known call too: the idle thread (the lowest priority one) in pause_self, which only runs when all
// the others wait, or any thread as it checks a message queue (osRecvMesg, waiting or not) with no
// other thread ready to run (a game that polls a queue may never let the idle thread run). It's
// loaded at a safe point where every thread is where it was when the state was taken. Each thread's
// place is told by its signature: the native call stack it waits (or runs) in, as offsets into the
// program (so it's the same each run of the same build), hashed with the wait's own values.
//
// What ultramodern holds and saves here, besides each thread's signature:
//   - the clock (osGetTime, osGetCount and the timers run on it), set back or forward on loading so
//     the game's own times (timers' deadlines, timestamps it keeps) stay right;
//   - the timers that are running (the list is the runtime's; the timers are in game memory);
//   - the VI's state (the video mode, the framebuffers, the retrace message and its rate) and the
//     queues that the SP, DP, AI and SI events are sent to.
// The game library (librecomp) adds each thread's registers and the game's memory.

#include <cstdint>
#include <string>
#include <vector>

#include "ultra64.h"

struct UltraThreadContext;

namespace ultramodern {
namespace save_states {

// --- Threads ---

// How a thread waits (each holds different native frames).
enum class WaitKind : uint32_t {
    None,
    // In osRecvMesg for an empty queue, or osSendMesg to a full one.
    Receive,
    Send,
    // Ready to run, but a higher priority thread runs.
    Preempted,
    // Created but not started, or stopped (osStopThread).
    Stopped,
    // Running at a safe point: the idle thread in pause_self, or a thread in osRecvMesg.
    Idle,
    ReceiveRunning,
};

struct ThreadInfo {
    PTR(OSThread) thread;
    OSId id;
    // The game library's context for the thread (its registers), or null if the thread hasn't
    // started running game code yet.
    void* game_context;
    // Where the thread waits (the hash of its native call stack, as offsets into the program), and
    // how many of the stack's frames are in the program. 0 if it hasn't waited yet.
    uint64_t signature;
    uint32_t depth;
    // The wait's kind and its own values (the queue, the message or where it goes, the flags).
    WaitKind kind;
    uint32_t args[3];
};

// From the game library, on the thread, before it runs the thread's game code: the thread's context.
void set_thread_game_context(void* game_context);

// The game's threads, in the order they were created. Only meaningful while no thread is running
// game code, as in the idle callback.
std::vector<ThreadInfo> get_threads(RDRAM_ARG1);

// Puts a thread's native context back into its OSThread (in game memory, which a load overwrote).
void restore_thread_link(RDRAM_ARG PTR(OSThread) thread);

// Tells this build of the program from others (a state's signatures only fit the build that took it):
// where some functions are in the program, its size, and the caller's own function (extra).
uint64_t program_fingerprint(uintptr_t extra);

// --- Safe points ---

// Called on the running game thread at each safe point (see above) while set_safe_points_wanted(true):
// the idle thread in pause_self before it waits for the next event, or a thread checking a message
// queue before it does. Only then is the running thread's signature taken (it's a walk of its stack,
// and a game can check a queue thousands of times a second).
using safe_point_callback_t = void (*)(RDRAM_ARG1);
void set_safe_point_callback(safe_point_callback_t callback);
void set_safe_points_wanted(bool wanted);

// Called by the idle thread (pause_self) as it gets to run, and by osRecvMesg (with its queue, where
// the message goes, and its flags) when no other thread is ready to run.
void on_idle(RDRAM_ARG1);
void on_receive(RDRAM_ARG PTR(OSMesgQueue) mq, PTR(OSMesg) msg, int32_t flags);

// Whether nothing is on its way to the game: no RSP task running or queued, no event message waiting
// to be handed to the game, no timer change waiting for the timer thread. A state is only taken (or
// loaded) then, as none of that is in a state.
bool is_quiet();

// After a load: drops the event messages that came in meanwhile (they were for the state that was
// replaced, and the next VI starts the loaded one off).
void drop_external_messages();

// --- The clock and the timers ---

struct TimerState {
    // The game's clock (osGetCount's ticks) when the state was taken.
    uint64_t now;
    // osSetTime's offset.
    int64_t ostime_offset;
    // The running timers (the OSTimers in game memory).
    std::vector<PTR(OSTimer)> active;
};

// The timer thread is held while the state is taken or loaded (it reads and writes the timers in game
// memory), from lock_timers until unlock_timers.
void lock_timers();
void unlock_timers();
// Between lock_timers and unlock_timers.
TimerState get_timer_state();
void set_timer_state(RDRAM_ARG const TimerState& state);

// --- The VI and the events ---

struct ViStateData {
    PTR(OSViMode) mode;
    PTR(void) framebuffer;
    PTR(OSMesgQueue) mq;
    OSMesg msg;
    uint32_t state;
    uint32_t control;
    int32_t retrace_count;
};

struct EventState {
    ViStateData vi[2];
    int32_t vi_cur_state;
    int32_t vi_field;
    int32_t remaining_retraces;
    PTR(OSMesgQueue) sp_mq, dp_mq, ai_mq, si_mq;
    OSMesg sp_msg, dp_msg, ai_msg, si_msg;
};

EventState get_event_state(RDRAM_ARG1);
void set_event_state(RDRAM_ARG const EventState& state);

// For is_quiet: an RSP task was handed to the RSP (or the renderer), or finished (once its last
// message has been sent).
void task_submitted();
void task_finished();

// --- Inside ultramodern ---

// threads.cpp: a thread made by osCreateThread, and once it's ended.
void register_thread(UltraThreadContext* context, PTR(OSThread) thread);
void unregister_thread(UltraThreadContext* context);
// threads.cpp: on the thread, as it starts.
void set_current_thread(UltraThreadContext* context);
// threads.cpp: on the thread, just before it waits (to be resumed, started, or for the next event).
void record_wait(UltraThreadContext* context, WaitKind kind);
// mesgqueue.cpp: on the thread, just before a message queue wait: its kind and the values its native
// frames hold (taken up by the next record_wait).
void note_wait(WaitKind kind, uint32_t a, uint32_t b, uint32_t c);
// mesgqueue.cpp: the event messages waiting to be handed to the game.
size_t external_message_count();
// timer.cpp: osSetTimer and osStopTimer calls the timer thread hasn't taken up yet.
int32_t timer_actions_pending();

} // namespace save_states
} // namespace ultramodern

#endif
