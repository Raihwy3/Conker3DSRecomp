# Changelog

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
