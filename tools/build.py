#!/usr/bin/env python3
"""Build privately from the supplied ROM, leaving the public source tree clean."""
import argparse,os,shutil,subprocess,hashlib,json
from pathlib import Path
from audit_source import ROOT,audit

def run(args,cwd=None):
    print('+', ' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),cwd=cwd,check=True)

def main():
    p=argparse.ArgumentParser();p.add_argument('--rom',type=Path);p.add_argument('--jobs',type=int,default=4);p.add_argument('--portlibs',type=Path);a=p.parse_args()
    if a.jobs<1:p.error('--jobs must be positive')
    dkp=Path(os.environ.get('DEVKITPRO','/opt/devkitpro'));work=ROOT/'.local/workspace';work.mkdir(parents=True,exist_ok=True)
    for f in audit():
        dest=work/f.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True)
        if not dest.exists() or dest.read_bytes()!=f.read_bytes():shutil.copy2(f,dest)
    rom=work/'game/conker/baserom.us.z64';rom.parent.mkdir(parents=True,exist_ok=True)
    if a.rom:shutil.copy2(a.rom.resolve(),rom)
    if not rom.is_file():p.error('Supply --rom /path/to/US-retail-ROM.z64')
    tools=work/'game/tools/N64Recomp'
    run(['cmake','-S',tools,'-B',tools/'build','-G','Ninja','-DCMAKE_BUILD_TYPE=Release'])
    run(['cmake','--build',tools/'build','--target','N64RecompCLI','RSPRecomp','--parallel',a.jobs])
    # Regenerate only when ROM, recompilation inputs or either tool changes.
    inputs=[rom,tools/'build/N64Recomp',tools/'build/RSPRecomp',work/'game/conker.toml']
    inputs += sorted(p for p in (work/'game/recomp').iterdir() if p.suffix in {'.py','.toml','.txt','.patch'})
    fingerprint=hashlib.sha256(b''.join(hashlib.sha256(p.read_bytes()).digest() for p in inputs)).hexdigest()
    stamp=work/'recompile.sha256'
    if not stamp.exists() or stamp.read_text()!=fingerprint or not (work/'game/RecompiledFuncs/lookup.cpp').exists():
        run(['python3','recomp/recompile.py'],cwd=work/'game')
        stamp.write_text(fingerprint)
    build=work/'build-3ds'
    args=['cmake','-S',work/'game/platform/3ds','-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_TOOLCHAIN_FILE='+str(work/'renderer/cmake/devkitARM-3ds.cmake'),'-DDEVKITPRO='+str(dkp)]
    if a.portlibs:args+=['-DCONKER_UPDATE_PORTLIBS='+str(a.portlibs.resolve())]
    run(args);run(['cmake','--build',build,'--parallel',a.jobs])
    print('Built:',build/'ConkerRecomp.3dsx')
if __name__=='__main__':main()
