"""VP9/AAC-LC continuous ordinary MP4: exact paired packets and PCM windows."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile
from progressive_stream_hevc_aac_test import run as paired
from progressive_stream_remux_worker_test import serve, probe, fixture as avc_fixture

MODE='continuous-vp9-aac-window-v1'
PROFILES=[('main-48-mono',48000,1,'25','yuv420p','vp09',False,None),
          ('main10-48-stereo-frac',48000,2,'30000/1001','yuv420p10le','vp09',False,None),
          ('main-441-mono',44100,1,'25','yuv420p','vp09',False,None),
          ('main10-441-stereo',44100,2,'30000/1001','yuv420p10le','vp09',False,None),
          ('one-sample-audio-first',44100,1,'25','yuv420p','vp09',True,1),
          ('short-audio-main10',48000,2,'25','yuv420p10le','vp09',False,48000*4-137)]

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def fixture(root,profile,seconds=24,lag=0):
    name,rate,channels,fps,pixels,tag,audio_first,count=profile
    samples=rate*seconds-137 if count is None else count
    path=Path(root)/(name+'.mp4')
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={fps}:duration={seconds}',
          '-f','lavfi','-i',f'sine=frequency=880:sample_rate={rate}:duration={seconds}',
          '-map','1:a' if audio_first else '0:v','-map','0:v' if audio_first else '1:a',
          '-c:v','libvpx-vp9','-threads','2','-filter_threads','1','-cpu-used','6','-row-mt','1',
          '-pix_fmt',pixels,'-crf','40','-b:v','0','-g','25','-lag-in-frames',str(lag),'-auto-alt-ref','0','-profile:v','2' if pixels=='yuv420p10le' else '0','-tag:v',tag,
          '-c:a','aac','-ac',str(channels),'-af',f'atrim=end_sample={samples}',
          '-video_track_timescale','60000','-movie_timescale',str(math.lcm(60000,rate)),
          '-movflags','+faststart',str(path)]
    p=subprocess.run(args,capture_output=True,timeout=120)
    assert p.returncode==0,(p.returncode,p.stderr)
    return path,samples

def run(worker,report,consumer_kind=None,runner=None):
    info=json.loads(subprocess.check_output([str(worker),'--info']))
    assert info['contract']['profiles'][MODE]['pcmWindowRequired'] is True
    result=paired(worker,report,consumer_kind,runner,profiles=PROFILES,make_fixture=fixture,mode=MODE,video_codec='vp9')
    rows=result['rows'];report=Path(report)
    with tempfile.TemporaryDirectory(prefix='fox-vp9-aac-invalid-') as tmp:
        root=Path(tmp);source,_=fixture(root,PROFILES[0],seconds=5);raw=source.read_bytes()
        damaged=bytearray(raw);damaged[damaged.index(b'vpcC')+4]=0
        bad=root/'bad-vpcc.mp4';bad.write_bytes(damaged)
        probes=probe(source);last_video=next(p for p in reversed(probes['packets']) if p['codec_type']=='video')
        truncated=root/'truncated.mp4';truncated.write_bytes(raw[:int(last_video['pos'])])
        avc=avc_fixture(root,'actual-avc',bframes=2,audio=True)
        from progressive_stream_vp9_test import superframe_fixture,superframe
        video=superframe_fixture(root);hidden=root/'superframe-aac.mp4'
        args=['ffmpeg','-v','error','-y','-i',str(video),'-f','lavfi','-i','sine=frequency=880:sample_rate=48000:duration=24',
              '-map','0:v','-map','1:a','-c:v','copy','-c:a','aac','-ac','1','-af','atrim=end_sample=1151863',
              '-video_track_timescale','60000','-movie_timescale','240000','-tag:v','vp09','-movflags','+faststart',str(hidden)]
        done=subprocess.run(args,capture_output=True,timeout=120);assert done.returncode==0,done.stderr
        body=hidden.read_bytes();meta=probe(hidden)
        assert any(superframe(body[int(p['pos']):int(p['pos'])+int(p['size'])]) for p in meta['packets'] if p['codec_type']=='video')
        for name,path,reasons in [('bad-vpcc',bad,['unsupported-stream-vp9-configuration']),
                 ('truncated-complete',truncated,['incomplete-stream-vp9-samples']),
                 ('actual-avc',avc,['unsupported-stream-codec']),
                 ('hidden-or-superframe',hidden,['unsupported-stream-vp9-frame','unsupported-stream-vp9-superframe'])]:
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
