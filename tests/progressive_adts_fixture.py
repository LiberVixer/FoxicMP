"""Actual ADTS AAC-LC and independent decoded-PCM oracle, no downloader."""
import hashlib,json,subprocess,uuid
from fractions import Fraction
from pathlib import Path
PROFILES=tuple('audio-adts-'+c+'-'+str(r) for r in (44100,48000) for c in ('mono','stereo'))
def frames(raw):
    result=[];at=0
    while at<len(raw):
        assert raw[at:at+2]==b'\xff\xf1';n=((raw[at+3]&3)<<11)|(raw[at+4]<<3)|(raw[at+5]>>5);assert n>7 and at+n<=len(raw);result.append((at,n));at+=n
    assert at==len(raw);return result

def fixture(root,profile,seconds=20):
    root=Path(root);root.mkdir(parents=True,exist_ok=True);rate=int(profile.split('-')[-1]);channels=2 if '-stereo-' in profile else 1;out=root/'original.aac'
    subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','sine=frequency=431:sample_rate='+str(rate)+':duration='+str(seconds),'-ac',str(channels),'-c:a','aac','-profile:a','aac_low','-b:a','128k' if channels==2 else '64k','-f','adts',str(out)],check=True,capture_output=True)
    raw=out.read_bytes();packets=frames(raw);probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-of','json',str(out)]));assert len(probe['packets'])==len(packets)
    for packet,(at,n) in zip(probe['packets'],packets):
        assert int(packet['pos'])==at and int(packet['size'])==n;packet['_payload_sha256']=hashlib.sha256(raw[at+7:at+n]).hexdigest()
    pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(out),'-map','0:a:0','-c:a','pcm_s16le','-f','s16le','-']);probe.update(_pcm_sha256=hashlib.sha256(pcm).hexdigest(),_pcm_bytes=len(pcm))
    clock=Fraction(probe['streams'][0]['time_base']);duration=max((int(p['pts'])+int(p['duration']))*clock for p in probe['packets']);assert duration==Fraction(len(packets)*1024,rate)
    m=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(int(duration*10000000)+1),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=None if channels==1 else str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind='audio',required=True,codecHint='aac-lc',containerHint='adts',initializationReady=True,state='downloading',source=dict(resourceId='media'))],metadata=dict(revision='1',title='ADTS '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))]))
    return m,raw,out,probe

def prefix_at_seconds(raw,probe,seconds):
    clock=Fraction(probe['streams'][0]['time_base'])
    for p in probe['packets']:
        if (int(p['pts'])+int(p['duration']))*clock>=seconds:return int(p['pos'])+int(p['size'])
    raise ValueError('Insufficient ADTS duration')
