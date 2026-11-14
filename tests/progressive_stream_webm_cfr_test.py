"""Explicit CFR rate binding, all clocks, sealed prefixes and negative controls."""
import argparse,json,tempfile,hashlib
from pathlib import Path
from progressive_webm_cfr_fixtures import CASES,media
from progressive_webm_opus_remux_preflight import probe,pcm,packets,configuration
from progressive_webm_remux_preflight import scan,unknown_clusters,decoded
from progressive_stream_remux_worker_test import serve
from progressive_webm_fixtures import locate,width
MODE='continuous-webm-vp9-opus-cfr-v1'
def run(worker,report):
 report=Path(report);report.mkdir(parents=True,exist_ok=True);rows=[]
 with tempfile.TemporaryDirectory(prefix='fox-cfr-worker-') as t:
  root=Path(t);control=None
  for case in CASES:
   name,n,d,profile=case;folder=root/name;source,pixels,frames,duration,ref=media(folder,case);raw=source.read_bytes();audio=pcm(source,folder,'pcm-source');images=decoded(source,pixels,folder,'video-source');rate=dict(numerator=n,denominator=d)
   for unknown in (False,True):
    path=folder/('unknown.webm' if unknown else 'finite.webm');path.write_bytes(unknown_clusters(raw,scan(raw)) if unknown else raw);before=hashlib.sha256(path.read_bytes()).hexdigest();partials=[]
    def checkpoint(data,event):
     if not event['mediaReady']:return
     p=folder/'partial.webm';p.write_bytes(data);result=probe(p,folder,'probe-partial')
     for kind in ('video','audio'):assert packets(result,kind)==packets(ref,kind)[:len(packets(result,kind))]
     actual=pcm(p,folder,'pcm-partial');assert actual==audio[:len(actual)];partials.append(dict(bytes=len(data),audioSamples=len(actual)//(4*profile[2])))
    row,out=serve(worker,path,mode=MODE,format_hint='webm',video_samples=frames,audio_samples=profile[5],video_rate=rate,on_wait=checkpoint,short_reads=True)
    assert row['exitCode']==0 and row['result']['code']=='data' and row['firstCompleteFragmentBeforeSourceComplete'] and len(partials)>=3,row
    sealed=folder/'sealed.webm';sealed.write_bytes(out);result=probe(sealed,folder,'probe-sealed')
    for kind in ('video','audio'):assert packets(ref,kind)==packets(result,kind)
    assert pcm(sealed,folder,'pcm-sealed')==audio and decoded(sealed,pixels,folder,'video-sealed')==images and hashlib.sha256(path.read_bytes()).hexdigest()==before
    row.update(profile=name+('-unknown' if unknown else '-finite'),rate=rate,frames=frames,duration100ns=duration,exactPcm=True,exactPacketsAndClocks=True,decodedFramesIdentical=True,sourceUnchanged=True,partials=partials);rows.append(row)
    if control is None and unknown:control=path,frames,profile[5],rate
  source,frames,samples,rate=control
  for label,bad in [('missing',None),('bool',dict(numerator=True,denominator=1)),('float',dict(numerator=24000.,denominator=1001)),('zero',dict(numerator=0,denominator=1)),('unreduced-alias',dict(numerator=48000,denominator=2002)),('unsupported-rate',dict(numerator=27,denominator=1)),('extra',dict(numerator=24000,denominator=1001,extra=1)),('wrong-admitted-rate',dict(numerator=25,denominator=1))]:
   row,_=serve(worker,source,mode=MODE,format_hint='webm',video_samples=frames,audio_samples=samples,video_rate=bad,growing=False);assert row['exitCode']==1 and row['result']['code']!='data';row['profile']='reject-rate-'+label;rows.append(row)
  raw=source.read_bytes();layout=scan(raw);video=[b for b in layout['blocks'] if b['track']==configuration(raw)[1]['number']]
  changed=bytearray(raw);at=video[2]['body']+width(raw[video[2]['body']])+1;changed[at]=(changed[at]+4)%256
  bad=root/'off-grid.webm';bad.write_bytes(changed)
  lost=root/'lost-last-frame.webm';last=video[-1];lost.write_bytes(raw[:last['offset']]+raw[last['end']:])
  _,a,z=locate(raw,0xe7);huge=root/'overflow.webm';huge.write_bytes(raw[:_]+bytes.fromhex('e788')+(2**63-1).to_bytes(8,'big')+raw[z:])
  for label,path,count,reason in [('off-grid',bad,frames,'unsupported-stream-webm-clock'),('lost-last-frame',lost,frames,'incomplete-stream-webm-opus-samples'),('overflow',huge,frames,'unsupported-stream-webm-clock'),('wrong-frame-count',source,frames-1,'unsupported-stream-webm-clock')]:
   row,_=serve(worker,path,mode=MODE,format_hint='webm',video_samples=count,audio_samples=samples,video_rate=rate,growing=False);assert row['exitCode']==1 and row['result']['code']==reason,(label,row);row['profile']='reject-'+label;rows.append(row)
  for fault in ('cancel-wait','cancel-write','integrity','unconfirmed-eof','kill-wait'):
   row,_=serve(worker,source,mode=MODE,format_hint='webm',video_samples=frames,audio_samples=samples,video_rate=rate,fault=fault);assert row['exitCode']!=0;row['profile']=fault;rows.append(row)
 result=dict(result='PASS',count=len(rows),rows=rows,workerSha256=hashlib.sha256(Path(worker).read_bytes()).hexdigest(),ordinaryMainTested=False)
 (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('CFR worker PASS',len(rows));return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--worker',type=Path,required=True);p.add_argument('--report-directory',required=True);a=p.parse_args();run(a.worker.resolve(),a.report_directory)
