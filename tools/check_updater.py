"""Check release parsing and transactional updates without touching a real install."""
import copy,hashlib,http.server,json,os,ssl,struct,subprocess,tempfile,threading,zipfile,warnings
from pathlib import Path
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[1]
build=SimpleNamespace(BUILD=ROOT/".local",TARGET=ROOT/"game/platform/3ds/updater")
build_probe=SimpleNamespace(run=lambda args: subprocess.run(list(map(str,args)),check=True))

def main():
    out=build.BUILD/'updater-host';out.mkdir(exist_ok=True)
    binary=out/'check'
    (out/'update_config.h').write_text('#define NATIVE_UPDATE_REPOSITORY "test-owner/conker-test"\n#define NATIVE_RELEASE_TAG "v0.1.6"\n')
    args=['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-DNATIVE_UPDATE_HOST','-DNATIVE_UPDATE_RENAME=native_test_rename',
          '-include',ROOT/'tests/update_rename.h','-I'+str(build.TARGET),'-I'+str(out),
          ROOT/'tests/update_host.c',build.TARGET/'native_update.c',build.TARGET/'native_update_http.c','-lminizip','-lz','-lcurl','-lcrypto','-o',binary]
    build_probe.run(args)
    def run(*args,ok=True,cancel=False):
        p=subprocess.run([str(binary),*map(str,args)],capture_output=True,text=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0',**({'EOG_TEST_CANCEL':'1'} if cancel else {})},timeout=20)
        assert p.returncode==(0 if ok else 1),(args,p.returncode,p.stdout,p.stderr)
        return p.stdout
    for remote,current,expected in [('v0.9.6-r5','v0.9.6-r4',True),('v0.9.6-r5','v0.9.6-r5',False),('v0.9.6-r4','v0.9.6-r5',False),('v0.9.6-r4','v0.9.6-r3',True),('v0.9.6-r4','v0.9.6-r4',False),('v0.9.6-r3','v0.9.6-r4',False),('v0.9.6-r3','v0.9.6-r2',True),('v0.9.6-r3','v0.9.6-r3',False),('v0.9.6-r2','v0.9.6-r3',False),('v0.9.6-r2','v0.9.6-r1',True),('v0.9.6-r2','v0.9.6-r2',False),('v0.9.6-r1','v0.9.6-r2',False),('v0.9.6','v0.9.5',True),('v0.9.6','v0.9.6',False),('v0.9.4-r2','v0.9.4-r1',True),('v0.9.5','v0.9.4-r9',True),('v0.10.0','v0.9.9',True),('v0.9.4-r1','v0.9.4-r1',False),('v0.9.3','v0.9.4',False),('v0.9.5-beta','v0.9.4',False),('garbage','v0.9.4',False)]:run('compare',remote,current,ok=expected)
    with tempfile.TemporaryDirectory(prefix='eog-updater-') as temporary:
        tmp=Path(temporary);fixture=tmp/'release.json'
        release={'tag_name':'v0.9.5','draft':False,'prerelease':False,'body':'text containing "tag_name" and { nested-looking text }','assets':[{'name':'eyes-of-gold-source.zip','size':100,'browser_download_url':'https://example.com/source.zip'},
            {'name':'conker-3ds-recomp.zip','size':100,'digest':'sha256:'+'a'*64,'browser_download_url':'https://github.com/test-owner/conker-test/releases/download/v0.9.5/conker-3ds-recomp.zip'}]}
        def parse(d,ok):fixture.write_text(json.dumps(d));return run('release',fixture,'v0.9.4-r1',ok=ok)
        assert parse(release,True).splitlines()[0]=='v0.9.5'
        mixed=copy.deepcopy(release)
        cia=copy.deepcopy(mixed['assets'][1]);cia['name']='conker-3ds-recomp-v0.9.5.cia';cia['browser_download_url']='https://github.com/test-owner/conker-test/releases/download/v0.9.5/'+cia['name'];mixed['assets'].insert(0,cia)
        fixture.write_text(json.dumps(mixed));assert run('release-cia',fixture,'v0.9.4').splitlines()[1].endswith('.cia')
        assert run('release',fixture,'v0.9.4').splitlines()[1].endswith('.zip')
        fixture.write_text(json.dumps(release));run('release-cia',fixture,'v0.9.4',ok=False)
        for name in ('source-conker-3ds-recomp.cia','conker-3ds-recomp/other.cia','conker-3ds-recomp%2Fother.cia'):
            bad=copy.deepcopy(mixed);bad['assets']=bad['assets'][:1];bad['assets'][0]['name']=name;bad['assets'][0]['browser_download_url']='https://github.com/test-owner/conker-test/releases/download/v0.9.5/'+name
            fixture.write_text(json.dumps(bad));run('release-cia',fixture,'v0.9.4',ok=False)
        # Minimal synthetic CIA container: expected title, bounded content.
        align=lambda x:(x+63)&~63
        cia_file=tmp/'title.cia';header=0x2020;cert=ticket=tmd=64;at=align(header)+cert+ticket+tmd
        container=bytearray(at+0x200);struct.pack_into('<IHHIIIIQ',container,0,header,0,0,cert,ticket,tmd,0,0x200)
        container[at+0x100:at+0x104]=b'NCCH';struct.pack_into('<Q',container,at+0x118,0x000400000E0CBF00)
        cia_file.write_bytes(container);run('cia',cia_file)
        struct.pack_into('<Q',container,at+0x118,0x000400000E064D00);cia_file.write_bytes(container);run('cia',cia_file,ok=False)
        struct.pack_into('<Q',container,24,2**63);cia_file.write_bytes(container);run('cia',cia_file,ok=False)
        cia_file.write_bytes(b'bad');run('cia',cia_file,ok=False)
        for key in ('draft','prerelease'):
            bad=copy.deepcopy(release);bad[key]=True;parse(bad,False)
        for key,value in [('digest','sha256:bad'),('size',999999999),('browser_download_url','http://github.com/evil.zip'),('browser_download_url','https://github.com/other/repo/releases/download/v0.9.5/conker-3ds-recomp.zip')]:
            bad=copy.deepcopy(release);bad['assets'][1][key]=value;parse(bad,False)
        fixture.write_text('{"tag_name":');run('release',fixture,'v0.9.4',ok=False)
        fixture.write_text('[]');run('release',fixture,'v0.9.4',ok=False)
        fixture.write_text(json.dumps(release)+'{}');run('release',fixture,'v0.9.4',ok=False)
        folder='3ds/ConkerRecomp/';names=['ConkerRecomp.3dsx','ConkerRecomp.smdh'];old=[b'old-launcher',b'old-icon']
        launcher=bytearray(1024);struct.pack_into('<4sHH',launcher,0,b'3DSX',32,8)
        icon=b'SMDH'+bytes(0x36c0-4);archive=tmp/'update.zip'
        def zipwrite(payloads=(launcher,icon),extra=()):
            with warnings.catch_warnings():
                warnings.simplefilter('ignore')
                with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
                    for name,data in zip(names,payloads):z.writestr(folder+name,data)
                    for name,data in extra:z.writestr(name,data)
        def reset():
            d=tmp/'install';d.mkdir(exist_ok=True)
            for p in d.iterdir():p.unlink()
            for name,data in zip(names,old):(d/name).write_bytes(data)
            for name in ['CBFD.z64','settings.ini','save.eep','native-game.dat']:(d/name).write_bytes(b'keep '+name.encode())
            return d
        def unchanged(d):
            for name in ['CBFD.z64','settings.ini','save.eep','native-game.dat']:assert (d/name).read_bytes()==b'keep '+name.encode()
        zipwrite(extra=[('../../outside',b'bad'),(folder+'settings.ini',b'bad')]);d=reset();run('install',archive,d)
        assert (d/names[0]).read_bytes()==launcher and (d/names[1]).read_bytes()==icon;unchanged(d);assert not (tmp/'outside').exists()
        # Conker's native launcher is larger than Eyes of Gold's old 16 MiB cap.
        large=bytearray(24*1024*1024);large[:len(launcher)]=launcher
        zipwrite(payloads=(large,icon));d=reset();run('install',archive,d)
        assert (d/names[0]).stat().st_size==len(large);unchanged(d)
        zipwrite()
        for failure in range(1,5):
            d=reset();run('install',archive,d,failure,ok=False)
            for name,data in zip(names,old):assert (d/name).read_bytes()==data
            unchanged(d);assert not (d/'launcher-update.pending').exists()
        d=reset();run('install',archive,d,0,1,ok=False);assert (d/names[0]).read_bytes()==old[0]
        zipwrite(payloads=(b'not 3DSX'*64,icon));d=reset();run('install',archive,d,ok=False);assert (d/names[0]).read_bytes()==old[0]
        zipwrite(extra=[(folder+names[0],launcher)]);d=reset();run('install',archive,d,ok=False)
        d=reset();(d/'launcher-update.pending').write_bytes(b'CBFU\x03');(d/(names[0]+'.update-old')).write_bytes(old[0]);(d/names[0]).write_bytes(launcher);run('recover',d)
        for name,data in zip(names,old):assert (d/name).read_bytes()==data
        zipwrite();zip_bytes=archive.read_bytes();digest=hashlib.sha256(zip_bytes).hexdigest()
        # Exercise the production curl transport against a local TLS server.
        cert=tmp/'server.pem';key=tmp/'server.key'
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',key,'-out',cert,'-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost'],check=True,capture_output=True)
        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self,*args):pass
            def do_GET(self):
                if self.path=='/downgrade':self.send_response(302);self.send_header('Location','http://localhost/');self.end_headers();return
                data=b'x'*8192 if self.path=='/oversize' else b'{}' if self.path=='/metadata' else zip_bytes
                self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        class Server(http.server.ThreadingHTTPServer):
            def handle_error(self,request,address):
                import sys
                if not isinstance(sys.exception(),(ConnectionResetError,BrokenPipeError)):super().handle_error(request,address)
        server=Server(('127.0.0.1',0),Handler);context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(cert,key);server.socket=context.wrap_socket(server.socket,server_side=True)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();url='https://localhost:'+str(server.server_port)
        try:
            assert '{}' in run('fetch',url+'/metadata',cert,'-',0,'-')
            part=tmp/'download.part';run('fetch',url+'/zip',cert,part,len(zip_bytes),digest);assert part.read_bytes()==zip_bytes
            run('fetch',url+'/zip',cert,part,len(zip_bytes),'0'*64,ok=False);assert not part.exists()
            run('fetch',url+'/zip',cert,part,len(zip_bytes)-1,digest,ok=False);assert not part.exists()
            run('fetch',url+'/downgrade',cert,'-',0,'-',ok=False)
            run('fetch',url+'/metadata',build.TARGET/'cacert.pem','-',0,'-',ok=False)
            run('fetch',url+'/oversize',cert,'-',0,'-',ok=False)
            run('fetch',url.replace('localhost','127.0.0.1')+'/metadata',cert,'-',0,'-',ok=False)
            run('fetch',url+'/zip',cert,part,len(zip_bytes),digest,ok=False,cancel=True);assert not part.exists()
        finally:server.shutdown();server.server_close();thread.join()
    print('PASS updater: revisions, separate CIA/ZIP asset selection, CIA title/container checks, verified TLS, HTTPS-only redirects, SHA-256/size rejection, ZIP validation, cancellation, four replacement failures, recovery and preserved game files')
if __name__=='__main__':main()
