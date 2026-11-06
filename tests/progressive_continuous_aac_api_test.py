"""SDK opt-in over production Qt Window/IPC and Classic API, isolated profiles."""
import argparse
import copy
from fractions import Fraction
import json
import shutil
from pathlib import Path
import sys
import os
sys.dont_write_bytecode=True
os.environ["PYTHONDONTWRITEBYTECODE"]="1"
import time
import traceback
bootstrap=argparse.ArgumentParser(add_help=False)
bootstrap.add_argument('--sdk-directory',type=Path)
sdk_args,_=bootstrap.parse_known_args()
sdk_directory=(sdk_args.sdk_directory or Path(__file__).resolve().parents[1]/'qt/tools').resolve()
sys.path.insert(0,str(sdk_directory))
if sdk_args.sdk_directory:
    import importlib
    for name in ('progressive_schema','progressive_client','progressive_integrity','progressive_resources','progressive_sdk','progressive_v2','progressive_continuous','progressive_windows','progressive_preview','progressive_writer'):
        module=importlib.import_module(name)
        assert Path(module.__file__).resolve().parent==sdk_directory,(name,module.__file__)
from progressive_continuous import ContinuousRemux, AAC_MODE
from progressive_continuous_api_test import run
from progressive_continuous_aac_controller_test import av_snapshot
from progressive_integrity import protect_snapshot
from progressive_player_crash_recovery import ApiError, sha, wait
from progressive_stream_aac_window_test import PROFILES
from progressive_stream_aac_aligned_test import fixture
from progressive_stream_remux_worker_test import probe


def exercise(peers,root,audio_output='null'):
    rows=[];client=peers[0].client
    for label,rate,channels,fps,bframes,first,n in PROFILES:
        folder=root/label;folder.mkdir();samples=n if n is not None else rate*14+137
        original,_=fixture(folder,'original',rate,channels,fps,bframes,first,False,seconds=14.137,samples=samples)
        raw=original.read_bytes();before=sha(original);packets=probe(original)['packets']
        prefix=int(next(p['pos'] for p in packets if p['codec_type']=='video' and Fraction(p['pts_time'])>=12))+13
        path=folder/'input.part';model=av_snapshot(path,raw,4);path.write_bytes(raw)
        model['timeline']['duration']='143000000'
        sdk=ContinuousRemux(client,str(folder),mode=AAC_MODE)
        def until(fn,label):
            def poll():
                status=sdk.status()
                assert status['state']!='error',(label,status)
                if not status['outputComplete'] and status['state'] not in ('error','cancelled','forgotten'):
                    try:sdk.heartbeat()
                    except ApiError as e:assert e.code=='continuous-closed',e.reply
                return fn()
            return wait(poll,label,20)
        def publish(count,complete=False,failed=None):
            nonlocal model
            old=model;model=copy.deepcopy(old);model['revision']=str(int(old['revision'])+1)
            state='complete' if complete else 'downloading';model['writer']['state']=model['resources'][0]['state']=state
            for t in model['tracks']:t['state']='failed' if t['kind']==failed else state
            model['resources'][0]['ranges']=[dict(offset='0',length=str(count))]
            model=protect_snapshot(model,folder,previous=old);return sdk.publish(model)
        initial=sdk.prepare(model,'clip')
        try:
            pending=until(lambda:(lambda s:s if s['producerCode']=='waiting' else None)(sdk.status()),'AAC pending initialization')
            assert not pending['playbackReady'] and not pending['audioWindowReady'],pending
            try:sdk.play()
            except RuntimeError:pass
            else:raise AssertionError('Playback accepted before PCM plan')
            assert sdk.play_request_id is None
            publish(prefix)
            ready=until(lambda:(lambda s:s if s['playbackReady'] else None)(sdk.status()),'AAC validated plan')
            assert ready['audioWindowReady'] and ready['audioSampleRate']==rate and ready['audioChannels']==channels and ready['audioPresentationSamples']==str(samples),ready
            sdk.play(title='AAC SDK '+label)
            def playing():
                s=sdk.playback_status();(folder/'last-playback.json').write_text(json.dumps(s,indent=2),encoding='utf-8');assert s['state'] not in ('error','closed'),s
                return s if s['state']=='playing' and s['positionSeconds']>.15 and s['pcmWindowApplied'] else None
            running=until(playing,'AAC decoder playback')
            assert running['audio'] and not running['outputComplete'],running
            if peers[0].edition=='qt':assert (running['audioOutput']==audio_output or (samples==1 and running['audioOutput']=='')) and running['audioCodec']=='aac',running
            sdk.control('pause');paused=until(lambda:(lambda s:s if s['state']=='paused' else None)(sdk.playback_status()),'AAC pause')
            publish(len(raw));time.sleep(.2);held=sdk.playback_status()
            assert held['state']=='paused' and abs(held['positionSeconds']-paused['positionSeconds'])<.15,held
            try:sdk.control('seek',seconds=1)
            except ApiError as e:assert e.code=='continuous-seek-unavailable',e.reply
            else:raise AssertionError('Unconfirmed seek accepted')
            publish(len(raw),True);until(lambda:sdk.status()['outputComplete'],'AAC complete')
            seek_reply=sdk.control('seek',seconds=13.5)
            (folder/'seek-reply.json').write_text(json.dumps(seek_reply,indent=2),encoding='utf-8')
            (folder/'before-seek.json').write_text(json.dumps(dict(ready=ready,running=running,paused=paused,heldPause=held),indent=2),encoding='utf-8')
            def seek_completed():
                s=sdk.playback_status()
                with (folder/'seek-status.jsonl').open('a',encoding='utf-8') as log:
                    log.write(json.dumps(s)+'\n')
                return s if s['state']=='paused' and s['positionSeconds']>13 else None
            sought=until(seek_completed,'AAC seek after complete')
            sdk.control('play');finished=until(lambda:(lambda s:s if s['state']=='finished' else None)(sdk.playback_status()),'AAC natural EOF')
            assert finished['outputComplete'] and not finished['active'],finished
            receipt=sdk.close(timeout=2);assert receipt['released'] and receipt['sourceFilesClosed'],receipt
            forgotten=sdk.forget(timeout=2);assert forgotten['forgotten'],forgotten
            assert sha(path)==sha(original)==before
            rows.append(dict(profile=label,result='PASS',initial=initial,pending=pending,ready=ready,running=running,pause=paused,heldPause=held,seekReply=seek_reply,seek=sought,finished=finished,release=receipt,forgotten=forgotten,sourceSha256=before))
            (root/'completed-cases.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
        finally:
            primary_failure=sys.exc_info()[0] is not None
            if not sdk.forgotten:
                try:
                    receipt=sdk.close(timeout=5)
                    if receipt['released'] and receipt['sourceFilesClosed']:sdk.forget(timeout=5)
                except Exception:
                    (folder/'cleanup-error.txt').write_text(traceback.format_exc(),encoding='utf-8')
                    if not primary_failure:raise
    # Stop and required-audio failure must detach paused decoder projections;
    # status polling must not manufacture a successful EOF.
    for mode in ['stop-paused','audio-failed-paused','cancel-before-init']:
        folder=root/mode;folder.mkdir();path=folder/'input.part';model=av_snapshot(path,raw,4 if mode=='cancel-before-init' else prefix);path.write_bytes(raw)
        model['timeline']['duration']='143000000'
        sdk=ContinuousRemux(client,str(folder),mode=AAC_MODE);sdk.prepare(model,'clip')
        try:
            if mode!='cancel-before-init':
                until(lambda:sdk.status()['playbackReady'],'failure test ready');sdk.play(title=mode)
                running=until(playing,'failure test playing');sdk.control('pause')
                until(lambda:sdk.playback_status()['state']=='paused','failure test paused')
                if mode=='stop-paused':sdk.control('stop')
                else:
                    publish(prefix,failed='audio')
                    failed=wait(lambda:(lambda s:s if s['state']=='error' else None)(sdk.playback_status()),'required audio failure',10)
                    assert not failed['active'] and not failed['outputComplete'],failed
            began=time.monotonic();receipt=sdk.close(timeout=2);elapsed=time.monotonic()-began
            assert receipt['released'] and receipt['sourceFilesClosed'] and elapsed<2,receipt
            sdk.forget(timeout=2);assert sha(path)==sha(original)
            rows.append(dict(profile=mode,result='PASS',release=receipt,closeSeconds=elapsed))
            (root/'completed-cases.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
        finally:
            primary_failure=sys.exc_info()[0] is not None
            if not sdk.forgotten:
                try:
                    receipt=sdk.close(timeout=5)
                    if receipt['released'] and receipt['sourceFilesClosed']:sdk.forget(timeout=5)
                except Exception:
                    (folder/'cleanup-error.txt').write_text(traceback.format_exc(),encoding='utf-8')
                    if not primary_failure:raise
    return dict(result='PASS',count=len(rows),rows=rows,privateAudioOutputs=audio_output in ('null','pulse'),audioOutputPolicy=audio_output,publicMode=AAC_MODE,packagedMainAcceptance=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--edition',choices=('qt','classic'),required=True);parser.add_argument('--player',action='append',required=True);parser.add_argument('--reader-runner',required=True);parser.add_argument('--report-directory',required=True);parser.add_argument('--expected-audio-output',choices=('null','pulse','wasapi'),default='null');parser.add_argument('--sdk-directory',type=Path);args=parser.parse_args()
    try:
        def checked(peers,root):
            try:return exercise(peers,root,args.expected_audio_output)
            except BaseException:
                shutil.copytree(root,Path(args.report_directory)/'failure-artifacts');raise
        result=run(args,exercise_case=checked,source_file=__file__)
        result['sdkDirectory']=str(sdk_directory)
        result['sdkModuleSha256']={p.name:sha(p) for p in sorted(sdk_directory.glob('progressive_*.py'))}
        (Path(args.report_directory)/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(dict(result=result['result'],count=result['count'])))
    except BaseException:
        Path(args.report_directory).mkdir(parents=True,exist_ok=True);(Path(args.report_directory)/'error.txt').write_text(traceback.format_exc());raise
