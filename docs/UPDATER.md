# Startup updates

The updater is adapted from Eyes of Gold Launcher. Set `repository` in `release.json` to your GitHub `owner/repository`, and set `tag` to the version being built. An empty repository disables network checks. This project uses `Raihwy3/Conker3DSRecomp`.

The repository and release assets must be public: the console does not use a GitHub account or access token. Private repositories cannot serve updates through this updater.

With Wi-Fi connected, startup checks that repository’s latest public GitHub Release. A newer version offers Install or Not now. B skips a check or cancels a download. Offline or unavailable checks leave normal startup available. Put an empty `no-update-check.flag` in `/3ds/ConkerRecomp/` to disable checks.

Downloads use HTTPS with certificate and hostname verification. The expected size and SHA-256 digest come from GitHub’s asset metadata; ZIP CRCs and launcher headers are also checked. The ZIP is streamed to the SD card, not held in RAM. Limits are 32 MiB for the ZIP and 64 MiB for the uncompressed launcher.

For Homebrew Launcher, only `ConkerRecomp.3dsx` and `ConkerRecomp.smdh` are installed. ROMs, saves and settings are kept. Temporary files, backups and a recovery journal protect file replacement; startup attempts recovery before checking for updates. The installed build runs after relaunching. Recovery also runs when network checks are disabled.

## Publish an update

1. Set the repository and tag in `release.json` before building. Tags use `vMAJOR.MINOR.PATCH`, optionally followed by `-rN`.
2. Build, test and run `python3 tools/package.py`.
3. Publish a GitHub Release with that exact tag and upload both generated ZIPs. Mark it Latest, with the prerelease checkbox off.

The updater selects `conker-3ds-recomp*.zip`, never the source archive. Keep the install layout `3ds/ConkerRecomp/ConkerRecomp.3dsx` plus its `.smdh`. GitHub must provide the asset’s `sha256:` digest. Equal, older, draft and prerelease versions are ignored. Do not reuse a tag for different update content.

Real Wi-Fi download and installation on a 3DS still need testing for this port. Host tests cover the shared parser, HTTPS transport, integrity checks and recovery, not the console network stack.

## Installed CIA version

Installed titles select a separate `.cia` release asset and save it as `/3ds/ConkerRecomp/launcher-update.cia` after verification. Close the game and install that file with FBI. This does not replace the SD-card `.3dsx`. See [CIA build and updates](CIA.md).
