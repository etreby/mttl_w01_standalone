"""Portable Windows build engine. No registry writes or global environment edits."""
from pathlib import Path,PurePosixPath
from contextlib import contextmanager
import hashlib,json,os,shutil,subprocess,tarfile,threading,time,urllib.request,uuid,zipfile

class Cancelled(Exception):pass
class Engine:
    def __init__(self,home,lock,log=print,cancel=None):
        self.home=Path(home).resolve();self.lock=lock;self.log=log;self.cancel=cancel or threading.Event()
        self.key=hashlib.sha256(json.dumps(lock,sort_keys=True).encode()).hexdigest()[:12]
        self.environment=self.home/'tools'/('env-'+self.key)
        self.cache=self.home/'downloads'
    def check_cancel(self):
        if self.cancel.is_set():raise Cancelled('Task cancelled.')
    @contextmanager
    def exclusive(self):
        import msvcrt
        self.home.mkdir(parents=True,exist_ok=True)
        with (self.home/'.builder.lock').open('a+b') as f:
            if f.tell()==0:f.write(b'0');f.flush()
            f.seek(0)
            try:msvcrt.locking(f.fileno(),msvcrt.LK_NBLCK,1)
            except OSError:raise RuntimeError('Another builder is working in this folder.')
            try:yield
            finally:f.seek(0);msvcrt.locking(f.fileno(),msvcrt.LK_UNLCK,1)
    @staticmethod
    def digest(path):
        h=hashlib.sha256()
        with Path(path).open('rb') as f:
            while data:=f.read(1024*1024):h.update(data)
        return h.hexdigest()
    def validate_home(self):
        if not str(self.home).isascii():raise RuntimeError('Extract the builder into an ASCII path for the legacy ARM GCC, e.g. C:\\Temp\\MTTL-Builder')
    def download(self,item):
        self.cache.mkdir(parents=True,exist_ok=True);dest=self.cache/item['file']
        if dest.is_file() and self.digest(dest)==item['sha256']:
            self.log('Cache verified: '+item['name']);return dest
        for attempt in range(3):
            self.check_cancel();part=self.cache/(item['file']+'.part')
            try:
                self.log('Downloading: '+item['name']+' — '+item['url'])
                req=urllib.request.Request(item['url'],headers={'User-Agent':'MTTL-Builder/0.1.0'})
                with urllib.request.urlopen(req,timeout=30) as response,part.open('wb') as out:
                    received=0;last=time.monotonic()
                    while data:=response.read(256*1024):
                        self.check_cancel();out.write(data);received+=len(data)
                        if received>item['bytes']:raise RuntimeError('Download exceeds expected size')
                        if time.monotonic()-last>2:self.log(f"  {item['name']}: {received/1048576:.1f} MiB");last=time.monotonic()
                if part.stat().st_size!=item['bytes'] or self.digest(part)!=item['sha256']:raise RuntimeError('Download hash/size mismatch: '+item['name'])
                part.replace(dest);return dest
            except Cancelled:raise
            except Exception:
                if attempt==2:raise
                self.log('Retrying: '+item['name'])
        raise RuntimeError('Download failed')
    @staticmethod
    def safe_name(name):
        name=name.replace('\\','/');p=PurePosixPath(name)
        if p.is_absolute() or any(x in ('..','') for x in p.parts) or ':' in name:raise RuntimeError('Invalid archive path: '+name)
        return p.parts
    def extract(self,archive,dest,kind):
        dest.mkdir(parents=True,exist_ok=True);written=0
        def copy(name,stream,size):
            nonlocal written
            parts=self.safe_name(name)
            if kind=='sdk':
                if len(parts)<4 or parts[1:3]!=('Arduino_package','hardware'):return
                parts=parts[3:]
            elif kind in ('gcc','sdk_release'):parts=parts[1:]
            elif kind=='npm':parts=parts[1:]
            if not parts:return
            written+=size
            if written>750*1024*1024:raise RuntimeError('Archive extraction size limit exceeded')
            target=dest.joinpath(*parts)
            if not target.resolve().is_relative_to(dest.resolve()):raise RuntimeError('Archive path escapes destination')
            self.check_cancel();target.parent.mkdir(parents=True,exist_ok=True)
            with target.open('wb') as out:shutil.copyfileobj(stream,out)
        if zipfile.is_zipfile(archive):
            with zipfile.ZipFile(archive) as z:
                for info in z.infolist():
                    self.safe_name(info.filename)
                    if (info.external_attr>>16)&0o170000==0o120000:raise RuntimeError('Archive links are not supported.')
                    if not info.is_dir():
                        with z.open(info) as stream:copy(info.filename,stream,info.file_size)
        else:
            with tarfile.open(archive) as t:
                for info in t:
                    self.safe_name(info.name)
                    if info.issym() or info.islnk():raise RuntimeError('Archive links are not supported.')
                    if info.isfile():
                        with t.extractfile(info) as stream:copy(info.name,stream,info.size)
    def env(self,root,temporary):
        env=dict(os.environ)
        for key in list(env):
            if key.upper() in {'PYTHONHOME','PYTHONPATH','NODE_PATH','VIRTUAL_ENV','GCC_EXEC_PREFIX','COMPILER_PATH','LIBRARY_PATH','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','_PYI_APPLICATION_HOME_DIR','_PYI_ARCHIVE_FILE','_PYI_PARENT_PROCESS_LEVEL','_PYI_SPLASH_IPC'}:env.pop(key,None)
        windows=Path(env.get('SystemRoot',r'C:\Windows'))
        env.update(PATH=os.pathsep.join(map(str,[root/'node',root/'python',root/'gcc/bin',windows/'System32',windows])),PYTHONHOME=str(root/'python'),PYTHONPATH=str(root/'pydeps'),PYTHONNOUSERSITE='1',PYTHONDONTWRITEBYTECODE='1',TEMP=str(temporary),TMP=str(temporary),PYTHONIOENCODING='utf-8',PYTHONUTF8='1',MTTL_GCC_DIR=str(root/'gcc'),MTTL_TERSER_ROOT=str(root/'js/node_modules/terser'))
        temporary.mkdir(parents=True,exist_ok=True);return env
    def run(self,args,cwd,env):
        self.check_cancel();self.log('$ '+' '.join(map(str,args)))
        flags=subprocess.CREATE_NO_WINDOW|subprocess.CREATE_NEW_PROCESS_GROUP
        process=subprocess.Popen(list(map(str,args)),cwd=cwd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,creationflags=flags)
        lines=[]
        def reader():
            for line in iter(process.stdout.readline,b''):
                value=line.decode('utf-8',errors='replace').rstrip();lines.append(value);self.log(value)
        thread=threading.Thread(target=reader,daemon=True);thread.start()
        while process.poll() is None:
            if self.cancel.wait(.15):
                subprocess.run([str(Path(env['SystemRoot'])/'System32/taskkill.exe'),'/PID',str(process.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
                process.wait();thread.join(timeout=5);raise Cancelled('Cancelled · No output FWR was published.')
        thread.join()
        if process.returncode:raise RuntimeError('Task failed (exit '+str(process.returncode)+'). Check the log.')
        return '\n'.join(lines)
    def ready(self):
        try:
            marker=json.loads((self.environment/'ready.json').read_text())
            return marker['key']==self.key and all((self.environment/p).is_file() for p in ('python/python.exe','node/node.exe','gcc/bin/arm-none-eabi-gcc.exe','sdk/platform.txt','pydeps/unicorn/__init__.py','pydeps/zopfli/gzip.py','js/node_modules/terser/package.json'))
        except (OSError,ValueError,KeyError):return False
    def prepare(self):
        self.validate_home()
        with self.exclusive():
            if self.ready():self.log('Tools are already prepared.');self.probe(self.environment);return
            stage=self.home/'tools'/('.prepare-'+uuid.uuid4().hex[:12]);stage.mkdir(parents=True)
            try:
                for item in self.lock['artifacts']:
                    self.check_cancel();archive=self.download(item);kind=item['kind'];self.log('Preparing: '+item['name'])
                    if kind=='node':(stage/'node').mkdir(exist_ok=True);shutil.copy2(archive,stage/'node/node.exe')
                    elif kind=='sdkfile':
                        target=(stage/'sdk').joinpath(*self.safe_name(item['relative']));target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(archive,target)
                    elif kind=='npm':self.extract(archive,stage/'js/node_modules'/item['package'],kind)
                    else:self.extract(archive,stage/('pydeps' if kind=='wheel' else 'sdk' if kind=='sdk_release' else kind),kind)
                (stage/'python/python312._pth').write_text('python312.zip\n.\n../pydeps\nimport site\n',encoding='ascii')
                self.probe(stage)
                # Replace only builder-owned, uncommitted staging/version folders.
                if self.environment.exists():raise RuntimeError('Existing tools are incomplete. Move the tools folder elsewhere and retry.')
                (stage/'ready.json').write_text(json.dumps({'key':self.key,'schema':1}),encoding='utf-8')
                stage.replace(self.environment);self.log('Environment ready')
            except Exception:
                self.log('Environment preparation incomplete: '+str(stage)+' (not used as a ready environment)');raise
    def probe(self,root):
        env=self.env(root,self.home/'temp');python=root/'python/python.exe'
        self.run([python,'-c','import sys,unicorn,zopfli.gzip; print(sys.version); print(unicorn.__file__); print(zopfli.gzip.__file__)'],self.home,env)
        self.run([root/'node/node.exe','-e','console.log(process.version); console.log(require(process.argv[1]).minify?"Terser OK":"FAIL")',root/'js/node_modules/terser'],self.home,env)
        self.run([root/'gcc/bin/arm-none-eabi-gcc.exe','--version'],self.home,env)
    def build(self,source):
        self.validate_home();source=Path(source).resolve()
        if not (source/'mttl-source.json').is_file():raise RuntimeError('Extract the source ZIP and select the folder containing mttl-source.json.')
        if not self.ready():raise RuntimeError('Prepare the environment first.')
        if source==self.home or self.home.is_relative_to(source):raise RuntimeError('Select a separate source folder, not the entire builder folder.')
        if any(p.is_symlink() or (hasattr(p,'is_junction') and p.is_junction()) for p in source.rglob('*')):raise RuntimeError('Source folder symlinks/junctions are not supported.')
        with self.exclusive():
            build_id=time.strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6]
            work=self.home/'build'/build_id;self.log('Build folder: '+str(work))
            shutil.copytree(source,work,ignore=shutil.ignore_patterns('__pycache__','results','.git','tools','downloads','output','build'))
            self.check_cancel();sdk=work/'work/reference/ameba-arduino-1/Arduino_package/hardware';sdk.parent.mkdir(parents=True,exist_ok=True)
            shutil.copytree(self.environment/'sdk',sdk)
            env=self.env(self.environment,work/'temp')
            self.run([self.environment/'python/python.exe',work/'build.py'],work,env)
            self.check_cancel();out=work/'standalone/results/network-ota-test';manifest=json.loads((out/'manifest.json').read_text())
            fwr=next(x for x in manifest['files'] if x['path'].endswith('.fwr'));path=Path(fwr['path']).resolve()
            if not path.is_relative_to(work) or not path.is_file() or self.digest(path)!=fwr['sha256']:raise RuntimeError('FWR result validation failed')
            if manifest['static_gap_to_msp_bytes']<=0 or not manifest['bin_fwr_roundtrip']:raise RuntimeError('Memory/package validation failed')
            target=self.home/'output'/build_id;target.mkdir(parents=True)
            final=target/('comMTTL-W01_'+manifest['version']+'-build-'+manifest['build_id']+'.fwr')
            shutil.copy2(path,final);shutil.copy2(out/'manifest.json',target/'manifest.json')
            self.log('Build succeeded: '+str(final));self.log('SHA-256: '+self.digest(final));return final
