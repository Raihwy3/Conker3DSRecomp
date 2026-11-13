#!/usr/bin/env python3
"""List publishable files and reject private/game-derived build outputs."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
EXCLUDED={'.local','.git','__pycache__'}
FIRMWARE={'renderer/third_party/OpenDSP/firmware/opendsp.cdc','renderer/third_party/OpenDSP/firmware/aac_tables.bin','renderer/third_party/OpenDSP/firmware/mp3_tables.bin'}
def files():
    return sorted(p for p in ROOT.rglob('*') if p.is_file() and not any(x in EXCLUDED for x in p.relative_to(ROOT).parts))
def audit():
    bad=[]
    for p in files():
        r=p.relative_to(ROOT).as_posix()
        if p.is_symlink() or p.suffix.lower() in {'.z64','.n64','.v64','.3dsx','.smdh','.cia','.eep','.fla','.sra','.elf','.so','.dll','.exe','.zip','.7z','.a','.o','.dmp','.bmp','.log','.pyc','.wav','.mp3','.ogg','.flac','.opus','.mp4'} or (p.suffix.lower() in {'.bin','.cdc'} and r not in FIRMWARE) or any(x in r.split('/') for x in ['RecompiledFuncs','RecompiledPatches','build','build-3ds','sdmc','performance-review','audio-review']) or p.name in {'settings.ini','log.txt','AUTOTEST','save.bin'}:
            bad.append(r)
        if p.stat().st_size==64*1024*1024:bad.append(r+' (ROM-sized file)')
    if bad:raise SystemExit('Do not publish:\n'+'\n'.join(bad))
    for name in ['LICENSE','NOTICE.md','CREDITS.md','docs/UPSTREAM.json','licenses/launcher-MIT.txt']:
        if not (ROOT/name).is_file():raise SystemExit('Missing '+name)
    return files()
if __name__=='__main__':print(f'PASS: {len(audit())} publishable source/licence files; private build directory excluded.')
