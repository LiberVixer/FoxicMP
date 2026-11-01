"""Real native FLAC files with the existing independent integer PCM oracle."""
import json,subprocess
from pathlib import Path
from progressive_standalone_flac_fixture import fixture as fmp4_fixture
PROFILES=tuple('audio-raw-flac-'+c+'-'+str(b)+'-'+str(r) for r in (44100,48000) for c in ('mono','stereo') for b in (16,24))
def fixture(root,profile,seconds=20):
    root=Path(root);m,_,_,oracle=fmp4_fixture(root,profile.replace('audio-raw-flac-','audio-flac-'),seconds);rate=int(profile.split('-')[-1]);bits=int(profile.split('-')[-2]);channels=2 if '-stereo-' in profile else 1;out=root/'original.flac'
    subprocess.run(['ffmpeg','-v','error','-y','-f',oracle['_pcm_format'],'-ar',str(rate),'-ac',str(channels),'-i',str(root/'input.pcm'),'-c:a','flac','-sample_fmt','s16' if bits==16 else 's32','-bits_per_raw_sample',str(bits),'-compression_level','12' if bits==24 else '0','-f','flac',str(out)],check=True,capture_output=True)
    raw=out.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(out)]));probe.update({k:v for k,v in oracle.items() if k.startswith('_pcm_')})
    m['tracks'][0].update(containerHint='flac');m['resources'][0]['length']=None if channels==1 else str(len(raw));m['metadata']['title']='Raw FLAC '+profile
    return m,raw,out,probe

def prefix_at_seconds(raw,probe,seconds):
    from fractions import Fraction
    clock=Fraction(probe['streams'][0]['time_base'])
    for p in probe['packets']:
        if (int(p['pts'])+int(p['duration']))*clock>=seconds:return int(p['pos'])+int(p['size'])
    raise ValueError('Insufficient raw FLAC duration')
