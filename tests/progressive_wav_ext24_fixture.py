"""Independent Python PCM oracle; FFmpeg and struct-based extensible WAVE writers."""
import hashlib,json,struct,subprocess
from pathlib import Path
from progressive_wav24_fixture import fixture as legacy_fixture
PROFILES=tuple('audio-wav-ext24-'+c+'-'+str(r) for r in (44100,48000) for c in ('mono','stereo'))
PCM_GUID=bytes.fromhex('0100000000001000800000aa00389b71')
def manual_wave(pcm,rate,channels):
 fmt=struct.pack('<HHIIHHHHI',65534,channels,rate,rate*channels*3,channels*3,24,22,24,4 if channels==1 else 3)+PCM_GUID
 body=b'WAVEfmt '+struct.pack('<I',40)+fmt+b'data'+struct.pack('<I',len(pcm))+pcm+(b'\0' if len(pcm)%2 else b'')
 return b'RIFF'+struct.pack('<I',len(body))+body

def data_offset(raw):
 at=12
 while at+8<=len(raw):
  tag=raw[at:at+4];n=int.from_bytes(raw[at+4:at+8],'little')
  if tag==b'data':return at+8
  at+=8+n+(n&1)
 raise AssertionError('No WAVE data chunk')

def fixture(root,profile,seconds=20,writer='ffmpeg'):
 root=Path(root);model,_,legacy,probe=legacy_fixture(root/'legacy',profile.replace('audio-wav-ext24-','audio-wav24-'),seconds)
 original=root/'original-extensible.wav';pcm=(root/'legacy/input.pcm').read_bytes();rate=int(profile.split('-')[-1]);channels=2 if '-stereo-' in profile else 1
 if writer=='ffmpeg':
  subprocess.run(['ffmpeg','-v','error','-y','-i',str(legacy),'-map_metadata','-1','-bitexact','-c:a','copy',str(original)],check=True)
 else:original.write_bytes(manual_wave(pcm,rate,channels))
 raw=original.read_bytes();offset=data_offset(raw)
 assert raw[20:22]==b'\xfe\xff' and int.from_bytes(raw[16:20],'little')==40 and raw[offset:offset+len(pcm)]==pcm
 probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-of','json',str(original)]));probe.update(_pcm_bytes=len(pcm),_pcm_sha256=hashlib.sha256(pcm).hexdigest(),_data_offset=offset,_block_align=channels*3)
 decoded=subprocess.check_output(['ffmpeg','-v','error','-i',str(original),'-c:a','pcm_s24le','-f','s24le','-']);assert decoded==pcm
 model['tracks'][0].update(containerHint='wav-extensible-pcm24');model['resources'][0]['length']=None if channels==1 else str(len(raw));model['metadata']['title']='WAVEFORMATEXTENSIBLE '+profile
 return model,raw,original,probe

def prefix_at_seconds(raw,probe,seconds):return min(len(raw),probe['_data_offset']+int(seconds)*int(probe['streams'][0]['sample_rate'])*probe['_block_align'])
