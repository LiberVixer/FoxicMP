"""Independent FFmpeg Ogg muxer and page/packet inspection for Opus tests."""
import hashlib,json,struct,subprocess,uuid
from pathlib import Path
PROFILES=tuple('audio-ogg-opus-'+c+'-'+str(ms) for ms in (10,20) for c in ('mono','stereo'))
def pages(raw):
 out=[];at=0
 while at<len(raw):
  assert raw[at:at+4]==b'OggS' and at+27<=len(raw);n=raw[at+26];laces=raw[at+27:at+27+n];end=at+27+n+sum(laces);assert len(laces)==n and end<=len(raw)
  out.append(dict(at=at,end=end,flags=raw[at+5],granule=struct.unpack_from('<Q',raw,at+6)[0],sequence=struct.unpack_from('<I',raw,at+18)[0],laces=laces,body=at+27+n));at=end
 return out

def packets(raw):
 result=[];pending=bytearray()
 for p in pages(raw):
  at=p['body']
  for n in p['laces']:
   pending+=raw[at:at+n];at+=n
   if n<255:result.append(bytes(pending));pending.clear()
 assert not pending
 return result

def fixture(root,profile,seconds=20):
 root=Path(root);root.mkdir(parents=True,exist_ok=True);ms=profile.split('-')[-1];channels=2 if '-stereo-' in profile else 1;original=root/'original.opus'
 expr='0.25*sin(2*PI*880*t)'+('|0.25*sin(2*PI*440*t)' if channels==2 else '')+f':s=48000:d={seconds}'
 subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','aevalsrc='+expr,'-c:a','libopus','-ac',str(channels),'-b:a','96k','-frame_duration',ms,'-page_duration','1000000','-f','ogg',str(original)],check=True,capture_output=True)
 raw=original.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(original)]));items=packets(raw);assert items[0][:8]==b'OpusHead' and items[1][:8]==b'OpusTags'
 assert [p['data_hash'] for p in probe['packets']]==['SHA256:'+hashlib.sha256(v).hexdigest() for v in items[2:]]
 pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(original),'-c:a','pcm_s16le','-f','s16le','-']);assert len(pcm)==seconds*48000*channels*2
 probe.update(_pcm_sha256=hashlib.sha256(pcm).hexdigest(),_pcm_bytes=len(pcm),_packets=items,_pages=pages(raw))
 model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(seconds*10000000),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=None if channels==1 else str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind='audio',required=True,codecHint='opus',containerHint='ogg-opus',initializationReady=True,state='downloading',source=dict(resourceId='media'))],metadata=dict(revision='1',title='Ogg Opus '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))]))
 return model,raw,original,probe

def prefix_at_seconds(raw,probe,seconds):
 skip=struct.unpack_from('<H',probe['_packets'][0],10)[0]
 for p in probe['_pages']:
  if p['granule']!=2**64-1 and p['granule']>=seconds*48000+skip:return p['end']
 return len(raw)

def crc_page(raw):
 # Table implementation, independent from the production bitwise CRC loop.
 table=[]
 for value in range(256):
  c=value<<24
  for _ in range(8):c=((c<<1)^0x04c11db7 if c&0x80000000 else c<<1)&0xffffffff
  table.append(c)
 b=bytearray(raw);b[22:26]=bytes(4);c=0
 for value in b:c=((c<<8)&0xffffffff)^table[(c>>24)^value]
 struct.pack_into('<I',b,22,c);return bytes(b)
