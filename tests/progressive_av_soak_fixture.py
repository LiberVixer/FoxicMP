"""One immutable, bounded ten-minute A/V source shared by soak editions."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import subprocess

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def create(directory):
    directory.mkdir(parents=True,exist_ok=False)
    path=directory/'reference.mp4'
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=640x360:rate=25:duration=600',
        '-f','lavfi','-i','sine=sample_rate=48000:frequency=997:duration=600',
        '-c:v','libx264','-threads','2','-preset','veryfast','-b:v','100k','-maxrate','140k','-bufsize','200k',
        '-bf','3','-g','50','-keyint_min','50','-sc_threshold','0',
        '-c:a','aac','-b:a','64k','-ac','2','-af','atrim=end_sample=28799863',
        '-video_track_timescale','60000','-movie_timescale','240000','-movflags','+faststart',str(path)]
    run=subprocess.run(args,capture_output=True);(directory/'encode.exit').write_text(str(run.returncode)+'\n')
    (directory/'encode.log').write_bytes(run.stdout+run.stderr);assert run.returncode==0,run.stderr.decode(errors='replace')
    result=subprocess.run(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(path)],capture_output=True)
    assert result.returncode==0,result.stderr
    probe=json.loads(result.stdout);(directory/'packets.json').write_bytes(result.stdout)
    counts={kind:sum(p['codec_type']==kind for p in probe['packets']) for kind in ['video','audio']}
    assert sum(counts.values())<50000 and path.stat().st_size<60*1024**2
    video=[p for p in probe['packets'] if p['codec_type']=='video'];cuts={};cursor=0
    scale=Fraction(next(s['time_base'] for s in probe['streams'] if s['codec_type']=='video'))
    for second in range(2,600,2):
        while int(video[cursor]['pts'])*scale<second:cursor+=1
        cuts[str(second)]=int(video[cursor]['pos'])+13
    value=dict(result='PASS',seconds=600,video='h264',width=640,height=360,fps='25',bframes=3,
        audio='aac-lc',sampleRate=48000,channels=2,presentationSamples='28799863',
        bytes=path.stat().st_size,sha256=sha(path),packetCounts=counts,cuts=cuts,
        packetsSha256=sha(directory/'packets.json'),sourceSha256=sha(__file__),encodeArgs=args,
        ffmpegVersion=subprocess.check_output(['ffmpeg','-version']).decode().splitlines()[0],
        ffprobeVersion=subprocess.check_output(['ffprobe','-version']).decode().splitlines()[0])
    (directory/'fixture.json').write_text(json.dumps(value,indent=2)+'\n')
    print(json.dumps({k:v for k,v in value.items() if k not in ('cuts','encodeArgs')}))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path);create(parser.parse_args().directory.resolve())
