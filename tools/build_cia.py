#!/usr/bin/env python3
"""Build a standalone CIA sharing the 3DSX version's SD data directory."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,struct,subprocess,wave
from audit_source import ROOT,audit
TITLE_ID=0x000400000E0CBF00

def run(args):subprocess.run(list(map(str,args)),check=True)
def tool(name,override):
    value=override or os.environ.get(name.upper()) or shutil.which(name)
    if not value:raise SystemExit('Install '+name+' or pass --'+name+' /path/to/'+name)
    return value

def validate(cia,elf,smdh,banner):
    from elftools.elf.elffile import ELFFile
    raw=cia.read_bytes();header,kind,version,cert,ticket,tmd,meta,content=struct.unpack_from('<IHHIIIIQ',raw)
    align=lambda x:(x+63)&~63
    at=align(header)+align(cert)+align(ticket)+align(tmd)
    ncch=raw[at:at+content];assert ncch[0x100:0x104]==b'NCCH'
    assert struct.unpack_from('<Q',ncch,0x118)[0]==TITLE_ID
    assert ncch[0x18f]&4
    assert ncch[0x40f]==0x30, 'Invalid native main-thread priority'
    assert struct.unpack_from('<II',ncch,0x1b0)==(0,0), 'Game assets must not be bundled in RomFS'
    start,blocks=struct.unpack_from('<II',ncch,0x1a0);exefs=ncch[start*512:(start+blocks)*512];entries={}
    for i in range(10):
        name,offset,size=struct.unpack_from('<8sII',exefs,i*16);name=name.rstrip(b'\0').decode()
        if name:
            data=exefs[512+offset:512+offset+size];assert len(data)==size
            assert hashlib.sha256(data).digest()==exefs[0xc0+(9-i)*32:0xc0+(10-i)*32];entries[name]=data
    assert set(entries)=={'.code','icon','banner','logo'}
    assert entries['icon']==smdh.read_bytes() and entries['banner']==banner.read_bytes()
    assert hashlib.sha256(entries['logo']).hexdigest()=='62a5a1f9091aefb46b52e31fbeca2fdba9a99fe2473237e21e35b8d2e5659dff'
    with elf.open('rb') as f:
        e=ELFFile(f);loads=[s for s in e.iter_segments() if s['p_type']=='PT_LOAD'];base=min(s['p_vaddr'] for s in loads)
        for s in loads:
            offset=s['p_vaddr']-base;assert entries['.code'][offset:offset+s['p_filesz']]==s.data()
    assert b'sdmc:/3ds/ConkerRecomp' in entries['.code']
    return {'title_id':f'{TITLE_ID:016X}','sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'payload_matches_elf':True,'romfs_bundled':False,'console_tested':False}

def main():
    p=argparse.ArgumentParser();p.add_argument('--makerom');p.add_argument('--bannertool');a=p.parse_args()
    makerom=tool('makerom',a.makerom);bannertool=tool('bannertool',a.bannertool)
    audit();build=ROOT/'.local/workspace/build-3ds';elf=build/'ConkerRecomp';smdh=build/'ConkerRecomp.smdh'
    if not elf.exists() or not smdh.exists():raise SystemExit('Build with tools/build.py first.')
    for source in audit():
        rel=source.relative_to(ROOT)
        if rel.parts[0] in {'game','renderer'}:
            staged=ROOT/'.local/workspace'/rel
            if not staged.exists() or staged.read_bytes()!=source.read_bytes():raise SystemExit('Rebuild changed source before packaging: '+str(rel))
    release=json.loads((ROOT/'release.json').read_text());tag=release['tag']
    assert ('"'+tag+'"') in (build/'update_config.h').read_text()
    version=tag[1:].split('-')[0];major,minor,micro=map(int,version.split('.'))
    out=ROOT/'.local/cia';out.mkdir(parents=True,exist_ok=True)
    from PIL import Image,ImageDraw,ImageFont
    image=Image.new('RGB',(256,128),(30,22,12));draw=ImageDraw.Draw(image)
    draw.rounded_rectangle((4,4,251,123),radius=10,outline=(185,139,58),width=2)
    icon=Image.open(ROOT/'game/platform/3ds/icon.png').convert('RGBA');icon=icon.resize((64,64),Image.Resampling.NEAREST);image.paste(icon,(96,11),icon)
    font=ImageFont.truetype('DejaVuSans.ttf',16)
    draw.text((128,82),'CONKER: 3DS RECOMP',font=font,fill=(244,214,151),anchor='mt');image.save(out/'banner.png')
    with wave.open(str(out/'silence.wav'),'wb') as f:f.setparams((1,2,32000,0,'NONE','not compressed'));f.writeframes(bytes(6400))
    banner=out/'banner.bnr';run([bannertool,'makebanner','-i',out/'banner.png','-a',out/'silence.wav','-o',banner])
    dest=ROOT/'.local/releases'/('conker-3ds-recomp-'+tag+'.cia');dest.parent.mkdir(exist_ok=True)
    run([makerom,'-f','cia','-o',dest,'-elf',elf,'-rsf',ROOT/'game/platform/3ds/cia/launcher.rsf','-icon',smdh,'-banner',banner,'-exefslogo','-target','t','-major',major,'-minor',minor,'-micro',micro])
    report=validate(dest,elf,smdh,banner);report['version']=version
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));print(dest)
if __name__=='__main__':main()
