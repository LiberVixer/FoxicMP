"""AAC sample-window worker and real Qt/Classic PCM consumers, no audio device."""
import argparse
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import traceback

from progressive_stream_aac_aligned_test import fixture, pcm, fragment_durations
from progressive_stream_remux_worker_test import serve, probe
from progressive_stream_reorder_test import sha, frames

MODE = 'continuous-h264-aac-window-v1'
PROFILES = [('48000-mono',48000,1,'25',2,False,None),
            ('48000-stereo-fractional',48000,2,'30000/1001',3,False,None),
            ('44100-mono',44100,1,'25',2,False,None),
            ('44100-stereo-b16',44100,2,'60000/1001',16,False,None),
            ('aligned-control',48000,1,'25',0,False,191488),
            ('one-sample-audio-first',44100,1,'25',2,True,1)]


def reference(path, rate, channels, samples, float_pcm=False):
    fmt = 'f32le' if float_pcm else 's16le'
    p = subprocess.run(['ffmpeg','-v','error','-i',str(path),'-map','0:a','-f',fmt,'-'],capture_output=True)
    assert p.returncode == 0, p.stderr.decode(errors='replace')
    stride = channels*(4 if float_pcm else 2)
    count = len(p.stdout)//stride
    assert samples <= count < samples+1024, (samples,count)
    # FFmpeg 5.x on Linux ignores the source's final partial AAC sample window.
    # Normalize ONLY the original oracle to the explicitly generated sample count.
    # Consumer output is never cropped here. Windows FFmpeg must honor it itself.
    if os.name == 'nt':assert count == samples, (samples,count)
    return p.stdout[:samples*stride],count


def packets(path):
    p = probe(path)
    out = {}
    durations = fragment_durations(path.read_bytes())
    for stream in p['streams']:
        base = Fraction(stream['time_base'])
        values = [x for x in p['packets'] if x['stream_index']==stream['index']]
        table = durations.get(int(stream['id'],0))
        rows=[]
        for i,x in enumerate(values):
            d=table[i] if table is not None else int(x['duration'])
            rows.append([x['data_hash'],str(int(x['pts'])*base),str(int(x['dts'])*base),str(d*base)])
        out[stream['codec_type']]=rows
    return out


def codec_config(path):
    p=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_data','-of','json',str(path)]))
    s=next(s for s in p['streams'] if s['codec_type']=='audio')
    body=''.join(line.split(':',1)[1].split('  ',1)[0].replace(' ','') for line in s['extradata'].splitlines() if ':' in line)
    return bytes.fromhex(body)


def consumer(kind,runner,source,output,plan,root,samples,rate,channels):
    captures=[];targets=list(dict.fromkeys(t for t in (0,1,samples-1) if t<samples))
    expected,_=reference(source,rate,channels,samples,float_pcm=kind=='classic')
    if kind=='classic':
        packet_probe=probe(source);data=source.read_bytes();audio=[p for p in packet_probe['packets'] if p['codec_type']=='audio']
        coded=root/'audio-packets.bin';coded.write_bytes(b''.join(struct.pack('<I',int(p['size']))+data[int(p['pos']):int(p['pos'])+int(p['size'])] for p in audio))
        asc=root/'config.bin';asc.write_bytes(codec_config(source))
        def capture(mode,target=0,short=False):
            packets=coded
            if short:
                packets=root/'short-packets.bin';raw=coded.read_bytes();at=last=0
                while at<len(raw):last=at;at+=4+int.from_bytes(raw[at:at+4],'little')
                packets.write_bytes(raw[:last])
            dest=root/(mode+'-'+str(target)+'.f32');frames=samples-target if mode=='trim' else len(audio)*1024
            args=[str(runner),str(packets),str(dest),str(asc),str(rate),str(channels),str(samples),plan['rawBegin'],str(target),mode,str(frames*channels*4)]
            process=subprocess.run(args,capture_output=True,timeout=30)
            if short:
                assert process.returncode==1 and b'truncated before end' in process.stderr,(process.returncode,process.stderr)
                return dict(rejectedTruncatedPcm=True,exitCode=process.returncode)
            assert process.returncode==0,(process.returncode,process.stdout,process.stderr)
            return dest.read_bytes(),json.loads(process.stdout.decode().strip().splitlines()[-1])
        plain,plain_meta=capture('plain')
        assert len(plain)==len(audio)*1024*channels*4
        begin=int(plan['rawBegin'])*channels*4;end=int(plan['rawEnd'])*channels*4
        audible=plain[begin:end]
        # Independent decoder versions may differ by float rounding. Actual
        # trimming is ALSO checked bit-for-bit against this decoder's raw PCM.
        left=struct.unpack('<'+'f'*(len(audible)//4),audible);right=struct.unpack('<'+'f'*(len(expected)//4),expected)
        assert len(left)==len(right)
        maximum=max(abs(a-b) for a,b in zip(left,right))
        assert maximum<1e-5,maximum
        for target in targets:
            actual,meta=capture('trim',target)
            assert actual==plain[begin+target*channels*4:end]
            captures.append(dict(targetSample=target,pcmFrames=len(actual)//channels//4,sha256=hashlib.sha256(actual).hexdigest(),decoder=meta))
        for mode in ('flush','replace'):
            actual,meta=capture(mode);assert actual==plain
            captures.append(dict(mode=mode,windowCleared=True,decoder=meta))
        captures.append(capture('trim',short=True))
        return dict(captures=captures,maxFloatErrorAgainstIndependentDecoder=maximum,rawDecoder=plain_meta,
                    bitExactAgainstRawDecoderWindow=True,graphPlaybackTested=False)
    for target in targets:
        dest=root/('qt-'+str(target)+'.pcm')
        process=subprocess.run([str(runner),str(output),str(dest),str(rate),str(channels),str(target),str(samples),'0'],capture_output=True,timeout=45)
        assert process.returncode==0,(process.returncode,process.stdout,process.stderr)
        meta=json.loads(process.stdout.decode());assert meta['result']=='PASS' and not meta['audioDeviceOpened']
        actual=dest.read_bytes();assert len(actual)==(samples-target)*channels*2,(target,samples,len(actual))
        assert actual==expected[target*channels*2:],(target,'real Qt PCM differs')
        captures.append(dict(targetSample=target,pcmFrames=len(actual)//channels//2,sha256=hashlib.sha256(actual).hexdigest(),player=meta))
    return dict(captures=captures,realPlayerPcmAdapter=True,hardwareOutputOpened=False)


def run(worker,report,consumer_kind=None,runner=None):
    report.mkdir(parents=True,exist_ok=False);rows=[];source_hash=sha(__file__)
    try:
        with tempfile.TemporaryDirectory(prefix='foxicmp-aac-window-') as name:
            root=Path(name);sources=[]
            for label,rate,channels,fps,bframes,first,n in PROFILES:
                folder=root/label;folder.mkdir();samples=n if n is not None else rate*4+137
                source,_=fixture(folder,'original',rate,channels,fps,bframes,first,False,seconds=4.137,samples=samples)
                sources.append(source);before=sha(source);windows=[]
                def accepted(plan):
                    assert not windows;windows.append(plan)
                    assert plan['version']==1 and plan['sampleRate']==rate and plan['channels']==channels
                    assert plan['rawOrigin']=='untrimmed-decoder-samples' and plan['timestampOrigin']=='presentation-samples'
                    assert plan['timestampBegin']=='0' and int(plan['timestampEnd'])==samples
                    assert int(plan['rawBegin'])==1024 and int(plan['rawEnd'])==samples+1024
                    assert int(plan['encodedEnd'])-int(plan['rawEnd'])==int(plan['endPadding'])<1024
                    return True
                row,data=serve(worker,source,mode=MODE,short_reads=True,cut_seconds=(1,2,3,4),on_audio_window=accepted)
                assert row['exitCode']==0 and row['result']['code']=='data',row
                result=row['result'];assert result['pcmWindowRequired'] and result['aacPcmWindow']==windows[0]
                assert result['sourceComplete'] and result['sourceEof'] and not result['playbackSupported']
                assert result['aacEndTrimRequired']==(int(windows[0]['endPadding'])!=0)
                output=folder/'output.mp4';output.write_bytes(data);assert sha(output)==result['sha256']
                original,actual=packets(source),packets(output)
                assert original['video']==actual['video']
                assert [x[:3] for x in original['audio']]==[x[:3] for x in actual['audio']]
                full,_=reference(source,rate,channels,samples);raw=pcm(output)
                assert raw[:samples*channels*2]==full
                assert len(raw)//channels//2==int(windows[0]['encodedEnd'])-1024
                assert frames(source)==frames(output) and row['firstCompleteFragmentBeforeSourceComplete']
                row.update(profile=label,sampleRate=rate,channels=channels,presentationSamples=samples,
                           originalSha256=before,windowBeforeMedia=True,encodedPcmFrames=len(raw)//channels//2,
                           rawOutputStillRequiresWindow=True,videoPacketClocksExact=True,audioPayloadPtsDtsExact=True,
                           originalOracleNormalization='declared presentation samples; output not cropped',
                           rawPcmPrefixMatches=True,originalLastAudioDuration=original['audio'][-1][3],
                           outputLastAudioDuration=actual['audio'][-1][3])
                if consumer_kind:row['consumer']=consumer(consumer_kind,runner,source,output,windows[0],folder,samples,rate,channels)
                assert sha(source)==before;rows.append(row)
                (report/'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in rows])))
            for fault in ('refused-window','window-digest'):
                row,data=serve(worker,sources[0],mode=MODE,growing=False,fault=fault,on_audio_window=lambda _:fault!='refused-window')
                assert row['exitCode']==1 and row['result']['code']=='audio-window-not-accepted' and not data,row
                row['profile']=fault;rows.append(row)
            for mode in ('continuous-h264-video-v1','continuous-h264-reordered-video-v1','continuous-h264-aac-aligned-v1'):
                row,data=serve(worker,sources[0],mode=mode,growing=False)
                assert row['exitCode']==1 and row['result']['code'] in ('unsupported-stream-count','unsupported-stream-aac-end-trim') and not data,row
                row['profile']='unchanged-'+mode;rows.append(row)
            for fault,code in [('cancel-wait','cancelled'),('cancel-write','cancelled'),('unconfirmed-eof','unconfirmed-eof'),('integrity','integrity-mismatch'),('kill-wait',None)]:
                row,_=serve(worker,sources[0],mode=MODE,fault=fault,cut_seconds=(1,2,3,4),on_audio_window=lambda _:True)
                assert code is None or (row['exitCode']==1 and row['result']['code']==code and not row['result']['sourceComplete']),row
                row['profile']=fault;rows.append(row)
            assert sha(__file__)==source_hash
        result=dict(result='PASS',count=len(rows),rows=rows,testSourceSha256=source_hash,workerSha256=sha(worker),
                    privateMode=MODE,consumer=consumer_kind,consumerSha256=sha(runner) if runner else None,
                    publicAudioCapability=False,managedParentCacheIntegrated=False,physicalAudioOutput=False,
                    temporaryWorkRemoved=not root.exists())
        (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
    except BaseException:
        (report/'failure.json').write_text(json.dumps(dict(rows=rows,error=traceback.format_exc()),indent=2)+'\n');raise


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--worker',type=Path,required=True);p.add_argument('--report-directory',type=Path,required=True)
    p.add_argument('--consumer',choices=('qt','classic'));p.add_argument('--runner',type=Path);a=p.parse_args()
    if a.consumer and not a.runner:p.error('--consumer requires --runner')
    r=run(a.worker.resolve(),a.report_directory.resolve(),a.consumer,a.runner.resolve() if a.runner else None)
    print(json.dumps(dict(result=r['result'],count=r['count'])))
