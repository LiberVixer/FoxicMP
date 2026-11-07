"""Ordinary continuous AAC players across rates, layouts and early audio EOF.

Six profiles, five functional lifecycle cases each. Physical append-only source,
real 10-second writer stall, decoder/worker identity, explicit retry and fences.
No downloader changes, no physical A/V or full stress acceptance.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import traceback

from progressive_continuous import AAC_MODE
from progressive_continuous_aac_controller_test import av_snapshot
from progressive_continuous_waiting_conformance import MeasuredClient
from progressive_continuous_worker_crash_conformance import OwnedWorker
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_player_crash_recovery import ApiError, sha, wait
from progressive_stream_aac_aligned_test import fixture
from progressive_stream_aac_window_test import PROFILES, packets as packet_records
from progressive_stream_remux_worker_test import probe


def exercise(peers, root, report, mode=AAC_MODE):
    assert mode == AAC_MODE
    owned, watches, regular_sessions, rows = [], [], [], []
    clients = [MeasuredClient(p.client) for p in peers]
    pids = [p.process.pid for p in peers]
    active_control = [None]
    control_initial = [None]
    control_buffers = []
    control_pending = [None]

    def save(name, value):
        (report/(name+'.json')).write_text(json.dumps(value, indent=2)+'\n', encoding='utf-8')

    def checkpoint(profile, case):
        save('current', dict(profile=profile, case=case))

    def record(profile, case, **fields):
        rows.append(dict(profile=profile, case=case, result='PASS', **fields))
        save('progress', dict(completed=len(rows), rows=rows))
        print(json.dumps(dict(profile=profile, case=case, result='PASS')), flush=True)

    def control():
        job = active_control[0]
        if job is None:
            return None
        value = job.playback_status()
        expected = control_initial[0]
        assert value['state'] in ('playing','buffering') and value['active'], value
        assert value['outputComplete'], value
        assert value['playbackId']==expected['playbackId'], value
        assert set(value['readerIds']) <= set(expected['readerIds']) and value['readerIds'], value
        assert value['positionSeconds'] >= expected['positionSeconds']-.1, value
        if value['state']=='buffering':
            if control_pending[0] is None:
                control_pending[0]=dict(profile=label,playbackId=value['playbackId'],position=value['positionSeconds'],began=time.monotonic())
            assert time.monotonic()-control_pending[0]['began']<2, value
        elif control_pending[0] is not None:
            event=control_pending[0];elapsed=time.monotonic()-event['began']
            assert elapsed<2, (event,value)
            control_buffers.append(dict(profile=event['profile'],playbackId=event['playbackId'],before=event['position'],after=value['positionSeconds'],seconds=elapsed))
            control_pending[0]=None
            save('independent-decoder-buffers',control_buffers)
        return value

    def until(check, label, seconds=20):
        def poll():
            control()
            assert all(p.process.poll() is None and p.process.pid==pid for p,pid in zip(peers,pids))
            return check()
        return wait(poll, label, seconds)

    def create(index, folder, model):
        from progressive_continuous import ContinuousRemux
        sdk = ContinuousRemux(clients[index], str(folder), mode=mode)
        owned.append(sdk); sdk.prepare(model, 'clip')
        return sdk

    def producer(sdk):
        value = sdk.status()
        save('last-producer', value)
        assert not value['errorCode'], value
        if not value['outputComplete']:
            try:
                sdk.heartbeat()
            except ApiError as error:
                if error.code != 'continuous-closed':
                    raise
                # Completion can occur after the healthy status reply. Only
                # confirmed completion may explain rejection of this heartbeat.
                value = sdk.status()
                assert value['outputComplete'] and not value['errorCode'], value
        return value

    def ready(sdk, rate, channels, samples):
        value = until(lambda: (lambda s:s if s['playbackReady'] else None)(producer(sdk)), 'validated AAC plan')
        assert value['audioWindowReady'] and value['audioSampleRate']==rate and value['audioChannels']==channels, value
        assert value['audioPresentationSamples']==str(samples), value
        return value

    def playing(sdk, samples, index=0, minimum=.2):
        def poll():
            state = sdk.playback_status()
            save('last-playback',state)
            assert state['state'] not in ('error','closed','finished'), state
            if state['state']!='playing' or state['positionSeconds']<=minimum or not state['decoderReaders'] or not state['pcmWindowApplied']:
                return None
            assert state['audio'], state
            if peers[index].edition=='qt':
                assert state['audioCodec']=='aac', state
                # A one-sample presentation may finish before an AO opens.
                expected = 'wasapi' if os.name=='nt' else 'pulse'
                assert state['audioOutput']==expected or (samples==1 and state['audioOutput']==''), state
            return state
        return until(poll, 'actual AAC decoder playback')

    def stalled(sdk, limit):
        last = [-1, time.monotonic()]
        def poll():
            info = producer(sdk); state = sdk.playback_status()
            assert state['state'] not in ('error','closed','finished'), state
            assert not info['outputComplete'] and int(info['maximumReadEnd'])<=limit, info
            if abs(state['positionSeconds']-last[0])>.02:
                last[:] = [state['positionSeconds'], time.monotonic()]
            if state['state']=='buffering' and state['positionSeconds']>1 and state['decoderReaders'] and time.monotonic()-last[1]>=.5:
                return state
        return until(poll, 'video waits for confirmed tail, including early audio EOF', 25)

    def finish(sdk, model):
        began=time.monotonic();proof=sdk.close(timeout=2)
        assert proof['released'] and proof['sourceFilesClosed'], proof
        receipts=until(lambda:(lambda rs:rs if rs and all(r['released'] and r['filesClosed'] and r['reason']=='files-closed' for r in rs) else None)(sdk.client.request(dict(version=1,action='readers',sessionId=model['sessionId'],generation='1'))['readers']), 'actual reader close receipts',2)
        forgotten=sdk.forget(timeout=2);assert forgotten['forgotten'],forgotten
        elapsed=time.monotonic()-began;assert elapsed<2,elapsed
        return dict(release=proof,receipts=receipts,forgotten=forgotten,closeSeconds=elapsed)

    def append(path, raw, count):
        old=path.stat().st_size;assert old<=count
        with path.open('ab') as file:
            assert file.write(raw[old:count])==count-old
            file.flush();os.fsync(file.fileno())
        assert path.read_bytes()==raw[:count]

    def publish(sdk, model, folder, count, complete=False, failed_audio=False):
        updated=copy.deepcopy(model);updated['revision']=str(int(model['revision'])+1)
        state='complete' if complete else 'downloading'
        updated['writer']['state']=updated['resources'][0]['state']=state
        for track in updated['tracks']:track['state']='failed' if failed_audio and track['kind']=='audio' else state
        updated['resources'][0]['ranges']=[dict(offset='0',length=str(count))]
        updated=protect_snapshot(updated,folder,previous=model);sdk.publish(updated)
        return updated

    ordinary=root/'ordinary.mp4'
    subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=160x90:rate=25:duration=24',
                    '-c:v','libx264','-threads','2','-bf','0','-g','25','-movflags','+frag_keyframe+empty_moov+default_base_moof',str(ordinary)],check=True,capture_output=True)
    ordinary_raw=ordinary.read_bytes();ordinary_sha=sha(ordinary)
    try:
        for label,rate,channels,fps,bframes,first,n in PROFILES:
            folder=root/label;folder.mkdir();samples=n if n is not None else rate*24-137
            source,_=fixture(folder,'reference',rate,channels,fps,bframes,first,False,seconds=24,samples=samples)
            raw=source.read_bytes();source_sha=sha(source);source_probe=probe(source)
            cuts={s:int(next(p['pos'] for p in source_probe['packets'] if p['codec_type']=='video' and float(p['pts_time'])>=s))+13 for s in (12,16,20,22)}
            audio_packets=[p for p in source_probe['packets'] if p['codec_type']=='audio']
            plan_audio_end=max(int(p['pos'])+int(p['size']) for p in audio_packets[:2])
            # Timestamp order need not match physical packet order. Classic
            # requires ten seconds of remuxed video before its first Run. A
            # short audio packet with an earlier DTS can block demux there.
            startup_audio=[p for p in audio_packets if float(p['dts_time'])<10]
            required_audio_end=max(plan_audio_end, *(int(p['pos'])+int(p['size']) for p in startup_audio))
            initial_second=next(second for second in (12,16) if cuts[second]>=required_audio_end)
            initial_cut=cuts[initial_second]
            # Classic resumes after five seconds of validated video. When the
            # startup prefix is 16 s, the 20 s prefix can leave only 4.96 s
            # beyond its paused position. Supply a sufficient partial refill.
            refill_second=20 if initial_second==12 else 22
            control_samples=n if n is not None else rate*120-137
            other_source,_=fixture(folder,'independent',rate,channels,fps,bframes,first,False,seconds=120,samples=control_samples)
            other_raw=other_source.read_bytes();other_sha=sha(other_source)
            other_model=av_snapshot(other_source,other_raw,len(other_raw),True);other_model['timeline']['duration']='1203000000'
            other=create(1,folder,other_model);ready(other,rate,channels,control_samples)
            until(lambda:producer(other)['outputComplete'],'independent complete')
            other.play(title='AAC profile independent '+label)
            other_initial=playing(other,control_samples,1)
            save(label+'-independent-start',other_initial)
            assert other_initial['positionSeconds']<2, ('New job resumed another job history',other_initial)
            control_initial[0]=other_initial;active_control[0]=other

            def make(name,prefix):
                directory=folder/name;directory.mkdir();path=directory/'input.part'
                model=av_snapshot(path,raw,prefix);model['timeline']['duration']='243000000'
                return directory,path,model,create(0,directory,model)

            # Physical growth, not just publication of a pre-existing full file.
            checkpoint(label,'growing-wait-complete');directory,path,model,sdk=make('growing',4)
            pending=until(lambda:(lambda s:s if s['producerCode']=='waiting' else None)(producer(sdk)),'header wait')
            assert pending['maximumReadEnd']=='4' and not pending['playbackReady'],pending
            try:sdk.play()
            except RuntimeError:pass
            else:raise AssertionError('AAC playback accepted before validated PCM plan')
            append(path,raw,cuts[12]);model=publish(sdk,model,directory,cuts[12])
            waiting_for_audio=None
            if initial_second!=12:
                waiting_for_audio=until(lambda:(lambda s:s if s['appliedRevision']==model['revision'] and s['producerCode']=='waiting' and int(s['neededOffset'])>=cuts[12] else None)(producer(sdk)),'unpublished required AAC packets after applied revision')
                assert int(waiting_for_audio['maximumReadEnd'])<=cuts[12],waiting_for_audio
                assert bool(waiting_for_audio['playbackReady'])==(cuts[12]>=plan_audio_end),waiting_for_audio
                append(path,raw,initial_cut);model=publish(sdk,model,directory,initial_cut)
            plan=ready(sdk,rate,channels,samples)
            sdk.play(title=label+' growing');initial=playing(sdk,samples);blocked=stalled(sdk,initial_cut);worker=producer(sdk)['workerId']
            began=time.monotonic();positions=[]
            while time.monotonic()-began<10:
                p=producer(sdk);value=sdk.playback_status();control()
                assert p['workerId']==worker and int(p['maximumReadEnd'])<=initial_cut and not p['outputComplete'],p
                assert value['state']=='buffering' and value['playbackId']==initial['playbackId'] and value['decoderReaders'],value
                assert set(value['readerIds'])<=set(initial['readerIds']) and value['readerIds'],value
                assert abs(value['positionSeconds']-blocked['positionSeconds'])<.15,value
                positions.append(value['positionSeconds']);time.sleep(.2)
            waited=time.monotonic()-began;assert waited>=10
            sdk.control('pause');paused=until(lambda:(lambda s:s if s['state']=='paused' else None)(sdk.playback_status()),'pause at tail')
            append(path,raw,cuts[refill_second]);gap=time.monotonic()
            while time.monotonic()-gap<.4:
                assert int(producer(sdk)['maximumReadEnd'])<=initial_cut;control();time.sleep(.05)
            model=publish(sdk,model,directory,cuts[refill_second]);time.sleep(.25)
            held=sdk.playback_status();assert held['state']=='paused' and abs(held['positionSeconds']-paused['positionSeconds'])<.15,held
            began=time.monotonic();sdk.control('play');resumed=playing(sdk,samples,minimum=blocked['positionSeconds']+.2);resume_seconds=time.monotonic()-began;assert resume_seconds<2
            assert producer(sdk)['workerId']==worker and resumed['playbackId']==initial['playbackId']
            sdk.control('pause');until(lambda:sdk.playback_status()['state']=='paused','pause before complete')
            append(path,raw,len(raw));model=publish(sdk,model,directory,len(raw),True)
            completed=until(lambda:(lambda s:s if s['outputComplete'] and s['sourceFilesClosed'] else None)(producer(sdk)),'verified full output')
            caches=list(Path(str(peers[0].registry)+'.continuous-cache').glob('remux-*/output.fmp4'));assert len(caches)==1,caches
            copied=directory/'verified.fmp4';copied.write_bytes(caches[0].read_bytes());expected=packet_records(source);actual=packet_records(copied)
            assert expected['video']==actual['video']
            assert [p[:3] for p in expected['audio']]==[p[:3] for p in actual['audio']]
            sought=[]
            for target in (18,1,22):
                sdk.control('seek',seconds=target)
                sought.append(until(lambda:(lambda s:s if s['state']=='paused' and abs(s['positionSeconds']-target)<.2 else None)(sdk.playback_status()),'seek after complete'))
            sdk.control('play');ended=until(lambda:(lambda s:s if s['state']=='finished' else None)(sdk.playback_status()),'actual completed EOF',10)
            assert ended['outputComplete'] and not ended['active'];assert sha(path)==source_sha
            record(label,'growing-wait-complete',sampleRate=rate,channels=channels,presentationSamples=samples,initialPublishedVideoSecond=initial_second,refillVideoSecond=refill_second,requiredAudioEnd=required_audio_end,pcmPlanAudioEnd=plan_audio_end,waitingForRequiredAudio=waiting_for_audio,initial=initial,validatedPlan=plan,stalled=blocked,waitSeconds=waited,heldRange=[min(positions),max(positions)],manualPause=paused,heldPause=held,resumed=resumed,resumeSeconds=resume_seconds,completed=completed,seeks=sought,ended=ended,packetProof=dict(video=len(actual['video']),audio=len(actual['audio']),canonicalSha256=hashlib.sha256(json.dumps(actual).encode()).hexdigest()),sourceSha256=source_sha,**finish(sdk,model))

            checkpoint(label,'cancel-replay');directory,path,model,sdk=make('cancel',initial_cut);ready(sdk,rate,channels,samples);sdk.play(title=label+' cancel');running=playing(sdk,samples);sdk.control('pause')
            until(lambda:sdk.playback_status()['state']=='paused','cancel manual pause');wire=dict(version=1,action='continuousRemuxPlay',jobId=sdk.job_id,requestId=sdk.play_request_id,**sdk.identity,title=label+' cancel',replace=False,activate=False)
            proof=sdk.close(timeout=2);assert proof['released'] and proof['sourceFilesClosed'];closed=until(lambda:(lambda s:s if not s['active'] and s['decoderReaders']==0 else None)(sdk.playback_status()),'cancel closes decoder',2);assert not closed['active'] and closed['decoderReaders']==0,closed
            for _ in range(3):
                reply=clients[0].request(wire);assert reply['state']=='closed' and reply['playbackId']==sdk.playback_id,reply
            record(label,'cancel-replay',running=running,closed=closed,replays=3,sourceSha256=sha(path),**finish(sdk,model))

            checkpoint(label,'worker-death-buffering');directory,path,model,sdk=make('worker',initial_cut);ready(sdk,rate,channels,samples);sdk.play(title=label+' worker');running=playing(sdk,samples);blocked=stalled(sdk,initial_cut)
            info=producer(sdk);watch=OwnedWorker(peers[0],info['workerId']);watches.append(watch);independent_before=control();killed=watch.terminate();began=time.monotonic();trace=[]
            def dead_released():
                s=sdk.status();trace.append(dict(seconds=time.monotonic()-began,producer=s))
                if s['errorCode']:assert s['errorCode']=='backend-worker-exited',s
                return s if s['errorCode'] and s['released'] and s['sourceFilesClosed'] else None
            try:terminal=until(dead_released,'worker death automatic release',2)
            finally:save(label+'-worker-release',trace)
            release_seconds=time.monotonic()-began;assert killed['terminationSeconds']+release_seconds<2
            stopped=until(lambda:(lambda s:s if s['state']=='error' and s['decoderReaders']==0 else None)(sdk.playback_status()),'dead worker closes decoder',2);assert stopped['error']=='backend-worker-exited',stopped
            final=finish(sdk,model);append(path,raw,cuts[20]);previous=model;model=copy.deepcopy(model);model['revision']='2';model['resources'][0]['ranges']=[dict(offset='0',length=str(cuts[20]))];model=protect_snapshot(model,directory,previous=previous)
            retry=create(0,directory,model);ready(retry,rate,channels,samples);assert not sdk.playback_status()['active'];retry.play(title=label+' explicit retry');restarted=playing(retry,samples)
            assert restarted['playbackId']!=running['playbackId'] and not set(restarted['readerIds'])&set(info['readerIds'])
            record(label,'worker-death-buffering',stalled=blocked,killed=killed,terminal=terminal,decoderTerminal=stopped,automaticReleaseSeconds=release_seconds,independentBefore=independent_before,independentAfter=control(),explicitRetry=restarted,retryClosed=finish(retry,model),writerAppendAfterRelease=True,**final)

            checkpoint(label,'ordinary-replacement');directory,path,model,sdk=make('replacement',initial_cut);ready(sdk,rate,channels,samples);sdk.play(title=label+' replacement');running=playing(sdk,samples);sdk.control('pause');until(lambda:sdk.playback_status()['state']=='paused','replace paused AAC')
            regular=snapshot(ordinary,ordinary_raw,complete=True);regular['timeline']['duration']='240000000';regular['tracks'][0].update(containerHint='fmp4',codecHint='h264',initializationReady=True);regular_sessions.append(regular);admission=[]
            def admit():
                try:return clients[0].request(dict(version=1,action='start',session=regular,sessionDirectory=str(root),replace=True))
                except ApiError as error:
                    assert error.code in ('preparation_cancelled','reader_registry_busy'),error.reply
                    admission.append(error.code);return None
            until(admit,'explicit ordinary replacement admission',2)
            def regular_playing():
                value=clients[0].request(dict(version=1,action='heartbeat',sessionId=regular['sessionId']))
                return value if value.get('active') and value['state']=='playing' and value['positionSeconds']>.2 else None
            replacement=until(regular_playing,'ordinary replacement actual playback')
            old=until(lambda:(lambda s:s if s['state']=='closed' and s['decoderReaders']==0 else None)(sdk.playback_status()),'old AAC closed by replacement',2)
            sdk.control('stop');clients[0].request(dict(version=1,action='continuousRemuxCancel',jobId=sdk.job_id,**sdk.identity))
            for reader in running['readerIds']:clients[0].request(dict(version=1,action='readerRelease',readerId=reader))
            continued=until(lambda:(lambda s:s if s and s['readerId']==replacement['readerId'] and s['positionSeconds']>replacement['positionSeconds']+.15 else None)(regular_playing()),'late AAC events leave ordinary view alive',2)
            clients[0].request(dict(version=1,action='cancel',sessionId=regular['sessionId']))
            receipts=until(lambda:(lambda rs:rs if rs and all(r['released'] and r['filesClosed'] for r in rs) else None)(clients[0].client.readers(regular['sessionId'],1)),'ordinary close receipts',2)
            for receipt in receipts:assert clients[0].client.forget_reader(receipt['readerId'])['forgotten']
            regular_sessions.remove(regular)
            record(label,'ordinary-replacement',oldPlayback=running,closedOld=old,ordinaryInitial=replacement,ordinaryContinued=continued,admissionRetries=admission,ordinaryReceipts=receipts,**finish(sdk,model))

            checkpoint(label,'required-audio-failure');directory,path,model,sdk=make('audio-failure',initial_cut);ready(sdk,rate,channels,samples);sdk.play(title=label+' required audio');running=playing(sdk,samples);sdk.control('pause');until(lambda:sdk.playback_status()['state']=='paused','required audio paused')
            model=publish(sdk,model,directory,initial_cut,failed_audio=True);began=time.monotonic()
            failed=until(lambda:(lambda s:s if s['errorCode']=='writer-failed' and s['released'] and s['sourceFilesClosed'] else None)(sdk.status()),'required audio failure release',2)
            stopped=until(lambda:(lambda s:s if s['state']=='error' and s['decoderReaders']==0 else None)(sdk.playback_status()),'required audio failure closes decoder',2)
            assert stopped['error']=='writer-failed',stopped;elapsed=time.monotonic()-began;assert elapsed<2
            record(label,'required-audio-failure',running=running,producerFailed=failed,playbackFailed=stopped,automaticReleaseSeconds=elapsed,**finish(sdk,model))
            assert sha(source)==source_sha and sha(ordinary)==ordinary_sha and sha(other_source)==other_sha
            assert not list(Path(str(peers[0].registry)+'.continuous-cache').glob('remux-*'))
            final_control=until(lambda:(lambda s:s if s['state']=='playing' else None)(control()),'independent endpoint playing after cases',2)
            assert control_pending[0] is None
            active_control[0]=None;other_final=finish(other,other_model)
            save(label+'-independent',dict(initial=other_initial,final=final_control,closed=other_final,sourceSha256=other_sha))
            if peers[0].edition=='classic':
                # Ordinary replacement may add its own history. The pre-existing
                # virtual alias must neither be read, reordered nor overwritten.
                for peer in peers:
                    history=Path(peer.player).parent/'history.mpc_lst'
                    text=history.read_text(encoding='utf-8-sig')
                    sections=[part for part in text.split('\n[') if 'Path=continuous-video.mp4\n' in part]
                    assert len(sections)==1 and 'Title=Existing continuous alias\n' in sections[0] and 'Position=00:00:37\n' in sections[0], text
                save(label+'-history',dict(existingAliasPreserved=True,rememberFilePosition=True,newJobStartsAtBeginning=True))
        maximum=max(c['seconds'] for client in clients for c in client.calls);assert maximum<2
        return dict(result='PASS',count=len(rows),rows=rows,fixtureProfile='six-aac-lc-profiles',profiles=[p[0] for p in PROFILES],maxRequestSeconds=maximum,realWriterPauseSeconds=10,publicMode=mode,independentPlayingEndpoint=True,independentDecoderBufferEvents=control_buffers,independentDecoderBufferLimitSeconds=2,sameParentPids=pids,profileSourceSha256=sha(Path(__file__).with_name('progressive_stream_aac_window_test.py')),fixtureGeneratorSha256=sha(Path(__file__).with_name('progressive_stream_aac_aligned_test.py')),workerKillHelperSha256=sha(Path(__file__).with_name('progressive_continuous_worker_crash_conformance.py')),snapshotHelperSha256=sha(Path(__file__).with_name('progressive_continuous_aac_controller_test.py')),ordinaryReplacementSha256=ordinary_sha,physicalAvNotMeasured=True,fullStressAcceptance=False)
    finally:
        primary_failure=sys.exc_info()[0] is not None;errors=[];active_control[0]=None
        for regular in reversed(regular_sessions):
            try:
                clients[0].request(dict(version=1,action='cancel',sessionId=regular['sessionId']))
                receipts=clients[0].client.readers(regular['sessionId'],1)
                for receipt in receipts:
                    assert receipt['released'] and receipt['filesClosed'],receipt
                    clients[0].client.forget_reader(receipt['readerId'])
            except Exception:errors.append(traceback.format_exc())
        for sdk in reversed(owned):
            if not sdk.forgotten:
                try:
                    proof=sdk.close(timeout=5);assert proof['released'] and proof['sourceFilesClosed'],proof;sdk.forget(timeout=5)
                except Exception:errors.append(traceback.format_exc())
        for watch in watches:watch.close()
        save('calls',[c.calls for c in clients])
        if errors:
            save('cleanup-errors',errors)
            if not primary_failure:raise AssertionError('Profile test cleanup failed')
