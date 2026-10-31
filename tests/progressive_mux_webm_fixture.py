"""Player-owned growing WebM VP9/Opus producer; no downloader access."""
import json
import subprocess
import uuid
from pathlib import Path
from fractions import Fraction
from progressive_growing_ts_fixture import publish
from progressive_webm_fixtures import element,locate

PROFILES=('vp9-8','vp9-10','vp9-fractional','vp9-known')


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
    ten=profile=='vp9-10';fractional=profile=='vp9-fractional';rate='30000/1001' if fractional else '25';channels=1 if fractional else 2
    original=root/'mux-original.webm'
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={rate}:duration={seconds}',
          '-f','lavfi','-i',f'sine=frequency=880:sample_rate=48000:duration={seconds}','-map','0:v','-map','1:a',
          '-c:v','libvpx-vp9','-deadline','realtime','-cpu-used','8','-threads','2','-row-mt','1','-lag-in-frames','0','-auto-alt-ref','0',
          '-g','25','-pix_fmt','yuv420p10le' if ten else 'yuv420p','-profile:v','2' if ten else '0','-b:v','300k',
          '-c:a','libopus','-ac',str(channels),'-b:a','96k','-cluster_time_limit','1000']
    if profile!='vp9-known':args+=['-live','1']
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
    for kind,codec in [('video','vp9'),('audio','opus')]:
        model['tracks'].append(dict(id=kind,kind=kind,required=True,codecHint=codec,containerHint='webm-muxed',state='downloading',initializationReady=True,source=dict(resourceId='mux')))
    model['metadata']=dict(revision='1',title='Muxed WebM '+profile,chapters=[dict(id='intro',time='0',title='Intro'),dict(id='two',time='20000000',title='Two seconds')])
    return model,raw,original,groups,scales
