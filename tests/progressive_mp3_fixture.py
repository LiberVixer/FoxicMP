"""Actual MPEG-1 Layer III and independent decoded-PCM oracle, no downloader."""
import hashlib,json,subprocess,uuid
from fractions import Fraction
from pathlib import Path
PROFILES=tuple('audio-mp3-'+c+'-'+mode+'-'+str(r) for r in (44100,48000) for c in ('mono','stereo') for mode in ('cbr','vbr'))
def frames(raw):
    result=[];at=0
    if raw[:3]==b'ID3':
        n=0
        for b in raw[6:10]:n=(n<<7)|b
        at=10+n+(10 if raw[3]==4 and raw[5]&16 else 0)
    while at<len(raw):
        if raw[at:at+3]==b'TAG' and len(raw)-at==128:break
        assert raw[at:at+2]==b'\xff\xfb';p=raw[at:at+4];rate=48000 if p[2]&4 else 44100;kbps=(0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0)[p[2]>>4];n=144000*kbps//rate+((p[2]>>1)&1);assert n>36 and at+n<=len(raw);result.append((at,n));at+=n
    assert at==len(raw) or len(raw)-at==128;return result

def fixture(root,profile,seconds=20):
    root=Path(root);root.mkdir(parents=True,exist_ok=True);rate=int(profile.split('-')[-1]);channels=2 if '-stereo-' in profile else 1;out=root/'original.mp3'
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i','aevalsrc=0.2*sin(2*PI*(431+17*t)*t):s='+str(rate)+':d='+str(seconds),'-ac',str(channels),'-c:a','libmp3lame']
    args+=['-q:a','3'] if '-vbr-' in profile else ['-b:a','32k']
    args+=['-write_xing','0','-id3v2_version','0','-write_id3v1','0','-f','mp3',str(out)]
    subprocess.run(args,check=True,capture_output=True)
    body=out.read_bytes();version=3 if channels==1 else 4;tag=b'\x00'*32;header=b'ID3'+bytes([version,0,0])+bytes([0,0,0,len(tag)])
    out.write_bytes(header+tag+body+b'TAG'+b'\x00'*125)
    raw=out.read_bytes();packets=frames(raw);probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-of','json',str(out)]));assert len(probe['packets'])==len(packets)
    for packet,(at,n) in zip(probe['packets'],packets):
        assert int(packet['pos'])==at and int(packet['size'])==n;packet['_payload_sha256']=hashlib.sha256(raw[at:at+n]).hexdigest()
    pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(out),'-map','0:a:0','-c:a','pcm_s16le','-f','s16le','-']);probe.update(_pcm_sha256=hashlib.sha256(pcm).hexdigest(),_pcm_bytes=len(pcm))
    clock=Fraction(probe['streams'][0]['time_base']);duration=max((int(p['pts'])+int(p['duration']))*clock for p in probe['packets']);assert duration==Fraction(len(packets)*1152,rate)
    m=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(int(duration*10000000)+1),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=None if channels==1 else str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind='audio',required=True,codecHint='mp3',containerHint='mp3',initializationReady=True,state='downloading',source=dict(resourceId='media'))],metadata=dict(revision='1',title='MP3 '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))]))
    return m,raw,out,probe

def prefix_at_seconds(raw,probe,seconds):
    clock=Fraction(probe['streams'][0]['time_base'])
    for p in probe['packets']:
        if (int(p['pts'])+int(p['duration']))*clock>=seconds:return int(p['pos'])+int(p['size'])
    raise ValueError('Insufficient MP3 duration')
