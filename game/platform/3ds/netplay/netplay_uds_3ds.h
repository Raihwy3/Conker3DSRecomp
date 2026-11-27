#pragma once

#include <3ds.h>
#include <cstdint>

namespace conker_netplay {

// Opens the 3DS Local Wireless (nwm::UDS) lobby UI from the launcher.
// Returns true if a multiplayer match is ready to boot, false if cancelled.
bool run_local_wireless_lobby(PrintConsole* top, PrintConsole* bottom, uint64_t rom_hash);

bool is_active();
int player_count();
int local_port();

// Called during conker_on_init_3ds() right before the game starts running.
void on_game_init();

// Cleanly leaves the match and shuts down UDS.
void shutdown();

} // namespace conker_netplay
