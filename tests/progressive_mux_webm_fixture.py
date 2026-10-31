"""Player-owned growing WebM VP8/VP9/AV1 and Opus/Vorbis producer; no downloader access."""
import json
import subprocess
import uuid
from pathlib import Path
from fractions import Fraction
from progressive_growing_ts_fixture import publish
from progressive_webm_fixtures import element,locate

PROFILES=('vp9-8','vp9-10','vp9-fractional','vp9-known','av1-8','av1-10','av1-fractional','av1-known','vp8','vp8-fractional','vp8-known','vp8-vorbis','vp8-fractional-vorbis-mono','vp9-8-vorbis','vp9-10-vorbis-mono','av1-8-vorbis','av1-10-vorbis-mono','vp9-known-vorbis')


def top_elements(raw):
    tag,_,at=element(raw,0);assert tag==0x1a45dfa3
    tag,start,limit=element(raw,at);assert tag==0x18538067
    at=start
    while at<len(raw):
        tag,body,end=element(raw,at);assert end is not None and end<=len(raw)
        yield tag,at,body,end
        at=end


def fixture(root,profile='vp9-8',seconds=20):
    root=Path(root);root.mkdir(parents=True,exist_ok=True)
    assert profile in PROFILES,profile
    vorbis='vorbis' in profile
    vp8=profile.startswith('vp8');av1=profile.startswith('av1-');ten='-10' in profile;fractional='fractional' in profile;rate='30000/1001' if fractional else '25';channels=1 if fractional or profile.endswith('-mono') else 2
    audio_codec='libvorbis' if vorbis else 'libopus'
    signal=f"aevalsrc=exprs='if(lt(mod(t,0.5),0.03),0.7*sin(2*PI*880*t),0)':s=48000:d={seconds}" if vorbis else f'sine=frequency=880:sample_rate=48000:duration={seconds}'
    original=root/'mux-original.webm'
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={rate}:duration={seconds}',
          '-f','lavfi','-i',signal,'-map','0:v','-map','1:a',
          '-c:v','libaom-av1' if av1 else 'libvpx' if vp8 else 'libvpx-vp9',*(['-usage','realtime'] if av1 else ['-deadline','realtime','-auto-alt-ref','0'] if vp8 else ['-deadline','realtime','-row-mt','1','-auto-alt-ref','0']),'-cpu-used','8','-threads','2','-lag-in-frames','0',
          '-g','25','-pix_fmt','yuv420p10le' if ten else 'yuv420p','-profile:v','0' if av1 else '2' if ten else '0','-b:v','300k',
          '-c:a',audio_codec,'-ac',str(channels),'-b:a','96k','-cluster_time_limit','1000']
    if av1:
        # FFmpeg needs AV1 extradata before writing the WebM header. Generate
        # it in a delayed-moov video-only fixture, then copy its real packets.
        encoded=root/'encoded-av1.mp4'
        encoder=args[args.index('-c:v'):args.index('-c:a')]
        subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={rate}:duration={seconds}',
                        *encoder,'-an','-movflags','+delay_moov+frag_keyframe+default_base_moof',str(encoded)],check=True,capture_output=True)
        args=['ffmpeg','-v','error','-y','-i',str(encoded),'-f','lavfi','-i',signal,
              '-map','0:v','-map','1:a','-c:v','copy','-c:a',audio_codec,'-ac',str(channels),'-b:a','96k','-cluster_time_limit','1000']
    if 'known' not in profile:args+=['-live','1']
    subprocess.run(args+[str(original)],check=True,capture_output=True)
    raw=original.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(original)]))
    groups=[[p for p in probe['packets'] if p['codec_type']==kind] for kind in ('video','audio')];scales=[Fraction(s['time_base']) for s in probe['streams']]
    _,b,e=locate(raw,0x23e383);default_duration=int.from_bytes(raw[b:e],'big');duration=0
    for k,g in enumerate(groups):
        for p in g:
            length=int(p.get('duration','0'))*scales[k]*10000000
            if not k:length=Fraction(default_duration,100)
            duration=max(duration,int(int(p['pts'])*scales[k]*10000000+length))
    model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),
               timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(duration),timestampOffset='0'),
               resources=[dict(id='mux',name='mux.part',length=None if fractional else str(len(raw)),state='downloading',ranges=[])],tracks=[])
    for kind,codec in [('video','av1' if av1 else 'vp8' if vp8 else 'vp9'),('audio','vorbis' if vorbis else 'opus')]:
        model['tracks'].append(dict(id=kind,kind=kind,required=True,codecHint=codec,containerHint='webm-muxed',state='downloading',initializationReady=True,source=dict(resourceId='mux')))
    model['metadata']=dict(revision='1',title='Muxed WebM '+profile,chapters=[dict(id='intro',time='0',title='Intro'),dict(id='two',time='20000000',title='Two seconds')])
    return model,raw,original,groups,scales
