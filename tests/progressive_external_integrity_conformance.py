"""Authenticated external SHA tables through the real RF64/ResourceSource bridge."""
import argparse,copy,hashlib,json,os,struct,subprocess,tempfile
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_integrity import protect_snapshot,externalize_integrity
from progressive_rf64_fixture import fixture
from progressive_mux_ts_conformance import Rpc

def run(runner):
 checks=[]
 with tempfile.TemporaryDirectory(prefix='foxicmp-ext-integrity-') as tmp:
  root=Path(tmp)
  for bits in (16,24):
   work=root/str(bits);m,raw,original,_=fixture(work/'original','audio-rf64-'+str(bits)+'-stereo-48000',2);work.mkdir(exist_ok=True);(work/'mux.part').write_bytes(raw)
   m['resources'][0].update(length=str(len(raw)),ranges=[dict(offset='0',length=str(len(raw)))],state='complete');m['tracks'][0]['state']='complete';m['writer']['state']='complete'
   inline=protect_snapshot(m,work,block_bytes=256);wire=externalize_integrity(inline,work);ref=wire['resources'][0]['integrity']['index'];index=work/ref['name'];assert len(json.dumps(inline))>65536 and len(json.dumps(wire).encode())<2048
   rpc=Rpc([runner,'--registry',str(work/'registry')])
   try:
    assert rpc('open',session=wire,directory=str(work))['ok'];assert rpc('index')['tracks'][0]['samples']==(96000+16383)//16384
    size=int(rpc('layout')['tracks'][0]['available']);data=bytearray()
    for at in range(0,size,65536):
     v=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,size-at)));assert v['bytesRead']>0;data+=bytes.fromhex(v['hex'])
    out=work/'projected.mp4';out.write_bytes(data);pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(out),'-f','s'+str(bits)+'le','-c:a','pcm_s'+str(bits)+'le','-']);assert pcm==raw[80 if bits==16 else 104:]
    assert int(rpc('status')['physicalReadMaxEndBytes'])==len(raw)
    rpc('release');assert not rpc('readers',version=1,sessionId=m['sessionId'],generation='1')['readers']
    assert original.read_bytes()==raw;checks.extend(str(bits)+'-'+n for n in ['large-table-small-wire','decoded-pcm-exact','physical-read-telemetry','reader-released','original-unchanged'])
   finally:rpc.close()
   pristine=index.read_bytes()
   def reject(name,change=None,payload=None,rehash=False):
    v=copy.deepcopy(wire)
    if change:change(v)
    if payload is not None:
     index.write_bytes(payload)
     if rehash:v['resources'][0]['integrity']['index'].update(bytes=str(len(payload)),sha256=hashlib.sha256(payload).hexdigest())
    rpc=Rpc([runner,'--registry',str(work/(name+'-registry'))])
    try:
     a=rpc('open',session=v,directory=str(work));assert not a['ok'],(name,a);assert not rpc('readers',version=1,sessionId=m['sessionId'],generation='1')['readers'];checks.append(str(bits)+'-'+name+'-rejected-without-reader')
    finally:rpc.close();index.write_bytes(pristine)
   def reference(v):return v['resources'][0]['integrity']['index']
   for name,change in [('format',lambda v:reference(v).update(format='unknown')),('path',lambda v:reference(v).update(name='../escape')),('alias',lambda v:reference(v).update(name='mux.part')),('size',lambda v:reference(v).update(bytes='180225')),('record-size',lambda v:reference(v).update(bytes='43')),('digest',lambda v:reference(v).update(sha256='g'*64)),('missing',lambda v:reference(v).update(name='missing.idx')),('double-table',lambda v:v['resources'][0]['integrity'].update(blocks=[])),('wrong-profile',lambda v:v['tracks'][0].update(containerHint='wav' if bits==16 else 'wav-extensible-pcm24'))]:reject(name,change=change)
   reject('truncated',payload=pristine[:-1]);reject('tamper',payload=bytes([pristine[0]^1])+pristine[1:])
   b=bytearray(pristine);struct.pack_into('<Q',b,0,2**63);reject('offset-overflow',payload=b,rehash=True)
   b=bytearray(pristine);struct.pack_into('<I',b,8,0);reject('zero-block',payload=b,rehash=True)
   b=bytearray(pristine);struct.pack_into('<I',b,8,4194305);reject('block-oversized',payload=b,rehash=True)
   reject('withdrawn',payload=pristine[44:],rehash=True);reject('duplicate-block',payload=pristine[:44]+pristine,rehash=True)
   reject('empty-coverage',payload=b'',rehash=True)
   alias=work/'hardlink.idx';os.link(index,alias)
   try:reject('hardlinked-index')
   finally:alias.unlink()
   # Sidecar bytes are copied after SHA verification; same-reference mutation
   # after attachment never changes cached digests or permits media rewrites.
   rpc=Rpc([runner,'--registry',str(work/'immutable-registry')])
   try:
    assert rpc('open',session=wire,directory=str(work))['ok'];index.write_bytes(b'X'*len(pristine));assert rpc('index')['tracks'][0]['samples']>0;rpc('release');checks.append(str(bits)+'-immutable-verified-table')
   finally:rpc.close();index.write_bytes(pristine)
   # Append references may change; previously confirmed hashes may not.
   growing=copy.deepcopy(m);growing['writer']['state']='downloading';growing['tracks'][0]['state']='downloading';growing['resources'][0]['state']='downloading';growing['resources'][0]['ranges'][0]['length']=str((80 if bits==16 else 104)+48000*(bits//8)*2)
   first=protect_snapshot(growing,work,block_bytes=4096);one=externalize_integrity(first,work);rpc=Rpc([runner,'--registry',str(work/'append-registry')])
   try:
    assert rpc('open',session=one,directory=str(work))['ok'];plan=copy.deepcopy(m);plan['revision']='2';next=protect_snapshot(plan,work,previous=first,block_bytes=4096);assert rpc('publish',session=externalize_integrity(next,work))['ok'];rpc('release');checks.append(str(bits)+'-append-reference-with-stable-blocks')
   finally:rpc.close()
   rpc=Rpc([runner,'--registry',str(work/'rewrite-registry')])
   try:
    assert rpc('open',session=one,directory=str(work))['ok'];bad=copy.deepcopy(next);bad['resources'][0]['integrity']['blocks'][0]['sha256']='0'*64
    assert not rpc('publish',session=externalize_integrity(bad,work))['ok'];assert rpc('read',decoder=True,track=0,offset='0',length='1')['bytesRead']==-1;rpc('release');assert not rpc('readers',version=1,sessionId=m['sessionId'],generation='1')['readers'];checks.append(str(bits)+'-changed-published-digest-rejected-and-released')
   finally:rpc.close()
 return dict(result='PASS',count=len(checks),checks=checks)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
