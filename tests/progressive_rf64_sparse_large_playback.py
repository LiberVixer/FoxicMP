"""Physical sparse RF64 >4GiB: authenticated island I/O, gap fill and full EOF."""
import argparse,copy,ctypes,errno,hashlib,json,math,os,shutil,struct,subprocess,time
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_client import ProgressiveClient,ApiError
from progressive_v2 import V2PreviewSession
from progressive_integrity import protect_snapshot,externalize_integrity
from progressive_rf64_fixture import fixture
from progressive_v2_playback_conformance import forget_closed_readers

def windows_ioctl(stream,code,value=b'',capacity=0):
 import msvcrt
 dll=ctypes.WinDLL('kernel32',use_last_error=True);fn=dll.DeviceIoControl
 fn.argtypes=[ctypes.c_void_p,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.POINTER(ctypes.c_ulong),ctypes.c_void_p];fn.restype=ctypes.c_int
 inp=ctypes.create_string_buffer(value) if value else None;out=ctypes.create_string_buffer(capacity) if capacity else None;n=ctypes.c_ulong()
 if not fn(msvcrt.get_osfhandle(stream.fileno()),code,inp,len(value),out,capacity,ctypes.byref(n),None):raise ctypes.WinError(ctypes.get_last_error())
 return out.raw[:n.value] if out is not None else b''

def create_sparse(path,total):
 with path.open('x+b') as f:
  if os.name=='nt':
   # CRT truncate can zero-fill and allocate the whole sparse file on Windows.
   # Extend logical EOF directly without writing any of the missing media.
   import msvcrt
   windows_ioctl(f,0x900c4) # FSCTL_SET_SPARSE, test file only.
   dll=ctypes.WinDLL('kernel32',use_last_error=True);handle=msvcrt.get_osfhandle(f.fileno())
   dll.SetFilePointerEx.argtypes=[ctypes.c_void_p,ctypes.c_longlong,ctypes.c_void_p,ctypes.c_ulong];dll.SetFilePointerEx.restype=ctypes.c_int
   dll.SetEndOfFile.argtypes=[ctypes.c_void_p];dll.SetEndOfFile.restype=ctypes.c_int
   if not dll.SetFilePointerEx(handle,total,None,0) or not dll.SetEndOfFile(handle):raise ctypes.WinError(ctypes.get_last_error())
  else:f.truncate(total)
  f.flush()
 assert path.stat().st_size==total
 if os.name=='nt':assert path.stat().st_file_attributes&0x200

def unallocated(path,at,length):
 with path.open('r+b') as f:
  if os.name=='nt':return not windows_ioctl(f,0x940cf,struct.pack('<qq',at,length),65536)
  try:return os.lseek(f.fileno(),at,os.SEEK_DATA)>=at+length
  except OSError as e:
   if e.errno==errno.ENXIO:return True
   raise

def merged(ranges):
 result=[]
 for a,z in sorted(ranges):
  if result and a<=result[-1][1]:result[-1]=(result[-1][0],max(z,result[-1][1]))
  else:result.append((a,z))
 return result

def exercise(client,work,control,bits):
 root=Path(work).resolve();root.mkdir(parents=True,exist_ok=True);assert shutil.disk_usage(root).free>12*1024**3
 model,seed,_,probe=fixture(root/'seed','audio-rf64-'+str(bits)+'-stereo-48000',1,writer='manual');offset=probe['_data_offset'];rate=48000;align=bits//8*2
 frames=((1<<32)+16*1024**2+align-1)//align;data_bytes=frames*align;total=offset+data_bytes;duration=frames*10000000//rate
 far=math.ceil(((1<<32)+1024**2-offset)/align/rate)+2;tail_ticks=duration-30000000;tail=tail_ticks/10000000;at=lambda seconds:offset+round(seconds*rate)*align
 header=bytearray(seed[:offset]);struct.pack_into('<QQQ',header,20,total-8,data_bytes,frames)
 model['tracks'][0]['containerHint']='rf64-sparse-pcm'+str(bits);model['timeline']['duration']=str(duration);model['resources'][0].update(name='large-sparse-rf64.part',length=str(total),ranges=[],state='downloading');model['metadata']=dict(revision='1',title='Physical sparse RF64 '+str(bits),chapters=[dict(id='c'+str(i),time=str(t),title=str(i)) for i,t in enumerate((0,40000000,130000000,far*10000000,tail_ticks))])
 family=client.request(dict(version=1,action='capabilities'))['schema2Playback'];assert family['standalone']['rf64Pcm'+str(bits)]['sparsePlayback']['islandSeek'];assert model['tracks'][0]['containerHint'] in family['integrity']['externalIndex']['playbackContainers']
 period=b''.join((((i*(137+c*74)*257+c*93)%(1<<bits))-(1<<(bits-1)) & ((1<<bits)-1)).to_bytes(bits//8,'little') for i in range(4093) for c in range(2));cache=period*((1024**2+len(period)*2)//len(period)+1)
 def pcm(start,n):
  first=start%len(period);return cache[first:first+n]
 path=root/'large-sparse-rf64.part';create_sparse(path,total);written=0;ranges=[];previous=None;revision=0;phases=[];max_request=0;max_read=0;started=time.monotonic()
 def write(a,z):
  nonlocal written
  assert 0<=a<z<=total
  assert all(z<=left or a>=right for left,right in ranges),'Never rewrite confirmed bytes'
  with path.open('r+b') as f:
   f.seek(a)
   while a<z:
    n=min(1024**2,z-a)
    if a<offset:data=bytes(header[a:min(offset,a+n)])
    else:data=pcm(a-offset,n)
    assert data;f.write(data);written+=len(data);a+=len(data)
   f.flush()
 original_request=client.request
 def request(message,timeout=5):
  nonlocal max_request
  size=len(json.dumps(message,ensure_ascii=False,allow_nan=False,separators=(',',':')).encode())+1;assert size<=65536;max_request=max(max_request,size);return original_request(message,timeout=timeout)
 client.request=request;preview=V2PreviewSession(client,str(root),title='Sparse RF64 >4GiB');reader=None
 def publish(complete=False):
  nonlocal previous,revision
  revision+=1;v=copy.deepcopy(model);v['revision']=str(revision);v['resources'][0]['ranges']=[dict(offset=str(a),length=str(z-a)) for a,z in ranges]
  if complete:v['writer']['state']=v['tracks'][0]['state']=v['resources'][0]['state']='complete'
  previous=protect_snapshot(v,root,previous=previous,block_bytes=4*1024**2);return preview.publish(externalize_integrity(previous,root))
 def wait(predicate,label,timeout=90,record=True):
  nonlocal max_read
  end=time.monotonic()+timeout;s={}
  while time.monotonic()<end:
   s=preview.heartbeat();assert s.get('ok') and not s.get('error'),s;assert s['readerId']==reader;max_read=max(max_read,int(s.get('physicalReadMaxEndBytes','0')))
   if predicate(s):
    if record:phases.append(dict(phase=label,state=s['state'],positionSeconds=s.get('positionSeconds'),availableSeconds=s.get('availableSeconds'),physicalReadMaxEndBytes=s.get('physicalReadMaxEndBytes'),decoderIoBytes=s.get('decoderIoBytes'),complete=s.get('complete')))
    return s
   time.sleep(.04)
  raise AssertionError((bits,label,s))
 try:
  for a,z in ((0,at(4)),(at(far-2),at(far+14)),(at((duration-100000000)/10000000),total)):
   write(a,z);ranges=merged(ranges+[(a,z)])
  holes=[(32*1024**2,65536),((1<<32)-8*1024**2,65536)];assert all(unallocated(path,a,n) for a,n in holes)
  sparse_before=dict(fileBytes=total,writtenBytes=written,holes=[dict(offset=a,length=n,unallocated=True) for a,n in holes],filesystemProbe='FSCTL_QUERY_ALLOCATED_RANGES' if os.name=='nt' else 'SEEK_DATA',allocatedBytes=path.stat().st_blocks*512 if hasattr(path.stat(),'st_blocks') else None)
  publish();reader=preview.reader_id;assert reader;wait(lambda s:s['state']=='buffering','head-buffers')
  write(at(4),at(12));ranges=merged(ranges+[(at(4),at(12))]);publish();wait(lambda s:s['state']=='playing' and s['positionSeconds']>=2,'head-plays');control('pause',None);wait(lambda s:s['state']=='paused','manual-pause')
  control('reject-seek',13);time.sleep(.3);s=wait(lambda s:s['state']=='paused','hole-seek-rejected');assert s['positionSeconds']<12 and int(s['physicalReadMaxEndBytes'])<=at(12)
  control('seek',far);wait(lambda s:s['state']=='paused' and abs(s['positionSeconds']-far)<1,'far-island-paused');control('pause',None)
  s=wait(lambda s:s['state']=='playing' and s['positionSeconds']>=far+2 and int(s['physicalReadMaxEndBytes'])>2**32,'physical-island-after-4GiB');assert int(s['decoderIoBytes']['audio'])<64*1024**2
  assert all(unallocated(path,a,n) for a,n in holes)
  with path.open('rb') as f:
   for a,n in holes:f.seek(a);assert f.read(n)==b'\0'*n
  # A complete marker with these gaps must be rejected without retiring the
  # active source or changing its accepted revision.
  invalid=copy.deepcopy(previous);invalid['revision']=str(revision+1);invalid['writer']['state']=invalid['tracks'][0]['state']=invalid['resources'][0]['state']='complete'
  try:preview.publish(externalize_integrity(invalid,root));raise AssertionError('False complete accepted')
  except (ApiError,ValueError,RuntimeError) as e:assert not isinstance(e,AssertionError)
  s=wait(lambda s:not s['complete'] and str(s['revision'])==str(revision),'gapped-complete-rejected');assert not s['released']
  decoded=subprocess.check_output(['ffmpeg','-v','error','-ss',str(far),'-i',str(path),'-t','0.5','-c:a','pcm_s'+str(bits)+'le','-f','s'+str(bits)+'le','-'],timeout=90);assert decoded==pcm(far*rate*align,rate//2*align);far_sha=hashlib.sha256(decoded).hexdigest()
  wait(lambda s:s['state']=='buffering' and far+12<=s['positionSeconds']<far+14,'island-end-waits')
  write(at(far+14),at(far+22));ranges=merged(ranges+[(at(far+14),at(far+22))]);publish();wait(lambda s:s['state']=='playing' and s['positionSeconds']>=far+16,'extended-island-resumes');control('pause',None);wait(lambda s:s['state']=='paused','pause-for-backwards');control('seek',4);wait(lambda s:s['state']=='paused' and 3.7<=s['positionSeconds']<5,'backwards-over-4GiB-gap')
  # Fill only missing ranges, each update <=16MiB new bytes. Old island
  # digests and offsets stay immutable while the player's manual pause holds.
  missing=[];cursor=0
  for a,z in ranges:
   if cursor<a:missing.append((cursor,a))
   cursor=z
  if cursor<total:missing.append((cursor,total))
  for a,z in missing:
   while a<z:
    end=min(z,a+16*1024**2);write(a,end);ranges=merged(ranges+[(a,end)]);publish();wait(lambda s:str(s['revision'])==str(revision),'fill-revision',record=False);a=end
  assert ranges==[(0,total)] and written==total
  with path.open('r+b') as f:f.flush();os.fsync(f.fileno())
  s=wait(lambda s:s['state']=='paused','full-fill-preserves-pause');assert s['positionSeconds']<6 and not s['complete']
  expected=hashlib.sha256(header)
  for a in range(0,data_bytes,1024**2):expected.update(pcm(a,min(1024**2,data_bytes-a)))
  with path.open('rb') as f:actual=hashlib.file_digest(f,'sha256').hexdigest()
  assert actual==expected.hexdigest()
  publish(True);wait(lambda s:s['complete'],'writer-complete');control('seek',tail);wait(lambda s:s['state']=='paused' and abs(s['positionSeconds']-tail)<1,'complete-tail-paused');control('pause',None);wait(lambda s:s['state']=='finished' and s.get('released'),'natural-eof-release');assert preview.close(timeout=30)['released'];forget_closed_readers(client,previous)
  result=dict(result='PASS',bits=bits,fileBytes=total,allBytesWritten=written,sha256=actual,initialSparseFile=sparse_before,unallocatedGapsPreservedWhileIslandPlayed=True,farSeconds=far,farPhysicalByte=at(far),physicalReadMaxEndBytes=max_read,farPcmSha256=far_sha,gappedCompleteRejected=True,readerIdentityPreserved=True,publications=revision,integrityRecords=len(previous['resources'][0]['integrity']['blocks']),maxApiRequestBytes=max_request,phases=phases,secondsElapsed=round(time.monotonic()-started,3))
  path.unlink();result['largeFileRemovedAfterRelease']=True;return result
 finally:
  client.request=original_request
  if preview.started and not preview.closed:preview.close(timeout=30)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile',required=True);p.add_argument('--work',required=True);p.add_argument('--bits',type=int,choices=(16,24),required=True);a=p.parse_args();seq=[0]
 def control(action,value):
  seq[0]+=1;root=Path(a.work);path=root/'control.json';tmp=root/'control.tmp';tmp.write_text(json.dumps(dict(sequence=seq[0],action=action,value=value)),encoding='utf-8');end=time.monotonic()+2
  while True:
   try:tmp.replace(path);break
   except PermissionError:
    if time.monotonic()>=end:raise
    time.sleep(.01)
 print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.work,control,a.bits)))
