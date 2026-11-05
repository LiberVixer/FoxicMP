"""Real Qt/Classic IPC playback of the private continuous H.264 remux profile."""
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
from progressive_player_crash_recovery import ApiError, wait, sha
from progressive_stream_remux_worker_test import fixture
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot


def exercise(peers, root, reordered_video=False):
    peer=peers[0];client=peer.client;rows=[];job=None;play=None;model=None;directory=None
    mode='continuous-h264-reordered-video-v1' if reordered_video else 'continuous-h264-video-v1'
    source=fixture(root,'source',bframes=2 if reordered_video else 0);raw=source.read_bytes();latencies=[];old_playbacks=[]
    def command(action,_mode=mode,**fields):
        began=time.monotonic();reply=client.request(dict(version=1,action=action,**fields));latencies.append(time.monotonic()-began)
        if action.startswith('continuous'):assert reply['mode']==_mode,(action,reply)
        return reply
    def reject(action,code,**fields):
        try:command(action,**fields)
        except ApiError as e:assert e.code==code,e.reply
        else:raise AssertionError(('Unexpected success',action))
    def preparation():return command('continuousRemuxStatus',jobId=job['jobId'])
    def fenced(action):return command(action,**{key:job[key] for key in ('jobId','sessionId','generation','writerInstanceId')})
    def heartbeat():
        s=preparation()
        if not s['outputComplete'] and s['state'] not in ('error','cancelled','cancelling','forgetting','forgotten'):
            try:fenced('continuousRemuxHeartbeat')
            except ApiError as e:assert e.code=='continuous-closed',e.reply
    def until(check,label,timeout=20):
        def poll():
            heartbeat();return check()
        return wait(poll,label,timeout)
    def status():return command('continuousPlaybackStatus',playbackId=play['playbackId'])
    def control(name,**fields):return command('continuousPlaybackControl',playbackId=play['playbackId'],command=name,**fields)
    def playing():
        s=status();assert s['state'] not in ('error','closed'),s
        return s if s['state']=='playing' and s['positionSeconds']>.2 and s['decoderReaders']>0 else None
    def publish(prefix,complete=False,failed=False):
        nonlocal model
        previous=model;model=copy.deepcopy(model);model['revision']=str(int(model['revision'])+1)
        phase='complete' if complete else 'downloading';model['writer']['state']=model['resources'][0]['state']=model['tracks'][0]['state']=phase
        if failed:model['tracks'][0]['state']='failed'
        model['resources'][0]['ranges']=[dict(offset='0',length=str(prefix))];model=protect_snapshot(model,directory,previous=previous)
        return command('continuousRemuxUpdate',jobId=job['jobId'],session=model)
    def request_play(**changes):
        fields={key:job[key] for key in ('jobId','sessionId','generation','writerInstanceId')}
        fields.update(requestId=uuid.uuid4().hex,title='Continuous private playback',**changes);return fields
    def open_play(fields=None):
        nonlocal play
        fields=fields or request_play();play=command('continuousRemuxPlay',**fields)
        assert play['jobId']==job['jobId'] and play['durationSeconds']==24,play
        assert command('continuousRemuxPlay',**fields)['playbackId']==play['playbackId']
        assert command('continuousPlaybackStatus',requestId=fields['requestId'])['playbackId']==play['playbackId']
        try:peers[1].client.request(dict(version=1,action='continuousPlaybackStatus',playbackId=play['playbackId']))
        except ApiError as e:assert e.code=='unknown-continuous-playback',e.reply
        else:raise AssertionError('Playback crossed endpoints')
        changed=dict(fields,title='conflicting title');reject('continuousRemuxPlay','continuous-playback-conflict',**changed)
        return fields
    def normal_stop():
        if peer.edition=='classic':
            from windows.progressive_family_test import post_command
            post_command(peer.process.pid,890)
        else:
            result=subprocess.run([peer.player,'--profile',str(peer.profile),'--command','stop','--no-activate'],capture_output=True,timeout=5)
            assert result.returncode==0,(result.returncode,result.stderr)
    def forgotten():
        fenced('continuousRemuxForget');s=until(lambda:(lambda v:v if v['forgotten'] else None)(preparation()),'forgotten')
        assert s['released'] and s['sourceFilesClosed'],s
        if play is not None:
            closed=status();assert closed['state'] in ('closed','error') and closed['decoderReaders']==0,closed
        return s
    def case(name,fn,prefix=None):
        nonlocal job,play,model,directory
        directory=root/name;directory.mkdir();path=directory/'input.part';n=len(raw)*2//3 if prefix is None else prefix
        model=snapshot(path,raw,complete=False,prefix=n);model['timeline']['duration']='240000000';path.write_bytes(raw);before=sha(path);play=None
        fields=dict(requestId=uuid.uuid4().hex,sessionDirectory=str(directory),session=model,resourceId='clip')
        if reordered_video:fields['mode']=mode
        job=command('continuousRemuxStart',**fields)
        try:
            if reordered_video:
                assert command('continuousRemuxStart',**fields)['jobId']==job['jobId']
                reject('continuousRemuxStart','continuous-conflict',**dict(fields,mode='continuous-h264-video-v1'))
                reject('continuousRemuxUpdate','invalid_snapshot',jobId=job['jobId'],session=model,mode=mode)
            until(lambda:(lambda v:v if v['producerCode']=='waiting' and (v['playbackReady'] if n>4 else not v['playbackReady']) else None)(preparation()),'prepared source')
            detail=fn();final=forgotten();assert sha(path)==before
            rows.append(dict(profile=name,result='PASS',final=final,**(detail or {})))
            if play is not None:old_playbacks.append(play['playbackId'])
        finally:
            try:forgotten()
            except Exception:pass

    def complete():
        bad=request_play(generation='2');reject('continuousRemuxPlay','generation_changed',**bad)
        fields=open_play();running=until(playing,'actual decoder playback')
        reject('continuousPlaybackControl','continuous-seek-unavailable',playbackId=play['playbackId'],command='seek',seconds=1)
        control('pause');paused=until(lambda:(lambda s:s if s['state']=='paused' else None)(status()),'manual pause');pos=paused['positionSeconds']
        publish(len(raw)*5//6);until(lambda:preparation()['appliedRevision']=='2','append while paused')
        time.sleep(.4);held=status();assert held['state']=='paused' and abs(held['positionSeconds']-pos)<.15,held
        publish(len(raw),True);until(lambda:preparation()['outputComplete'],'successful source completion')
        reject('continuousPlaybackControl','invalid_request',playbackId=play['playbackId'],command='seek',seconds=24)
        control('seek',seconds=22);seeked=until(lambda:(lambda s:s if s['state']=='paused' and s['positionSeconds']>21 else None)(status()),'complete-source seek')
        control('play');finished=until(lambda:(lambda s:s if s['state']=='finished' else None)(status()),'verified natural end',12)
        assert finished['outputComplete'] and not finished['active'],finished
        eof_receipts=[]
        for identifier in finished['readerIds']:
            command('readerRelease',readerId=identifier)
            receipt=wait(lambda:(lambda r:r if r['released'] and r['filesClosed'] else None)(command('readerStatus',readerId=identifier)['reader']),'EOF reader closes after release',5)
            assert receipt['reason']=='files-closed',receipt
            eof_receipts.append(receipt)
        return dict(actualPlayback=running,pause=paused,heldPause=held,seek=seeked,finished=finished,playRequest=fields,eofOutputReceipts=eof_receipts)
    case('growing-pause-append-complete-seek-eof',complete)

    def stopped():
        fields=open_play();running=until(playing,'playing before ordinary toolbar Stop');normal_stop()
        closed=until(lambda:(lambda s:s if s['state']=='closed' and s['decoderReaders']==0 else None)(status()),'toolbar Stop closes decoder')
        time.sleep(.2);assert command('continuousRemuxPlay',**fields)['state']=='closed'
        reject('continuousPlaybackControl','continuous-playback-closed',playbackId=play['playbackId'],command='play')
        return dict(actualPlayback=running,closed=closed,replayDidNotRestart=True)
    case('ordinary-toolbar-stop-and-request-replay',stopped)

    def release_output(paused=False, waiting=False):
        fields=open_play();running=until(playing,'playing before readerRelease')
        if paused:
            control('pause');until(lambda:status()['state']=='paused','paused before readerRelease')
        if waiting:
            position=running['positionSeconds'];advanced=time.monotonic()
            def tail_wait():
                nonlocal position,advanced
                s=status();assert s['active'] and s['state'] not in ('error','closed','finished'),s
                if abs(s['positionSeconds']-position)>.02:position=s['positionSeconds'];advanced=time.monotonic()
                return s if s['positionSeconds']>1 and time.monotonic()-advanced>.5 and preparation()['producerCode']=='waiting' else None
            until(tail_wait,'decoder stalls at unpublished tail without EOF',20)
        outputs=until(lambda:status()['readerIds'],'output reader identity')
        # Source leases have a different owner. A metadata request must not stop
        # playback, acknowledge open files, or cancel an independent endpoint.
        sources=[identifier for identifier in preparation()['readerIds'] if identifier not in outputs]
        assert sources,preparation()
        source_reply=command('readerRelease',readerId=sources[0])
        assert source_reply['reader']['readerId']==sources[0] and not source_reply['reader']['released'],source_reply
        assert status()['active'],status()
        reject('readerRelease','unknown_reader',readerId=uuid.uuid4().hex)
        reject('readerRelease','invalid_snapshot',readerId=outputs[0],generation='1')
        try:peers[1].client.request(dict(version=1,action='readerRelease',readerId=outputs[0]))
        except ApiError as e:assert e.code=='unknown_reader',e.reply
        else:raise AssertionError('Reader release crossed endpoints')
        before=status();release_began=time.monotonic();reply=command('readerRelease',readerId=outputs[0])
        assert reply['reader']['readerId']==outputs[0],reply
        closed=until(lambda:(lambda s:s if s['state']=='closed' and s['decoderReaders']==0 else None)(status()),'readerRelease closes owned output',5)
        receipts=[]
        for identifier in outputs:
            receipt=wait(lambda:(lambda r:r if r['released'] and r['filesClosed'] else None)(command('readerStatus',readerId=identifier)['reader']),'actual output files closed',5)
            assert receipt['reason']=='files-closed',receipt
            repeated=command('readerRelease',readerId=identifier)['reader']
            assert repeated['released'] and repeated['filesClosed'],repeated
            receipts.append(receipt)
        final=until(lambda:(lambda s:s if s['released'] and s['sourceFilesClosed'] else None)(preparation()),'source/cache released after output request',5)
        release_seconds=time.monotonic()-release_began;assert release_seconds<2,release_seconds
        assert not final['outputComplete'],final
        assert command('continuousRemuxPlay',**fields)['state']=='closed','Replay restarted released playback'
        if paused:
            for identifier in outputs:
                assert command('readerForget',readerId=identifier)['forgotten']
                reject('readerStatus','unknown_reader',readerId=identifier)
        return dict(actualPlayback=running,beforeRelease=before,reply=reply,closed=closed,outputReceipts=receipts,sourceMetadataReply=source_reply,producerReleased=final,releaseSeconds=release_seconds,outputReceiptAcknowledged=paused)
    case('reader-release-playing-output',release_output)
    case('reader-release-paused-output',lambda:release_output(paused=True))
    case('reader-release-waiting-tail-output',lambda:release_output(waiting=True))

    def replaced():
        fields=open_play();running=until(playing,'playing before ordinary API replacement')
        outputs=until(lambda:status()['readerIds'],'reader identities before replacement')
        other=directory/'ordinary.mp4';subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=160x90:rate=25:duration=12','-c:v','libx264','-threads','2','-bf','0','-g','25','-movflags','+frag_keyframe+empty_moov+default_base_moof',str(other)],check=True,capture_output=True)
        data=other.read_bytes();ordinary=snapshot(other,data,complete=True);ordinary['timeline']['duration']='120000000';ordinary['tracks'][0].update(containerHint='fmp4',codecHint='h264',initializationReady=True)
        command('start',session=ordinary,sessionDirectory=str(directory),replace=True)
        def regular():
            s=command('heartbeat',sessionId=ordinary['sessionId']);return s if s.get('active') and s['positionSeconds']>.2 else None
        current=wait(regular,'replacement plays');closed=status();assert closed['state']=='closed',closed
        control('stop');assert regular(), 'Stale stop affected replacement'
        for identifier in outputs:
            command('readerRelease',readerId=identifier)
        assert regular(),'Stale output readerRelease affected replacement'
        assert command('continuousRemuxPlay',**fields)['state']=='closed'
        command('cancel',sessionId=ordinary['sessionId'])
        receipts=wait(lambda:(lambda rs:rs if rs and all(r['released'] and r['filesClosed'] for r in rs) else None)(command('readers',sessionId=ordinary['sessionId'],generation='1')['readers']),'ordinary replacement readers close')
        for r in receipts:command('readerForget',readerId=r['readerId'])
        return dict(previousPlayback=running,replacement=current,closed=closed,staleControlIsolated=True)
    case('ordinary-source-replacement-isolates-old-controls',replaced)

    def failed_replacement():
        open_play();running=until(playing,'playing before failed replacement')
        bad=directory/'invalid-fmp4.part';ordinary=snapshot(bad,b'\0'*100,complete=True);ordinary['timeline']['duration']='120000000';ordinary['tracks'][0].update(containerHint='fmp4',codecHint='h264',initializationReady=True)
        reject('start','session_busy',session=ordinary,sessionDirectory=str(directory))
        try:command('start',session=ordinary,sessionDirectory=str(directory),replace=True)
        except ApiError as e:
            rejected=e.reply;assert e.code!='preparation_cancelled',e.reply
        else:raise AssertionError('Invalid complete fMP4 was accepted')
        continued=until(lambda:(lambda s:s if s['state']=='playing' and s['positionSeconds']>running['positionSeconds']+.2 else None)(status()),'failed preparation leaves original playback running')
        receipts=wait(lambda:(lambda rs:dict(readers=rs) if all(r['released'] and r['filesClosed'] for r in rs) else None)(command('readers',sessionId=ordinary['sessionId'],generation='1')['readers']),'failed replacement readers close')['readers']
        for r in receipts:command('readerForget',readerId=r['readerId'])
        return dict(actualPlayback=running,rejectedReplacement=rejected,continued=continued,currentPlaybackPreserved=True)
    case('failed-replacement-preserves-current-playback',failed_replacement)

    for action in ('continuousRemuxCancel','continuousRemuxForget'):
        def cancel(action=action):
            open_play();running=until(playing,'playing before '+action)
            wrong={key:job[key] for key in ('jobId','sessionId','generation','writerInstanceId')};wrong['writerInstanceId']=uuid.uuid4().hex
            reject(action,'generation_changed',**wrong);assert status()['active']
            fenced(action);closed=until(lambda:(lambda s:s if s['state']=='closed' and s['decoderReaders']==0 else None)(status()),'producer command closes owned decoder')
            return dict(actualPlayback=running,closed=closed)
        case(action+'-closes-only-owned-playback',cancel)

    def failed():
        open_play();running=until(playing,'playing before writer failure');publish(len(raw)*2//3,failed=True)
        terminal=until(lambda:(lambda s:s if s['state']=='error' and s['decoderReaders']==0 else None)(status()),'failed writer closes decoder')
        state=until(lambda:(lambda s:s if s['released'] else None)(preparation()),'failure releases source/cache')
        assert state['errorCode']=='writer-failed' and not state['outputComplete'],state
        if peer.edition=='qt':assert terminal['reasonCode']=='writer-failed',terminal
        return dict(actualPlayback=running,error=terminal,preparation=state)
    case('required-track-failure-during-playback',failed)

    def not_ready():
        fields=request_play();reject('continuousRemuxPlay','continuous-not-ready',**fields)
        publish(len(raw)*2//3);until(lambda:preparation()['playbackReady'],'later ready');normal_stop();time.sleep(.15)
        reject('continuousPlaybackStatus','unknown-continuous-playback',requestId=fields['requestId'])
        assert len(preparation()['readerIds'])==1
        open_play();normal_stop();closed=until(lambda:(lambda s:s if s['state']=='closed' and s['decoderReaders']==0 else None)(status()),'stop before decoder load settles')
        return dict(noImplicitStart=True,closed=closed)
    case('not-ready-is-not-an-implicit-delayed-start',not_ready,prefix=4)

    for i in range(6):
        def immediate():
            fields=open_play();initial=dict(play);normal_stop()
            closed=until(lambda:(lambda s:s if s['state']=='closed' and s['decoderReaders']==0 else None)(status()),'stop fences opening commit')
            time.sleep(.12);assert command('continuousRemuxPlay',**fields)['state']=='closed'
            return dict(initial=initial,closed=closed,noLateRestart=True)
        case('stop-during-opening-%d'%i,immediate)
    if reordered_video:
        # Selecting reorder never changes the controller default. A retained old
        # playback ticket must also keep its mode after the next job is admitted.
        legacy='continuous-h264-video-v1';directory=root/'default-after-reorder';directory.mkdir();path=directory/'input.part'
        model=snapshot(path,raw,complete=True);model['timeline']['duration']='240000000';before=sha(path)
        fields=dict(requestId=uuid.uuid4().hex,sessionDirectory=str(directory),session=model,resourceId='clip')
        reject('continuousRemuxStart','invalid_request',**dict(fields,mode='continuous-h264-audio-v1'))
        job=command('continuousRemuxStart',_mode=legacy,**fields)
        error=wait(lambda:(lambda s:s if s['state']=='error' and s['released'] else None)(command('continuousRemuxStatus',_mode=legacy,jobId=job['jobId'])),'Default still rejects reordered input')
        assert error['errorCode']=='unsupported-stream-reorder' and not error['playbackReady'] and not error['outputComplete'],error
        retained=command('continuousPlaybackStatus',playbackId=old_playbacks[-1]);assert retained['state']=='closed',retained
        command('continuousRemuxForget',_mode=legacy,**{key:job[key] for key in ('jobId','sessionId','generation','writerInstanceId')})
        final=wait(lambda:(lambda s:s if s['forgotten'] else None)(command('continuousRemuxStatus',_mode=legacy,jobId=job['jobId'])),'Default job forgotten')
        assert final['sourceFilesClosed'] and sha(path)==before
        rows.append(dict(profile='default-profile-and-old-ticket-mode-isolation',result='PASS',defaultError=error,retainedPlayback=retained,final=final))
    assert max(latencies)<2,latencies
    return dict(result='PASS',count=len(rows),rows=rows,maxRequestSeconds=max(latencies),ordinaryPlayerIpc=True,playbackCommit=True,publicCapability=not reordered_video,internalReorderedProfile=reordered_video,mode=mode)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--player',action='append',required=True);p.add_argument('--reader-runner',required=True);p.add_argument('--report-directory',required=True);p.add_argument('--reordered-video',action='store_true');args=p.parse_args()
    try:
        result=run(args,lambda peers,root:exercise(peers,root,args.reordered_video),__file__);print(json.dumps(dict(result=result['result'],count=result['count'])))
    except Exception:
        Path(args.report_directory).mkdir(parents=True,exist_ok=True);(Path(args.report_directory)/'error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
