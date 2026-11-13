"""AV1/AAC-LC continuous ordinary MP4: exact paired packets and PCM windows."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile
from progressive_stream_hevc_aac_test import run as paired
from progressive_stream_remux_worker_test import serve, probe, fixture as avc_fixture

MODE='continuous-av1-aac-window-v1'
PROFILES=[('main-48-mono',48000,1,'25','yuv420p','av01',False,None),
          ('main10-48-stereo-frac',48000,2,'30000/1001','yuv420p10le','av01',False,None),
          ('main-441-mono',44100,1,'25','yuv420p','av01',False,None),
          ('main10-441-stereo',44100,2,'30000/1001','yuv420p10le','av01',False,None),
          ('one-sample-audio-first',44100,1,'25','yuv420p','av01',True,1),
          ('short-audio-main10',48000,2,'25','yuv420p10le','av01',False,48000*4-137)]

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def fixture(root,profile,seconds=24,lag=0):
    name,rate,channels,fps,pixels,tag,audio_first,count=profile
    samples=rate*seconds-137 if count is None else count
    path=Path(root)/(name+'.mp4')
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={fps}:duration={seconds}',
          '-f','lavfi','-i',f'sine=frequency=880:sample_rate={rate}:duration={seconds}',
          '-map','1:a' if audio_first else '0:v','-map','0:v' if audio_first else '1:a',
          '-c:v','libaom-av1','-threads','2','-filter_threads','1','-cpu-used','8','-row-mt','1',
          '-pix_fmt',pixels,'-crf','40','-b:v','0','-g','25','-lag-in-frames',str(lag),'-tag:v',tag,
          '-c:a','aac','-ac',str(channels),'-af',f'atrim=end_sample={samples}',
          '-video_track_timescale','60000','-movie_timescale',str(math.lcm(60000,rate)),
          '-movflags','+faststart',str(path)]
    p=subprocess.run(args,capture_output=True,timeout=120)
    assert p.returncode==0,(p.returncode,p.stderr)
    return path,samples

def run(worker,report,consumer_kind=None,runner=None):
    info=json.loads(subprocess.check_output([str(worker),'--info']))
    assert info['contract']['profiles'][MODE]['pcmWindowRequired'] is True
    result=paired(worker,report,consumer_kind,runner,profiles=PROFILES,make_fixture=fixture,mode=MODE,video_codec='av1')
    rows=result['rows'];report=Path(report)
    with tempfile.TemporaryDirectory(prefix='fox-av1-aac-invalid-') as tmp:
        root=Path(tmp);source,_=fixture(root,PROFILES[0],seconds=5);raw=source.read_bytes()
        damaged=bytearray(raw);damaged[damaged.index(b'av1C')+4]=1
        bad=root/'bad-av1c.mp4';bad.write_bytes(damaged)
        probes=probe(source);last_video=next(p for p in reversed(probes['packets']) if p['codec_type']=='video')
        truncated=root/'truncated.mp4';truncated.write_bytes(raw[:int(last_video['pos'])])
        avc=avc_fixture(root,'actual-avc',bframes=2,audio=True)
        hidden,_=fixture(root,('hidden',*PROFILES[0][1:]),seconds=5,lag=25)
        for name,path,reasons in [('bad-av1c',bad,['unsupported-stream-av1-configuration']),
                 ('truncated-complete',truncated,['incomplete-stream-av1-samples']),
                 ('actual-avc',avc,['unsupported-stream-codec']),
                 ('hidden-or-multiple-frame-obus',hidden,['unsupported-stream-av1-frame','unsupported-stream-av1-packet'])]:
            before=sha(path)
            row,_=serve(worker,path,mode=MODE,growing=False,on_audio_window=lambda _:True)
            assert row['exitCode']==1 and row['result']['code'] in reasons and sha(path)==before,row
            row['profile']='reject-'+name;rows.append(row)
    result.update(count=len(rows),sharedTestSourceSha256=result['testSourceSha256'],testSourceSha256=sha(__file__),workerInfo=info)
    (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--worker',required=True);p.add_argument('--report-directory',required=True)
    p.add_argument('--consumer',choices=['qt','classic']);p.add_argument('--runner');a=p.parse_args()
    if a.consumer and not a.runner:p.error('--consumer requires --runner')
    r=run(Path(a.worker).resolve(),a.report_directory,a.consumer,a.runner);print(json.dumps(dict(result=r['result'],count=r['count'])))
