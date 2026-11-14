"""Indexed VP9 alt-ref + AAC-LC: growing exact packet/clock/video/PCM checks."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile
from progressive_stream_hevc_aac_test import run as paired
from progressive_stream_vp9_aac_test import PROFILES
from progressive_stream_vp9_superframe_test import superframe
from progressive_stream_remux_worker_test import serve, probe, fixture as avc_fixture

MODE='continuous-vp9-superframe-aac-window-v1'

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def fixture(root,profile,seconds=24):
    name,rate,channels,fps,pixels,tag,audio_first,count=profile
    samples=rate*seconds-137 if count is None else count
    root=Path(root);video=root/(name+'-video.mp4');output=root/(name+'.mp4')
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={fps}:duration={seconds}',
          '-an','-c:v','libvpx-vp9','-threads','2','-filter_threads','1','-cpu-used','4','-b:v','200k',
          '-g','50','-lag-in-frames','25','-auto-alt-ref','1','-passlogfile',str(root/(name+'-pass')),
          '-pix_fmt',pixels,'-profile:v','2' if pixels=='yuv420p10le' else '0']
    for tail in [['-pass','1','-f','null','-'],['-pass','2','-video_track_timescale','60000','-tag:v',tag,'-movflags','+faststart',str(video)]]:
        done=subprocess.run(args+tail,capture_output=True,timeout=120)
        assert done.returncode==0,(done.returncode,done.stderr)
    args=['ffmpeg','-v','error','-y','-i',str(video),'-f','lavfi','-i',f'sine=frequency=880:sample_rate={rate}:duration={seconds}',
          '-map','1:a' if audio_first else '0:v','-map','0:v' if audio_first else '1:a',
          '-c:v','copy','-c:a','aac','-ac',str(channels),'-af',f'atrim=end_sample={samples}',
          '-video_track_timescale','60000','-movie_timescale',str(math.lcm(60000,rate)),
          '-tag:v',tag,'-movflags','+faststart',str(output)]
    done=subprocess.run(args,capture_output=True,timeout=120)
    assert done.returncode==0,(done.returncode,done.stderr)
    assert indexed_count(output)>0,'Every positive fixture must contain actual indexed superframes'
    return output,samples

def indexed_count(path):
    raw=Path(path).read_bytes()
    return sum(superframe(raw[int(p['pos']):int(p['pos'])+int(p['size'])]) for p in probe(path)['packets'] if p['codec_type']=='video')

def run(worker,report,consumer_kind=None,runner=None):
    info=json.loads(subprocess.check_output([str(worker),'--info']))
    assert info['contract']['profiles'][MODE]['pcmWindowRequired'] is True
    assert info['contract']['profiles'][MODE]['superframes'] is True
    indexed={}
    def measured_fixture(root,profile,seconds=24):
        path,samples=fixture(root,profile,seconds=seconds)
        indexed[profile[0]]=indexed_count(path);return path,samples
    result=paired(worker,report,consumer_kind,runner,profiles=PROFILES,make_fixture=measured_fixture,mode=MODE,video_codec='vp9',verify_partial_video=True)
    for row in result['rows'][:6]:row['indexedSuperframePackets']=indexed[row['profile']]
    rows=result['rows'];report=Path(report)
    with tempfile.TemporaryDirectory(prefix='fox-vp9-superframe-aac-invalid-') as tmp:
        root=Path(tmp);source,_=fixture(root,PROFILES[0],seconds=5);raw=source.read_bytes();meta=probe(source)
        damaged=bytearray(raw);damaged[damaged.index(b'vpcC')+4]=0
        bad=root/'bad-vpcc.mp4';bad.write_bytes(damaged)
        last_video=next(p for p in reversed(meta['packets']) if p['codec_type']=='video')
        truncated=root/'truncated.mp4';truncated.write_bytes(raw[:int(last_video['pos'])])
        avc=avc_fixture(root,'actual-avc',bframes=2,audio=True)
        invalids=[('bad-vpcc',bad,['unsupported-stream-vp9-configuration']),
                  ('truncated-complete',truncated,['incomplete-stream-vp9-samples']),
                  ('actual-avc',avc,['unsupported-stream-codec'])]
        packet=next(p for p in meta['packets'] if p['codec_type']=='video' and superframe(raw[int(p['pos']):int(p['pos'])+int(p['size'])]))
        pos=int(packet['pos']);size=int(packet['size']);marker=raw[pos+size-1];count=(marker&7)+1;mag=((marker>>3)&3)+1;index=pos+size-(2+count*mag)
        first_length=int.from_bytes(raw[index+1:index+1+mag],'little')
        for label,offset,value,reasons in [
          ('hidden-marked-shown',pos,raw[pos]|2,['unsupported-stream-vp9-frame']),
          ('last-marked-hidden',pos+first_length,raw[pos+first_length]&~2,['unsupported-stream-vp9-frame','unsupported-stream-vp9-intra-only']),
          ('hidden-show-existing',pos,raw[pos]|8,['unsupported-stream-vp9-frame']),
          ('zero-index-length',index+1,0,['unsupported-stream-vp9-superframe-index','invalid-media'])]:
            content=bytearray(raw);content[offset]=value
            if label=='zero-index-length':content[index+1:index+1+mag]=bytes(mag)
            invalid=root/(label+'.mp4');invalid.write_bytes(content);invalids.append((label,invalid,reasons))
        for name,path,reasons in invalids:
            before=sha(path);row,_=serve(worker,path,mode=MODE,growing=False,on_audio_window=lambda _:True)
            assert row['exitCode']==1 and row['result']['code'] in reasons and sha(path)==before,row
            row['profile']='reject-'+name;rows.append(row)
        for selected,reasons in [('continuous-vp9-aac-window-v1',['unsupported-stream-vp9-superframe','unsupported-stream-vp9-frame']),
                                 ('continuous-vp9-superframe-video-v1',['unsupported-stream-count'])]:
            row,_=serve(worker,source,mode=selected,growing=False,on_audio_window=lambda _:True)
            assert row['exitCode']==1 and row['result']['code'] in reasons,row
            row['profile']='old-mode-still-rejects-'+selected;rows.append(row)
        row,_=serve(worker,root/(PROFILES[0][0]+'-video.mp4'),mode=MODE,growing=False,on_audio_window=lambda _:True)
        assert row['exitCode']==1 and row['result']['code']=='unsupported-stream-count',row
        row['profile']='reject-missing-audio';rows.append(row)
    result.update(count=len(rows),sharedTestSourceSha256=result['testSourceSha256'],testSourceSha256=sha(__file__),workerInfo=info,
                  generatedFixturesRequireIndexedSuperframes=True)
    (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--worker',required=True);p.add_argument('--report-directory',required=True)
    p.add_argument('--consumer',choices=['qt','classic']);p.add_argument('--runner');a=p.parse_args()
    if a.consumer and not a.runner:p.error('--consumer requires --runner')
    r=run(Path(a.worker).resolve(),a.report_directory,a.consumer,a.runner);print(json.dumps(dict(result=r['result'],count=r['count'])))
