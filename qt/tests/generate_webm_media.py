#!/usr/bin/env python3
"""Local streaming-style WebM fixtures copied/remuxed from the qualified corpus."""
from pathlib import Path
import hashlib,json,subprocess,sys

def generate(root):
    root=Path(root).resolve();report=[]
    for depth in (8,10):
        folder=root/('progressive-webm-vp9-'+str(depth));folder.mkdir(parents=True,exist_ok=True)
        for kind,source in [('video',root/('progressive-vp9-'+str(depth))/'video.mp4'),('audio',root/'progressive-opus-2/audio.mp4')]:
            out=folder/(kind+'.webm')
            args=['-i',str(source),'-map','0:0','-c','copy'] if kind=='video' else ['-f','lavfi','-i','aevalsrc=sin(2*PI*880*t)|sin(2*PI*440*t):s=48000:d=20','-c:a','libopus','-b:a','96k']
            subprocess.run(['ffmpeg','-v','error','-y',*args,'-f','webm',
                '-cluster_time_limit','1000','-live','1',str(out)],check=True)
            stream=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(out)]))['streams'][0]
            report.append(dict(directory=folder.name,track=kind,stream=stream,sha256=hashlib.sha256(out.read_bytes()).hexdigest()))
    (root/'progressive-webm-fixtures.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
if __name__=='__main__':generate(sys.argv[1])
