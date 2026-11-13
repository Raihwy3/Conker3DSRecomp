# Building

Linux is the tested build host. Install Python 3, CMake 3.20+, Ninja, a C/C++20 host compiler, `patch`, and devkitPro’s `3ds-dev`, `3ds-curl`, `3ds-mbedtls` and `3ds-zlib` packages. Set `DEVKITPRO` and `DEVKITARM` as described by devkitPro.

From the repository root:

```sh
python3 tools/build.py --rom /path/to/your/US-retail-ROM.z64
```

The build needs the unmodified US retail ROM in big-endian format, SHA-1 `4cbadd3c4e0729dec46af64ad018050eada4f47a`. The Uncensored ROM is a runtime choice, not the build input.

The script copies source into `.local/workspace`, builds the included recompiler, regenerates the game and RSP code from your ROM, then builds the 3DS launcher. ROMs, generated files and binaries stay under ignored `.local/`. It does not download tools or game data.

Outputs: `.local/workspace/build-3ds/ConkerRecomp.3dsx` and `.smdh`.

`--jobs N` sets build parallelism. If your 3DS portlibs are elsewhere, use `--portlibs /path/to/portlibs/3ds`. Subsequent builds may omit `--rom` to reuse the private build copy. No Git submodule setup is needed: the required modified dependency source is included.

Run `python3 tools/audit_source.py` before uploading source. `python3 tools/package.py` produces an install ZIP and a source ZIP in `.local/releases/`. Neither archive includes your ROM, generated source, saves, screenshots or test logs.

To update vendored code, preserve its notices and update `docs/UPSTREAM.json`. Native fixes in this tree are part of the port; replacing it with an unmodified upstream checkout will lose them.
