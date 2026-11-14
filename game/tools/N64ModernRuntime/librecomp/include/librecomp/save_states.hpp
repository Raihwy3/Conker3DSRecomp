#ifndef __RECOMP_SAVE_STATES_HPP__
#define __RECOMP_SAVE_STATES_HPP__

// Save states: a whole state of the game, as bytes (ultramodern/save_states.hpp tells how a save state
// of a recompiled game works). The game library adds to ultramodern's parts each thread's registers
// and the game's memory (the N64's 8MB; the mods' memory past it isn't in a state).
//
// Both are only called from ultramodern's idle callback (ultramodern::save_states::set_idle_callback),
// while every game thread but the idle one waits, and once ultramodern::save_states::is_quiet().

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace recomp {
namespace save_states {

// The game's memory in a state: the N64's 8MB.
constexpr uint32_t game_memory_size = 8 * 1024 * 1024;

// Takes a state. False, with why, if it can't be taken now (try again at a later idle moment).
bool capture(uint8_t* rdram, std::vector<uint8_t>& out, std::string& error);

enum class LoadResult {
    // Loaded: the game goes on from the state at the next event.
    Loaded,
    // The game's threads don't all wait where they waited in the state (so it can't be loaded now;
    // maybe at a later idle moment, as the game gets to the same point): why in the error.
    NotNow,
    // Not a state, or one from another version of the program: it can never be loaded.
    Invalid,
};

// Checks a state, and loads it if it can be loaded now.
LoadResult restore(uint8_t* rdram, std::span<const uint8_t> data, std::string& error);

// Whether a state was taken by this build of the program. In this build, a state loads where each
// thread waits in the same native calls (its signature) and the same calls through the game's code.
// From another build, only the game's code is checked (the native calls are that build's), so it
// loads as long as that build's game code is the same where its threads wait.
bool same_build(std::span<const uint8_t> data);

// What a state holds about where its threads wait, for a log: one line a thread.
std::string describe_threads(uint8_t* rdram, std::span<const uint8_t> data);
std::string describe_current_threads(uint8_t* rdram);

} // namespace save_states
} // namespace recomp

#endif
