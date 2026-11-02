"""Independent FFmpeg AC-3 encoding, packet hashes and PCM oracle."""
import hashlib,json,subprocess,uuid
from pathlib import Path
PROFILES=('audio-ac3-mono-64','audio-ac3-mono-192','audio-ac3-stereo-192','audio-ac3-stereo-384')
BITRATES=(32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640)
def frames(raw):
 out=[];at=0
 while at<len(raw):
  assert raw[at:at+2]==b'\x0b\x77' and len(raw)-at>=7;size=BITRATES[(raw[at+4]&63)//2]*4;assert at+size<=len(raw);out.append((at,size));at+=size
 return out

def fixture(root,profile,seconds=20):
 root=Path(root);root.mkdir(parents=True,exist_ok=True);bitrate=profile.split('-')[-1];channels=2 if '-stereo-' in profile else 1;original=root/'original.ac3'
 expr='0.25*sin(2*PI*880*t)'+('|0.25*sin(2*PI*440*t)' if channels==2 else '')+f':s=48000:d={seconds}'
 subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','aevalsrc='+expr,'-c:a','ac3','-ac',str(channels),'-b:a',bitrate+'k','-f','ac3',str(original)],check=True,capture_output=True)
 raw=original.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(original)]));items=frames(raw);assert len(items)==seconds*48000//1536
 assert [p['data_hash'] for p in probe['packets']]==['SHA256:'+hashlib.sha256(raw[at:at+n]).hexdigest() for at,n in items]
 pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(original),'-c:a','pcm_s16le','-f','s16le','-']);assert len(pcm)==seconds*48000*channels*2
 probe.update(_pcm_sha256=hashlib.sha256(pcm).hexdigest(),_pcm_bytes=len(pcm),_frames=items)
 model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(seconds*10000000),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=None if channels==1 else str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind='audio',required=True,codecHint='ac3',containerHint='ac3',initializationReady=True,state='downloading',source=dict(resourceId='media'))],metadata=dict(revision='1',title='AC-3 '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))]))
 return model,raw,original,probe

def prefix_at_seconds(raw,probe,seconds):
 count=(int(seconds)*48000+1535)//1536;at,n=probe['_frames'][min(count,len(probe['_frames']))-1];return at+n
