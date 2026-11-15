"""Production SimpleBlock VFR: incremental lookahead, exact media and failures."""
import argparse,hashlib,json,tempfile
from pathlib import Path
from progressive_stream_remux_worker_test import serve
from progressive_webm_simple_vfr_fixtures import CASES,media
from progressive_webm_opus_remux_preflight import configuration,packets,pcm,probe,children
from progressive_webm_remux_preflight import scan
from progressive_webm_vfr_preflight import decoded
from progressive_webm_fixtures import width
MODE='continuous-webm-vp9-opus-simple-vfr-v1'

def run(worker,report,auto_count=False,nominal=False):
 auto_count=auto_count or nominal
 global MODE
 MODE='continuous-webm-vp9-opus-simple-vfr-nominal-v1' if nominal else 'continuous-webm-vp9-opus-simple-vfr-auto-count-v1' if auto_count else 'continuous-webm-vp9-opus-simple-vfr-v1'
 if nominal:
  from progressive_webm_nominal_vfr_fixtures import CASES as cases,media as create_media
 else:cases,create_media=CASES,media
 report=Path(report);report.mkdir(parents=True,exist_ok=True);rows=[]
 with tempfile.TemporaryDirectory(prefix='fox-simple-vfr-worker-') as t:
  root=Path(t);control=None
  for case in cases:
   folder=root/case[0];source,pixels,frames,end,meta,profile=create_media(folder,case);raw=source.read_bytes();digest=hashlib.sha256(raw).hexdigest();layout=scan(raw)
   video=configuration(raw)[1]['number'];second=layout['clusters'][1]
   nextvideo=next(b for b in layout['blocks'] if b['track']==video and second['body']<=b['offset']<second['end'])
   cut=next(int(p['pos'])+13 for p in meta['packets'] if p['codec_type']=='video' and float(p['pts_time'])>=20)
   cuts=[layout['clusters'][0]['end'],nextvideo['end'],cut];partials=[]
   refpcm=pcm(source,folder,'source-pcm');refvideo=decoded(source,pixels,folder,'source-video')
   def checkpoint(data,event):
    if event['available']==cuts[0]:assert not event['mediaReady'],'Cluster escaped before next PTS'
    if event['available']==cuts[1]:assert event['mediaReady'],'Previous Cluster waited for next Cluster end'
    if not event['mediaReady']:return
    partial=folder/'partial.webm';partial.write_bytes(data);actual=probe(partial,folder,'partial-probe')
    for kind in ('video','audio'):assert packets(actual,kind)==packets(meta,kind)[:len(packets(actual,kind))]
    sound=pcm(partial,folder,'partial-pcm');assert sound==refpcm[:len(sound)]
    if event['available']==len(raw):assert len(packets(actual,'video'))<frames,'Final video escaped before writer EOF'
    partials.append(dict(bytes=len(data),available=event['available'],videoPackets=len(packets(actual,'video'))))
   row,output=serve(worker,source,mode=MODE,format_hint='webm',video_samples=None if auto_count else frames,video_duration=end,audio_samples=profile[5],byte_cuts=cuts,short_reads=True,on_wait=checkpoint)
   assert row['exitCode']==0 and row['result']['code']=='data' and row['firstCompleteFragmentBeforeSourceComplete'] and len(partials)>=3,row
   assert row['result']['packets']==frames+(profile[5]+1271)//960
   row['externalVideoCountProvided']=not auto_count
   out=folder/'output.webm';out.write_bytes(output);actual=probe(out,folder,'output-probe')
   for kind in ('video','audio'):assert packets(actual,kind)==packets(meta,kind)
   assert pcm(out,folder,'output-pcm')==refpcm and decoded(out,pixels,folder,'output-video')==refvideo
   sealed=scan(output)
   for c in sealed['clusters']:
    assert not c['unknownSize'];assert all(tag not in (0xbf,0xa7,0xab) for tag,_,_,_ in children(output,c['body'],c['end']))
   assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
   row.update(profile=case[0],nominalDefaultDurationPreserved=(scan(output)['defaultDurationNs']==layout['defaultDurationNs']) if nominal else None,frames=frames,duration100ns=end,exactPacketsAndClocks=True,exactPcm=True,decodedFramesIdentical=True,sourceUnchanged=True,nextPtsLookahead=True,lastClusterHeldUntilConfirmedEof=True,partials=partials)
   rows.append(row);(report/'progress.json').write_text(json.dumps(dict(count=len(rows))))
   if control is None:control=source,raw,frames,end,profile
  if nominal:
   for hint in (1000000,1000000000):
    folder=root/('hint-'+str(hint));src,pixels,count,endpoint,meta,pf=create_media(folder,cases[0],nominal_duration=hint)
    before=hashlib.sha256(src.read_bytes()).hexdigest()
    row,output=serve(worker,src,mode=MODE,format_hint='webm',video_duration=endpoint,audio_samples=pf[5],growing=False)
    assert row['exitCode']==0 and row['result']['packets']==count+(pf[5]+1271)//960
    target=folder/'canonical.webm';target.write_bytes(output);actual=probe(target,folder,'hint-output')
    for kind in ('video','audio'):assert packets(actual,kind)==packets(meta,kind)
    assert decoded(src,pixels,folder,'hint-source-video')==decoded(target,pixels,folder,'hint-output-video')
    assert pcm(src,folder,'hint-source-pcm')==pcm(target,folder,'hint-output-pcm')
    assert scan(output)['defaultDurationNs']==hint and hashlib.sha256(src.read_bytes()).hexdigest()==before
    row.update(profile='nominal-hint-'+str(hint),frames=count,duration100ns=endpoint,nominalDefaultDurationNs=hint,nominalHintIgnoredForIntervals=True,nominalDefaultDurationPreserved=True,externalVideoCountProvided=False,exactPacketsAndClocks=True,exactPcm=True,decodedFramesIdentical=True,sourceUnchanged=True)
    rows.append(row)
  source,raw,frames,end,profile=control
  def attempt(label,value=raw,count=None if auto_count else frames,endpoint=end,fault=None,samples=profile[5]):
   p=root/(label+'.webm');p.write_bytes(value);row,_=serve(worker,p,mode=MODE,format_hint='webm',video_samples=count,video_duration=endpoint,audio_samples=samples,growing=bool(fault),fault=fault)
   if fault=='kill-wait':assert row['terminationSeconds']<2
   else:assert row['exitCode']==1 and row['result']['code']!='data',(label,row)
   row['profile']='reject-'+label;rows.append(row)
  layout=scan(raw);track=configuration(raw)[1]['number'];v=[b for b in layout['blocks'] if b['track']==track]
  for label,delta in [('duplicate',-40),('regression',-80),('long-gap',2000)]:
   b=bytearray(raw);at=v[1]['body']+width(b[v[1]['body']]);ticks=int.from_bytes(b[at:at+2],'big',signed=True)+delta;b[at:at+2]=ticks.to_bytes(2,'big',signed=True);attempt(label,bytes(b))
  for label,count,endpoint in [('short-count',frames-1,end),('long-count',frames+1,end),('zero-count',0,end),('short-end',None if auto_count else frames,int(v[-1]['ticks'])*10000),('long-end',None if auto_count else frames,end+20000000),('non-ms-end',None if auto_count else frames,end+1),('missing-end',None if auto_count else frames,None)]:attempt(label,count=count,endpoint=endpoint)
  # Explicit-duration BlockGroups are not silently admitted by the new source mode.
  if nominal:
   from progressive_simple_vfr_preflight import head_without_default
   from progressive_simple_vfr_read_budget_test import with_default
   attempt('missing-nominal',head_without_default(raw))
   for value in (0,999999,1000000001):attempt('nominal-'+str(value),with_default(head_without_default(raw),value))
   attempt('duplicate-nominal',with_default(raw,20000000))
  from progressive_webm_vfr_preflight import explicit_durations
  from progressive_webm_vfr_fixtures import media as explicit_media,CASES as EXPLICIT_CASES
  explicit,_,_,_,_,_=explicit_media(root/'explicit-control',EXPLICIT_CASES[0 if nominal else 1]);attempt('video-block-group',explicit.read_bytes())
  if auto_count:attempt('derived-count-shared-packet-budget',samples=49999*960-312)
  for cut in (1,layout['headBytes']-1,len(raw)-1):attempt('truncated-'+str(cut),raw[:cut])
  for fault in ('unconfirmed-eof','integrity','cancel-wait','cancel-write','kill-wait'):attempt(fault,fault=fault)
 result=dict(result='PASS',mode=MODE,count=len(rows),rows=rows,workerSha256=hashlib.sha256(Path(worker).read_bytes()).hexdigest(),testSourceSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest());(report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--nominal',action='store_true');p.add_argument('--auto-count',action='store_true');p.add_argument('--worker',type=Path,required=True);p.add_argument('--report-directory',required=True);a=p.parse_args();r=run(a.worker.resolve(),a.report_directory,a.auto_count,a.nominal);print('PASS',r['count'])
