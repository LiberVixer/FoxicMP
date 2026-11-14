"""Managed structural WebM/Opus sealer: packets, PCM, growing bounds and rejection."""
import argparse,hashlib,json,tempfile
from pathlib import Path
from progressive_webm_opus_remux_preflight import fixture,PROFILES,probe,pcm,packets,configuration,children
from progressive_webm_remux_preflight import scan,unknown_clusters,decoded
from progressive_stream_remux_worker_test import serve
from progressive_webm_fixtures import locate,width
MODE='continuous-webm-vp9-opus-v1'
def run(worker,report):
 report=Path(report);report.mkdir(parents=True,exist_ok=True);rows=[]
 with tempfile.TemporaryDirectory(prefix='fox-opus-worker-') as t:
  root=Path(t);sources=[]
  for profile in (PROFILES[0],PROFILES[1],PROFILES[4]):
   folder=root/profile[0];source,pixels=fixture(folder,profile);raw=source.read_bytes();ref=probe(source,folder,'probe-source');reference_pcm=pcm(source,folder,'pcm-source');images=decoded(source,pixels,folder,"video-source")
   for unknown in (False,True):
    path=folder/('unknown.webm' if unknown else 'finite.webm');path.write_bytes(unknown_clusters(raw,scan(raw)) if unknown else raw);digest=hashlib.sha256(path.read_bytes()).hexdigest();partials=[]
    def checkpoint(data,event):
     if not event['mediaReady']:return
     target=folder/'partial.webm';target.write_bytes(data);out=probe(target,folder,'probe-partial')
     for kind in ('video','audio'):assert packets(out,kind)==packets(ref,kind)[:len(packets(out,kind))]
     actual=pcm(target,folder,'pcm-partial');assert actual==reference_pcm[:len(actual)]
     partials.append(dict(bytes=len(data),audioSamples=len(actual)//(4*profile[2]),sourceBytes=event['available'],originalIncomplete=True))
    row,out=serve(worker,path,mode=MODE,format_hint='webm',video_samples=600,audio_samples=profile[5],short_reads=True,on_wait=checkpoint)
    assert row['exitCode']==0 and row['result']['code']=='data' and row['firstCompleteFragmentBeforeSourceComplete'] and len(partials)>=3,row
    sealed=folder/'sealed.webm';sealed.write_bytes(out);result=probe(sealed,folder,'probe-sealed')
    for kind in ('video','audio'):assert packets(ref,kind)==packets(result,kind)
    assert pcm(sealed,folder,'pcm-sealed')==reference_pcm and decoded(sealed,pixels,folder,"video-sealed")==images and len(reference_pcm)//(4*profile[2])==profile[5]
    assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
    row.update(profile=profile[0]+('-unknown' if unknown else '-finite'),exactPcm=True,exactPayloadsAndClocks=True,decodedFramesIdentical=True,sourceUnchanged=True,partials=partials);rows.append(row);sources.append((path,profile[5]))
  source,samples=sources[1];raw=source.read_bytes();layout=scan(raw);audio=configuration(raw)[2];audio_blocks=[b for b in layout['blocks'] if b['track']==audio['number']]
  invalid=[]
  def mutate(label,offset,value):
   b=bytearray(raw);b[offset]=value;p=root/(label+'.webm');p.write_bytes(b);invalid.append((label,p,samples,600))
  _,b,e=locate(raw,0x4282);mutate('doctype',b,ord('x'))
  _,b,e=locate(raw,0x56aa);mutate('delay',e-1,raw[e-1]^1)
  _,b,e=locate(raw,0x63a2);mutate('gain',b+16,1)
  mutate('lacing',audio_blocks[0]['body']+width(raw[audio_blocks[0]['body']])+2,0x82)
  mutate('duration-10ms',audio_blocks[0]['payload'],raw[audio_blocks[0]['payload']]^8)
  mutate('audio-gap',audio_blocks[3]['body']+width(raw[audio_blocks[3]['body']])+1,100)
  a,b,e=locate(raw,0xe7);huge=root/'overflow-clock.webm';huge.write_bytes(raw[:a]+bytes.fromhex('e788')+(2**63-1).to_bytes(8,'big')+raw[e:]);invalid.append(('overflow-clock',huge,samples,600))
  truncated=root/'truncated.webm';truncated.write_bytes(raw[:-1]);invalid.append(('truncated',truncated,samples,600))
  empty=root/'empty-terminal-cluster.webm';empty.write_bytes(raw+bytes.fromhex('1f43b67501ffffffffffffff'));invalid.append(('empty-terminal-cluster',empty,samples,600))
  invalid.extend([('wrong-audio-samples',source,samples-1,600),('wrong-video-samples',source,samples,599),('one-packet-count',source,1,600)])
  for label,profile in [('actual-10ms',PROFILES[2]),('actual-5ms',PROFILES[3]),('actual-one-packet',PROFILES[5])]:
   p,_=fixture(root/label,profile);invalid.append((label,p,profile[5],600))
  for label,path,count,frames in invalid:
   row,_=serve(worker,path,mode=MODE,format_hint='webm',video_samples=frames,audio_samples=count,growing=False);assert row['exitCode']==1 and row['result']['code']!='data',(label,row)
   row['profile']='reject-'+label;rows.append(row)
  for fault in ('cancel-wait','cancel-write','integrity','unconfirmed-eof','kill-wait'):
   row,_=serve(worker,source,mode=MODE,format_hint='webm',video_samples=600,audio_samples=samples,fault=fault);assert row['exitCode']!=0;row['profile']=fault;rows.append(row)
 result=dict(result='PASS',count=len(rows),rows=rows,workerSha256=hashlib.sha256(Path(worker).read_bytes()).hexdigest(),ordinaryPlayerTested=False)
 (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--worker',required=True);p.add_argument('--report-directory',required=True);a=p.parse_args();print(json.dumps(run(Path(a.worker).resolve(),a.report_directory)))
