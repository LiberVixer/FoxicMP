"""Sparse RF64 production geometry, immutable offsets and guarded packet reads."""
import argparse,copy,hashlib,json,subprocess,tempfile
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_rf64_fixture import fixture,PROFILES,prefix_at_seconds
from progressive_integrity import protect_snapshot,externalize_integrity
from progressive_mux_ts_conformance import Rpc

def run(runner):
 checks=[];profiles=[]
 with tempfile.TemporaryDirectory(prefix='foxicmp-sparse-rf64-') as tmp:
  root=Path(tmp)
  for profile in PROFILES:
   work=root/profile;m,raw,original,probe=fixture(work/'source',profile,8,writer='manual');rate=int(probe['streams'][0]['sample_rate']);align=probe['_block_align'];offset=probe['_data_offset'];bits=probe['_bits'];packet=16384*align;work.mkdir(exist_ok=True)
   # Independent complete reference, using the unchanged ordinary RF64 profile.
   reference=work/'reference';reference.mkdir();(reference/'mux.part').write_bytes(raw);full=copy.deepcopy(m);full['resources'][0].update(length=str(len(raw)),state='complete',ranges=[dict(offset='0',length=str(len(raw)))]);full['tracks'][0]['state']=full['writer']['state']='complete';full=protect_snapshot(full,reference)
   rpc=Rpc([runner,'--registry',str(reference/'registry')]);canonical=bytearray()
   try:
    assert rpc('open',session=full,directory=str(reference))['ok'];size=int(rpc('layout')['tracks'][0]['available'])
    for at in range(0,size,65536):canonical+=bytes.fromhex(rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,size-at)))['hex'])
    rpc('release')
   finally:rpc.close()
   output=work/'reference.mp4';output.write_bytes(canonical);packets=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-of','json',str(output)]))['packets']
   for external in (False,True):
    data=work/('external' if external else 'inline');data.mkdir();path=data/'mux.part';head=offset+3*packet;island=offset+10*packet
    with path.open('wb') as f:f.truncate(len(raw));f.write(raw[:head]);f.seek(island);f.write(raw[island:])
    previous=None;rev=0
    def publish(ranges,complete=False):
     nonlocal previous,rev
     rev+=1;v=copy.deepcopy(m);v['revision']=str(rev);v['tracks'][0]['containerHint']='rf64-sparse-pcm'+str(bits);v['resources'][0].update(length=str(len(raw)),ranges=[dict(offset=str(a),length=str(z-a)) for a,z in ranges])
     if complete:v['writer']['state']=v['tracks'][0]['state']=v['resources'][0]['state']='complete'
     previous=protect_snapshot(v,data,previous=previous);return externalize_integrity(previous,data) if external else previous
    rpc=Rpc([runner,'--registry',str(data/'registry')]);tag=profile+('-external' if external else '-inline')
    try:
     assert rpc('open',session=publish([(0,head),(island,len(raw))]),directory=str(data))['ok'];before=rpc('layout');assert int(before['tracks'][0]['available'])==size and int(before['tracks'][0]['total'])==size
     assert rpc('index')['tracks'][0]['samples']==len(packets);status=rpc('status');assert status['sparsePlayback'] and status['mediaMode']=='standalone-rf64-sparse-pcm'+str(bits);assert int(status['physicalReadMaxEndBytes'])<=head
     assert rpc('sparseReady',ticks=str(4*16384*10000000//rate))['endTicks']==str(4*16384*10000000//rate)
     target=11;sample=packets[target];at=int(sample['pos']);n=int(sample['size']);r=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(n,65536)));assert r['bytesRead']>0 and bytes.fromhex(r['hex'])==raw[offset+target*packet:offset+target*packet+r['bytesRead']]
     hole=packets[5];assert rpc('read',decoder=True,track=0,offset=hole['pos'],length='16')['bytesRead']==-1
     assert rpc('read',decoder=True,track=0,offset=str(at),length='16')['bytesRead']==16
     assert int(rpc('status')['physicalReadMaxEndBytes'])>island
     end=rpc('sparseReady',ticks=str(11*16384*10000000//rate));assert end['sparse'] and end['endTicks']==str(80000000)
     assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==-1
     checks.extend(tag+'-'+n for n in ('stable-complete-geometry','metadata-index-without-hole-io','sparse-mode-explicit','hole-not-ready','island-pcm-exact','hole-read-waits','wait-does-not-poison','island-physical-io','island-ready-end','no-premature-eof'))
     with path.open('r+b') as f:f.seek(head);f.write(raw[head:island])
     assert rpc('publish',session=publish([(0,len(raw))]))['ok'];assert rpc('layout')==before
     assert rpc('read',decoder=True,track=0,offset=hole['pos'],length='16')['hex']==bytes(canonical[int(hole['pos']):int(hole['pos'])+16]).hex()
     assert rpc('publish',session=publish([(0,len(raw))],True))['ok'];assert rpc('layout')==before
     projected=bytearray()
     for at in range(0,size,65536):
      r=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,size-at)));assert r['bytesRead']>0;projected+=bytes.fromhex(r['hex'])
     assert projected==canonical;assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==0
     rpc('release');assert not rpc('readers',version=1,sessionId=m['sessionId'],generation='1')['readers'];assert path.read_bytes()==original.read_bytes()==raw
     checks.extend(tag+'-'+n for n in ('fill-preserves-offsets','filled-hole-pcm-exact','complete-preserves-offsets','full-projection-bit-exact','writer-eof','reader-release','original-unchanged'))
     profiles.append(dict(profile=profile,externalIndex=external,packets=len(packets),virtualBytes=size,pcmSha256=probe['_pcm_sha256']))
    finally:rpc.close()
 return dict(result='PASS',count=len(checks),checks=checks,profiles=profiles)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
