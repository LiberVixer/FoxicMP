"""Actual Qt Player or Classic internal graph PCM, never a system audio device."""
import argparse,json,tempfile,subprocess,hashlib
from pathlib import Path
from array import array
from progressive_webm_opus_remux_preflight import fixture,PROFILES,ok
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot

def run(runner,worker,edition,report):
 report=Path(report);report.mkdir(parents=True,exist_ok=True);rows=[]
 for profile in (PROFILES[0],PROFILES[1],PROFILES[4]):
  with tempfile.TemporaryDirectory(prefix='fox-opus-pcm-') as t:
   root=Path(t);original,_=fixture(root/'fixture',profile);raw=original.read_bytes();path=root/'video.part';model=snapshot(path,raw,complete=True);model['timeline']['duration']='240000000';model['tracks'][0].update(codecHint='vp9',containerHint='webm');import copy
   sound=copy.deepcopy(model['tracks'][0]);sound.update(id='sound',kind='audio',codecHint='opus');model['tracks'].append(sound);model=protect_snapshot(model,root);manifest=root/'complete.json';manifest.write_text(json.dumps(model));output=root/'actual.pcm'
   p=subprocess.run([str(runner),str(manifest),str(worker),str(root/'registry'),str(root/'cache'),str(output),str(profile[2]),str(profile[5])],capture_output=True,timeout=60)
   
   if p.returncode:
    import shutil
    shutil.copytree(root,report/(profile[0]+'-failure'),dirs_exist_ok=True)
   (report/(profile[0]+'.exit')).write_text(str(p.returncode));(report/(profile[0]+'.stdout')).write_bytes(p.stdout);(report/(profile[0]+'.stderr')).write_bytes(p.stderr);assert p.returncode==0,(profile[0],p.returncode,p.stdout,p.stderr)
   fmt='s16le' if edition=='qt' else 'f32le';reference=ok(['ffmpeg','-v','error','-threads','1','-i',original,'-map','0:a:0','-c:a','pcm_s16le' if edition=='qt' else 'pcm_f32le','-f',fmt,'-'],report,profile[0]+'-reference')
   (report/(profile[0]+'-reference.stdout')).unlink();actual=output.read_bytes();stride=profile[2]*(2 if edition=='qt' else 4);assert len(actual)==len(reference)==profile[5]*stride,(profile[0],len(actual)//stride,len(reference)//stride)
   left,right=array('h' if edition=='qt' else 'f'),array('h' if edition=='qt' else 'f');left.frombytes(actual);right.frombytes(reference);maximum=max(abs(a-b) for a,b in zip(left,right));limit=2 if edition=='qt' else 2e-6;
   if maximum>limit:
    (report/(profile[0]+'-actual.pcm')).write_bytes(actual);(report/(profile[0]+'-reference.pcm')).write_bytes(reference)
   assert maximum<=limit,(profile[0],maximum)
   assert not list((root/'cache').glob('remux-*')) and path.read_bytes()==raw
   rows.append(dict(profile=profile[0],exitCode=p.returncode,samples=profile[5],channels=profile[2],maxAbsolutePcmError=maximum,allowedError=limit,format=fmt,exactSampleCount=True,audioDeviceOpened=False,nativeConsumer=json.loads(p.stdout.strip().splitlines()[-1]),pcmSha256=hashlib.sha256(actual).hexdigest()))
 result=dict(result='PASS',count=len(rows),edition=edition,rows=rows,runnerSha256=hashlib.sha256(Path(runner).read_bytes()).hexdigest());(report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);p.add_argument('--worker',type=Path,required=True);p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--report-directory',required=True);a=p.parse_args();print(json.dumps(run(a.runner.resolve(),a.worker.resolve(),a.edition,a.report_directory)))
