"""Real single audio WebM; original packets/headers are never remuxed by the fixture."""
import json,subprocess,uuid
from fractions import Fraction
from pathlib import Path

def fixture(root,profile,seconds=20):
    root=Path(root);root.mkdir(parents=True,exist_ok=True);codec='vorbis' if '-vorbis-' in profile else 'opus';channels=2 if profile.endswith('stereo') else 1
    original=root/'original.webm'
    subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i',f'aevalsrc=sin(2*PI*880*t)'+('|sin(2*PI*440*t)' if channels==2 else '')+f':s=48000:d={seconds}','-vn','-c:a','lib'+codec,'-ac',str(channels),'-b:a','96k','-cluster_time_limit','1000','-live','1',str(original)],check=True,capture_output=True)
    raw=original.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(original)]));assert len(probe['streams'])==1
    duration=max(Fraction(p['pts_time'])+Fraction(p.get('duration_time','0')) for p in probe['packets'])
    model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(int(duration*10000000)+1),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=None if channels==1 else str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind='audio',required=True,codecHint=codec,containerHint='webm',initializationReady=True,state='downloading',source=dict(resourceId='media'))],metadata=dict(revision='1',title='Standalone '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))]))
    return model,raw,original,probe
