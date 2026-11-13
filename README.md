# Conker: 3DS Recomp

A work-in-progress Nintendo 3DS port of [CBFD-Recompiled](https://github.com/sciaschi/CBFD-Recompiled), using [rt64-3ds](https://github.com/oneup03/rt64-3ds) and N64ModernRuntime.

Includes stereoscopic 3D, interpolated presentation, graphics filters, DSP audio, camera and gyro options, and a touchscreen bottom-screen interface. Performance and stability still need work. Later chapters have not been fully tested.

## Install

1. Copy the release ZIP’s `3ds` folder to the root of your SD card.
2. Put your legally backed-up US retail ROM in `/3ds/ConkerRecomp/`, named `CBFD.z64`.
3. Start **Conker: 3DS Recomp** from Homebrew Launcher.

The Uncensored ROM can also be placed there as `CBFDUncensored.z64`. The launcher lets you choose which ROM to start. All versions share saves and settings.

For translations, place `CBFDfr.z64` (French), `CBFDes.z64` (Spanish) or `CBFDptbr.z64` (Brazilian Portuguese) in the same folder, then choose **Translations** at startup.

The launcher uses Conker’s ROM-derived font and pub-floor background for version selection and update screens. The Translations entry lists ES, FR and PTBR beneath it.

No ROM or extracted game artwork, music or textures is supplied in this source repository. The interface reads its game artwork from your ROM. The public launcher icon is an original monogram.

See [Controls and bottom screen](docs/CONTROLS.md) for control presets and display options.

## Build and updates

See [Building](docs/BUILDING.md) for the required tools and ROM-based build steps, and [Updates](docs/UPDATER.md) for release packaging and the optional startup updater.

## Credits and licences

See [Credits](CREDITS.md) and [Licences](NOTICE.md). The combined program uses GPLv3 because it links N64ModernRuntime. MIT and other dependency licences remain in effect for their respective files. The original game is not covered by these licences.
