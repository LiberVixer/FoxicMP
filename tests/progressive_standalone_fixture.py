"""One real fMP4 stream: no placeholder track or external downloader."""
import json,subprocess,uuid
from fractions import Fraction
from pathlib import Path
from progressive_growing_ts_fixture import publish
from progressive_v1_conformance import fragment_prefix as mp4_prefix
from progressive_webm_fixtures import webm_prefix

BASE_PROFILES=('audio-mono','audio-stereo-44100','video-main','video-vfr')
OPUS_PROFILES=('audio-opus-mono','audio-opus-stereo')
WEBM_PROFILES=tuple('audio-webm-'+codec+'-'+channels for codec in ('opus','vorbis') for channels in ('mono','stereo'))
PROFILES=BASE_PROFILES+OPUS_PROFILES+WEBM_PROFILES
def fragment_prefix(raw,count):
    return webm_prefix(raw,count) if raw[:4]==bytes.fromhex('1a45dfa3') else mp4_prefix(raw,count)
def fixture(root,profile='audio-mono',seconds=20):
    if profile in WEBM_PROFILES:
        from progressive_standalone_webm_fixture import fixture as webm_fixture
        return webm_fixture(root,profile,seconds)
    root=Path(root);root.mkdir(parents=True,exist_ok=True);audio=profile.startswith('audio');opus='-opus-' in profile;original=root/'original.mp4'
    if audio:
        rate=44100 if '44100' in profile else 48000
        args=['-f','lavfi','-i',f'sine=frequency=880:sample_rate={rate}:duration={seconds}','-vn','-c:a','libopus' if opus else 'aac',*(['-frame_duration','20','-application','audio'] if opus else []),'-ac','2' if 'stereo' in profile else '1','-frag_duration','1000000']
    else:
        args=['-f','lavfi','-i',f'testsrc2=size=160x90:rate=25:duration={seconds}','-an','-c:v','libx264','-threads','2','-preset','veryfast','-profile:v','main','-pix_fmt','yuv420p','-x264-params','keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes=2']
        if profile=='video-vfr':args+=['-vf',"select='not(eq(mod(n,5),1))'",'-fps_mode:v','vfr']
    subprocess.run(['ffmpeg','-v','error','-y',*args,'-movflags',('delay_moov' if opus else 'empty_moov')+'+default_base_moof+frag_keyframe',str(original)],check=True,capture_output=True)
    raw=original.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_format','-show_data_hash','sha256','-of','json',str(original)]));assert len(probe['streams'])==1
    kind='audio' if audio else 'video';duration=max(Fraction(p['pts_time'])+Fraction(p.get('duration_time','0')) for p in probe['packets']);duration=max(duration,Fraction(str(probe['format']['duration'])))
    model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(int(duration*10000000)+1),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind=kind,required=True,codecHint=('opus' if opus else 'aac-lc') if audio else 'h264',containerHint='fmp4',initializationReady=True,state='downloading',source=dict(resourceId='media'))])
    if profile in ('audio-mono','audio-opus-mono'):model['resources'][0]['length']=None
    model['metadata']=dict(revision='1',title='Standalone '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))])
    return model,raw,original,probe
