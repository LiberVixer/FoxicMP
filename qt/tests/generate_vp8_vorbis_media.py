#!/usr/bin/env python3
"""Offline bounded VP8 and mono/stereo Vorbis fixtures for progressive WebM."""
from pathlib import Path
import hashlib,json,subprocess,sys

def generate(root):
    root=Path(root).resolve();folder=root/'progressive-webm-vp8';folder.mkdir(parents=True,exist_ok=True)
    jobs=[(folder/'video.webm',['-f','lavfi','-i','testsrc2=size=320x180:rate=25:duration=20','-an','-c:v','libvpx','-deadline','realtime','-cpu-used','8','-threads','2','-g','25','-auto-alt-ref','0'])]
    for channels in (1,2):
        folder=root/('progressive-webm-vorbis-'+str(channels));folder.mkdir(parents=True,exist_ok=True)
        jobs.append((folder/'audio.webm',['-f','lavfi','-i','aevalsrc=sin(2*PI*880*t)|sin(2*PI*440*t):s=48000:d=20','-ac',str(channels),'-c:a','libvorbis','-q:a','4']))
    report=[]
    for out,args in jobs:
        subprocess.run(['ffmpeg','-v','error','-y',*args,'-f','webm','-cluster_time_limit','1000','-live','1',str(out)],check=True)
        streams=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(out)]))['streams']
        report.append(dict(name=str(out.relative_to(root)),sha256=hashlib.sha256(out.read_bytes()).hexdigest(),streams=streams))
    (root/'progressive-vp8-vorbis-fixtures.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':generate(sys.argv[1])
