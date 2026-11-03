"""Actual public API RF64 playback while the writer publishes out of order."""
import argparse,copy,hashlib,json,os,time
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
 root=Path(work).resolve();model,raw,original,probe=fixture(root/'source',profile,20,writer='manual');root.mkdir(exist_ok=True);model['metadata']['chapters'][-1].update(id='c14',time='140000000',title='14');external='-stereo-' in profile;path=root/'mux.part';initial=prefix_at_seconds(raw,probe,4);twelve=prefix_at_seconds(raw,probe,12);fourteen=prefix_at_seconds(raw,probe,14);sixteen=prefix_at_seconds(raw,probe,16);inline=None;revision=0;phases=[]
 cap=client.request(dict(version=1,action='capabilities'))['schema2Playback']['standalone']['rf64Pcm'+str(probe['_bits'])]['outOfOrder'];assert cap['mode']=='prefix-gated-v1' and cap['islandSeek'] is False
 with path.open('wb') as f:f.truncate(len(raw))
 def write(a,z):
  with path.open('r+b') as f:f.seek(a);f.write(raw[a:z]);f.flush()
 write(0,initial);write(sixteen,len(raw));preview=V2PreviewSession(client,str(root),title='Out-of-order RF64 '+profile);reader=None
 def publish(ranges,complete=False):
  nonlocal inline,revision
  revision+=1;v=copy.deepcopy(model);v['revision']=str(revision);v['resources'][0].update(ranges=[dict(offset=str(a),length=str(z-a)) for a,z in ranges],state='complete' if complete else 'downloading')
  if complete:v['writer']['state']=v['tracks'][0]['state']='complete';v['resources'][0]['length']=str(len(raw))
  inline=protect_snapshot(v,root,previous=inline,block_bytes=4*1024**2);wire=externalize_integrity(inline,root) if external else inline;return preview.publish(wire)
 def wait(predicate,label,timeout=35):
  end=time.monotonic()+timeout;s={}
  while time.monotonic()<end:
   s=preview.heartbeat();assert s.get('ok') and not s.get('error'),s;assert s['readerId']==reader
   if predicate(s):
    phases.append(dict(phase=label,state=s['state'],positionSeconds=s.get('positionSeconds'),availableSeconds=s.get('availableSeconds'),confirmedPrefixBytes=s.get('confirmedPrefixBytes'),pendingOutOfOrderBytes=s.get('pendingOutOfOrderBytes'),physicalReadMaxEndBytes=s.get('physicalReadMaxEndBytes')));return s
   time.sleep(.05)
  raise AssertionError((profile,label,s))
 try:
  publish([(0,initial),(sixteen,len(raw))]);reader=preview.reader_id;assert reader
  first=wait(lambda s:s['state']=='buffering','island-before-start');assert int(first['pendingOutOfOrderBytes'])==len(raw)-sixteen and first['confirmedPrefixBytes']==str(initial)
  write(initial,twelve);publish([(0,twelve),(sixteen,len(raw))]);wait(lambda s:s['state']=='playing' and s['positionSeconds']>=2,'prefix-starts');control('pause',None);wait(lambda s:s['state']=='paused','manual-pause')
  write(fourteen,sixteen);publish([(0,twelve),(fourteen,len(raw))]);s=wait(lambda s:s['state']=='paused' and s['revision']==str(revision),'island-update-preserves-pause');assert s['confirmedPrefixBytes']==str(twelve) and int(s['pendingOutOfOrderBytes'])==len(raw)-fourteen
  control('reject-seek',14);time.sleep(.4);s=wait(lambda s:s['state']=='paused','island-seek-rejected');assert s['positionSeconds']<12 and int(s['physicalReadMaxEndBytes'])<=twelve
  control('pause',None);s=wait(lambda s:s['state']=='buffering' and s['positionSeconds']>=9,'hole-waits-not-eof');assert not s['complete'] and s['positionSeconds']<12 and int(s['physicalReadMaxEndBytes'])<=twelve and path.stat().st_size==len(raw)
  with path.open('rb') as f:f.seek(twelve);assert f.read(fourteen-twelve)==b'\0'*(fourteen-twelve)
  time.sleep(.3);s=preview.heartbeat();assert s['state']=='buffering' and not s['released'] and s['positionSeconds']<12
  write(twelve,fourteen);publish([(0,len(raw))]);s=wait(lambda s:s['state']=='playing' and s['positionSeconds']>=13,'gap-fill-resumes-same-reader');assert s['pendingOutOfOrderBytes']=='0' and not s['complete']
  control('pause',None);wait(lambda s:s['state']=='paused','pause-after-fill');control('seek',14);wait(lambda s:s['state']=='paused' and 13.7<=s['positionSeconds']<15,'seek-now-confirmed');control('pause',None)
  s=wait(lambda s:s['state']=='buffering' and s['positionSeconds']>=18,'full-prefix-waits-for-writer');assert not s['complete'] and not s['released']
  publish([(0,len(raw))],True);wait(lambda s:s['state']=='finished' and s.get('released'),'complete-eof-release');assert preview.close(timeout=15)['released'];forget_closed_readers(client,inline)
  assert path.read_bytes()==original.read_bytes()==raw
  return dict(result='PASS',profile=profile,externalIndex=external,fileBytes=len(raw),fileSizeDidNotConfirmHoles=True,initialHoleBytes=sixteen-initial,lastHoleBytes=fourteen-twelve,islandSeekRejected=True,readerIdentityPreserved=True,originalUnchanged=True,sha256=hashlib.sha256(raw).hexdigest(),phases=phases)
 finally:
  if preview.started and not preview.closed:preview.close(timeout=15)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile');p.add_argument('--work',required=True);p.add_argument('--media-profile',choices=PROFILES,required=True);a=p.parse_args();seq=[0]
 def control(action,value):
  seq[0]+=1;root=Path(a.work);path=root/'control.json';tmp=root/'control.tmp';tmp.write_text(json.dumps(dict(sequence=seq[0],action=action,value=value)),encoding='utf-8');end=time.monotonic()+2
  while True:
   try:tmp.replace(path);break
   except PermissionError:
    if time.monotonic()>=end:raise
    time.sleep(.01)
 print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.work,control,a.media_profile)))
