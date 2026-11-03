"""Real decoder at effective RF64 upper bounds; no full-file completion claim."""
import argparse,copy,hashlib,json,subprocess,time
from pathlib import Path
from progressive_rf64_limits_fixture import fixture,geometry,pcm
from progressive_rf64_sparse_large_playback import unallocated
from progressive_client import ProgressiveClient,ApiError
from progressive_v2 import V2PreviewSession
from progressive_integrity import protect_snapshot
from progressive_v2_playback_conformance import forget_closed_readers

def exercise(client,work,control,bits):
 root=Path(work).resolve();g=geometry(bits,2,48000);model,path,write,head,tail,far=fixture(root,g);preview=V2PreviewSession(client,str(root));reader=None;phases=[]
 def wait(predicate,label,timeout=90):
  deadline=time.monotonic()+timeout;s={}
  while time.monotonic()<deadline:
   s=preview.heartbeat();assert s.get('ok') and not s.get('error'),s;assert s['readerId']==reader
   if predicate(s):phases.append(dict(phase=label,state=s['state'],positionSeconds=s.get('positionSeconds'),physicalReadMaxEndBytes=s.get('physicalReadMaxEndBytes'),decoderIoBytes=s.get('decoderIoBytes')));return s
   time.sleep(.04)
  raise AssertionError((label,s))
 try:
  preview.publish(model);reader=preview.reader_id;wait(lambda s:s['state']=='buffering','head-buffers')
  write(head,g['offset']+12*g['rate']*g['align']);v=copy.deepcopy(model);v['revision']='2';v['resources'][0]['ranges'][0]['length']=str(g['offset']+12*g['rate']*g['align']);model=protect_snapshot(v,root,previous=model);preview.publish(model)
  wait(lambda s:s['state']=='playing' and s['positionSeconds']>=2,'head-plays');control('pause',None);wait(lambda s:s['state']=='paused','manual-pause')
  control('seek',far);wait(lambda s:s['state']=='paused' and abs(s['positionSeconds']-far)<1,'maximum-tail-paused');control('pause',None)
  s=wait(lambda s:s['state']=='playing' and s['positionSeconds']>=far+2 and int(s['physicalReadMaxEndBytes'])>g['fileBytes']-8*1024**2,'maximum-tail-plays');assert int(s['decoderIoBytes']['audio'])<64*1024**2
  assert unallocated(path,32*1024**2,65536) and unallocated(path,(1<<32)-8*1024**2,65536)
  decoded=subprocess.check_output(['ffmpeg','-v','error','-ss',str(far),'-i',str(path),'-t','0.5','-c:a','pcm_s'+str(bits)+'le','-f','s'+str(bits)+'le','-'],timeout=90);assert decoded==pcm(g,far*g['rate']*g['align'],g['rate']//2*g['align'])
  control('pause',None);wait(lambda s:s['state']=='paused','pause-far');control('seek',4);wait(lambda s:s['state']=='paused' and 3.7<=s['positionSeconds']<5,'back-to-head');control('seek',far);wait(lambda s:s['state']=='paused' and abs(s['positionSeconds']-far)<1,'repeat-maximum-seek');control('pause',None)
  wait(lambda s:s['state']=='buffering' and s['positionSeconds']>g['duration']/10000000-2,'unconfirmed-eof-waits');s=preview.heartbeat();assert not s['complete'] and not s['released']
  t=time.monotonic();closed=preview.close(timeout=2);elapsed=time.monotonic()-t;assert closed['released'] and elapsed<2,(closed,elapsed);forget_closed_readers(client,model)
  path.unlink();return dict(result='PASS',**g,farSeconds=far,farPhysicalByte=g['offset']+far*g['rate']*g['align'],farPcmSha256=hashlib.sha256(decoded).hexdigest(),phases=phases,cancelReleaseSeconds=elapsed,readerIdentityPreserved=True,largeFileRemovedAfterRelease=True,unallocatedGapsPreserved=True,fullFileCompleted=False)
 finally:
  if preview.started and not preview.closed:preview.close(timeout=30)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile',required=True);p.add_argument('--work',required=True);p.add_argument('--bits',type=int,choices=(16,24),required=True);a=p.parse_args();sequence=[0]
 def control(action,value):
  sequence[0]+=1;root=Path(a.work);tmp=root/'control.tmp';tmp.write_text(json.dumps(dict(sequence=sequence[0],action=action,value=value)),encoding='utf-8');end=time.monotonic()+2
  while True:
   try:tmp.replace(root/'control.json');return
   except PermissionError:
    if time.monotonic()>=end:raise
    time.sleep(.01)
 print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.work,control,a.bits)))
