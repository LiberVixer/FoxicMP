#!/usr/bin/env python3
"""Separate Opus mono/stereo fMP4 fixtures; never downloads or changes real media."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys


def generate(root):
    root=Path(root).resolve();report=[]
    for channels in (1,2):
        folder=root/('progressive-opus-'+str(channels));folder.mkdir(parents=True,exist_ok=True)
        audio=folder/'audio.mp4'
        expression='sin(2*PI*880*t)' if channels==1 else 'sin(2*PI*880*t)|sin(2*PI*440*t)'
        subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i',
            'aevalsrc='+expression+':s=48000:d=20','-vn','-c:a','libopus','-b:a','96k',
            '-frame_duration','20','-movflags','+delay_moov+default_base_moof+frag_keyframe',
            '-frag_duration','1000000',str(audio)],check=True)
        stream=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(audio)]))['streams'][0]
        assert stream['codec_name']=='opus' and stream['channels']==channels and stream['sample_rate']=='48000'
        report.append(dict(directory=folder.name,channels=channels,stream=stream,sha256=hashlib.sha256(audio.read_bytes()).hexdigest()))
    (root/'progressive-opus-fixtures.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':generate(sys.argv[1])
