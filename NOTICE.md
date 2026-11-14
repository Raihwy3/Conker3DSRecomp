# Licences

This repository contains several projects with different licences. `LICENSE` is GNU GPL version 3, supplied for the combined program because N64ModernRuntime is linked into it. It does not replace the licences or copyright notices of individual components, and does not grant rights to the original game.

| Component | Licence | Location |
| --- | --- | --- |
| CBFD-Recompiled host replacements | MIT, Sean Ciaschi | `game/LICENSE` |
| rt64-3ds and RT64-derived renderer | MIT, RT64 contributors | `renderer/LICENSE` |
| N64ModernRuntime | GPLv3 | `game/tools/N64ModernRuntime/COPYING` |
| N64Recomp and its build tools | MIT, Wiseguy | both tool checkouts’ `LICENSE` |
| Conker launcher additions and adapted Eyes of Gold updater | MIT, Raihwy3 | `licenses/launcher-MIT.txt` |
| GamepadMotionHelpers | MIT, Jibb Smart | `licenses/GamepadMotionHelpers-MIT.txt` |
| OpenDSP | BSD-2-Clause, Cellenseres | `licenses/OpenDSP-BSD-2-Clause.txt` |
| minimp3 | CC0/public-domain dedication | `licenses/minimp3-CC0.txt`, companion notice |
| miniz, o1heap, nlohmann/json, sse2neon | MIT | original notices retained in their source trees |
| xxHash, concurrentqueue | BSD; semaphore also has a zlib notice | original files and `licenses/` |
| jsmn | MIT | `licenses/jsmn-MIT.txt` |
| curl, mbedTLS, MiniZip, zlib | curl licence, Apache-2.0, MiniZip/zlib terms | `licenses/` |
| makerom Homebrew boot logo (CIA only) | MIT | `licenses/makerom-homebrew-logo.txt` |
| Mozilla CA certificates | MPL-2.0 | `licenses/Mozilla-MPL-2.0.txt` |
| libctru, citro3d, newlib | component-specific permissive terms | `licenses/` |
| GCC runtime libraries | GPLv3 with GCC Runtime Library Exception | `licenses/` |

Build-only dependencies (including ELFIO, fmt, rabbitizer, toml++ and SLJIT) retain their own licence files in the vendored tool trees. Upstream repository URLs and starting commits are recorded in `docs/UPSTREAM.json`. These are modified snapshots, not unchanged upstream releases.

Native 3DS integration, renderer fixes, audio adaptations, input, UI and the updater have been modified for this port. Existing upstream notices remain attached to inherited work.

When distributing binaries, include these notices and the corresponding source for that build, including modified dependencies and build instructions. The packaging script creates a matching source archive alongside the install ZIP. The ROM and generated game code must be produced locally from the user’s own copy; they are excluded from the public source archive. A software licence cannot grant redistribution rights to Rare or Nintendo’s game code or assets.
