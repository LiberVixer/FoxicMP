"""Upper bounds + 100 native core open/cancel cycles, real sparse resources."""
import argparse,copy,json,statistics,sys,tempfile,time
from pathlib import Path
from progressive_mux_ts_conformance import Rpc
from progressive_rf64_limits_fixture import fixture,geometry,pcm,PACKET,PACKETS,FILE_LIMIT
from progressive_process_metrics import sample

def run(runner):
 rows=[];rejects=[];checks=[];measurements=[];live=[];opens=[];closes=[]
 with tempfile.TemporaryDirectory(prefix='foxicmp-rf64-limit-') as tmp:
  root=Path(tmp);rpc=Rpc([runner,'--registry',str(root/'registry')]);fixtures=[]
  try:
   for bits in (16,24):
    for channels in (1,2):
     for rate in (44100,48000):
      g=geometry(bits,channels,rate);name=f'{bits}-{channels}-{rate}';work=root/name;model,path,_,head,tail,far=fixture(work,g);fixtures.append((g,model,path,work))
      assert rpc('open',session=model,directory=str(work))['ok'];status=rpc('status');assert status['sparsePlayback'] and status['wavPackets']==g['packets'] and int(status['physicalReadMaxEndBytes'])<=head
      t=time.monotonic();index=rpc('index')['tracks'][0];index_seconds=time.monotonic()-t;assert index['samples']==g['packets'];last=index['lastSample'];assert int(last['end'])==g['duration'] and int(last['offset'])>2**32
      n=min(17,int(last['size']));r=rpc('read',decoder=True,track=0,offset=last['offset'],length=str(n));assert r['bytesRead']==n and bytes.fromhex(r['hex'])==pcm(g,(g['packets']-1)*PACKET*g['align'],n)
      assert int(rpc('status')['physicalReadMaxEndBytes'])>2**32
      assert rpc('cancel')['ok'] and rpc('release')['ok'];assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
      print('RF64 maximum profile',name,'PASS',flush=True,file=sys.stderr)
      rows.append(dict(**g,lastSample=last,indexSeconds=round(index_seconds,6),payloadTailExact=True,headOnlyInitialIo=True,readerReleased=True))
      checks.extend(name+'-'+x for x in ('maximum-geometry','no-hole-io','last-sample-index','last-pcm-exact','cancel-release'))
      bad=geometry(bits,channels,rate,g['frames']+1);v,badpath,_,_,_,_=fixture(root/(name+'-over'),bad)
      answer=rpc('open',session=v,directory=str(badpath.parent));assert not answer['ok'] and answer['code']=='unsupported_media',answer
      assert not rpc('readers',version=1,sessionId=v['sessionId'],generation='1')['readers'];badpath.unlink();rejects.append(dict(profile=name,frames=bad['frames'],fileBytes=bad['fileBytes'],packets=bad['packets'],code=answer['code'],reason=answer['error'],readerReleased=True));checks.append(name+'-one-frame-over-effective-limit-rejected')
   assert any(g['fileBytes']==FILE_LIMIT for g,_,_,_ in fixtures) and any(g['packets']==PACKETS for g,_,_,_ in fixtures)
   # Warm both depths before establishing the allocator high-water baseline.
   pair=[next(f for f in fixtures if f[0]['bits']==bits and f[0]['channels']==2 and f[0]['rate']==48000) for bits in (16,24)]
   for i in range(104):
    g,model,path,work=pair[i%2];t=time.monotonic();assert rpc('open',session=model,directory=str(work))['ok'];opens.append(time.monotonic()-t);s=rpc('status');assert s['wavPackets']==g['packets'] and int(s['physicalReadMaxEndBytes'])<4*1024**2
    if i%10==0:assert rpc('index')['tracks'][0]['samples']==g['packets']
    live.append(sample(rpc.p.pid));t=time.monotonic();assert rpc('cancel')['ok'] and rpc('release')['ok'];elapsed=time.monotonic()-t;assert elapsed<2,elapsed;closes.append(elapsed)
    assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];m=dict(**sample(rpc.p.pid),heap=rpc('heapMetrics'));assert m['heap']['ok']
    if i>=4:measurements.append(dict(cycle=i-3,**m))
    if i>=4 and (i-3)%20==0:print('RF64 core cycles',i-3,'RSS',m['rssBytes'],'handles',m['handles'],flush=True,file=sys.stderr)
   assert len(measurements)==100
   early=measurements[:10];steady=measurements[30:40];late=measurements[-10:];baseline=max(m['rssBytes'] for m in early);private=max(m['privateBytes'] for m in early);handles=max(m['handles'] for m in early)
   # OS/allocator lazy caches can settle after the first few opens. Preserve
   # the full initial growth, bound its size, then guard the later trend.
   assert max(m['handles'] for m in measurements)<=handles+8
   assert max(m['handles'] for m in late)<=max(m['handles'] for m in steady)+2
   assert max(m['rssBytes'] for m in measurements)<=baseline+64*1024**2 and max(m['privateBytes'] for m in measurements)<=private+64*1024**2
   print('RF64 core measurement summary: '+json.dumps(measurements),flush=True,file=sys.stderr)
   assert max(m['heap']['busyBytes'] for m in late)<=max(m['heap']['busyBytes'] for m in early)+1024**2
   assert max(m['rssBytes'] for m in live)<=256*1024**2
   if 'threads' in measurements[0]:assert max(m['threads'] for m in measurements)==min(m['threads'] for m in measurements)
   for _,_,path,_ in fixtures:path.unlink()
   checks.extend(('100-core-open-cancel-release-cycles','no-residual-readers','bounded-fd-handles','bounded-resident-private-and-live-heap','cancel-release-under-2s','maximum-packet-and-byte-boundaries-exercised'))
   return dict(result='PASS',count=len(checks),checks=checks,maximumProfiles=rows,rejections=rejects,coreCycles=100,warmupCycles=4,processMeasurements=measurements,peakObservedLiveRssBytes=max(m['rssBytes'] for m in live),medianOpenSeconds=statistics.median(opens[4:]),maxOpenSeconds=max(opens[4:]),maxCancelReleaseSeconds=max(closes[4:]),memoryBudget=dict(lastVsFirstMedianRssGrowthBytes=statistics.median(m['rssBytes'] for m in late)-statistics.median(m['rssBytes'] for m in early),steadyVsFinalMedianRssGrowthBytes=statistics.median(m['rssBytes'] for m in late)-statistics.median(m['rssBytes'] for m in steady),initialToleranceBytes=64*1024**2,liveHeapToleranceBytes=1024**2,peakObservedLiveBudgetBytes=256*1024**2),physicalFilesWereSparse=True,fullPlayerLifecycleTested=False,longDurationStressTested=False)
  finally:rpc.close()
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
