"""Regression: bounded confirmed-prefix prefetch, not one IPC per EBML field."""
import argparse,json,tempfile
from pathlib import Path
from progressive_stream_remux_worker_test import serve
from progressive_webm_simple_vfr_fixtures import CASES,media
from progressive_webm_fixtures import element
from progressive_webm_opus_remux_preflight import children
from progressive_webm_vfr_preflight import ebml,unsigned
from progressive_webm_remux_preflight import scan
MODE='continuous-webm-vp9-opus-simple-vfr-v1'
def with_default(raw,value):
 layout=scan(raw);_,_,segment=element(raw,0);_,start,_=element(raw,segment)
 def rewrite(a,z):
  output=bytearray()
  for tag,at,b,end in children(raw,a,z):
   if tag in (0x1654ae6b,0xae):
    body=rewrite(b,end)
    if tag==0xae:
     kind=next(int.from_bytes(raw[x:y],'big') for t,_,x,y in children(raw,b,end) if t==0x83)
     if kind==1:body+=unsigned(0x23e383,value)
    output+=ebml(tag,body)
   else:output+=raw[at:end]
  return bytes(output)
 return raw[:start]+rewrite(start,layout['headBytes'])+raw[layout['headBytes']:]
def run(worker,report,auto_count=False):
 global MODE
 MODE='continuous-webm-vp9-opus-simple-vfr-auto-count-v1' if auto_count else 'continuous-webm-vp9-opus-simple-vfr-v1'
 report=Path(report);report.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='fox-simple-read-budget-') as t:
  source,_,frames,end,_,profile=media(Path(t)/'fixture',CASES[0])
  row,_=serve(worker,source,mode=MODE,format_hint='webm',video_samples=None if auto_count else frames,video_duration=end,audio_samples=profile[5],growing=False)
  assert row['exitCode']==0 and row['result']['code']=='data',row
  bound=(source.stat().st_size+65535)//65536+1
  assert row['reads']<=bound,(row['reads'],bound)
  rejects=[]
  for duration in (0,40000000):
   path=source.with_name('default-'+str(duration)+'.webm');path.write_bytes(with_default(source.read_bytes(),duration))
   rejected,_=serve(worker,path,mode=MODE,format_hint='webm',video_samples=None if auto_count else frames,video_duration=end,audio_samples=profile[5],growing=False)
   assert rejected['exitCode']==1 and rejected['result']['code'] in ('unsupported-stream-webm-default-duration','unsupported-stream-webm-configuration'),rejected
   rejects.append(dict(defaultDuration=duration,exitCode=rejected['exitCode'],code=rejected['result']['code']))
  result=dict(result='PASS',reads=row['reads'],maxReads=bound,inputBytes=source.stat().st_size,sourceComplete=True,worker=row,defaultDurationRejections=rejects)
  (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--auto-count',action='store_true');p.add_argument('--worker',type=Path,required=True);p.add_argument('--report-directory',required=True);a=p.parse_args();print(json.dumps(run(a.worker.resolve(),a.report_directory,a.auto_count)))
