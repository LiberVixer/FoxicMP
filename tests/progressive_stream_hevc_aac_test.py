"""HEVC/AAC-LC copy-remux: two packet clocks and an exact immutable PCM window."""
import argparse
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile

from progressive_stream_remux_worker_test import probe, serve
from progressive_stream_aac_window_test import packets, reference, consumer
from progressive_stream_hevc_test import frames
from progressive_mux_mp4_fixture import boxes

MODE='continuous-hevc-aac-window-v1'
PROFILES=[('main-48-mono',48000,1,'25','yuv420p','hvc1',False,None),
          ('main10-48-stereo-frac',48000,2,'30000/1001','yuv420p10le','hev1',False,None),
          ('main-441-mono',44100,1,'25','yuv420p','hev1',False,None),
          ('main10-441-stereo',44100,2,'30000/1001','yuv420p10le','hvc1',False,None),
          ('one-sample-audio-first',44100,1,'25','yuv420p','hvc1',True,1),
          ('short-audio-main10',48000,2,'25','yuv420p10le','hev1',False,48000*4-137)]


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fixture(root,profile,seconds=24):
    name,rate,channels,fps,pixels,tag,audio_first,count=profile
    samples=rate*seconds-137 if count is None else count
    path=Path(root)/(name+'.mp4')
    args=['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=160x90:rate={fps}:duration={seconds}',
          '-f','lavfi','-i',f'sine=frequency=880:sample_rate={rate}:duration={seconds}',
          '-map','1:a' if audio_first else '0:v','-map','0:v' if audio_first else '1:a',
          '-c:v','libx265','-threads','1','-filter_threads','1','-preset','fast','-pix_fmt',pixels,'-tag:v',tag,
          '-x265-params','pools=1:frame-threads=1:log-level=error:keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes=2',
          '-c:a','aac','-ac',str(channels),'-af',f'atrim=end_sample={samples}',
          '-video_track_timescale','60000','-movie_timescale',str(math.lcm(60000,rate)),
          '-movflags','+faststart',str(path)]
    process=subprocess.run(args,capture_output=True,timeout=90)
    assert process.returncode==0,(process.returncode,process.stderr)
    return path,samples


def run(worker,report,consumer_kind=None,runner=None):
    report=Path(report);report.mkdir(parents=True,exist_ok=True);rows=[]
    with tempfile.TemporaryDirectory(prefix='fox-hevc-aac-') as temporary:
        root=Path(temporary);sources=[]
        for profile in PROFILES:
            name,rate,channels,fps,pixels,tag,first,_=profile
            folder=root/name;folder.mkdir();seconds=5 if consumer_kind else 24
            source,samples=fixture(folder,profile,seconds=seconds);sources.append(source)
            before=sha(source);windows=[]
            def accept(window):
                assert not windows
                assert window['sampleRate']==rate and window['channels']==channels
                assert int(window['rawBegin'])==1024 and int(window['timestampEnd'])==samples
                assert int(window['rawEnd'])==samples+1024
                windows.append(window);return True
            row,data=serve(worker,source,mode=MODE,on_audio_window=accept,short_reads=True,
                           cut_seconds=(1,2,3,4) if consumer_kind else (4,8,12,20))
            assert row['exitCode']==0 and row['result']['code']=='data',row
            assert row['firstCompleteFragmentBeforeSourceComplete'] and len(row['waits'])>=4,row
            assert row['result']['pcmWindowRequired'] and row['result']['aacPcmWindow']==windows[0]
            output=folder/'output.mp4';output.write_bytes(data)
            original,actual=packets(source),packets(output)
            assert original['video']==actual['video']
            assert [p[:3] for p in original['audio']]==[p[:3] for p in actual['audio']]
            stream=next(s for s in probe(output)['streams'] if s['codec_type']=='video')
            assert stream['codec_name']=='hevc' and stream['pix_fmt']==pixels and stream['codec_tag_string']==tag
            assert frames(source,pixels)==frames(output,pixels)
            pcm=subprocess.run(['ffmpeg','-v','error','-i',str(output),'-map','0:a','-f','s16le','-'],capture_output=True,timeout=30)
            assert pcm.returncode==0 and not pcm.stderr
            expected,oracle_frames=reference(source,rate,channels,samples)
            assert pcm.stdout[:samples*channels*2]==expected
            encoded_samples=len(pcm.stdout)//channels//2
            assert encoded_samples==int(windows[0]['encodedEnd'])-1024
            assert sha(source)==before and sha(output)==row['result']['sha256']
            row.update(profile=name,pixelFormat=pixels,sourceSha256=before,presentationSamples=samples,
                       exactVideoPayloadPtsDtsDuration=True,exactAudioPayloadPtsDts=True,
                       decodedVideoIdentical=True,rawPcmPrefixIdentical=True,encodedPcmFrames=encoded_samples,
                       oracleDecodedFrames=oracle_frames,audioTrimRequired=True,audioWindow=windows[0])
            if consumer_kind:row['consumer']=consumer(consumer_kind,runner,source,output,windows[0],folder,samples,rate,channels)
            rows.append(row);(report/'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in rows])))
        for selected in ['continuous-hevc-reordered-video-v1','continuous-h264-aac-window-v1']:
            row,data=serve(worker,sources[0],mode=selected,growing=False,on_audio_window=lambda _:True)
            assert row['exitCode']==1 and row['result']['code'] in ['unsupported-stream-count','unsupported-stream-codec'] and not data,row
            row['profile']='reject-through-'+selected;rows.append(row)
        raw=bytearray(sources[0].read_bytes());changed=False
        for at,size,kind,header in boxes(raw):
            if kind!=b'moov':continue
            movie=bytes(raw[at+header:at+size])
            for track_at,track_size,track_kind,track_header in boxes(movie):
                if track_kind!=b'trak':continue
                start=at+header+track_at+track_header
                track=bytes(raw[start:at+header+track_at+track_size])
                if b'soun' not in track:continue
                for edit_at,edit_size,edit_kind,edit_header in boxes(track):
                    if edit_kind!=b'edts':continue
                    edits=track[edit_at+edit_header:edit_at+edit_size]
                    for elst_at,elst_size,elst_kind,elst_header in boxes(edits):
                        if elst_kind!=b'elst':continue
                        body=start+edit_at+edit_header+elst_at+elst_header
                        assert raw[body]==0 and int.from_bytes(raw[body+4:body+8],'big')==1
                        assert int.from_bytes(raw[body+12:body+16],'big')==1024
                        raw[body+12:body+16]=bytes(4);changed=True
        assert changed
        zero=root/'zero-aac-skip.mp4';zero.write_bytes(raw)
        row,data=serve(worker,zero,mode=MODE,growing=False,on_audio_window=lambda _:True)
        assert row['exitCode']==1 and row['result']['code']=='unsupported-stream-aac-trim' and not data,row
        row['profile']='reject-noncontract-aac-initial-skip';rows.append(row)
        for fault,reason in [('cancel-wait','cancelled'),('cancel-write','cancelled'),('integrity','integrity-mismatch')]:
            row,_=serve(worker,sources[0],mode=MODE,fault=fault,on_audio_window=lambda _:True,
                        cut_seconds=(1,2,3,4) if consumer_kind else (4,8,12,20))
            assert row['exitCode']==1 and row['result']['code']==reason,row
            row['profile']=fault;rows.append(row)
    result=dict(result='PASS',count=len(rows),mode=MODE,rows=rows,workerSha256=sha(worker),testSourceSha256=sha(__file__),
                consumer=consumer_kind,consumerSha256=sha(runner) if runner else None,
                publicPlayerTested=False,physicalAudioMeasured=False)
    (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--worker',required=True);parser.add_argument('--report-directory',required=True)
    parser.add_argument('--consumer',choices=['qt','classic']);parser.add_argument('--runner')
    args=parser.parse_args()
    if args.consumer and not args.runner:parser.error('--consumer requires --runner')
    result=run(Path(args.worker).resolve(),args.report_directory,args.consumer,args.runner)
    print(json.dumps(dict(result=result['result'],count=result['count'])))
