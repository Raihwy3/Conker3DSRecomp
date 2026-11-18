# External build libraries

The modified runtime, renderer, OpenDSP and recompiler sources are included in this repository. The 3DS SDK and HTTPS libraries are supplied separately by devkitPro.

The validated build used devkitARM GCC 16.1.0, libctru 2.7.0, Citro3D 1.7.1, and these devkitPro portlibs packages:

- 3ds-curl 8.4.0-1 — https://curl.se/download/curl-8.4.0.tar.xz
- 3ds-mbedtls 2.28.8-1 — https://github.com/Mbed-TLS/mbedtls/tree/v2.28.8
- 3ds-zlib 1.3.1-1 (including MiniZip) — https://github.com/madler/zlib/tree/v1.3.1

Package recipes and platform adaptations are maintained by devkitPro: https://github.com/devkitPro/3ds_portlibs . libctru and Citro3D source: https://github.com/devkitPro/libctru and https://github.com/devkitPro/citro3d . newlib source: https://github.com/devkitPro/newlib . GCC runtime licence terms include the Runtime Library Exception; both texts are retained in `licenses/`.

These versions describe the validation environment, not a promise that a different installed version is identical. Preserve the actual dependency notices when rebuilding with updated packages. The updater’s vendored CA bundle and jsmn fingerprints are recorded beside them in `sources.json`.
