"""Player-owned shared fMP4 producer; no downloader access."""
import json
import subprocess
from fractions import Fraction
from pathlib import Path
from progressive_growing_ts_fixture import publish
import uuid


def boxes(raw):
    at=0
    while at<len(raw):
        n=int.from_bytes(raw[at:at+4],'big');kind=raw[at+4:at+8];h=8
        if n==1:n=int.from_bytes(raw[at+8:at+16],'big');h=16
        assert n>=h and at+n<=len(raw),(at,n,len(raw))
        yield at,n,kind,h
        at+=n


def fixture(root,profile='main-bframes',seconds=20):
    root=Path(root);root.mkdir(parents=True,exist_ok=True)
    av1=profile.startswith('av1-');hevc=profile.startswith('hevc');ten=profile.startswith(('hevc10','av1-10'));high=profile in ('high-fractional','hevc10-fractional','av1-10-fractional');rate='30000/1001' if high else '25';audio_rate=44100 if high else 48000
    original=root/'mux-original.mp4'
    options=['-f','lavfi','-i',f'testsrc2=size=160x90:rate={rate}:duration={seconds}',
             '-f','lavfi','-i',f'sine=frequency=880:sample_rate={audio_rate}:duration={seconds}',
             '-map','0:v','-map','1:a',*(
                 ['-c:v','libaom-av1','-threads','2','-filter_threads','1','-pix_fmt','yuv420p10le' if ten else 'yuv420p',
                  '-cpu-used','8','-row-mt','1','-g','25','-keyint_min','25','-crf','40','-b:v','0','-lag-in-frames','0'] if av1 else
                 ['-c:v','libx265','-threads','2','-filter_threads','1','-preset','fast','-pix_fmt','yuv420p10le' if ten else 'yuv420p',
                  '-x265-params','pools=1:frame-threads=1:log-level=error:keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes=2',
                  '-tag:v','hvc1' if profile=='hevc-main' else 'hev1'] if hevc else
                 ['-c:v','libx264','-preset','veryfast','-profile:v','high' if high else 'main','-pix_fmt','yuv420p',
                  '-x264-params','force-cfr=1:keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes=2']),
             '-c:a','aac','-ac','2','-b:a','96k','-avoid_negative_ts','make_zero',
             '-movflags',('delay_moov' if av1 or profile in ('main-delayed','hevc-delayed') else 'empty_moov')+'+frag_keyframe+default_base_moof+negative_cts_offsets',str(original)]
    subprocess.run(['ffmpeg','-v','error','-y',*options],check=True,capture_output=True)
    raw=original.read_bytes()
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(original)]))
    groups=[[p for p in probe['packets'] if p['codec_type']==kind] for kind in ('video','audio')]
    scales=[Fraction(s['time_base']) for s in probe['streams']]
    durations=[int(Fraction(1,1)/Fraction(probe['streams'][0]['r_frame_rate'])/scales[0]),1024]
    for k,g in enumerate(groups):
        for p in g:p.setdefault('duration',str(durations[k]))
    duration=max(int((int(p['pts'])+int(p['duration']))*scales[k]*10000000) for k,g in enumerate(groups) for p in g)
    model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),
               timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(duration),timestampOffset='0'),
               resources=[dict(id='mux',name='mux.part',length=None if high else str(len(raw)),state='downloading',ranges=[])],tracks=[])
    for kind,codec in [('video','av1' if av1 else 'hevc' if hevc else 'h264'),('audio','aac-lc')]:
        model['tracks'].append(dict(id=kind,kind=kind,required=True,codecHint=codec,containerHint='fmp4-muxed',state='downloading',initializationReady=True,source=dict(resourceId='mux')))
    model['metadata']=dict(revision='1',title='Muxed fMP4 '+profile,chapters=[dict(id='intro',time='0',title='Intro'),dict(id='two',time='20000000',title='Two seconds')])
    return model,raw,original,groups,scales
