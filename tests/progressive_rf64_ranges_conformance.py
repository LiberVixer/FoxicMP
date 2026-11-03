"""RF64 out-of-order publication: real holes, prefix gating, authenticated fill."""
import argparse,copy,hashlib,json,subprocess,tempfile
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_integrity import protect_snapshot,externalize_integrity
from progressive_rf64_fixture import fixture,PROFILES,prefix_at_seconds
from progressive_mux_ts_conformance import Rpc

def run(runner):
 checks=[];reports=[]
 with tempfile.TemporaryDirectory(prefix='foxicmp-rf64-ranges-') as tmp:
  root=Path(tmp)
  for profile in PROFILES:
   for external in (False,True):
    work=root/(profile+('-external' if external else '-inline'));m,raw,original,probe=fixture(work/'source',profile,4,writer='manual');work.mkdir(exist_ok=True);path=work/'mux.part';one=prefix_at_seconds(raw,probe,1);two=prefix_at_seconds(raw,probe,2);three=prefix_at_seconds(raw,probe,3);offset=probe['_data_offset'];align=probe['_block_align'];rate=int(profile.split('-')[-1]);bits=probe['_bits'];inline=None;revision=0
    with path.open('wb') as out:out.truncate(len(raw));out.seek(0);out.write(raw[:one]);out.seek(three);out.write(raw[three:])
    def publish(ranges,complete=False):
     nonlocal inline,revision
     revision+=1;v=copy.deepcopy(m);v['revision']=str(revision);v['resources'][0].update(ranges=[dict(offset=str(a),length=str(z-a)) for a,z in ranges],state='complete' if complete else 'downloading')
     if complete:v['writer']['state']=v['tracks'][0]['state']='complete';v['resources'][0]['length']=str(len(raw))
     inline=protect_snapshot(v,work,previous=inline,block_bytes=4*1024**2);return externalize_integrity(inline,work) if external else inline
    rpc=Rpc([runner,'--registry',str(work/'registry')]);tag=work.name
    def oracle(prefix):
     size=int(rpc('layout')['tracks'][0]['available']);data=bytearray()
     for at in range(0,size,65536):
      v=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,size-at)));assert v['bytesRead']>0;data+=bytes.fromhex(v['hex'])
     out=work/'virtual.mp4';out.write_bytes(data);pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(out),'-c:a','pcm_s'+str(bits)+'le','-f','s'+str(bits)+'le','-']);n=(prefix-offset)//align//16384*16384 if prefix<len(raw) else (len(raw)-offset)//align
     assert pcm==raw[offset:offset+n*align];assert int(rpc('status')['physicalReadMaxEndBytes'])<=prefix;return size
    try:
     assert rpc('open',session=publish([(0,one),(three,len(raw))]),directory=str(work))['ok']
     readers=lambda:rpc('readers',version=1,sessionId=m['sessionId'],generation='1')['readers'];ids=[v['readerId'] for v in readers()];assert len(ids)==1
     s=rpc('status');assert s['confirmedPrefixBytes']==str(one) and s['pendingOutOfOrderBytes']==str(len(raw)-three) and s['confirmedRangeCount']==2
     size=oracle(one);assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==-1
     with path.open('rb') as f:f.seek(one);assert f.read(two-one)==b'\0'*(two-one)
     checks.extend(tag+'-'+n for n in ('out-of-order-open','islands-reported','hole-not-exposed','prefix-pcm-exact','hole-is-unwritten'))
     with path.open('r+b') as f:f.seek(one);f.write(raw[one:two])
     assert rpc('publish',session=publish([(0,two),(three,len(raw))]))['ok'];assert [v['readerId'] for v in readers()]==ids;oracle(two)
     checks.extend(tag+'-'+n for n in ('partial-gap-fill','reader-preserved','partial-fill-pcm-exact'))
     with path.open('r+b') as f:f.seek(two);f.write(raw[two:three])
     assert rpc('publish',session=publish([(0,len(raw))]))['ok'];size=oracle(len(raw));assert rpc('status')['pendingOutOfOrderBytes']=='0';assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==-1
     checks.extend(tag+'-'+n for n in ('gap-fill-unlocks-old-island','full-pcm-exact','full-prefix-still-waits'))
     assert rpc('publish',session=publish([(0,len(raw))],True))['ok'];assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==0
     rpc('release');assert not readers();assert original.read_bytes()==path.read_bytes()==raw
     checks.extend(tag+'-'+n for n in ('writer-complete-eof','reader-release','original-unchanged'))
     reports.append(dict(profile=profile,externalIndex=external,bytes=len(raw),holeBytes=three-one,pcmSha256=probe['_pcm_sha256']))
    finally:rpc.close()
  # One missing byte may unlock >16 MiB already confirmed earlier. Bound new
  # publication bytes, not prefix advancement or file size.
  work=root/'large-unlock';m,raw,_,probe=fixture(work/'source','audio-rf64-16-stereo-48000',120,writer='manual');work.mkdir(exist_ok=True);path=work/'mux.part';one=prefix_at_seconds(raw,probe,1);path.write_bytes(raw);previous=None;rev=0
  def plan(ranges):
   nonlocal previous,rev
   rev+=1;v=copy.deepcopy(m);v['revision']=str(rev);v['resources'][0]['ranges']=[dict(offset=str(a),length=str(z-a)) for a,z in ranges];previous=protect_snapshot(v,work,previous=previous,block_bytes=4*1024**2);return externalize_integrity(previous,work)
  rpc=Rpc([runner,'--registry',str(work/'registry')])
  try:
   first=plan([(0,one)]);assert rpc('open',session=first,directory=str(work))['ok'];n=one+1+16*1024**2
   oversized=copy.deepcopy(m);oversized['revision']='2';oversized['resources'][0]['ranges']=[dict(offset='0',length=str(len(raw)))];oversized=externalize_integrity(protect_snapshot(oversized,work,previous=previous,block_bytes=4*1024**2),work)
   assert not rpc('publish',session=oversized)['ok'];assert rpc('status')['revision']=='1';checks.append('update-budget-rejected-source-preserved')
   assert rpc('publish',session=plan([(0,one),(one+1,n)]))['ok'];assert rpc('publish',session=plan([(0,one),(one+1,len(raw))]))['ok'];before=int(rpc('layout')['tracks'][0]['available']);assert rpc('status')['confirmedPrefixBytes']==str(one)
   assert rpc('publish',session=plan([(0,len(raw))]))['ok'];assert int(rpc('layout')['tracks'][0]['available'])-before>16*1024**2 and rpc('status')['pendingOutOfOrderBytes']=='0';checks.append('one-byte-fill-unlocks-over-16MiB-confirmed-island')
   rpc('release')
  finally:rpc.close()
  for name,mutate in [('initial-budget',lambda v:v['resources'][0].update(ranges=[dict(offset='0',length=str(len(raw)))])),('extent',lambda v:v['resources'][0].update(length=None,ranges=[dict(offset='0',length=str(one)),dict(offset=str(len(raw)),length='1')])),('known-length',lambda v:v['resources'][0].update(length=str(len(raw)+1),ranges=[dict(offset='0',length=str(one))]))]:
   v=copy.deepcopy(m);mutate(v)
   if name=='extent':
    with path.open('ab') as f:f.write(b'X')
   v=externalize_integrity(protect_snapshot(v,work,block_bytes=4*1024**2),work);rpc=Rpc([runner,'--registry',str(work/(name+'-registry'))])
   try:
    assert not rpc('open',session=v,directory=str(work))['ok'];assert not rpc('readers',version=1,sessionId=m['sessionId'],generation='1')['readers'];checks.append(name+'-rejected-without-reader')
   finally:rpc.close()
 return dict(result='PASS',count=len(checks),checks=checks,profiles=reports)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
