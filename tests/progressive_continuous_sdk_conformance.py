"""Public SDK against real Qt/Classic IPC, worker, decoder and file receipts."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import time
import traceback
import uuid

from progressive_continuous_api_test import run
from progressive_player_crash_recovery import ApiError, sha, wait
from progressive_backend_demux_conformance import snapshot
from progressive_stream_remux_worker_test import fixture
from progressive_integrity import protect_snapshot
from progressive_continuous import ACTIONS, ContinuousRemux, MODE, REORDERED_MODE


class DropReplies:
    def __init__(self, client):self.client=client;self.drop=set();self.calls=[]
    def request(self, message, timeout=5):
        self.calls.append(copy.deepcopy(message));reply=self.client.request(message,timeout=timeout)
        if message['action'] in self.drop:self.drop.remove(message['action']);raise TimeoutError('applied reply intentionally lost')
        return reply


def exercise(peers, root, mode=MODE, initial_position=False):
    rows=[];source=fixture(root,'source',bframes=2 if mode==REORDERED_MODE else 0);raw=source.read_bytes();client=peers[0].client
    cap=client.request(dict(version=1,action='capabilities'))
    assert ACTIONS <= set(cap['actions']) and cap['continuousRemux']['mode']==MODE,cap
    assert peers[0].client.info['continuousRemux']==cap['continuousRemux']
    if mode==REORDERED_MODE:assert cap['continuousRemux']['profiles'][mode]['bFrames'] is True,cap
    worker=Path(peers[0].player).parent/('foxicmp-remux-stream-worker'+('.exe' if peers[0].edition=='classic' or os.name=='nt' else ''))
    probe=subprocess.run([str(worker),'--info'],capture_output=True,text=True,timeout=5)
    assert probe.returncode==0,(probe.returncode,probe.stderr,probe.stdout)
    info=json.loads(probe.stdout);assert info['runtimeVerified'] and info['workerProtocol']==1 and info['contract']==cap['continuousRemux'],info
    def make(name, content=raw, prefix=None, transport=client, selected_mode=mode):
        directory=root/name;directory.mkdir();path=directory/'input.part';n=len(content)*2//3 if prefix is None else prefix
        model=snapshot(path,content,complete=False,prefix=n);model['timeline']['duration']='240000000';path.write_bytes(content)
        sdk=ContinuousRemux(transport,str(directory)) if selected_mode==MODE else ContinuousRemux(transport,str(directory),mode=selected_mode)
        return directory,path,model,sdk
    def prepared(sdk):
        def poll():
            s=sdk.status();assert not s['errorCode'],s
            if s['playbackReady']:return s
            sdk.heartbeat();return None
        return wait(poll,'SDK preparation ready')
    def playing(sdk):
        def poll():
            s=sdk.playback_status();assert s['state'] not in ('error','closed'),s
            return s if s['state']=='playing' and s['positionSeconds']>.2 and s['decoderReaders'] else None
        return wait(poll,'SDK actual decoder playback')
    def close(sdk,path,before):
        began=time.monotonic();proof=sdk.close(timeout=2);elapsed=time.monotonic()-began
        assert proof['released'] and proof['sourceFilesClosed'] and elapsed<2,proof
        final=sdk.forget(timeout=2);assert final['forgotten'] and final['released'],final
        assert sha(path)==before
        return dict(proof=proof,forgotten=final,closeSeconds=elapsed,originalUnchanged=True)

    transport=DropReplies(client);directory,path,model,sdk=make('sdk-growing',prefix=4,transport=transport);before=sha(path)
    transport.drop.add('continuousRemuxStart')
    try:sdk.prepare(model,'clip')
    except TimeoutError:pass
    else:raise AssertionError('Reply loss was not injected')
    recovered=sdk.status();assert sdk.job_id==recovered['jobId']
    waiting=wait(lambda:(lambda s:s if s['producerCode']=='waiting' else None)(sdk.status()),'SDK waiting on 4-byte prefix')
    try:sdk.play()
    except RuntimeError:pass
    else:raise AssertionError('Not-ready preview started')
    assert sdk.play_request_id is None
    def publish(n,complete=False):
        nonlocal model
        previous=model;model=copy.deepcopy(model);model['revision']=str(int(model['revision'])+1)
        phase='complete' if complete else 'downloading';model['writer']['state']=model['resources'][0]['state']=model['tracks'][0]['state']=phase
        model['resources'][0]['ranges']=[dict(offset='0',length=str(n))];model=protect_snapshot(model,directory,previous=previous)
        return sdk.publish(model)
    publish(len(raw)*2//3);prepared(sdk);sdk.heartbeat()
    if initial_position:wait(lambda:sdk.status()['indexedEndSeconds']>2,'initial position coverage')
    assert not any(m['action']=='continuousRemuxPlay' for m in transport.calls)
    transport.drop.add('continuousRemuxPlay')
    try:sdk.play(title='SDK public preview',**({'start_seconds':1} if initial_position else {}))
    except TimeoutError:pass
    else:raise AssertionError('Playback reply loss was not injected')
    restored=sdk.playback_status();running=playing(sdk)
    if initial_position:assert restored['startSeconds']==1 and .99<=running['positionSeconds']<2,running
    n=len(transport.calls)
    try:sdk.play()
    except RuntimeError:pass
    else:raise AssertionError('Lost playback reply was replayed')
    assert len(transport.calls)==n
    play_calls=[m for m in transport.calls if m['action']=='continuousRemuxPlay'];assert len(play_calls)==1 and not play_calls[0]['activate'] and not play_calls[0]['replace']
    assert sum(m['action']=='continuousRemuxStart' for m in transport.calls)==1
    sdk.control('pause');paused=wait(lambda:(lambda s:s if s['state']=='paused' else None)(sdk.playback_status()),'SDK manual pause')
    publish(len(raw)*5//6);time.sleep(.2);held=sdk.playback_status();assert held['state']=='paused' and abs(held['positionSeconds']-paused['positionSeconds'])<.15,held
    try:sdk.control('seek',seconds=1)
    except ApiError as e:assert e.code=='continuous-seek-unavailable',e.reply
    else:raise AssertionError('Seek before output completion was accepted')
    publish(len(raw),True);wait(lambda:sdk.status()['outputComplete'],'SDK complete output')
    sdk.control('seek',seconds=22);sdk.control('play');finished=wait(lambda:(lambda s:s if s['state']=='finished' else None)(sdk.playback_status()),'SDK confirmed EOF',12)
    # Another endpoint retains an independently prepared job throughout close.
    other_dir,other_path,other_model,other=make('other-endpoint',transport=peers[1].client);other_sha=sha(other_path);other.prepare(other_model,'clip');prepared(other)
    final=close(sdk,path,before);independent=other.status();assert independent['playbackReady'] and not independent['released'],independent
    other_final=close(other,other_path,other_sha)
    rows.append(dict(profile='sdk-growing-explicit-play-lost-replies-pause-seek-eof-isolation',result='PASS',waiting=waiting,recoveredJob=recovered,restoredPlayback=restored,actualPlayback=running,finished=finished,independent=independent,otherFinal=other_final,**final))

    for kind in ('prepare-only','control-stop'):
        directory,path,model,sdk=make('sdk-'+kind);before=sha(path);sdk.prepare(model,'clip');prepared(sdk)
        if kind=='control-stop':
            sdk.play();actual=playing(sdk);sdk.control('stop')
            for fn in (sdk.heartbeat,lambda:sdk.publish(model),sdk.play):
                try:fn()
                except RuntimeError:pass
                else:raise AssertionError('Writer event restarted a stopped SDK object')
        else:actual=dict(playbackNeverRequested=sdk.play_request_id is None)
        rows.append(dict(profile='sdk-'+kind,result='PASS',actual=actual,**close(sdk,path,before)))

    for kind in ('audio','bframes'):
        unsupported=fixture(root,'unsupported-'+kind,audio=kind=='audio',bframes=2 if kind=='bframes' else 0).read_bytes()
        directory,path,model,sdk=make('sdk-reject-'+kind,content=unsupported,selected_mode=MODE if kind=='bframes' else mode);before=sha(path);sdk.prepare(model,'clip')
        error=wait(lambda:(lambda s:s if s['state']=='error' and s['released'] else None)(sdk.status()),'SDK unsupported media error')
        assert error['errorCode'] in ('unsupported-stream-count','unsupported-stream-reorder'),error
        assert not error['outputComplete'] and not error['playbackReady'] and sdk.play_request_id is None,error
        rows.append(dict(profile='sdk-reject-'+kind,result='PASS',error=error,**close(sdk,path,before)))
    if mode==REORDERED_MODE:
        plain=fixture(root,'plain-no-b').read_bytes();directory,path,model,sdk=make('sdk-reordered-no-b',content=plain);before=sha(path)
        sdk.prepare(model,'clip');prepared(sdk);sdk.play();actual=playing(sdk)
        rows.append(dict(profile='sdk-reordered-profile-also-accepts-no-b-video',result='PASS',actual=actual,**close(sdk,path,before)))
    return dict(result='PASS',count=len(rows),rows=rows,mode=mode,publicCapability=True,sdkPlayback=True,workerInfo=info,workerInfoExitCode=probe.returncode)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--player',action='append',required=True);p.add_argument('--reader-runner',required=True);p.add_argument('--report-directory',required=True);p.add_argument('--reordered-video',action='store_true');args=p.parse_args()
    try:
        result=run(args,lambda peers,root:exercise(peers,root,REORDERED_MODE if args.reordered_video else MODE),__file__);print(json.dumps(dict(result=result['result'],count=result['count'])))
    except Exception:
        Path(args.report_directory).mkdir(parents=True,exist_ok=True);(Path(args.report_directory)/'error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
