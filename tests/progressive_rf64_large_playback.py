"""Actual >4 GiB RF64 through the public API, authenticated indexes and real decoder."""
import argparse,copy,hashlib,json,math,os,shutil,struct,subprocess,time
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_client import ProgressiveClient
from progressive_v2 import V2PreviewSession
from progressive_integrity import protect_snapshot,externalize_integrity
from progressive_rf64_fixture import fixture
from progressive_v2_playback_conformance import forget_closed_readers

def exercise(client,work,control,bits):
 root=Path(work).resolve();root.mkdir(parents=True,exist_ok=True);assert shutil.disk_usage(root).free>12*1024**3
 profile='audio-rf64-'+str(bits)+'-stereo-48000';model,seed,_,probe=fixture(root/'seed',profile,1,writer='manual');offset=probe['_data_offset'];align=bits//8*2;rate=48000
 frames=((1<<32)+8*1024**2+align-1)//align;data_bytes=frames*align;total=offset+data_bytes;duration=frames*10000000//rate
 far=math.ceil(((1<<32)+1024**2-offset)/align/rate);tail=(duration-30000000)/10000000
 header=bytearray(seed[:offset]);struct.pack_into('<QQQ',header,20,total-8,data_bytes,frames)
 model['timeline']['duration']=str(duration);model['resources'][0].update(name='large-rf64.part',length=str(total),ranges=[],state='downloading');model['metadata']=dict(revision='1',title='Large RF64 '+str(bits),chapters=[dict(id='c'+str(i),time=str(t),title=str(i)) for i,t in enumerate((0,20000000,far*10000000,duration-30000000))])
 period=b''.join((((i*(137+c*74)*257+c*93)%(1<<bits))-(1<<(bits-1)) & ((1<<bits)-1)).to_bytes(bits//8,'little') for i in range(4093) for c in range(2))
 cache=period*((1024**2+len(period)*2)//len(period)+1)
 def pcm(at,n):
  start=at%len(period);return cache[start:start+n]
 path=root/'large-rf64.part';digest=hashlib.sha256();written=0;inline=None;max_request=0;max_read=0;published=0;phases=[];started=time.monotonic()
 with path.open('xb') as out:out.write(header);digest.update(header);written=len(header)
 def write_to(end):
  nonlocal written
  with path.open('ab') as out:
   assert out.tell()==written
   while written<end:
    data=pcm(written-offset,min(1024**2,end-written));assert data;out.write(data);digest.update(data);written+=len(data)
   out.flush()
 original=client.request
 def request(message,timeout=5):
  nonlocal max_request
  n=len(json.dumps(message,ensure_ascii=False,allow_nan=False,separators=(',',':')).encode())+1;assert n<=65536;max_request=max(max_request,n);return original(message,timeout=timeout)
 client.request=request;preview=V2PreviewSession(client,str(root),title='RF64 >4 GiB '+str(bits));reader=None
 def wait(predicate,label,timeout=60):
  nonlocal max_read
  until=time.monotonic()+timeout;status={}
  while time.monotonic()<until:
   status=preview.heartbeat();assert status.get('ok') and not status.get('error'),status
   assert status['readerId']==reader;max_read=max(max_read,int(status.get('physicalReadMaxEndBytes','0')))
   if predicate(status):return status
   time.sleep(.04)
  raise AssertionError((label,status))
 def publish(end,complete=False):
  nonlocal inline,published
  write_to(end);value=copy.deepcopy(model);value['revision']=str(published+1);r=value['resources'][0];r['ranges']=[dict(offset='0',length=str(end))]
  if complete:value['writer']['state']=r['state']=value['tracks'][0]['state']='complete'
  inline=protect_snapshot(value,root,previous=inline,block_bytes=4*1024**2);wire=externalize_integrity(inline,root);answer=preview.publish(wire);published+=1
  return answer
 try:
  answer=publish(offset+4*rate*align);reader=preview.reader_id;assert reader,answer;wait(lambda s:s['state']=='buffering','initial-buffer');phases.append('initial-buffer')
  publish(offset+12*rate*align);wait(lambda s:s['state']=='playing' and s['positionSeconds']>=2,'initial-play');control('pause',None);wait(lambda s:s['state']=='paused','pause');phases.append('playing-before-complete')
  while written<total:
   publish(min(total,written+16*1024**2));revision=inline['revision'];wait(lambda s:str(s['revision'])==revision,'publication')
  with path.open('ab') as f:os.fsync(f.fileno())
  before=wait(lambda s:s['state']=='paused','manual-pause-preserved');assert before['positionSeconds']<15;assert path.stat().st_size==total>2**32;phases.append('physical-large-file-published-still-downloading')
  control('seek',far);wait(lambda s:s['state']=='paused' and abs(s['positionSeconds']-far)<1,'far-paused-seek');control('pause',None)
  far_status=wait(lambda s:s['state']=='playing' and s['positionSeconds']>=far+2 and int(s.get('physicalReadMaxEndBytes','0'))>2**32,'decoded-after-4GiB');phases.append('playing-after-physical-4GiB')
  publish(total,complete=True);revision=inline['revision'];wait(lambda s:str(s['revision'])==revision,'complete');control('seek',tail)
  wait(lambda s:s['state']=='finished' and s.get('released'),'natural-eof',90);assert preview.close(timeout=30)['released'];forget_closed_readers(client,inline);phases.append('complete-eof-and-release')
  # Independent decoder seeks the real RF64 at the same large byte position.
  decoded=subprocess.check_output(['ffmpeg','-v','error','-ss',str(far),'-i',str(path),'-t','0.5','-c:a','pcm_s'+str(bits)+'le','-f','s'+str(bits)+'le','-'],timeout=90)
  assert decoded==pcm(far*rate*align,rate//2*align);window_sha=hashlib.sha256(decoded).hexdigest()
  with path.open('rb') as f:actual=hashlib.file_digest(f,'sha256').hexdigest()
  assert actual==digest.hexdigest();phases.append('independent-far-pcm-and-full-file-hash')
  result=dict(result='PASS',bits=bits,profile=profile,fileBytes=total,allBytesWritten=written,physicalSparsePlaceholder=False,sha256=actual,farSeconds=far,farPhysicalByte=offset+far*rate*align,physicalReadMaxEndBytes=max_read,farPcmSha256=window_sha,integrityRecords=len(inline['resources'][0]['integrity']['blocks']),publications=published,maxApiRequestBytes=max_request,readerIdentityPreserved=True,profileIsolated=True,phases=phases,secondsElapsed=round(time.monotonic()-started,3))
  path.unlink();result['largeFileRemovedAfterRelease']=True;return result
 finally:
  client.request=original
  if preview.started and not preview.closed:preview.close(timeout=30)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile',required=True);p.add_argument('--work',required=True);p.add_argument('--bits',type=int,choices=(16,24),required=True);a=p.parse_args();sequence=[0]
 def control(action,value):
  sequence[0]+=1;root=Path(a.work);path=root/'control.json';tmp=root/'control.tmp';tmp.write_text(json.dumps(dict(sequence=sequence[0],action=action,value=value)),encoding='utf-8');deadline=time.monotonic()+2
  while True:
   try:tmp.replace(path);break
   except PermissionError:
    if time.monotonic()>=deadline:raise
    time.sleep(.01)
 print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.work,control,a.bits)))
