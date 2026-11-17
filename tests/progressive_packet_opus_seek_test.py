"""Exact sample counts after decoder seek; no hardware or ordinary-main claim."""
import argparse,copy,hashlib,json,subprocess,tempfile
from pathlib import Path
from array import array
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_webm_packet_opus_fixtures import CASES,media

def run(runner,worker,edition,report):
 report.mkdir(parents=True,exist_ok=True);rows=[]
 # Cover a short initial packet, largest tail trim and changing packet lengths.
 for name in ['fixed-2.5-finite','fixed-120-unknown','mixed-stereo-finite','mixed-short-finite']:
  case=next((c for c in CASES if c[0]==name),None)
  if case is None:continue
  with tempfile.TemporaryDirectory(prefix='fox-packet-seek-') as t:
   root=Path(t);source,_,_,end,_,profile=media(root/'fixture',case);raw=source.read_bytes();path=root/'video.part'
   model=snapshot(path,raw,complete=True);model['timeline']['duration']=str(end);model['tracks'][0].update(codecHint='vp9',containerHint='webm');sound=copy.deepcopy(model['tracks'][0]);sound.update(id='sound',kind='audio',codecHint='opus');model['tracks'].append(sound);model=protect_snapshot(model,root);manifest=root/'initial.json';manifest.write_text(json.dumps(model))
   fmt,code,typ,stride=('s16le','pcm_s16le','h',2*profile[2]) if edition=='qt' else ('f32le','pcm_f32le','f',4*profile[2])
   q=subprocess.run(['ffmpeg','-v','error','-threads','1','-i',str(source),'-map','0:a:0','-threads','1','-c:a',code,'-f',fmt,'-'],capture_output=True,timeout=30);assert q.returncode==0
   reference=q.stdout;assert len(reference)==profile[5]*stride
   for target in [1,144017,profile[5]-17]:
    output=root/'actual.pcm'
    cmd=[str(runner),str(manifest),str(worker),str(root/'registry'),str(root/'cache'),str(output),str(profile[2]),'0','packet-opus','0',str(end),'--seek-sample',str(target)]
    p=subprocess.run(cmd,capture_output=True,timeout=60);label=name+'-'+str(target)
    (report/(label+'.stdout')).write_bytes(p.stdout);(report/(label+'.stderr')).write_bytes(p.stderr);(report/(label+'.exit')).write_text(str(p.returncode)+'\n');assert p.returncode==0,(label,p.returncode,p.stderr)
    actual=output.read_bytes();expected=reference[target*stride:]
    if len(actual)!=len(expected):
     import shutil;shutil.copytree(root,report/(label+'-failure'),dirs_exist_ok=True)
    assert len(actual)==len(expected),(label,len(actual)//stride,len(expected)//stride)
    a,b=array(typ),array(typ);a.frombytes(actual);b.frombytes(expected);error=max(abs(x-y) for x,y in zip(a,b))
    # Opus decoder history after bounded preroll need not be bit-identical to a
    # decode from origin. Count is exact; record waveform error independently.
    assert error<=(128 if edition=='qt' else 0.004),(label,error)
    assert not list((root/'cache').glob('remux-*')) and path.read_bytes()==raw
    rows.append(dict(profile=name,targetSample=target,samples=len(actual)//stride,expectedSamples=profile[5]-target,exactSampleCount=True,maxAbsolutePcmError=error,exitCode=p.returncode,format=fmt,audioDeviceOpened=False))
 result=dict(result='PASS',count=len(rows),edition=edition,rows=rows,runnerSha256=hashlib.sha256(runner.read_bytes()).hexdigest(),workerSha256=hashlib.sha256(worker.read_bytes()).hexdigest(),testSourceSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),physicalAvSyncMeasured=False)
 (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);p.add_argument('--worker',type=Path,required=True);p.add_argument('--edition',choices=['qt','classic'],required=True);p.add_argument('--report-directory',type=Path,required=True);a=p.parse_args();run(a.runner.resolve(),a.worker.resolve(),a.edition,a.report_directory.resolve())
