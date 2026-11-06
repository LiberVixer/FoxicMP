"""Actual Qt Player / Classic AV graph over private continuous AAC cache."""
import argparse
import copy
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct
import shutil
import subprocess
import tempfile
import traceback
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_stream_aac_aligned_test import fixture
from progressive_stream_aac_window_test import PROFILES, reference, codec_config
from progressive_stream_remux_worker_test import probe
from progressive_stream_reorder_test import sha


def seek_reference(oracle, original, packets, rate, channels, samples, folder):
    audio=[p for p in packets if p['codec_type']=='audio']
    # Independent packet geometry, not the production AudioSeekSample helper.
    first=next(i for i,p in enumerate(audio) if Fraction(p['pts_time'])+Fraction(p['duration_time'])>Fraction(3,4))
    raw=original.read_bytes();coded=folder/'oracle-packets.bin'
    coded.write_bytes(b''.join(struct.pack('<I',int(p['size']))+raw[int(p['pos']):int(p['pos'])+int(p['size'])] for p in audio))
    config=folder/'oracle-asc.bin';config.write_bytes(codec_config(original));output=folder/'oracle-raw.f32'
    process=subprocess.run([str(oracle),str(coded),str(output),str(config),str(rate),str(channels),str(samples),'1024',str(first),'seek-raw',str((len(audio)-first)*1024*channels*4)],capture_output=True,timeout=30)
    assert process.returncode==0,(process.returncode,process.stdout,process.stderr)
    raw=output.read_bytes();begin=(1024+rate-first*1024)*channels*4
    wanted=raw[begin:begin+(samples-rate)*channels*4]
    assert len(wanted)==(samples-rate)*channels*4
    return wanted,dict(exitCode=process.returncode,firstPacket=first,sha256=sha(output),bitExactRawWindow=True)


def run(runner,worker,edition,report,only=None,seek_oracle=None):
    report.mkdir(parents=True,exist_ok=False);rows=[];exits=[]
    inputs=dict(testSourceSha256=sha(__file__),runnerSha256=sha(runner),workerSha256=sha(worker))
    try:
        with tempfile.TemporaryDirectory(prefix='foxicmp-continuous-aac-decode-') as temp:
            root=Path(temp)
            for label,rate,channels,fps,bframes,first,n in PROFILES:
                if only and label!=only:continue
                origin=root/label;origin.mkdir();samples=n if n is not None else rate*6+137
                original,_=fixture(origin,'original',rate,channels,fps,bframes,first,False,seconds=6.137,samples=samples)
                raw=original.read_bytes();before=sha(original);packets=probe(original)['packets']
                prefix=int(next(p['pos'] for p in packets if p['codec_type']=='video' and Fraction(p['pts_time'])>=3))+13
                expected,_=reference(original,rate,channels,samples,float_pcm=edition=='classic')
                video=None
                video_pts=sorted(Fraction(p['pts_time']) for p in packets if p['codec_type']=='video')
                if edition=='classic':
                    decoded=subprocess.run(['ffmpeg','-v','error','-i',str(original),'-an','-pix_fmt','nv12','-fps_mode','passthrough','-f','rawvideo','-'],capture_output=True)
                    assert decoded.returncode==0,decoded.stderr;quantum=160*90*3//2
                    assert len(decoded.stdout)%quantum==0
                    video=[hashlib.sha256(decoded.stdout[at:at+quantum]).hexdigest() for at in range(0,len(decoded.stdout),quantum)]
                modes=('complete','growing','seek','stop','failure')+(('foreign',) if edition=='classic' else ())
                if label in ('aligned-control','one-sample-audio-first'):modes+=('seek-ended',)
                for mode in modes:
                    # Seeking beyond the only audible sample is a separate EOF
                    # case; initial coverage here keeps a nonempty AAC segment.
                    if samples<=rate and mode=='seek':continue
                    folder=origin/mode;folder.mkdir();path=folder/'input.part'
                    model=snapshot(path,raw,complete=False,prefix=prefix);model['timeline']['duration']='62000000'
                    audio=copy.deepcopy(model['tracks'][0]);audio.update(id='sound',kind='audio',codecHint='aac-lc');model['tracks'][0]['codecHint']='h264';model['tracks'].append(audio)
                    # The managed-native matrix separately proves physical append.
                    # Here a physical tail exists but remains unpublished initially.
                    with path.open('ab') as f:f.write(raw[prefix:])
                    initial=folder/'initial.json';initial.write_text(json.dumps(model),encoding='utf8')
                    previous=model;model=copy.deepcopy(previous);model['revision']='2';model['resources'][0]['ranges']=[dict(offset='0',length=str(len(raw)))];model=protect_snapshot(model,folder,previous=previous)
                    full=folder/'full.json';full.write_text(json.dumps(model),encoding='utf8')
                    model=copy.deepcopy(model);model['revision']='3';model['writer']['state']=model['resources'][0]['state']='complete'
                    for track in model['tracks']:track['state']='complete'
                    complete=folder/'complete.json';complete.write_text(json.dumps(model),encoding='utf8')
                    output=folder/'output.pcm';args=[str(runner),str(initial),str(full),str(complete),str(worker),str(folder/'registry'),str(folder/'cache'),str(output),mode,str(rate),str(channels)]
                    result=subprocess.run(args,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=60)
                    exits.append(dict(profile=label,mode=mode,exitCode=result.returncode))
                    if result.returncode!=0:shutil.copytree(folder,report/"failure-artifacts")
                    assert result.returncode==0,(label,mode,result.returncode,result.stdout,result.stderr)
                    status=json.loads(result.stdout.strip().splitlines()[-1]);assert status['result']=='PASS' and not status['audioDeviceOpened'],status
                    if mode in ('complete','growing','seek','seek-ended'):
                        actual=output.read_bytes() if output.exists() else b'';start=rate if mode=='seek' else 0
                        if mode=='seek' and edition=='qt':
                            # mpv synchronizes the audio start to the first displayed
                            # video frame, not an invented frame exactly at 1 second.
                            frame_pts=next(t for t in video_pts if t>=1)
                            oracle_args=args.copy();oracle_args[5]+='.oracle';oracle_args[6]+='.oracle';oracle_args[7]+='.oracle.pcm';oracle_args.append("@cache")
                            oracle=subprocess.run(oracle_args,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=60)
                            assert oracle.returncode==0,(oracle.returncode,oracle.stdout,oracle.stderr)
                            wanted=Path(oracle_args[7]).read_bytes()
                            start=samples-len(wanted)//channels//2
                            assert abs(start-frame_pts*rate)<=1,(label,'completed file Player seek precision',start,str(frame_pts))
                            status['seekAudioStartSample']=start
                            status['seekFirstVideoPts']=str(frame_pts)
                            status['completedCachePlayerOracle']=dict(exitCode=oracle.returncode,sha256=sha(oracle_args[7]),bitExact=True)
                            if actual!=wanted:shutil.copytree(folder,report/'failure-artifacts')
                            assert actual==wanted,(label,mode,'completed file Player oracle differs')
                        if not(mode=='seek' and edition=='qt'):wanted=expected[start*channels*(4 if edition=='classic' else 2):]
                        if mode=='seek-ended':wanted=b''
                        if mode=='seek' and edition=='classic':
                            wanted,oracle=seek_reference(seek_oracle,original,packets,rate,channels,samples,folder)
                            status['seekOracle']=oracle
                            assert actual==wanted,(label,mode,'raw decoder window differs')
                        if len(actual)!=len(wanted):shutil.copytree(folder,report/"failure-artifacts")
                        assert len(actual)==len(wanted),(label,mode,len(actual),len(wanted))
                        if edition=='qt':assert actual==wanted,(label,mode,'PCM differs')
                        else:
                            x=struct.unpack('<'+'f'*(len(actual)//4),actual);y=struct.unpack('<'+'f'*(len(wanted)//4),wanted)
                            maximum=max((abs(a-b) for a,b in zip(x,y)),default=0)
                            if maximum>=1e-5:shutil.copytree(folder,report/'failure-artifacts')
                            assert maximum<1e-5,(label,mode,maximum)
                            frames=[json.loads(line) for line in Path(str(output)+'.video.jsonl').read_text().splitlines()]
                            seek_time=Fraction(1,100) if mode=='seek-ended' and samples==1 else Fraction(41,10) if mode=='seek-ended' else Fraction(1)
                            first_frame=next((i for i,t in enumerate(video_pts) if t>=seek_time),len(video_pts)) if mode in ('seek','seek-ended') else 0
                            assert [p['sha256'] for p in frames]==video[first_frame:],(label,mode,'video differs',len(frames),len(video[first_frame:]))
                        status.update(pcmFrames=len(actual)//channels//(4 if edition=='classic' else 2),pcmSha256=hashlib.sha256(actual).hexdigest(),exactPcmLength=True)
                    if mode=='growing':assert status['pcmBeforeComplete']>0 if edition=='classic' else status['playedBeforeComplete']
                    if mode in ('stop','failure'):assert status['stopSeconds']<2
                    if mode=='foreign':assert status['foreignRejected'] and status['samples']==0
                    assert sha(path)==sha(original)==before
                    rows.append(dict(profile=label,mode=mode,result='PASS',status=status,originalSha256=before))
                    (report/'progress.json').write_text(json.dumps(dict(completed=[r['profile']+'/'+r['mode'] for r in rows])))
            assert sha(__file__)==inputs['testSourceSha256']
        result=dict(result='PASS',**inputs,edition=edition,count=len(rows),rows=rows,exactDriverExits=exits,temporaryWorkRemoved=not root.exists(),physicalAudioOutput=False,publicAudioCapability=False)
        (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
    except BaseException:
        (report/'failure.json').write_text(json.dumps(dict(inputs=inputs,rows=rows,exactDriverExits=exits,error=traceback.format_exc()),indent=2)+'\n');raise


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);p.add_argument('--worker',type=Path,required=True);p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--report-directory',type=Path,required=True);p.add_argument('--only');p.add_argument('--seek-oracle',type=Path);a=p.parse_args()
    if a.edition=='classic' and not a.seek_oracle:p.error('Classic requires --seek-oracle for independent untrimmed seek capture')
    v=run(a.runner.resolve(),a.worker.resolve(),a.edition,a.report_directory.resolve(),a.only,a.seek_oracle.resolve() if a.seek_oracle else None);print(json.dumps(dict(result=v['result'],count=v['count'])))
