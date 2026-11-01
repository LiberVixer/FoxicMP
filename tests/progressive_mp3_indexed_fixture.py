"""Real Xing/Info and independently assembled VBRI, with encoded-sample PCM oracle."""
import hashlib,json,subprocess,uuid
from fractions import Fraction
from pathlib import Path
from progressive_mp3_fixture import frames
PROFILES=tuple('audio-mp3-indexed-'+c+'-'+kind+'-'+str(r) for kind in ('xing','info','vbri') for r in (44100,48000) for c in ('mono','stereo'))
def fixture(root,profile,seconds=20):
    root=Path(root);root.mkdir(parents=True,exist_ok=True);rate=int(profile.split('-')[-1]);channels=2 if '-stereo-' in profile else 1;kind=profile.split('-')[-2];out=root/'original.mp3'
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i','aevalsrc=0.2*sin(2*PI*(431+17*t)*t):s='+str(rate)+':d='+str(seconds),'-ac',str(channels),'-c:a','libmp3lame']
    args+=['-b:a','128k'] if kind=='info' else ['-q:a','3']
    subprocess.run(args+['-write_xing','1','-id3v2_version','0','-f','mp3',str(out)],check=True,capture_output=True)
    body=out.read_bytes();parts=frames(body);n=parts[0][1];audio=body[n:];count=len(parts)-1;side=17 if channels==1 else 32;at=4+side;header=body[:n];fields=dict(type=header[at:at+4].decode(),frames=count,bytes=len(body),encoder='',delay=0,padding=0)
    flags=int.from_bytes(header[at+4:at+8],'big');cursor=at+16+(100 if flags&4 else 0)+(4 if flags&8 else 0);fields.update(encoder=header[cursor:cursor+9].decode(),delay=(header[cursor+21]<<4)|(header[cursor+22]>>4),padding=((header[cursor+22]&15)<<8)|header[cursor+23])
    if kind=='vbri':
        # One independently generated TOC entry covers the whole MPEG span.
        h=bytearray(n);h[:4]=body[:4];h[36:40]=b'VBRI';h[40:42]=(1).to_bytes(2,'big');h[46:50]=len(body).to_bytes(4,'big');h[50:54]=count.to_bytes(4,'big');h[54:56]=(1).to_bytes(2,'big');h[56:58]=(1).to_bytes(2,'big');h[58:60]=(4).to_bytes(2,'big');h[60:62]=count.to_bytes(2,'big');h[62:66]=len(body).to_bytes(4,'big');body=bytes(h)+audio;fields.update(type='VBRI',encoder='',delay=0,padding=0)
    tag=b'\x00'*32;out.write_bytes(b'ID3'+bytes([3 if channels==1 else 4,0,0,0,0,0,len(tag)])+tag+body+b'TAG'+b'\x00'*125)
    raw=out.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(out)]));assert len(probe['packets'])==count
    for p in probe['packets']:p['_payload_sha256']=hashlib.sha256(raw[int(p['pos']):int(p['pos'])+int(p['size'])]).hexdigest()
    pcm=subprocess.check_output(['ffmpeg','-v','error','-f','mp3','-i','pipe:0','-c:a','pcm_s16le','-f','s16le','-'],input=audio);assert len(pcm)==count*1152*channels*2
    probe.update(_pcm_sha256=hashlib.sha256(pcm).hexdigest(),_pcm_bytes=len(pcm),_index=fields)
    duration=Fraction(count*1152,rate)
    m=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(int(duration*10000000)+1),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=None if channels==1 else str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind='audio',required=True,codecHint='mp3',containerHint='mp3-indexed',initializationReady=True,state='downloading',source=dict(resourceId='media'))],metadata=dict(revision='1',title='Indexed MP3 '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))]))
    return m,raw,out,probe
from progressive_mp3_fixture import prefix_at_seconds
