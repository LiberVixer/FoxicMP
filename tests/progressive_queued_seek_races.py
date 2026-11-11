"""Real concurrent IPC and bounded queue lifecycle stress; no application hooks."""
import concurrent.futures
import copy
import json
from pathlib import Path
import subprocess
import sys
import threading
import time
from progressive_continuous import ContinuousRemux,AAC_MODE
from progressive_continuous_test_media import source_fixture,source_snapshot,source_cuts,audible_decoder_ready
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_player_crash_recovery import wait,ApiError,sha
from progressive_continuous_lifecycle_stress import BUDGET,bounds
from progressive_continuous_heartbeat_conformance import WorkerExit
from progressive_process_metrics import sample
from progressive_stream_aac_aligned_test import fixture as av_fixture

PROFILES = {'48000-mono': (48000,1,'25',2), '44100-stereo': (44100,2,'30000/1001',3)}

def exercise(peers,root,report,stress=False,fixture_profile=None):
    report=Path(report);report.mkdir(parents=True,exist_ok=True)
    if fixture_profile is None:
        original=source_fixture(root,'reference',AAC_MODE)
        rate,channels,fps,bframes=48000,2,'30000/1001',3
    else:
        rate,channels,fps,bframes=PROFILES[fixture_profile]
        original=av_fixture(root,'reference',rate,channels,fps,bframes,False,False,
            seconds=24,samples=rate*24-137)[0]
    raw=original.read_bytes();cut=source_cuts(original,AAC_MODE)[12]
    geometry=dict(sampleRate=rate,channels=channels,videoFps=fps,bframes=bframes,presentationSamples=str(rate*24-137))
    ordinary_source=source_fixture(root,'ordinary-source');ordinary=root/'ordinary.mp4'
    subprocess.run(['ffmpeg','-v','error','-y','-i',str(ordinary_source),'-c','copy','-movflags','+frag_keyframe+empty_moov+default_base_moof',str(ordinary)],check=True,capture_output=True)
    ordinary_raw=ordinary.read_bytes();original_sha=sha(original);ordinary_sha=sha(ordinary)
    peer=peers[0];pids=[p.process.pid for p in peers];owned=[];regular=[];rows=[];warmup=[];began=time.monotonic();requests=[]
    def request(message):
        started=time.monotonic()
        try:reply=peer.client.request(message,timeout=BUDGET['requestSeconds'])
        except ApiError as error:reply=error.reply
        row=dict(message=message,startedSeconds=started-began,finishedSeconds=time.monotonic()-began,reply=reply)
        requests.append(row);assert row['finishedSeconds']-row['startedSeconds']<BUDGET['requestSeconds'],row
        return row
    def control(sdk,command,**fields):return dict(version=1,action='continuousPlaybackControl',playbackId=sdk.playback_id,command=command,**fields)
    def queue(sdk,target):return control(sdk,'seek',seconds=target,allowGrowing=True,waitForRange=True)
    def pair(left,right,offset):
        barrier=threading.Barrier(3)
        def send(message,delay):
            barrier.wait(timeout=5)
            if delay:time.sleep(delay)
            return request(message)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            futures=[pool.submit(send,left,max(0,offset)),pool.submit(send,right,max(0,-offset))]
            barrier.wait(timeout=5);return [f.result(timeout=5) for f in futures]
    def ready(sdk):
        def poll():
            s=sdk.status();assert not s['errorCode'],s
            if s['playbackReady'] and s['indexedEndSeconds']>10:
                if fixture_profile is not None:
                    assert s['audioWindowReady'] and s['audioSampleRate']==rate and s['audioChannels']==channels and s['audioPresentationSamples']==geometry['presentationSamples'],s
                return s
            sdk.heartbeat()
        return wait(poll,'queue race ready',20)
    def active(sdk):
        def poll():
            s=sdk.playback_status();assert s['state'] not in ('error','closed','finished'),s
            return s if s['state']=='playing' and s['positionSeconds']>.2 and audible_decoder_ready(s,peer.edition,AAC_MODE) else None
        return wait(poll,'queue race playing',20)
    def create(name,client=peer.client,complete=False):
        folder=root/name;folder.mkdir();path=folder/'input.part';model=source_snapshot(path,raw,prefix=len(raw) if complete else cut,complete=complete,mode=AAC_MODE);path.write_bytes(raw)
        sdk=ContinuousRemux(client,str(folder),mode=AAC_MODE);owned.append(sdk);sdk.prepare(model,'clip');ready(sdk)
        if complete:wait(lambda:sdk.status()['outputComplete'],'independent complete')
        sdk.play(title=name);actual=active(sdk);sdk.control('pause');held=wait(lambda:(lambda s:s if s['state']=='paused' else None)(sdk.playback_status()),'queue race pause',5)
        return sdk,path,model,held,folder
    def updated(model,folder,complete=False,failed=False):
        new=copy.deepcopy(model);new['revision']=str(int(model['revision'])+1);new['resources'][0]['ranges']=[dict(offset='0',length=str(len(raw)))]
        if complete:
            new['writer']['state']=new['resources'][0]['state']='complete'
            for track in new['tracks']:track['state']='complete'
        if failed:new['tracks'][0]['state']='failed'
        return protect_snapshot(new,folder,previous=model)
    def finish(sdk,path):
        start=time.monotonic();receipt=sdk.close(timeout=BUDGET['releaseSeconds']);elapsed=time.monotonic()-start
        assert receipt['released'] and receipt['sourceFilesClosed'] and elapsed<BUDGET['releaseSeconds'],(elapsed,receipt)
        receipts=sdk.client.readers(sdk.identity['sessionId'],1);assert receipts and all(r['released'] and r['filesClosed'] and r['reason']=='files-closed' for r in receipts),receipts
        forgotten=sdk.forget(timeout=2);assert forgotten['forgotten'] and sha(path)==original_sha,(forgotten,sha(path))
        assert not list(Path(str(peer.registry)+'.continuous-cache').glob('remux-*')) if sdk.client is peer.client else True
        return dict(release=receipt,receipts=receipts,forgotten=forgotten,closeSeconds=elapsed)
    def settled(sdk,target,remaining):
        last=None
        def poll():
            nonlocal last
            s=sdk.playback_status();assert s['state'] not in ('error','closed','finished'),s
            last=s
            return s if s['state']=='paused' and s['pendingSeekSeconds'] is None and s['seekState']=='applied' and abs(s['positionSeconds']-target)<.12 else None
        try:s=wait(poll,'queued target applied',10)
        except AssertionError as e:raise AssertionError(('Queued seek deadline',target,remaining,last)) from e
        assert s['remainingGrowingSeeks'] in remaining if isinstance(remaining,tuple) else s['remainingGrowingSeeks']==remaining,s;return s
    def cancel_regular(model):
        answer=request(dict(version=1,action='cancel',sessionId=model['sessionId']))['reply'];assert answer['ok'],answer
        receipts=wait(lambda:(lambda rs:rs if rs and all(r['released'] and r['filesClosed'] for r in rs) else None)(peer.client.readers(model['sessionId'],1)),'regular replacement released',2)
        for receipt in receipts:assert peer.client.forget_reader(receipt['readerId'])['forgotten']
        regular.remove(model);return receipts
    try:
        other,other_path,other_model,independent,_=create('independent',peers[1].client,True)
        count=104 if stress else 36
        for index in range(count):
            mode='stress' if stress else ('target-target','target-cancel','target-stop','target-job-cancel','target-reader-release','target-replace','target-failure','publish-cancel','publish-target')[index//4]
            name=mode+'-'+str(index);(report/'current.json').write_text(json.dumps(dict(profile=name,completed=len(rows)))+'\n')
            sdk,path,model,held,folder=create(name);watch=WorkerExit(sdk.status()['workerId'])
            try:
                history=[];expected=16
                if stress:
                    # A single pending target may be replaced/cancelled repeatedly
                    # without opening a decoder or consuming the 16-seek budget.
                    for turn in range(16):
                        sdk.heartbeat();left,right=pair(queue(sdk,20.5),control(sdk,'cancelSeek') if turn%2 else queue(sdk,21.),0)
                        assert left['reply']['ok'] and right['reply']['ok'],(left,right)
                        current=sdk.playback_status();a=left['reply'];z=right['reply']
                        if turn%2:target=None if int(z['seekRevision'])==int(a['seekRevision']) else 20.5
                        else:target=20.5 if int(a['seekRevision'])>int(z['seekRevision']) else 21.
                        assert current['pendingSeekSeconds']==target and current['remainingGrowingSeeks']==16 and current['decoderReaders']==held['decoderReaders'] and current['readerIds']==held['readerIds'],current
                        assert current['state']=='paused' and abs(current['positionSeconds']-held['positionSeconds'])<.12,current
                        history.append(dict(left=left,right=right,observed=current))
                    sdk.control('cancelSeek')
                    for target in (2.,1.):sdk.control('seek',seconds=target,allow_growing=True,wait_for_range=True);expected-=1;history.append(settled(sdk,target,expected))
                    if index==4:
                        for target in [.5,1.]*7:sdk.control('seek',seconds=target,allow_growing=True,wait_for_range=True);expected-=1;history.append(settled(sdk,target,expected))
                        try:sdk.control('seek',seconds=20.5,allow_growing=True,wait_for_range=True)
                        except ApiError as e:assert e.code=='continuous-seek-limit',e.reply
                        else:raise AssertionError('17th queued transition accepted')
                        sdk.publish(updated(model,folder,complete=True));wait(lambda:sdk.status()['outputComplete'],'quota complete')
                        sdk.control('seek',seconds=22);wait(lambda:abs(sdk.playback_status()['positionSeconds']-22)<.12,'complete bypasses quota')
                    else:sdk.control('seek',seconds=20.5,allow_growing=True,wait_for_range=True)
                    ending=('stop','cancel','reader-release','failure')[index%4]
                    if ending=='stop':sdk.control('stop')
                    elif ending=='cancel':peer.client.request(dict(version=1,action='continuousRemuxCancel',jobId=sdk.job_id,**sdk.identity))
                    elif ending=='reader-release':
                        output_ids=set(sdk.playback_status()['readerIds'])
                        active_id=next(r['readerId'] for r in peer.client.readers(sdk.identity['sessionId'],1) if r['readerId'] in output_ids and not r['released'] and not r['filesClosed'])
                        peer.client.request(dict(version=1,action='readerRelease',readerId=active_id))
                    else:sdk.publish(updated(model,folder,failed=True));wait(lambda:sdk.playback_status()['state']=='error','stress producer failure',5)
                    terminal=wait(lambda:(lambda s:s if s['pendingSeekSeconds'] is None else None)(sdk.playback_status()),'stress drops pending',5)
                else:
                    offset=(0,-.003,.003,0)[index%4]
                    sdk.control('seek',seconds=20.,allow_growing=True,wait_for_range=True)
                    left=queue(sdk,20.5)
                    if mode=='target-target':right=queue(sdk,21.)
                    elif mode=='target-cancel':right=control(sdk,'cancelSeek')
                    elif mode=='target-stop':right=control(sdk,'stop')
                    elif mode=='target-job-cancel':right=dict(version=1,action='continuousRemuxCancel',jobId=sdk.job_id,**sdk.identity)
                    elif mode=='target-reader-release':right=dict(version=1,action='readerRelease',readerId=held['readerIds'][0])
                    elif mode=='target-failure':right=dict(version=1,action='continuousRemuxUpdate',jobId=sdk.job_id,session=updated(model,folder,failed=True))
                    elif mode=='target-replace':
                        regular_model=snapshot(ordinary,ordinary_raw,complete=True);regular_model['timeline']['duration']='240000000';regular_model['tracks'][0].update(containerHint='fmp4',codecHint='h264',initializationReady=True);regular.append(regular_model)
                        right=dict(version=1,action='start',session=regular_model,sessionDirectory=str(root),replace=True)
                    else:
                        left=dict(version=1,action='continuousRemuxUpdate',jobId=sdk.job_id,session=updated(model,folder))
                        right=control(sdk,'cancelSeek') if mode=='publish-cancel' else queue(sdk,21.)
                    history=pair(left,right,offset);(report/(name+'-requests.json')).write_text(json.dumps(history,indent=2)+'\n')
                    a,z=[x['reply'] for x in history];assert z['ok'],z
                    if not a['ok']:assert mode in ('target-stop','target-job-cancel','target-reader-release','target-replace','target-failure') and a['code']=='continuous-playback-closed',a
                    if mode in ('target-target','target-cancel'):
                        assert a['ok'],a
                        expected_target=(20.5 if int(a['seekRevision'])>int(z['seekRevision']) else 21.) if mode=='target-target' else (None if int(z['seekRevision'])==int(a['seekRevision']) else 20.5)
                        terminal=sdk.playback_status();assert terminal['pendingSeekSeconds']==expected_target and terminal['remainingGrowingSeeks']==16 and terminal['state']=='paused' and abs(terminal['positionSeconds']-held['positionSeconds'])<.12,terminal
                    elif mode.startswith('publish'):
                        assert a['ok'],a
                        if mode=='publish-target':terminal=settled(sdk,21.,(14,15))
                        else:
                            terminal=wait(lambda:(lambda s:s if s['pendingSeekSeconds'] is None else None)(sdk.playback_status()),'published cancellation settles',10)
                            assert terminal['remainingGrowingSeeks'] in (15,16),terminal
                            expected_position=20. if terminal['remainingGrowingSeeks']==15 else held['positionSeconds']
                            wait(lambda:abs(sdk.playback_status()['positionSeconds']-expected_position)<.12,'cancel never reverses committed seek',10)
                    else:
                        terminal=wait(lambda:(lambda s:s if s['state'] in ('closed','error') and s['pendingSeekSeconds'] is None else None)(sdk.playback_status()),'terminal queue fence',10)
                        assert terminal['remainingGrowingSeeks']==16,terminal
                        if mode=='target-replace':
                            def replacement():
                                s=peer.client.request(dict(version=1,action='heartbeat',sessionId=regular_model['sessionId']));return s if s.get('active') and s['state']=='playing' and s['positionSeconds']>.2 else None
                            replacement_initial=wait(replacement,'replacement plays',20)
                            for message in (control(sdk,'stop'),control(sdk,'cancelSeek'),queue(sdk,21.)):
                                reply=request(message)['reply'];assert reply['ok'] if message['command'] in ('stop','cancelSeek') else not reply['ok'] and reply['code']=='continuous-playback-closed',reply
                            continued=wait(lambda:(lambda s:s if s and s['positionSeconds']>replacement_initial['positionSeconds']+.15 else None)(replacement()),'late queue leaves replacement',2)
                            terminal=dict(terminal,replacementInitial=replacement_initial,replacementContinued=continued,regularReceipts=cancel_regular(regular_model))
                final=finish(sdk,path);wait(watch.exited,'queue worker exit',2)
                control_status=other.playback_status();assert control_status['state']=='paused' and control_status['playbackId']==independent['playbackId'] and control_status['readerIds']==independent['readerIds'] and abs(control_status['positionSeconds']-independent['positionSeconds'])<.12,control_status
                assert [p.process.pid for p in peers]==pids and all(p.process.poll() is None for p in peers)
                entry=dict(profile=name,result='PASS',initial=held,history=history,terminal=terminal,final=final,workerExit=watch.proof(),independent=control_status,wallSeconds=time.monotonic()-began,metrics=[sample(p) for p in pids])
                if stress and index<4:warmup.append(entry)
                else:rows.append(entry)
                (report/'progress.json').write_text(json.dumps(dict(completed=len(rows),warmup=warmup,rows=rows),indent=2)+'\n')
                print(json.dumps(dict(profile=name,result='PASS',completed=len(rows))),flush=True)
            finally:watch.close()
        resource_summary=None
        if stress:
            # Write all three measurement windows before the inherited asserts.
            windows={name:rows[slice_] for name,slice_ in [('early',slice(0,10)),('steady',slice(30,40)),('late',slice(-10,None))]}
            (report/'resource-windows.json').write_text(json.dumps(dict(budget=BUDGET,windows=windows),indent=2)+'\n')
            resource_summary=bounds(rows)
        other_final=finish(other,other_path);assert sha(original)==original_sha and sha(ordinary)==ordinary_sha
        return dict(result='PASS',count=len(rows),rows=rows,warmup=warmup,otherFinal=other_final,resourceBudget=BUDGET if stress else None,resourceSummary=resource_summary,realConcurrentIpcConnections=True,deterministicInternalIoSchedule=False,fullStressAcceptance=False,originalsUnchanged=True,totalRequests=len(requests),fixtureProfile=fixture_profile or '48000-stereo',fixtureGeometry=geometry,fixtureSha256=original_sha)
    finally:
        primary=sys.exc_info()[0] is not None;errors=[]
        for model in list(regular):
            try:cancel_regular(model)
            except Exception as e:errors.append(repr(e))
        for sdk in reversed(owned):
            if not sdk.forgotten:
                try:sdk.close(timeout=5);sdk.forget(timeout=5)
                except Exception as e:errors.append(repr(e))
        if errors:
            (report/'cleanup-errors.json').write_text(json.dumps(errors,indent=2))
            if not primary:raise AssertionError(errors)
