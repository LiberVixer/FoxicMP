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

def run(worker,report):
 report=Path(report);report.mkdir(parents=True,exist_ok=True);rows=[]
 with tempfile.TemporaryDirectory(prefix='fox-simple-vfr-worker-') as t:
  root=Path(t);control=None
  for case in CASES:
   folder=root/case[0];source,pixels,frames,end,meta,profile=media(folder,case);raw=source.read_bytes();digest=hashlib.sha256(raw).hexdigest();layout=scan(raw)
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
   row,output=serve(worker,source,mode=MODE,format_hint='webm',video_samples=frames,video_duration=end,audio_samples=profile[5],byte_cuts=cuts,short_reads=True,on_wait=checkpoint)
   assert row['exitCode']==0 and row['result']['code']=='data' and row['firstCompleteFragmentBeforeSourceComplete'] and len(partials)>=3,row
   out=folder/'output.webm';out.write_bytes(output);actual=probe(out,folder,'output-probe')
   for kind in ('video','audio'):assert packets(actual,kind)==packets(meta,kind)
   assert pcm(out,folder,'output-pcm')==refpcm and decoded(out,pixels,folder,'output-video')==refvideo
   sealed=scan(output)
   for c in sealed['clusters']:
    assert not c['unknownSize'];assert all(tag not in (0xbf,0xa7,0xab) for tag,_,_,_ in children(output,c['body'],c['end']))
   assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
   row.update(profile=case[0],frames=frames,duration100ns=end,exactPacketsAndClocks=True,exactPcm=True,decodedFramesIdentical=True,sourceUnchanged=True,nextPtsLookahead=True,lastClusterHeldUntilConfirmedEof=True,partials=partials)
   rows.append(row);(report/'progress.json').write_text(json.dumps(dict(count=len(rows))))
   if control is None:control=source,raw,frames,end,profile
  source,raw,frames,end,profile=control
  def attempt(label,value=raw,count=frames,endpoint=end,fault=None):
   p=root/(label+'.webm');p.write_bytes(value);row,_=serve(worker,p,mode=MODE,format_hint='webm',video_samples=count,video_duration=endpoint,audio_samples=profile[5],growing=bool(fault),fault=fault)
   if fault=='kill-wait':assert row['terminationSeconds']<2
   else:assert row['exitCode']==1 and row['result']['code']!='data',(label,row)
   row['profile']='reject-'+label;rows.append(row)
  layout=scan(raw);track=configuration(raw)[1]['number'];v=[b for b in layout['blocks'] if b['track']==track]
  for label,delta in [('duplicate',-40),('regression',-80),('long-gap',2000)]:
   b=bytearray(raw);at=v[1]['body']+width(b[v[1]['body']]);ticks=int.from_bytes(b[at:at+2],'big',signed=True)+delta;b[at:at+2]=ticks.to_bytes(2,'big',signed=True);attempt(label,bytes(b))
  for label,count,endpoint in [('short-count',frames-1,end),('long-count',frames+1,end),('zero-count',0,end),('short-end',frames,int(v[-1]['ticks'])*10000),('long-end',frames,end+20000000),('non-ms-end',frames,end+1),('missing-end',frames,None)]:attempt(label,count=count,endpoint=endpoint)
  # Explicit-duration BlockGroups are not silently admitted by the new source mode.
  from progressive_webm_vfr_preflight import explicit_durations
  from progressive_webm_vfr_fixtures import media as explicit_media,CASES as EXPLICIT_CASES
  explicit,_,_,_,_,_=explicit_media(root/'explicit-control',EXPLICIT_CASES[1]);attempt('video-block-group',explicit.read_bytes())
  for cut in (1,layout['headBytes']-1,len(raw)-1):attempt('truncated-'+str(cut),raw[:cut])
  for fault in ('unconfirmed-eof','integrity','cancel-wait','cancel-write','kill-wait'):attempt(fault,fault=fault)
 result=dict(result='PASS',mode=MODE,count=len(rows),rows=rows,workerSha256=hashlib.sha256(Path(worker).read_bytes()).hexdigest(),testSourceSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest());(report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--worker',type=Path,required=True);p.add_argument('--report-directory',required=True);a=p.parse_args();r=run(a.worker.resolve(),a.report_directory);print('PASS',r['count'])
