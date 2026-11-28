# Changelog

## 0.3.0-r3 (Wireless Multiplayer Polish & Sync Fixes)

- Fixed Old 3DS + New 3DS wireless multiplayer desyncs (resolved `osGetTime()` symbol collision with `ultramodern`'s virtual clock, fixed `timer_thread` stealing virtual timers on startup, matched Old/New 3DS settings during wireless play, disabled local-only cheats/photo mode/cutscene skip in wireless play, and raised peer timeout to 20 s).
- Fixed per-player fullscreen multiplayer HUD:
  - Each console now shows only its own player's HUD (no doubled splitscreen HUD), scaled up cleanly by `1.20x` with radar dots aligned inside the minimap.
  - Fixed the Pause menu blurred background and buttons rendering cleanly fullscreen.
  - Fixed per-player death iris/darkening wipes showing only on the player who died without holes.
- Enabled custom aiming crosshairs in multiplayer (replacing the built-in center aiming crosshair while leaving the sniper scope UI intact).
- Fixed Hold-B cutscene skip to trigger Conker's real native cutscene skipper (`func_1501E1B4` / `func_1501E05C`, clearing the unskippable flag `0x800C3C9C` and arming first-viewing skip `0x800D2E40`) instead of writing the wrong cutscene-request address.

## 0.3.0-r2 (Wireless Multiplayer Test)

- Added experimental 2–4 player **3DS Local Wireless (`nwm::UDS`) lockstep multiplayer** (press **Y: WIRELESS** on the Select Version screen to Host or Join a room; requires the same ROM on each console).
- Added **per-player fullscreen rendering** during splitscreen multiplayer so each 3DS renders only its own player's view fullscreen and skips other players' viewports for much better performance.
- Added deterministic virtual timeline, audio length, and music-start timing for lockstep sync, plus automatic recovery to local control if the host disconnects.
- Added `nwm::UDS` service permission to the CIA build.

## 0.3.0-r1

- Fixed CIA updates failing with "Unable to save the CIA on the SD card." when a previous `launcher-update.cia` already exists on the SD card.
- Improved Old 3DS / Old 2DS performance and memory headroom:
  - Skipped the unused per-frame GPU depth-buffer copy when 3D is off.
  - Defaulted bottom-screen animated tails to Off on Old 3DS (throttled to 15 Hz when enabled) and replaced software `/ 255` divides in bottom-screen HUD blending with fast integer shift math.
  - Kept the GPU command buffer at 512 KB on Old 3DS, lazy-allocated post-processing and frame-readback buffers, and skipped allocating right-eye/depth-copy buffers on Old 2DS (~1.4 MB more free linear RAM on Old 3DS; ~1.6 MB linear RAM + 576 KB VRAM on Old 2DS).

## 0.3.0

- Fixed the broken intro (Conker standing still, respawning at the checkpoint) caused by "Cheat: Unlock chapters" overwriting the game's story progress. It now only enables the game's own chapter-unlock flag and never touches saves.
- Fixed Invincible, Infinite lives and Max cash cheats writing to the wrong values; they now apply only during gameplay.
- Fixed Conker's shaders turning brown underwater (brown slurry area).
- Fixed a GPU command-buffer overflow crash.
- Added a more readable achievement popup.
- Added an optional animated "Loading..." indicator on the top screen while levels load.
- Fixed New 3DS-only rendering mode failing on original 3DS hardware.

## 0.2.9-r4

- Retain music-note release times when notes start, avoiding the invalid envelope read identified in the Frankie fight crash dump. The reported fight still needs console retesting.
- Added the animated tail as a lower-left top-screen autosave indicator, tied to game-save file writes.

## 0.1.5

- Added French, Spanish and Brazilian Portuguese ROM selection under **Translations**, with **ES FR PTBR** shown beneath the entry.
- Added chocolate health and money to the bottom screen, with separate visibility toggles.
- Added optional animated tails using the original game’s bending animation.
- Changed the optional aiming reticle to a white dot.
- Redesigned the ROM picker, loading and update screens with Conker’s font and pub-floor background.
- Brightened the bottom-screen background slightly.
