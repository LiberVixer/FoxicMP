"""Public API sparse RF64: island/back seeks, two hole waits, same reader and EOF."""
import argparse,copy,hashlib,json,time,uuid
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_client import ProgressiveClient
from progressive_v2 import V2PreviewSession
from progressive_integrity import protect_snapshot,externalize_integrity
from progressive_rf64_fixture import fixture,prefix_at_seconds
from progressive_v2_playback_conformance import forget_closed_readers
PROFILES=('audio-rf64-16-mono-44100','audio-rf64-16-stereo-48000','audio-rf64-24-mono-44100','audio-rf64-24-stereo-48000')
def exercise(client,work,control,profile):
 root=Path(work).resolve();model,raw,original,probe=fixture(root/'source',profile,36,writer='manual');root.mkdir(exist_ok=True);bits=probe['_bits'];model['tracks'][0]['containerHint']='rf64-sparse-pcm'+str(bits);model['resources'][0]['length']=str(len(raw));model['metadata']['chapters']=[dict(id='c'+str(i),time=str(round(t*10000000)),title=str(t)) for i,t in enumerate((0,4,13,16,32,35.95))]
 family=client.request(dict(version=1,action='capabilities'))['schema2Playback'];cap=family['standalone']['rf64Pcm'+str(bits)]['sparsePlayback'];assert cap['islandSeek'] and cap['knownLengthRequired'];assert model['tracks'][0]['containerHint'] in family['containers']
 path=root/'mux.part';at=lambda n:prefix_at_seconds(raw,probe,n);external='stereo' in profile;previous=None;revision=0;phases=[]
 with path.open('wb') as f:f.truncate(len(raw))
 def write(a,z):
  with path.open('r+b') as f:f.seek(a);f.write(raw[a:z]);f.flush()
 write(0,at(4));write(at(14),at(24));write(at(28),len(raw));ranges=[(0,at(4)),(at(14),at(24)),(at(28),len(raw))]
 preview=V2PreviewSession(client,str(root),title='Sparse RF64 '+profile);reader=None
 def publish(complete=False):
  nonlocal previous,revision
  revision+=1;v=copy.deepcopy(model);v['revision']=str(revision);v['resources'][0]['ranges']=[dict(offset=str(a),length=str(z-a)) for a,z in ranges]
  if complete:v['writer']['state']=v['tracks'][0]['state']=v['resources'][0]['state']='complete'
  previous=protect_snapshot(v,root,previous=previous,block_bytes=4*1024**2);return preview.publish(externalize_integrity(previous,root) if external else previous)
 def wait(predicate,label,timeout=40):
  end=time.monotonic()+timeout;s={}
  while time.monotonic()<end:
   s=preview.heartbeat();assert s.get('ok') and not s.get('error'),s;assert s['readerId']==reader
   if predicate(s):
    phases.append(dict(phase=label,state=s['state'],positionSeconds=s.get('positionSeconds'),availableSeconds=s.get('availableSeconds'),physicalReadMaxEndBytes=s.get('physicalReadMaxEndBytes'),revision=s.get('revision')));return s
   time.sleep(.05)
  raise AssertionError((profile,label,s))
 try:
  publish();reader=preview.reader_id;assert reader;wait(lambda s:s['state']=='buffering','head-buffering')
  write(at(4),at(12));ranges[0]=(0,at(12));publish();wait(lambda s:s['state']=='playing' and s['positionSeconds']>=2,'head-playing');control('pause',None);wait(lambda s:s['state']=='paused','manual-pause')
  control('reject-seek',13);time.sleep(.3);s=wait(lambda s:s['state']=='paused','hole-seek-rejected');assert s['positionSeconds']<12
  control('seek',16);s=wait(lambda s:s['state']=='paused' and 15.7<=s['positionSeconds']<17,'paused-island-seek');assert s['sparsePlayback'] and s['rf64RangeMap']['islandSeek'] and not s['complete']
  control('pause',None);s=wait(lambda s:s['state']=='playing' and s['positionSeconds']>=18,'island-playing');assert int(s['physicalReadMaxEndBytes'])>at(16)
  with path.open('rb') as f:f.seek(at(12));assert f.read(at(14)-at(12))==b'\0'*(at(14)-at(12))
  s=wait(lambda s:s['state']=='buffering' and 22<=s['positionSeconds']<24,'island-end-waits');assert not s['released'] and not s['complete']
  write(at(24),at(28));ranges=[ranges[0],(at(14),len(raw))];publish();wait(lambda s:s['state']=='playing' and s['positionSeconds']>=27,'island-extends-resumes');control('pause',None);wait(lambda s:s['state']=='paused','pause-for-back-seek')
  control('seek',4);wait(lambda s:s['state']=='paused' and 3.7<=s['positionSeconds']<5,'backwards-over-hole');control('pause',None);wait(lambda s:s['state']=='buffering' and 10<=s['positionSeconds']<12,'head-gap-waits')
  write(at(12),at(14));ranges=[(0,len(raw))];publish(True);control('seek',32);wait(lambda s:s['state']=='playing' and s.get('positionSeconds',0)>=31.7,'complete-tail-seek')
  control('pause',None);wait(lambda s:s['state']=='paused','complete-pause');control('seek',35.95);wait(lambda s:s['state']=='paused' and 35.7<=s.get('positionSeconds',0)<36,'complete-last-50ms-seek');control('pause',None)
  wait(lambda s:s['state']=='finished' and s.get('released'),'writer-eof-release');assert preview.close(timeout=20)['released'];forget_closed_readers(client,previous)
  assert path.read_bytes()==original.read_bytes()==raw
  cancelled=False
  if profile=='audio-rf64-16-mono-44100':
   # Separate session deliberately withholds the rest of an already written
   # file, so a blocked callback must be cancelled rather than reading its tail.
   model['sessionId']=uuid.uuid4().hex;model['writer']['instanceId']=uuid.uuid4().hex
   previous=None;revision=0;ranges=[(0,at(12))]
   preview=V2PreviewSession(client,str(root),title='Sparse cancel at hole');publish();reader=preview.reader_id
   wait(lambda s:s['state']=='playing' and s['positionSeconds']>=2,'cancel-session-starts')
   s=wait(lambda s:s['state']=='buffering' and 10<=s['positionSeconds']<12,'cancel-session-hole-wait');assert int(s['physicalReadMaxEndBytes'])<=at(12)
   began=time.monotonic();assert preview.close(timeout=20)['released'];cancelSeconds=time.monotonic()-began;forget_closed_readers(client,previous);cancelled=True
  return dict(result='PASS',profile=profile,externalIndex=external,readerIdentityPreserved=True,islandSeekPlayed=True,backwardSeekOverHole=True,unwrittenHolePreservedWhileIslandPlayed=True,originalUnchanged=True,sha256=hashlib.sha256(raw).hexdigest(),cancelWhileGapSeparateSession=cancelled,cancelSeconds=cancelSeconds if cancelled else None,phases=phases)
 finally:
  if preview.started and not preview.closed:preview.close(timeout=20)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile');p.add_argument('--work',required=True);p.add_argument('--media-profile',choices=PROFILES,required=True);a=p.parse_args();seq=[0]
 def control(action,value):
  seq[0]+=1;path=Path(a.work)/'control.json';tmp=path.with_suffix('.tmp');tmp.write_text(json.dumps(dict(sequence=seq[0],action=action,value=value)),encoding='utf-8');end=time.monotonic()+2
  while True:
   try:tmp.replace(path);break
   except PermissionError:
    if time.monotonic()>=end:raise
    time.sleep(.01)
 print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.work,control,a.media_profile)))
