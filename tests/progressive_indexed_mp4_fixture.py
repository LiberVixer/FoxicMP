"""Ordinary fast-start H.264/AAC fixture, independent of the downloader."""
import json,subprocess,uuid
from fractions import Fraction
from pathlib import Path
from progressive_growing_ts_fixture import publish
from progressive_mux_mp4_fixture import boxes

def fixture(root,profile='main-bframes',seconds=20,*,faststart=True):
    root=Path(root);root.mkdir(parents=True,exist_ok=True)
    rate='30000/1001' if profile=='fractional' else '25';sample_rate=44100 if profile=='fractional' else 48000
    original=root/'ordinary.mp4'
    command=['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={rate}:duration={seconds}',
             '-f','lavfi','-i',f'sine=frequency=880:sample_rate={sample_rate}:duration={seconds}',
             '-c:v','libx264','-threads','2','-preset','veryfast','-pix_fmt','yuv420p','-profile:v','high' if profile=='fractional' else 'main',
             '-x264-params','keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes='+('0' if profile=='no-bframes' else '2'),
             '-c:a','aac','-ac','2','-b:a','96k','-movflags','+faststart' if faststart else '0',str(original)]
    if profile=='vfr':command[-1:-1]=['-vf',"select='not(eq(mod(n,5),1))'",'-fps_mode:v','vfr']
    subprocess.run(command,check=True,capture_output=True)
    raw=original.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_format','-show_data_hash','sha256','-of','json',str(original)]))
    groups=[[p for p in probe['packets'] if p['codec_type']==kind] for kind in ('video','audio')]
    scales=[Fraction(s['time_base']) for s in probe['streams']]
    duration=int(Fraction(probe['format']['duration'])*10000000)
    model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),
        timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(duration),timestampOffset='0'),
        resources=[dict(id='mux',name='mux.part',length=str(len(raw)),state='downloading',ranges=[])],tracks=[])
    for kind,codec in [('video','h264'),('audio','aac-lc')]:
        model['tracks'].append(dict(id=kind,kind=kind,required=True,codecHint=codec,containerHint='mp4-muxed',state='downloading',source=dict(resourceId='mux'),initializationReady=True))
    model['metadata']=dict(revision='1',title='Ordinary fast-start MP4',chapters=[dict(id='intro',time='0',title='Intro'),dict(id='two',time='20000000',title='Two seconds')])
    return model,raw,original,groups,scales

def prefix_at(groups,scales,seconds):
    stops=[int(p['pos'])+int(p['size']) for k,group in enumerate(groups) for p in group if Fraction(int(p['pts']))*scales[k]<seconds]
    return max(stops)
