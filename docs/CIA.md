# CIA build

The standalone CIA launches from HOME Menu and uses `/3ds/ConkerRecomp/` for ROMs, saves and `settings.ini`, just like the Homebrew Launcher version. It does not require a `.3dsx` beside the ROM. Install the CIA with FBI on a console with custom firmware.

The title ID is `000400000E0CBF00`. Keep this ID for future releases so installing an update replaces this application rather than creating another icon.

## Updates

An installed title selects the `conker-3ds-recomp*.cia` asset from the latest GitHub release. Homebrew Launcher selects the ZIP instead. Both require a newer release tag and verify the download size and SHA-256 digest over HTTPS.

The CIA updater saves its verified download as `/3ds/ConkerRecomp/launcher-update.cia`. Close the game and install that file with FBI, then relaunch. It does not install or replace the running title itself. ROMs, settings and saves stay in their shared folder.

A release without a CIA asset is skipped by the installed version. The ZIP updater and its recovery behavior are unchanged.

## Build

First build normally with `python3 tools/build.py`. Install makerom, bannertool, Pillow and pyelftools on the build machine, then run:

```sh
python3 tools/build_cia.py --makerom /path/to/makerom --bannertool /path/to/bannertool
```

You can omit the tool arguments when both tools are on PATH. `MAKEROM` and `BANNERTOOL` environment variables also work.

The output is `.local/releases/conker-3ds-recomp-v<version>.cia`. The script checks the executable matches the source snapshot, validates the application ID and ExeFS hashes, compares the code with the ELF, and rejects bundled RomFS data. The HOME Menu banner uses the existing custom launcher icon. The makerom Homebrew logo licence is in `licenses/makerom-homebrew-logo.txt`.

Upload the raw CIA beside the install ZIP and matching source ZIP under the same release tag. Do not name a ZIP of the CIA like the Homebrew Launcher install ZIP: the latter must contain the `.3dsx` and `.smdh` pair.

Host structure and emulator checks do not replace a console boot and update test.
