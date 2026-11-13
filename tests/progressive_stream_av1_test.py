"""Bounded ordinary AV1 MP4 copy-remux: growing packet/clock/pixel fidelity."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from progressive_stream_remux_worker_test import probe, serve, fixture as avc_fixture
from progressive_stream_reorder_test import media_prefix
from progressive_stream_hevc_test import frames
from progressive_stream_aac_aligned_test import fragment_durations

MODE='continuous-av1-video-v1'
PROFILES=[('main8-25','25','yuv420p','av01'),('main10-25','25','yuv420p10le','av01'),
          ('main8-fractional','30000/1001','yuv420p','av01'),('main10-fractional','30000/1001','yuv420p10le','av01')]
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def fixture(root,name,rate='25',pixels='yuv420p',tag='av01',lag=0):
    path=Path(root)/(name+'.mp4')
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={rate}:duration=24',
          '-an','-c:v','libaom-av1','-threads','2','-filter_threads','1','-cpu-used','8','-row-mt','1',
          '-pix_fmt',pixels,'-crf','40','-b:v','0','-g','25','-lag-in-frames',str(lag),
          '-video_track_timescale','60000','-tag:v',tag,'-movflags','+faststart',str(path)]
    result=subprocess.run(args,capture_output=True,timeout=120)
    assert result.returncode==0,(result.returncode,result.stderr)
    return path

def packets(path):
    d=probe(path);assert len(d['streams'])==1 and d['streams'][0]['codec_name']=='av1'
    stream=d['streams'][0];clock=Fraction(stream['time_base'])
    table=fragment_durations(Path(path).read_bytes()).get(int(stream['id'],0))
    if table is not None:assert len(table)==len(d['packets'])
    rows=[]
    for i,p in enumerate(d['packets']):
        # FFmpeg6 omits AV1 packet durations for some fragmented outputs.
        # Read independent ISO trun/trex values rather than invent a frame rate.
        duration=table[i] if table is not None else int(p['duration'])
        if 'duration' in p:assert int(p['duration'])==duration
        rows.append([p['data_hash'],str(int(p['pts'])*clock),str(int(p['dts'])*clock),str(duration*clock)])
    return rows

def run(worker,report):
    report=Path(report);report.mkdir(parents=True,exist_ok=True);rows=[]
    info=json.loads(subprocess.check_output([str(worker),'--info']))
    assert info['contract']['profiles'][MODE]['oneDisplayedFramePerSample'] is True
    with tempfile.TemporaryDirectory(prefix='fox-av1-stream-') as name:
        root=Path(name);sources=[]
        for label,rate,pixels,tag in PROFILES:
            source=fixture(root,label,rate,pixels,tag);sources.append(source)
            before=sha(source);reference=packets(source);decoded=frames(source,pixels);partials=[]
            def checkpoint(raw,state):
                raw=media_prefix(raw)
                if not raw or state['available']>=source.stat().st_size:return
                digest=hashlib.sha256(raw).hexdigest()
                if any(p['sha256']==digest for p in partials):return
                p=root/(label+'-partial.mp4');p.write_bytes(raw);actual=packets(p);images=frames(p,pixels)
                assert 0<len(actual)<len(reference) and actual==reference[:len(actual)]
                assert images==decoded[:len(images)] and len(images)==len(actual)
                partials.append(dict(packets=len(actual),frames=len(images),sha256=digest,sourceAvailable=state['available']))
            row,data=serve(worker,source,mode=MODE,short_reads=True,on_wait=checkpoint)
            assert row['exitCode']==0 and row['result']['code']=='data',row
            assert row['firstCompleteFragmentBeforeSourceComplete'] and len(partials)>=3,row
            out=root/(label+'-output.mp4');out.write_bytes(data)
            actual=probe(out)['streams'][0]
            assert actual['codec_tag_string']=='av01' and actual['pix_fmt']==pixels
            assert packets(out)==reference and frames(out,pixels)==decoded
            assert sha(source)==before and sha(out)==row['result']['sha256']
            row.update(profile=label,pixelFormat=pixels,sourceSha256=before,partials=partials,
                       exactPayloadPtsDtsDuration=True,decodedFramesIdentical=True)
            rows.append(row);(report/'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in rows])))
        for mode in ['continuous-h264-video-v1','continuous-hevc-reordered-video-v1']:
            row,data=serve(worker,sources[0],mode=mode,growing=False)
            assert row['exitCode']==1 and row['result']['code']=='unsupported-stream-codec' and not data,row
            row['profile']='reject-through-'+mode;rows.append(row)
        avc=avc_fixture(root,'actual-avc')
        row,data=serve(worker,avc,mode=MODE,growing=False)
        assert row['exitCode']==1 and row['result']['code']=='unsupported-stream-codec' and not data,row
        row['profile']='reject-actual-avc';rows.append(row)
        raw=sources[0].read_bytes();at=raw.index(b'av1C')+4
        for label,offset,value in [('marker',0,1),('profile',1,32),('chroma',2,8),('depth-mismatch',2,0x4c),('presentation-delay',3,16)]:
            content=bytearray(raw);content[at+offset]=value;p=root/(label+'.mp4');p.write_bytes(content)
            row,_=serve(worker,p,mode=MODE,growing=False)
            assert row['exitCode']==1 and row['result']['code']=='unsupported-stream-av1-configuration',row
            row['profile']='reject-config-'+label;rows.append(row)
        original=probe(sources[0]);p=root/'truncated.mp4';p.write_bytes(raw[:int(original['packets'][-1]['pos'])])
        row,_=serve(worker,p,mode=MODE,growing=False)
        assert row['exitCode']==1 and row['result']['code']=='incomplete-stream-av1-samples',row
        row['profile']='reject-truncated-complete';rows.append(row)
        for fault,reason in [('cancel-wait','cancelled'),('cancel-write','cancelled'),('integrity','integrity-mismatch')]:
            row,_=serve(worker,sources[0],mode=MODE,fault=fault)
            assert row['exitCode']==1 and row['result']['code']==reason,row
            row['profile']=fault;rows.append(row)
        lagged=fixture(root,'hidden',lag=25)
        row,_=serve(worker,lagged,mode=MODE,growing=False)
        assert row['exitCode']==1 and row['result']['code'] in ['unsupported-stream-av1-packet','unsupported-stream-av1-frame'],row
        row['profile']='reject-hidden-or-multiple-frame-obus';rows.append(row)
    result=dict(result='PASS',count=len(rows),rows=rows,mode=MODE,workerSha256=sha(worker),testSourceSha256=sha(__file__),publicPlayerTested=False,workerInfo=info)
    (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--worker',required=True);p.add_argument('--report-directory',required=True);a=p.parse_args()
    r=run(Path(a.worker).resolve(),a.report_directory);print(json.dumps(dict(result=r['result'],count=r['count'])))
