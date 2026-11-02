"""Standard-library 24-bit WAVE writer and independent integer PCM oracle."""
import hashlib,json,subprocess,uuid,wave
from pathlib import Path
PROFILES=tuple('audio-wav24-'+c+'-'+str(r) for r in (44100,48000) for c in ('mono','stereo'))
def fixture(root,profile,seconds=20):
 root=Path(root);root.mkdir(parents=True,exist_ok=True);rate=int(profile.split('-')[-1]);channels=2 if '-stereo-' in profile else 1
 values=((((i*(137+c*74)*257+c*93)%16777216)-8388608) for i in range(seconds*rate) for c in range(channels))
 data=b''.join((v & 0xffffff).to_bytes(3,'little') for v in values);(root/'input.pcm').write_bytes(data);original=root/'original.wav'
 with wave.open(str(original),'wb') as out:out.setnchannels(channels);out.setsampwidth(3);out.setframerate(rate);out.writeframes(data)
 raw=original.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-of','json',str(original)]));probe.update(_pcm_bytes=len(data),_pcm_sha256=hashlib.sha256(data).hexdigest(),_data_offset=44,_block_align=channels*3)
 decoded=subprocess.check_output(['ffmpeg','-v','error','-i',str(original),'-c:a','pcm_s24le','-f','s24le','-']);assert decoded==data
 model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(seconds*10000000),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=None if channels==1 else str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind='audio',required=True,codecHint='pcm-s24le',containerHint='wav-pcm24',initializationReady=True,state='downloading',source=dict(resourceId='media'))],metadata=dict(revision='1',title='WAV '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))]))
 return model,raw,original,probe

def prefix_at_seconds(raw,probe,seconds):return min(len(raw),probe['_data_offset']+int(seconds)*int(probe['streams'][0]['sample_rate'])*probe['_block_align'])
