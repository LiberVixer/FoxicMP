"""One HEVC/AV1/VP9 fMP4 video stream; real encoded frames and original fragments."""
import json,subprocess,uuid
from fractions import Fraction
from pathlib import Path
from progressive_mux_mp4_fixture import boxes

def fixture(root,profile,seconds=20):
    root=Path(root);root.mkdir(parents=True,exist_ok=True);codec=profile.split('-')[2];ten='-10' in profile;fractional='fractional' in profile;rate='30000/1001' if fractional else '25';g='30' if fractional else '25';original=root/'original.mp4'
    common=['-an','-threads','2','-filter_threads','1','-pix_fmt','yuv420p10le' if ten else 'yuv420p']
    if codec=='hevc':encoder=['-c:v','libx265','-preset','fast','-x265-params',f'pools=1:frame-threads=1:log-level=error:keyint={g}:min-keyint={g}:scenecut=0:open-gop=0:bframes=2','-tag:v','hvc1' if '-hvc1' in profile else 'hev1']
    elif codec=='av1':encoder=['-c:v','libaom-av1','-cpu-used','8','-row-mt','1','-g',g,'-keyint_min',g,'-crf','40','-b:v','0','-lag-in-frames','0']
    else:encoder=['-c:v','libvpx-vp9','-profile:v','2' if ten else '0','-deadline','good','-cpu-used','4','-row-mt','1','-g',g,'-keyint_min',g,'-crf','40','-b:v','0','-lag-in-frames','25','-auto-alt-ref','1']
    subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={rate}:duration={seconds}',*common,*encoder,'-movflags',('empty_moov' if codec=='hevc' else 'delay_moov')+'+default_base_moof+frag_keyframe',str(original)],check=True,capture_output=True)
    raw=original.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(original)]));assert len(probe['streams'])==1
    clock=Fraction(probe['streams'][0]['time_base']);ticks=int(1/Fraction(rate)/clock)
    for p in probe['packets']:
        p.setdefault('duration',str(ticks));p.setdefault('duration_time',str(float(int(p['duration'])*clock)))
    duration=max((int(p['pts'])+int(p['duration']))*clock for p in probe['packets'])
    model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(int(duration*10000000)+1),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=None if fractional else str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind='video',required=True,codecHint=codec,containerHint='fmp4',initializationReady=True,state='downloading',source=dict(resourceId='media'))],metadata=dict(revision='1',title='Standalone '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))]))
    return model,raw,original,probe

def prefix_at_seconds(raw,probe,seconds):
    clock=Fraction(probe['streams'][0]['time_base']);packets=[(int(p['pos']),(int(p['pts'])+int(p['duration']))*clock) for p in probe['packets']]
    for at,size,kind,head in boxes(raw):
        end=at+size
        if kind==b'mdat' and any(at<=pos<end and stop>=seconds for pos,stop in packets):return end
    raise ValueError('Insufficient confirmed video duration')
