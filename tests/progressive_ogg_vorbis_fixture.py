"""Independent Ogg Vorbis encoder, packet parser and native PCM oracle."""
import hashlib,json,subprocess,uuid
from pathlib import Path
from progressive_ogg_opus_fixture import pages,packets,crc_page
PROFILES=tuple('audio-ogg-vorbis-'+c+'-'+str(q) for q in (2,5) for c in ('mono','stereo'))
def fixture(root,profile,seconds=20):
 root=Path(root);root.mkdir(parents=True,exist_ok=True);quality=profile.split('-')[-1];channels=2 if '-stereo-' in profile else 1;original=root/'original.ogg'
 expr='0.25*sin(2*PI*880*t)'+('|0.25*sin(2*PI*440*t)' if channels==2 else '')+f':s=48000:d={seconds}'
 source='anoisesrc=color=white:seed=41:amplitude=0.25:s=48000:d='+str(seconds) if '-noise-' in profile else 'aevalsrc='+expr
 subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i',source,'-c:a','libvorbis','-ac',str(channels),'-q:a',quality,'-page_duration','1000000','-f','ogg',str(original)],check=True,capture_output=True)
 raw=original.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(original)]));items=packets(raw);assert [v[0] for v in items[:3]]==[1,3,5]
 assert [p['data_hash'] for p in probe['packets']]==['SHA256:'+hashlib.sha256(v).hexdigest() for v in items[3:]]
 pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(original),'-c:a','pcm_s16le','-f','s16le','-']);assert len(pcm)==seconds*48000*channels*2
 probe.update(_pcm_sha256=hashlib.sha256(pcm).hexdigest(),_pcm_bytes=len(pcm),_packets=items,_pages=pages(raw))
 model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(seconds*10000000),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=None if channels==1 else str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind='audio',required=True,codecHint='vorbis',containerHint='ogg-vorbis',initializationReady=True,state='downloading',source=dict(resourceId='media'))],metadata=dict(revision='1',title='Ogg Vorbis '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))]))
 return model,raw,original,probe

def prefix_at_seconds(raw,probe,seconds):
 for p in probe['_pages']:
  if p['granule']!=2**64-1 and p['granule']>=seconds*48000:return p['end']
 return len(raw)
