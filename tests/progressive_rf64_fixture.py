"""RF64 with independent FFmpeg/manual headers and Python integer PCM oracle."""
import hashlib,json,struct,subprocess
from pathlib import Path
from progressive_wav_fixture import fixture as pcm16_fixture
from progressive_wav_ext24_fixture import fixture as pcm24_fixture,data_offset
PROFILES=tuple('audio-rf64-'+str(b)+'-'+c+'-'+str(r) for b in (16,24) for r in (44100,48000) for c in ('mono','stereo'))
def manual_rf64(wave,samples):
 offset=data_offset(wave);n=int.from_bytes(wave[offset-4:offset],'little');body=bytearray(wave[12:]);struct.pack_into('<I',body,offset-16,0xffffffff)
 ds64=struct.pack('<QQQI',len(wave)+28,n,samples,0)
 return b'RF64'+b'\xff'*4+b'WAVEds64'+struct.pack('<I',28)+ds64+body

def fixture(root,profile,seconds=20,writer='ffmpeg'):
 root=Path(root);bits=int(profile.split('-')[2]);channels=2 if '-stereo-' in profile else 1;rate=int(profile.split('-')[-1])
 if bits==16:model,base,legacy,probe=pcm16_fixture(root/'base',profile.replace('audio-rf64-16-','audio-wav-'),seconds)
 else:model,base,legacy,probe=pcm24_fixture(root/'base',profile.replace('audio-rf64-24-','audio-wav-ext24-'),seconds,writer='manual')
 pcm=base[probe['_data_offset']:probe['_data_offset']+probe['_pcm_bytes']];original=root/'original-rf64.wav'
 if writer=='ffmpeg':subprocess.run(['ffmpeg','-v','error','-y','-i',str(legacy),'-map_metadata','-1','-bitexact','-c:a','copy','-rf64','always',str(original)],check=True)
 else:original.write_bytes(manual_rf64(base,rate*seconds))
 raw=original.read_bytes();offset=data_offset(raw);assert raw[:4]==b'RF64' and raw[12:16]==b'ds64' and raw[offset:offset+len(pcm)]==pcm
 assert int.from_bytes(raw[20:28],'little')+8==len(raw) and int.from_bytes(raw[28:36],'little')==len(pcm) and int.from_bytes(raw[36:44],'little')==seconds*rate
 probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-of','json',str(original)]));probe.update(_pcm_bytes=len(pcm),_pcm_sha256=hashlib.sha256(pcm).hexdigest(),_data_offset=offset,_block_align=channels*bits//8,_bits=bits)
 decoded=subprocess.check_output(['ffmpeg','-v','error','-i',str(original),'-c:a','pcm_s'+str(bits)+'le','-f','s'+str(bits)+'le','-']);assert decoded==pcm
 model['tracks'][0].update(containerHint='rf64-pcm'+str(bits));model['resources'][0]['length']=None if channels==1 else str(len(raw));model['metadata']['title']='RF64 '+profile
 return model,raw,original,probe

def prefix_at_seconds(raw,probe,seconds):return min(len(raw),probe['_data_offset']+int(seconds)*int(probe['streams'][0]['sample_rate'])*probe['_block_align'])
