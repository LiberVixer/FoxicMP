"""Build the private UCRT runtime with a bounded detached-thread TSD cleanup fix.
Does not install into MSYS2 or any player directory. Standard library + UCRT GCC.
"""
import argparse,difflib,hashlib,json,os,re,struct,subprocess,tarfile,urllib.request
from pathlib import Path
COMMIT='63ebe6cdb3ec4d5c586b7f0f0c03dd502e44c296'
ARCHIVE_SHA256='94469ea48f974f9a4f7d8b7050cb7eceaa92c1f941ed3b6b8137591ba19491de'
URL='https://codeload.github.com/mingw-w64/mingw-w64/tar.gz/'+COMMIT

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def export_names(path):
 data=Path(path).read_bytes();u16=lambda at:struct.unpack_from('<H',data,at)[0];u32=lambda at:struct.unpack_from('<I',data,at)[0]
 if data[:2]!=b'MZ':raise ValueError('Expected a PE runtime')
 pe=u32(0x3c)
 if data[pe:pe+4]!=b'PE\0\0':raise ValueError('Invalid PE signature')
 sections=u16(pe+6);optional=pe+24;table=optional+u16(pe+20)
 if not 1<=sections<=96 or u16(optional) not in (0x10b,0x20b):raise ValueError('Unsupported PE layout')
 def physical(rva):
  for i in range(sections):
   at=table+i*40;start=u32(at+12);size=u32(at+16)
   if start<=rva<start+size:return u32(at+20)+rva-start
  raise ValueError('Unmapped PE export RVA')
 export=physical(u32(optional+(112 if u16(optional)==0x20b else 96)));count=u32(export+24)
 if not 1<=count<=65536:raise ValueError('Missing or excessive runtime exports')
 names=physical(u32(export+32));result=set()
 for i in range(count):
  at=physical(u32(names+i*4));end=data.find(b'\0',at,at+4096)
  if end<0:raise ValueError('Unterminated export name')
  result.add(data[at:end].decode('ascii'))
 return result

def exports(objdump,path):
 text=subprocess.check_output([str(objdump),'-p',str(path)],text=True,encoding='utf-8',errors='replace')
 return export_names(path),re.findall(r'DLL Name:\s*(\S+)',text)
def build(toolchain,work,archive=None):
 toolchain=Path(toolchain).resolve();work=Path(work).resolve();work.mkdir(parents=True,exist_ok=True);bin=toolchain/'ucrt64/bin';archive=Path(archive).resolve() if archive else work/'upstream.tar.gz'
 if not archive.exists():
  pending=archive.with_name(archive.name+'.download')
  with urllib.request.urlopen(URL,timeout=60) as response,pending.open('wb') as f:
   while chunk:=response.read(1024**2):f.write(chunk)
  if sha(pending)!=ARCHIVE_SHA256:raise ValueError('Downloaded upstream archive SHA-256 mismatch')
  pending.replace(archive)
 if sha(archive)!=ARCHIVE_SHA256:raise ValueError('Pinned winpthreads archive SHA-256 mismatch')
 source=work/'source';source.mkdir(exist_ok=True)
 with tarfile.open(archive,'r:gz') as tar:
  for item in tar:
   marker='/mingw-w64-libraries/winpthreads/'
   if marker not in item.name or not item.isfile():continue
   relative=Path(item.name.split(marker,1)[1])
   if relative.is_absolute() or '..' in relative.parts:raise ValueError('Unsafe upstream member')
   target=source/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(tar.extractfile(item).read())
 thread=source/'src/thread.c';before=thread.read_text();needle='      pthread_mutex_destroy (&tv->p_clock);\n      push_pthread_mem (tv);'
 if before.count(needle)!=1:raise ValueError('Detached cleanup patch no longer matches pinned source')
 fixed=before.replace(needle,'      pthread_mutex_destroy (&tv->p_clock);\n      pthread_spin_destroy (&tv->spin_keys);\n      push_pthread_mem (tv);')
 patch=Path(__file__).with_name('winpthreads-detached-spin.patch')
 if patch.read_text()!=''.join(difflib.unified_diff(before.splitlines(True),fixed.splitlines(True),fromfile='a/src/thread.c',tofile='b/src/thread.c')):raise ValueError('Reviewable patch differs from source transformation')
 thread.write_text(fixed)
 env=dict(os.environ,PATH=str(bin)+os.pathsep+os.environ.get('PATH',''))
 subprocess.run([str(bin/'windres.exe'),'-I',str(source/'src'),'-i',str(source/'src/version.rc'),'-O','coff','-o',str(work/'version.o')],check=True,env=env)
 files=[(source/'src'/(n+'.c')).as_posix() for n in ('barrier','cond','misc','mutex','rwlock','sem','spinlock','sched','time','thread')]
 library=work/'libwinpthread-1.dll';import_library=work/'libwinpthread.dll.a'
 subprocess.run([str(bin/'gcc.exe'),'-shared','-O2','-DDLL_EXPORT','-DIN_WINPTHREAD','-D__USE_MINGW_ANSI_STDIO=0','-no-pthread','-static-libgcc','-ffile-prefix-map='+source.as_posix()+'=winpthreads','-ffile-prefix-map='+work.as_posix()+'=winpthreads-build','-I'+str(source/'include'),'-I'+str(source/'src'),*files,str(work/'version.o'),'-Wl,--no-insert-timestamp','-Wl,--image-base,0x2e3650000','-Wl,--strip-all','-Wl,--out-implib,'+str(import_library),'-o',str(library)],check=True,env=env)
 original=bin/'libwinpthread-1.dll';old,_=exports(bin/'objdump.exe',original);new,imports=exports(bin/'objdump.exe',library)
 if old-new:raise ValueError('Missing stock runtime exports: '+str(sorted(old-new)))
 if any(n.lower() in ('libwinpthread-1.dll','msys-2.0.dll','cygwin1.dll') for n in imports):raise ValueError('Invalid runtime dependency')
 metadata=dict(upstreamCommit=COMMIT,sourceUrl=URL,archiveSha256=ARCHIVE_SHA256,threadBeforeSha256=hashlib.sha256(before.encode()).hexdigest(),threadAfterSha256=sha(thread),patchSha256=sha(Path(__file__).with_name('winpthreads-detached-spin.patch')),stockLibrarySha256=sha(original),librarySha256=sha(library),stockExports=len(old),fixedExports=len(new),allStockExportsPreserved=True,imports=imports,compiler=subprocess.check_output([str(bin/'gcc.exe'),'--version'],env=env,text=True).splitlines()[0],compilerFlags='-shared -O2 -DDLL_EXPORT -DIN_WINPTHREAD -D__USE_MINGW_ANSI_STDIO=0 -no-pthread -static-libgcc -ffile-prefix-map=<source>=winpthreads -ffile-prefix-map=<work>=winpthreads-build --no-insert-timestamp --image-base=0x2e3650000 --strip-all',fix='Destroy detached thread spin_keys before recycling _pthread_v; matched source state and public ABI preserved',license='MIT AND BSD-3-Clause',toolchainModified=False)
 (work/'winpthread-build.json').write_text(json.dumps(metadata,indent=2)+'\n');return metadata
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--toolchain',required=True);p.add_argument('--work',required=True);p.add_argument('--archive');a=p.parse_args();print(json.dumps(build(a.toolchain,a.work,a.archive)))
