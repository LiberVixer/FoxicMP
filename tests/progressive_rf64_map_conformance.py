"""Packet-aligned RF64 range diagnostics through the production resource bridge."""
import argparse,copy,json,random,subprocess,tempfile
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_rf64_fixture import fixture,PROFILES,prefix_at_seconds
from progressive_integrity import protect_snapshot,externalize_integrity
from progressive_mux_ts_conformance import Rpc

def expected_map(ranges,offset,frames,align,rate,prefix):
 # Independent frame-set oracle, deliberately not the implementation's
 # byte-rounding expression. Only full decoder packets qualify.
 intervals=[];current=None
 for frame in range(0,frames,16384):
  last=min(frame+16384,frames);a=offset+frame*align;z=offset+last*align;cursor=a
  for start,end in ranges:
   if start<=cursor<end:cursor=min(z,end)
   if cursor==z:break
  if cursor!=z:current=None;continue
  if current is None:
   current=dict(firstFrame=str(frame),endFrame=str(last),offset=str(a),length=str(z-a),startTicks=str(frame*10000000//rate),endTicks=str(last*10000000//rate),prefix=z<=prefix);intervals.append(current)
  else:
   current.update(endFrame=str(last),length=str(z-int(current['offset'])),endTicks=str(last*10000000//rate),prefix=z<=prefix)
 return dict(version=1,geometryOnly=True,islandSeek=False,payloadVerified=False,sampleRate=rate,packetFrames=16384,intervalCount=len(intervals),truncated=len(intervals)>32,intervals=intervals[:32])

def run(runner):
 checks=[];rows=[]
 with tempfile.TemporaryDirectory(prefix='foxicmp-rf64-map-') as tmp:
  root=Path(tmp)
  for profile in PROFILES:
   for external in (False,True):
    work=root/(profile+('-external' if external else '-inline'));m,raw,_,probe=fixture(work/'source',profile,4,writer='manual');work.mkdir(exist_ok=True);(work/'mux.part').write_bytes(raw)
    offset=probe['_data_offset'];align=probe['_block_align'];rate=int(profile.split('-')[-1]);frames=4*rate;prefix=prefix_at_seconds(raw,probe,1);packet=16384*align
    # Cut inside a PCM sample; no whole packet may straddle that hole.
    island=offset+3*packet+1;stop=offset+8*packet-1
    cases=[('partial-packets',[(0,prefix),(island,stop),(offset+9*packet,len(raw))]),('adjacent-splits',[(0,prefix),(offset+3*packet,offset+3*packet+1),(offset+3*packet+1,offset+6*packet)]),('single-byte-gap',[(0,prefix),(offset+3*packet,offset+4*packet),(offset+4*packet+1,len(raw))]),('header-only-island',[(0,prefix)]),('full-final-short-packet',[(0,len(raw))])]
    for name,ranges in cases:
     v=copy.deepcopy(m);v['resources'][0].update(length=str(len(raw)),ranges=[dict(offset=str(a),length=str(z-a)) for a,z in ranges]);v=protect_snapshot(v,work,block_bytes=4*1024**2);v=externalize_integrity(v,work) if external else v
     rpc=Rpc([runner,'--registry',str(work/(name+'-registry'))])
     try:
      assert rpc('open',session=v,directory=str(work))['ok'];before=rpc('status');actual=before['rf64RangeMap'];prefixEnd=ranges[0][1]
      assert actual==expected_map(ranges,offset,frames,align,rate,prefixEnd),(profile,name,actual)
      after=rpc('status');assert before['physicalReadMaxEndBytes']==after['physicalReadMaxEndBytes'] and before['decoderIoBytes']==after['decoderIoBytes'];assert int(after['physicalReadMaxEndBytes'])<=prefixEnd
      rpc('release');assert not rpc('readers',version=1,sessionId=m['sessionId'],generation='1')['readers']
      checks.extend(work.name+'-'+name+'-'+n for n in ('packet-map-exact','no-payload-io','reader-released'))
     finally:rpc.close()
    rows.append(dict(profile=profile,externalIndex=external,cases=len(cases)))
  # Status bounds: 65 intervals, 64 holes; only first32 serialized, with
  # explicit full count and truncation. Original range count stays <=128.
  work=root/'bounded';m,raw,_,probe=fixture(work/'source','audio-rf64-16-stereo-48000',50,writer='manual');work.mkdir(exist_ok=True);(work/'mux.part').write_bytes(raw);offset=probe['_data_offset'];packet=16384*4
  ranges=[(0,offset+packet)]+[(offset+i*packet,offset+(i+1)*packet) for i in range(2,130,2)]
  v=copy.deepcopy(m);v['resources'][0].update(length=str(len(raw)),ranges=[dict(offset=str(a),length=str(z-a)) for a,z in ranges]);v=externalize_integrity(protect_snapshot(v,work,block_bytes=4*1024**2),work);rpc=Rpc([runner,'--registry',str(work/'registry')])
  try:
   assert rpc('open',session=v,directory=str(work))['ok'];s=rpc('status');assert s['rf64RangeMap']==expected_map(ranges,offset,50*48000,4,48000,ranges[0][1]);assert s['rf64RangeMap']['intervalCount']==65 and len(s['rf64RangeMap']['intervals'])==32 and s['rf64RangeMap']['truncated'];assert len(json.dumps(s).encode())<16384
   checks.extend(('bounded-map-truncation-explicit','bounded-status-under-16KiB'));rpc('release')
  finally:rpc.close()
 return dict(result='PASS',count=len(checks),checks=checks,profiles=rows,largePhysicalFileCreated=False)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
