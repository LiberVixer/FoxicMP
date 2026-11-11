"""Three complete ten-minute AAC growing jobs in the same two ordinary players.

The 50000-packet/64 MiB public job limits remain unchanged. Smoke is explicit
and can never satisfy the thirty-minute acceptance gate. No physical A/V claim.
"""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import time
import traceback

from progressive_continuous import ContinuousRemux,AAC_MODE
from progressive_continuous_test_media import source_snapshot,audible_decoder_ready
from progressive_integrity import protect_snapshot
from progressive_player_crash_recovery import sha,ApiError
from progressive_continuous_heartbeat_conformance import WorkerExit
from progressive_process_metrics import sample
from progressive_stream_aac_window_test import packets

BUDGET=dict(handlesGrowth=8,threadsGrowth=2,memoryGrowthBytes=256*1024**2,averageCores=1.,
    requestSeconds=2.,readyToPlayingSeconds=3.,resumeSeconds=2.,releaseSeconds=2.,cacheBytes=68*1024**2)

class Measured:
    def __init__(self,client):self.client=client;self.calls=[]
    def request(self,message,timeout=5):
        size=len(json.dumps(message,ensure_ascii=False,separators=(',',':')).encode())+1
        assert size<=65536,size
        began=time.monotonic()
        try:return self.client.request(message,timeout=timeout)
        finally:
            elapsed=time.monotonic()-began
            self.calls.append(dict(action=message['action'],seconds=elapsed,bytes=size))
            assert elapsed<BUDGET['requestSeconds'],self.calls[-1]

def exercise(peers,root,report,fixture,smoke=False):
    fixture=Path(fixture);report=Path(report)
    manifest=json.loads((fixture/'fixture.json').read_text());reference=fixture/'reference.mp4'
    assert manifest['result']=='PASS' and manifest['sha256']==sha(reference)
    assert manifest['seconds']==600 and sum(manifest['packetCounts'].values())<50000
    assert manifest['bytes']<60*1024**2
    raw=reference.read_bytes();cuts={int(k):v for k,v in manifest['cuts'].items()}
    reference_packets=packets(reference)
    clients=[Measured(p.client) for p in peers];pids=[p.process.pid for p in peers]
    active=[];watches=[];samples=[];events=[];runs=[];began=time.monotonic();last_sample=-10.
    def save(name,value):
        path=report/(name+'.json');tmp=path.with_suffix('.pending');tmp.write_text(json.dumps(value,indent=2)+'\n');tmp.replace(path)
    def event(name,**fields):
        events.append(dict(event=name,seconds=time.monotonic()-began,**fields));save('events',events)
    def wait(check,label,timeout):
        until=time.monotonic()+timeout;last=None
        while time.monotonic()<until:
            last=check()
            if last:return last
            time.sleep(.05)
        raise AssertionError((label,last))
    def resource_bounds(rows,field):
        early=[r for r in rows if 60<=r['seconds']<=90];late=[r for r in rows if r['seconds']>=rows[-1]['seconds']-60]
        assert len(early)>=5 and len(late)>=10
        summaries=[]
        for index in range(2):
            high=lambda window,key:max(r[field][index][key] for r in window)
            for key,growth in [('handles',BUDGET['handlesGrowth']),('threads',BUDGET['threadsGrowth']),('rssBytes',BUDGET['memoryGrowthBytes']),('privateBytes',BUDGET['memoryGrowthBytes'])]:
                assert high(late,key)<=high(early,key)+growth,(field,index,key,high(early,key),high(late,key))
            cores=(late[-1][field][index]['cpuSeconds']-late[0][field][index]['cpuSeconds'])/(late[-1]['seconds']-late[0]['seconds'])
            assert 0<=cores<=BUDGET['averageCores'],(field,index,cores)
            summaries.append(dict(peer=index,early={k:high(early,k) for k in ['handles','threads','rssBytes','privateBytes']},
                late={k:high(late,k) for k in ['handles','threads','rssBytes','privateBytes']},lastMinuteAverageCores=cores))
        return summaries
    save('inputs',dict(budgets=BUDGET,fixture=manifest,playerPids=pids,sourceSha256=sha(__file__),smoke=smoke))
    try:
        for cycle in range(1 if smoke else 3):
            folder=root/('cycle-'+str(cycle));folder.mkdir();path=folder/'input.part'
            # A two-second GOP can leave only 9.92 s indexed at a 12 s byte cut.
            # Keep Classic's normal ten-second startup policy unchanged.
            prefix=cuts[16];model=source_snapshot(path,raw,prefix=prefix,complete=False,mode=AAC_MODE)
            model['timeline']['duration']='6000000000';digest=hashlib.sha256(raw[:prefix])
            published=model;jobs=[];identities=[];startup=[];cycle_samples=[];publications=[]
            for i,peer in enumerate(peers):
                sdk=ContinuousRemux(clients[i],str(folder),mode=AAC_MODE);active.append(sdk)
                prepare_at=time.monotonic();sdk.prepare(published,'clip')
                def ready():
                    value=sdk.status();assert not value['errorCode'],value
                    if value['playbackReady'] and value['indexedEndSeconds']>=10:
                        assert value['audioWindowReady'] and value['audioSampleRate']==48000 and value['audioChannels']==2 and value['audioPresentationSamples']==manifest['presentationSamples'],value
                        return value
                    sdk.heartbeat()
                producer=wait(ready,'soak preparation',30);prepared_at=time.monotonic()
                watch=WorkerExit(producer['workerId']);watches.append(watch)
                sdk.play(title='Private 30-minute A/V soak')
                startup_trace=[]
                def playing():
                    value=sdk.playback_status();assert value['state'] not in ('error','closed','finished'),value
                    startup_trace.append(dict(seconds=time.monotonic()-prepared_at,playback=value))
                    return value if value['state']=='playing' and value['positionSeconds']>.15 and value['decoderReaders'] and audible_decoder_ready(value,peer.edition,AAC_MODE) else None
                try:state=wait(playing,'ready to actual A/V playing',3)
                finally:save('startup-trace-%d-%d'%(cycle,i),startup_trace)
                latency=time.monotonic()-prepared_at;assert latency<BUDGET['readyToPlayingSeconds'],latency
                startup.append(dict(peer=i,preparationSeconds=prepared_at-prepare_at,readyToPlayingSeconds=latency,state=state,producer=producer))
                identities.append(dict(workerId=producer['workerId'],playbackId=state['playbackId'],readerIds=state['readerIds'],watch=watch))
                jobs.append(sdk)
            cycle_at=time.monotonic();heartbeat_at=cycle_at;complete=False;pause_at=None;pause_done=False;buffered=[False,False]
            pause_duration=2 if smoke else [10,30,60][cycle];pause_point=3 if smoke else 90
            resume_targets=None;resume_times=[None,None];manual=None;manual_done=False;manual_at=None;manual_resume=None
            gap_done=False;last_views=None
            event('cycle-started',cycle=cycle,startup=startup,pauseSeconds=pause_duration)
            def observe():
                nonlocal heartbeat_at,last_sample,last_views
                now=time.monotonic();views=[]
                assert all(p.process.poll() is None and p.process.pid==pid for p,pid in zip(peers,pids))
                for i,sdk in enumerate(jobs):
                    producer=sdk.status();state=sdk.playback_status()
                    assert not producer['errorCode'] and state['state'] not in ('error','closed'),(producer,state)
                    assert state['playbackId']==identities[i]['playbackId'] and state['readerIds']==identities[i]['readerIds'],state
                    if not smoke:assert state['positionSeconds']<=now-cycle_at+20,state
                    if not complete:
                        assert not producer['sourceComplete'] and not producer['outputComplete'] and state['state']!='finished',producer
                        assert not identities[i]['watch'].exited()
                    assert producer['workerId']==identities[i]['workerId'] and int(producer['maximumReadEnd'])<=prefix,producer
                    if now-heartbeat_at>=5 and not producer['outputComplete']:
                        try:sdk.heartbeat()
                        except ApiError as error:
                            final=sdk.status();assert error.code=='continuous-closed' and final['outputComplete'] and not final['errorCode'],final
                    views.append(dict(producer=producer,playback=state))
                if now-heartbeat_at>=5:heartbeat_at=now
                if now-began-last_sample>=5:
                    worker_metrics=[]
                    for i in range(2):
                        try:metric=None if identities[i]['watch'].exited() else sample(identities[i]['workerId'])
                        except OSError:
                            assert complete and identities[i]['watch'].exited()
                            metric=None
                        worker_metrics.append(metric)
                    disk=[sum(p.stat().st_size for p in Path(str(peer.registry)+'.continuous-cache').rglob('*') if p.is_file()) for peer in peers]
                    assert all(n<=BUDGET['cacheBytes'] for n in disk),disk
                    row=dict(seconds=now-began,cycleSeconds=now-cycle_at,cycle=cycle,publishedBytes=prefix,physicalBytes=path.stat().st_size,
                        completePublished=complete,mainMetrics=[sample(pid) for pid in pids],workerMetrics=worker_metrics,cacheBytes=disk,views=views)
                    samples.append(row);cycle_samples.append(dict(row,seconds=now-cycle_at));last_sample=now-began
                    save('samples',samples);save('progress',dict(cycle=cycle,seconds=now-began,cycleSeconds=now-cycle_at,smoke=smoke,
                        samples=len(samples),last=row,budgets=BUDGET))
                last_views=views;return views
            def publish(end,finished=False,writer_paused=False,gap=False):
                nonlocal prefix,published,complete
                old=prefix;assert end>=old
                if end>old:
                    with path.open('ab') as stream:
                        data=raw[old:end];assert stream.write(data)==end-old;stream.flush()
                        import os
                        os.fsync(stream.fileno());digest.update(data)
                if gap:
                    until=time.monotonic()+.5
                    while time.monotonic()<until:observe();time.sleep(.05)
                new=copy.deepcopy(published);new['revision']=str(int(new['revision'])+1)
                new['resources'][0]['ranges']=[dict(offset='0',length=str(end))]
                status='complete' if finished else 'paused' if writer_paused else 'downloading'
                new['writer']['state']=new['resources'][0]['state']=status
                for track in new['tracks']:track['state']=status
                new=protect_snapshot(new,folder,previous=published)
                prefix=end;complete=finished;published=new
                for sdk in jobs:sdk.publish(new)
                publications.append(dict(seconds=time.monotonic()-cycle_at,revision=new['revision'],before=old,after=end,complete=finished,writerPaused=writer_paused,unpublishedGap=gap))
            while True:
                views=observe();now=time.monotonic();elapsed=now-cycle_at;states=[v['playback'] for v in views]
                assert elapsed<900,('cycle deadline',cycle,elapsed,states)
                if pause_at is None and not pause_done and elapsed>=pause_point:
                    publish(prefix,writer_paused=True);pause_at=time.monotonic();event('writer-paused',cycle=cycle,duration=pause_duration)
                if pause_at is not None:
                    buffered=[b or s['state']=='buffering' for b,s in zip(buffered,states)]
                    if now-pause_at>=pause_duration:
                        target=min(598,2*(int(max(s['positionSeconds'] for s in states)+14)//2))
                        resume_at=time.monotonic();publish(cuts[target]);resume_targets=[(resume_at,s['positionSeconds']) for s in states]
                        pause_done=True;event('writer-resumed',cycle=cycle,actualPauseSeconds=time.monotonic()-pause_at,buffered=buffered);pause_at=None
                        if pause_duration>=30:assert all(buffered),buffered
                elif not complete:
                    target=2*(int(max(s['positionSeconds'] for s in states)+14)//2)
                    if target>=600:publish(len(raw),finished=True)
                    elif cuts[target]>prefix:
                        gap=not gap_done and elapsed>45
                        publish(cuts[target],gap=gap);gap_done|=gap
                if resume_targets:
                    for i,state in enumerate(states):
                        if resume_times[i] is None and state['state']=='playing' and state['positionSeconds']>resume_targets[i][1]+.15:
                            resume_times[i]=now-resume_targets[i][0];assert resume_times[i]<BUDGET['resumeSeconds'],resume_times
                        if resume_times[i] is None:assert now-resume_targets[i][0]<BUDGET['resumeSeconds'],(i,state)
                    if all(v is not None for v in resume_times):resume_targets=None
                if cycle==0 and not manual_done and elapsed>=(6 if smoke else 150):
                    jobs[0].control('pause');manual=wait(lambda:(lambda s:s if s['state']=='paused' else None)(jobs[0].playback_status()),'manual pause',2)
                    manual_at=time.monotonic();manual_done=True;event('manual-paused',cycle=cycle,state=manual)
                if manual_at is not None:
                    assert states[0]['state']=='paused' or now-manual_at<.3,states[0]
                    if now-manual_at>=(2 if smoke else 10):
                        resumed_at=time.monotonic();jobs[0].control('play')
                        resumed=wait(lambda:(lambda s:s if s['state']=='playing' and s['positionSeconds']>manual['positionSeconds']+.15 else None)(jobs[0].playback_status()),'manual resume',2)
                        manual_resume=time.monotonic()-resumed_at;assert manual_resume<2;manual_at=None
                        event('manual-resumed',cycle=cycle,latencySeconds=manual_resume,state=resumed)
                if smoke and elapsed>=20 and not complete:
                    publish(len(raw),finished=True)
                    wait(lambda:all(sdk.status()['outputComplete'] for sdk in jobs),'smoke worker complete',30)
                    for sdk in jobs:sdk.control('seek',seconds=598);sdk.control('play')
                if complete and all(s['state']=='finished' for s in states):break
                time.sleep(.2)
            assert pause_done and all(v is not None for v in resume_times)
            if not smoke:assert time.monotonic()-cycle_at>=599 and all(s['positionSeconds']>=599.5 for s in states)
            assert path.read_bytes()==raw and sha(path)==manifest['sha256']==digest.hexdigest()
            proofs=[];closed=[];worker_exits=[]
            for i,peer in enumerate(peers):
                wait(lambda:jobs[i].status()['outputComplete'],'source complete before proof',10)
                outputs=list(Path(str(peer.registry)+'.continuous-cache').glob('remux-*/output.fmp4'));assert len(outputs)==1
                actual=packets(outputs[0]);assert actual['video']==reference_packets['video']
                assert [p[:3] for p in actual['audio']]==[p[:3] for p in reference_packets['audio']]
                proofs.append(dict(peer=i,packetCounts={k:len(v) for k,v in actual.items()},canonicalSha256=hashlib.sha256(json.dumps(actual).encode()).hexdigest(),outputSha256=sha(outputs[0])))
                started=time.monotonic();proof=jobs[i].close(timeout=2);assert proof['released'] and proof['sourceFilesClosed'],proof
                def closed_readers():
                    try:values=peer.client.readers(model['sessionId'],1)
                    except ApiError as error:
                        assert error.code=='reader_registry_busy',error.reply
                        return None
                    return values if values and all(v['released'] and v['filesClosed'] and v['reason']=='files-closed' for v in values) else None
                receipts=wait(closed_readers,'soak files-closed receipts',2)
                forgotten=jobs[i].forget(timeout=2);assert forgotten['forgotten']
                close_seconds=time.monotonic()-started;assert close_seconds<2
                wait(identities[i]['watch'].exited,'completed worker exit',2);worker_exits.append(identities[i]['watch'].proof())
                assert not list(Path(str(peer.registry)+'.continuous-cache').glob('remux-*'))
                closed.append(dict(seconds=close_seconds,proof=proof,receipts=receipts,forgotten=forgotten))
            worker_rows=[r for r in cycle_samples if all(m is not None for m in r['workerMetrics'])]
            worker_bounds=None if smoke else resource_bounds(worker_rows,'workerMetrics')
            runs.append(dict(cycle=cycle,wallSeconds=time.monotonic()-cycle_at,startup=startup,writerPauseSeconds=pause_duration,
                buffered=buffered,resumeSeconds=resume_times,manualResumeSeconds=manual_resume,finalStates=states,
                packetProofs=proofs,closed=closed,workerExits=worker_exits,workerResourceBounds=worker_bounds,publications=publications))
            save('cycles',runs);event('cycle-complete',cycle=cycle)
        duration=time.monotonic()-began;save('samples',samples)
        main_bounds=None if smoke else resource_bounds(samples,'mainMetrics')
        if not smoke:assert len(runs)==3 and duration>=1800 and len(samples)>=350
        assert sha(reference)==manifest['sha256']
        return dict(result='PASS',count=len(runs),soakAcceptance=not smoke,elapsedSeconds=duration,smoke=smoke,runs=runs,
            samples=samples,events=events,budgets=BUDGET,mainResourceBounds=main_bounds,fixture=manifest,
            sameMainPids=pids,physicalAppendOnly=True,sourceSha256=manifest['sha256'],
            maxRequestSeconds=max(c['seconds'] for client in clients for c in client.calls),
            maxRequestBytes=max(c['bytes'] for client in clients for c in client.calls),physicalAvSyncMeasured=False,
            fullStressAcceptance=False,jobPacketAndCacheLimitsChanged=False)
    except BaseException:
        save('failure',dict(error=traceback.format_exc(),elapsedSeconds=time.monotonic()-began));raise
    finally:
        primary=__import__('sys').exc_info()[0] is not None;errors=[]
        for sdk in reversed(active):
            if not sdk.forgotten:
                try:sdk.close(timeout=5);sdk.forget(timeout=5)
                except Exception:errors.append(traceback.format_exc())
        for watch in watches:watch.close()
        save('calls',[c.calls for c in clients]);save('samples',samples)
        if errors:
            save('cleanup-errors',errors)
            if not primary:raise AssertionError('A/V soak cleanup failed')
