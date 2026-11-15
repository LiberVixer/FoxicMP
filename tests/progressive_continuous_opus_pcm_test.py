"""Actual Qt Player or Classic internal graph PCM, never a system audio device."""
import argparse,json,tempfile,subprocess,hashlib
from pathlib import Path
from array import array
from progressive_webm_opus_remux_preflight import fixture,PROFILES,ok
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot

def run(runner,worker,edition,report,ntsc=False,cfr=False,vfr=False,simple=False,auto_count=False,nominal=False,derived_audio=False,growing=False):
 auto_count=auto_count or nominal or derived_audio
 simple=simple or auto_count
 vfr=vfr or simple
 options=dict(rate="30000/1001") if ntsc else {}
 report=Path(report);report.mkdir(parents=True,exist_ok=True);rows=[]
 from progressive_webm_cfr_fixtures import CASES,media
 from progressive_webm_vfr_fixtures import CASES as VFR_CASES,media as vfr_media
 from progressive_webm_simple_vfr_fixtures import CASES as SIMPLE_CASES,media as simple_media
 if nominal:
  from progressive_webm_nominal_vfr_fixtures import CASES as SIMPLE_CASES,media as simple_media
 if derived_audio:
  from progressive_webm_derived_av_fixtures import CASES as SIMPLE_CASES,media as simple_media
 if simple:VFR_CASES,vfr_media=SIMPLE_CASES,simple_media
 if growing:
  assert derived_audio
  VFR_CASES=[c for c in VFR_CASES if c[1][0] in ('mono-full','p2-stereo-zero-trim','vfr-mono')]
 for case in (VFR_CASES if vfr else CASES if cfr else (PROFILES[0],PROFILES[1],PROFILES[4])):
  with tempfile.TemporaryDirectory(prefix='fox-opus-pcm-') as t:
   root=Path(t)
   if vfr:
    name=case[0];original,_,frames,duration,_,profile=vfr_media(root/'fixture',case)
   elif cfr:
    name,n,d,profile=case;original,_,frames,duration,_=media(root/'fixture',case)
   else:
    profile=case;name=profile[0];original,_=fixture(root/'fixture',profile,**options);duration=240240000 if ntsc else 240000000
   raw=original.read_bytes();path=root/'video.part';model=snapshot(path,raw,complete=True);model['timeline']['duration']=str(duration);model['tracks'][0].update(codecHint='vp9',containerHint='webm');import copy
   sound=copy.deepcopy(model['tracks'][0]);sound.update(id='sound',kind='audio',codecHint='opus');model['tracks'].append(sound);model=protect_snapshot(model,root);manifest=root/'initial.json';complete_manifest=root/'complete.json';complete_manifest.write_text(json.dumps(model));output=root/'actual.pcm'
   if growing:
    from fractions import Fraction
    from progressive_webm_opus_remux_preflight import probe
    meta=probe(original,root,'growing-prefix-probe');clock=Fraction(next(s for s in meta['streams'] if s['codec_type']=='video')['time_base'])
    cut=next(int(p['pos'])+13 for p in meta['packets'] if p['codec_type']=='video' and int(p['pts'])*clock>=8)
    model=copy.deepcopy(model);model['writer']['state']=model['resources'][0]['state']='downloading'
    for track in model['tracks']:track['state']='downloading'
    model['resources'][0]['ranges']=[dict(offset='0',length=str(cut))]
    model['resources'][0].pop('integrity',None)
    model=protect_snapshot(model,root)
    complete=copy.deepcopy(model);complete['revision']='2';complete['writer']['state']=complete['resources'][0]['state']='complete'
    for track in complete['tracks']:track['state']='complete'
    complete['resources'][0]['ranges']=[dict(offset='0',length=str(len(raw)))];complete=protect_snapshot(complete,root,previous=model);complete_manifest.write_text(json.dumps(complete))
   manifest.write_text(json.dumps(model))
   p=subprocess.run([str(runner),str(manifest),str(worker),str(root/'registry'),str(root/'cache'),str(output),str(profile[2]),str(0 if derived_audio else profile[5])]+(["derived-av" if derived_audio else "nominal-vfr" if nominal else "auto-vfr" if auto_count else "simple-vfr" if simple else "vfr",str(0 if auto_count else frames),str(duration)] if vfr else ["cfr",str(n),str(d)] if cfr else ["ntsc"] if ntsc else [])+([str(complete_manifest)] if growing else []),capture_output=True,timeout=60)
   
   if p.returncode:
    import shutil
    shutil.copytree(root,report/(name+'-failure'),dirs_exist_ok=True)
   (report/(name+'.exit')).write_text(str(p.returncode));(report/(name+'.stdout')).write_bytes(p.stdout);(report/(name+'.stderr')).write_bytes(p.stderr);assert p.returncode==0,(profile[0],p.returncode,p.stdout,p.stderr)
   consumer=json.loads(p.stdout.strip().splitlines()[-1])
   if growing:assert consumer['growingDecoder'] and consumer['partialPcmSamples']>=48000 and consumer['partialPcmSamples']<profile[5],consumer
   fmt='s16le' if edition=='qt' else 'f32le';reference=ok(['ffmpeg','-v','error','-threads','1','-i',original,'-map','0:a:0','-c:a','pcm_s16le' if edition=='qt' else 'pcm_f32le','-f',fmt,'-'],report,name+'-reference')
   (report/(name+'-reference.stdout')).unlink();actual=output.read_bytes();stride=profile[2]*(2 if edition=='qt' else 4);assert len(actual)==len(reference)==profile[5]*stride,(profile[0],len(actual)//stride,len(reference)//stride)
   left,right=array('h' if edition=='qt' else 'f'),array('h' if edition=='qt' else 'f');left.frombytes(actual);right.frombytes(reference);maximum=max(abs(a-b) for a,b in zip(left,right));limit=2 if edition=='qt' else 2e-6;
   if maximum>limit:
    (report/(name+'-actual.pcm')).write_bytes(actual);(report/(name+'-reference.pcm')).write_bytes(reference)
   assert maximum<=limit,(profile[0],maximum)
   assert not list((root/'cache').glob('remux-*')) and path.read_bytes()==raw
   rows.append(dict(profile=name,exitCode=p.returncode,samples=profile[5],channels=profile[2],maxAbsolutePcmError=maximum,allowedError=limit,format=fmt,exactSampleCount=True,audioDeviceOpened=False,nativeConsumer=consumer,pcmSha256=hashlib.sha256(actual).hexdigest()))
 result=dict(result='PASS',count=len(rows),edition=edition,rows=rows,runnerSha256=hashlib.sha256(Path(runner).read_bytes()).hexdigest());(report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--growing',action='store_true');p.add_argument('--derived-av',action='store_true');p.add_argument('--nominal',action='store_true');p.add_argument('--auto-count',action='store_true');p.add_argument('--simple-vfr',action='store_true');p.add_argument('--vfr',action='store_true');p.add_argument('--cfr',action='store_true');p.add_argument('--ntsc',action='store_true');p.add_argument('--runner',type=Path,required=True);p.add_argument('--worker',type=Path,required=True);p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--report-directory',required=True);a=p.parse_args();print(json.dumps(run(a.runner.resolve(),a.worker.resolve(),a.edition,a.report_directory,a.ntsc,a.cfr,a.vfr,a.simple_vfr,a.auto_count,a.nominal,a.derived_av,a.growing)))
