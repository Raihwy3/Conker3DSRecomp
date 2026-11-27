# Changelog

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
