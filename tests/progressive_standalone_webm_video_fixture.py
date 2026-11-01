"""Single VP8/VP9/AV1 WebM with real frames, bounded Clusters and exact clocks."""
import json,subprocess,uuid
from fractions import Fraction
from pathlib import Path
from progressive_webm_fixtures import locate

def fixture(root,profile,seconds=20):
    root=Path(root);root.mkdir(parents=True,exist_ok=True);codec=profile.split('-')[2];ten='-10' in profile;fractional='fractional' in profile;rate='30000/1001' if fractional else '25';original=root/'original.webm'
    source=['-f','lavfi','-i',f'testsrc2=size=160x90:rate={rate}:duration={seconds}']
    encoder=['-an','-c:v',{'vp8':'libvpx','vp9':'libvpx-vp9','av1':'libaom-av1'}[codec],*(['-usage','realtime'] if codec=='av1' else ['-deadline','realtime','-auto-alt-ref','0']),'-cpu-used','8','-threads','2','-lag-in-frames','0','-g','30' if fractional else '25','-pix_fmt','yuv420p10le' if ten else 'yuv420p','-profile:v','2' if codec=='vp9' and ten else '0','-b:v','300k']
    if codec=='av1':
        encoded=root/'encoded-av1.mp4'
        subprocess.run(['ffmpeg','-v','error','-y',*source,*encoder,'-movflags','+delay_moov+frag_keyframe+default_base_moof',str(encoded)],check=True,capture_output=True)
        args=['-i',str(encoded),'-an','-c:v','copy']
    else:args=source+encoder
    subprocess.run(['ffmpeg','-v','error','-y',*args,'-cluster_time_limit','1000','-live','1',str(original)],check=True,capture_output=True)
    raw=original.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(original)]));assert len(probe['streams'])==1
    _,b,e=locate(raw,0x23e383);frame_ns=int.from_bytes(raw[b:e],'big');duration=max(int(Fraction(p['pts_time'])*10000000)+frame_ns//100 for p in probe['packets'])
    model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(duration+1),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=None if fractional or codec=='vp8' else str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind='video',required=True,codecHint=codec,containerHint='webm',initializationReady=True,state='downloading',source=dict(resourceId='media'))],metadata=dict(revision='1',title='Standalone '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))]))
    return model,raw,original,probe

def prefix_at_seconds(raw,probe,seconds):
    # Clusters may also split by byte size, not only by the muxer's time limit.
    # Select complete original Clusters using independently probed packet PTS.
    from progressive_mux_webm_fixture import top_elements
    _,b,e=locate(raw,0x23e383);duration=Fraction(int.from_bytes(raw[b:e],'big'),1000000000)
    packets=[(int(p['pos']),Fraction(p['pts_time'])+duration) for p in probe['packets']]
    for tag,at,_,end in top_elements(raw):
        if tag==0x1f43b675 and any(at<=pos<end and stop>=seconds for pos,stop in packets):return end
    raise ValueError('Insufficient confirmed video duration')
