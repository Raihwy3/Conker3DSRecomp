#!/usr/bin/env python3
"""Package only the launcher pair, install instructions, licences and matching source."""
import hashlib,json,zipfile
from pathlib import Path
from audit_source import ROOT,audit
files=audit();release=json.loads((ROOT/'release.json').read_text());tag=release['tag'];out=ROOT/'.local/releases';out.mkdir(parents=True,exist_ok=True)
build=ROOT/'.local/workspace/build-3ds'
for name,magic in [('ConkerRecomp.3dsx',b'3DSX'),('ConkerRecomp.smdh',b'SMDH')]:
    p=build/name
    if not p.is_file() or p.read_bytes()[:4]!=magic:raise SystemExit('Build first: '+str(p))
# Do not silently label an older build with new release metadata.
config=build/'update_config.h'
if not config.exists() or ('"'+tag+'"') not in config.read_text() or ('"'+release['repository']+'"') not in config.read_text():raise SystemExit('Release metadata changed; rebuild before packaging.')
# The binary must have been built from these source files, not an older snapshot.
for p in files:
    rel=p.relative_to(ROOT)
    if rel.parts[0] in {'game','renderer'}:
        built=ROOT/'.local/workspace'/rel
        if not built.exists() or p.read_bytes()!=built.read_bytes():raise SystemExit('Source changed; rebuild before packaging: '+str(rel))
source=out/('source-conker-3ds-recomp-'+tag+'.zip')
with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for p in files:z.write(p,'conker-3ds-recomp/'+p.relative_to(ROOT).as_posix())
install=out/('conker-3ds-recomp-'+tag+'.zip')
with zipfile.ZipFile(install,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for name in ['ConkerRecomp.3dsx','ConkerRecomp.smdh']:z.write(build/name,'3ds/ConkerRecomp/'+name)
    for p in files:
        rel=p.relative_to(ROOT).as_posix()
        if rel.startswith('licenses/') or rel in {'LICENSE','NOTICE.md','CREDITS.md','README.md','docs/UPSTREAM.json'}:z.write(p,rel)
if install.stat().st_size>32*1024*1024:raise SystemExit('Install ZIP exceeds updater limit')
for p in [install,source]:print(p,hashlib.sha256(p.read_bytes()).hexdigest())
