"""90-second local VOD for multi-audio seeks beyond the former 10-second limit."""
from pathlib import Path
import subprocess

def generate(root):
    folder=Path(root)/'progressive-seek';folder.mkdir(parents=True,exist_ok=True)
    jobs=[
      ('video.mp4',['-f','lavfi','-i','testsrc2=size=320x180:rate=25:duration=90','-an','-c:v','libx264','-threads','2','-g','25','-keyint_min','25','-sc_threshold','0','-movflags','+empty_moov+default_base_moof+frag_keyframe']),
      ('audio.m4a',['-f','lavfi','-i','sine=frequency=880:sample_rate=48000:duration=90','-vn','-c:a','aac','-movflags','+empty_moov+default_base_moof+frag_keyframe','-frag_duration','1000000']),
      ('opus.mp4',['-f','lavfi','-i','aevalsrc=sin(2*PI*440*t)|sin(2*PI*660*t):s=48000:d=90','-vn','-c:a','libopus','-b:a','96k','-frame_duration','20','-movflags','+delay_moov+default_base_moof+frag_keyframe','-frag_duration','1000000']),
      ('opus.webm',['-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=90','-c:a','libopus','-f','webm','-cluster_time_limit','1000','-live','1']),
      ('vorbis.webm',['-f','lavfi','-i','aevalsrc=sin(2*PI*880*t)|sin(2*PI*440*t):s=48000:d=90','-c:a','libvorbis','-q:a','4','-f','webm','-cluster_time_limit','1000','-live','1']),
      ('video.webm',['-f','lavfi','-i','testsrc2=size=320x180:rate=25:duration=90','-an','-c:v','libvpx-vp9','-deadline','realtime','-cpu-used','8','-threads','2','-g','25','-f','webm','-cluster_time_limit','1000','-live','1']),
    ]
    for name,codec,pixels,extra in [
      ('video-hevc10.mp4','libx265','yuv420p10le',['-preset','fast','-x265-params','pools=1:frame-threads=1:log-level=error:keyint=25:min-keyint=25:scenecut=0','-tag:v','hev1','-movflags','+empty_moov+default_base_moof+frag_keyframe']),
      ('video-av1-10.mp4','libaom-av1','yuv420p10le',['-cpu-used','8','-row-mt','1','-g','25','-keyint_min','25','-crf','40','-b:v','0','-lag-in-frames','0','-movflags','+delay_moov+default_base_moof+frag_keyframe']),
      ('video-vp8.webm','libvpx','yuv420p',['-deadline','realtime','-cpu-used','8','-g','25','-auto-alt-ref','0','-f','webm','-cluster_time_limit','1000','-live','1']),
    ]:
      jobs.append((name,['-f','lavfi','-i','testsrc2=size=320x180:rate=25:duration=90','-an','-c:v',codec,'-threads','2','-filter_threads','1','-pix_fmt',pixels,*extra]))
    for name,args in jobs:
      out=folder/name
      if not out.exists():
        temporary=out.with_name('.'+out.stem+'.generating'+out.suffix)
        subprocess.run(['ffmpeg','-v','error','-y',*args,str(temporary)],check=True)
        temporary.replace(out)
if __name__=='__main__':
    import sys
    generate(sys.argv[1])
