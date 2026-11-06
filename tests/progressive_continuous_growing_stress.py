"""One sustained physical append-only MP4 writer, two actual player endpoints.

128 s H.264 video-only input, real 60 s writer pause, explicit heartbeat,
manual pause, reader replacement, complete/packet proof/seek/EOF/release.
No downloader changes and no claim of multi-hour or A/V acceptance.
"""
import argparse
import copy
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import time
import traceback

from progressive_continuous_api_test import run
from progressive_continuous import ContinuousRemux, MODE, AAC_MODE
from progressive_continuous_test_media import source_fixture, source_snapshot, audible_decoder_ready
from progressive_continuous_heartbeat_conformance import WorkerExit
from progressive_continuous_waiting_conformance import MeasuredClient
from progressive_integrity import protect_snapshot
from progressive_stream_remux_worker_test import probe
from progressive_process_metrics import sample
from progressive_player_crash_recovery import sha, ApiError

SOURCE_SHA256=sha(__file__)
BUDGET=dict(handles=8,threads=2,memoryBytes=256*1024**2,averageCores=1.0,requestSeconds=2,releaseSeconds=2,resumeSeconds=2)


def packets(value):
    tracks={s['index']:Fraction(s['time_base']) for s in value['streams'] if s['codec_type']=='video'}
    return [(p['data_hash'],*[str(int(p[key])*tracks[p['stream_index']]) for key in ('pts','dts','duration')])
            for p in value['packets'] if p['stream_index'] in tracks]


def exercise(peers,root,report,mode=MODE):
    owned=[];watchers=[];clients=[MeasuredClient(p.client) for p in peers]
    jobs=[None,None];expected=[None,None];samples=[];events=[];publications=[]
    began=time.monotonic();last_sample=-1;last_heartbeat=began;complete=False;phase='setup'
    def save(name,value):
        (report/(name+'.json')).write_text(json.dumps(value,indent=2)+'\n')
    def event(name,**fields):
        events.append(dict(event=name,seconds=time.monotonic()-began,**fields));save('events',events)
    save('owned-identities',dict(pids=[p.process.pid for p in peers],players=[str(p.player) for p in peers],playerSha256=[sha(p.player) for p in peers],testSourceSha256=SOURCE_SHA256,budgets=BUDGET))
    reference=source_fixture(root,'reference',mode,seconds=128);reference_sha=sha(reference);raw=reference.read_bytes()
    original=probe(reference)
    if mode==AAC_MODE:
        from progressive_stream_aac_window_test import packets as av_packets
        reference_packets=av_packets(reference)
    else:reference_packets=packets(original)
    cuts={s:int(next(p['pos'] for p in original['packets'] if p['codec_type']=='video' and float(p['pts_time'])>=s))+13 for s in range(12,128,4)}
    directory=root/'shared-writer';directory.mkdir();path=directory/'input.part'
    prefix=cuts[12];model=source_snapshot(path,raw,complete=False,prefix=prefix,mode=mode);model['timeline']['duration']='1283000000' if mode==AAC_MODE else '1280000000'
    assert path.stat().st_size==prefix and len(raw)>prefix
    def tick():
        nonlocal last_heartbeat,last_sample
        now=time.monotonic()
        if not complete and now-last_heartbeat>=5:
            for job in jobs:
                if job:job.heartbeat()
            last_heartbeat=now
        views=[]
        for i,job in enumerate(jobs):
            if job is None:views.append(None);continue
            producer=job.status();actual=job.playback_status()
            assert not producer['errorCode'],producer
            assert actual['state'] not in ('error','closed'),actual
            if not complete:
                assert not producer['sourceComplete'] and not producer['outputComplete'],producer
                assert actual['state']!='finished',actual
                assert not expected[i]['watcher'].exited(),'Worker exited before writer complete'
            assert producer['workerId']==expected[i]['workerId'],producer
            assert actual['playbackId']==expected[i]['playbackId'],actual
            if not complete:assert actual['readerIds']==expected[i]['readerIds'],actual
            views.append(dict(producer=producer,playback=actual))
        if now-last_sample>=1:
            samples.append(dict(seconds=now-began,phase=phase,physicalBytes=path.stat().st_size,publishedBytes=prefix,
                completePublished=complete,views=views,parents=[sample(p.process.pid) for p in peers]))
            last_sample=now;save('progress',dict(phase=phase,seconds=now-began,samples=len(samples),publishedBytes=prefix,completePublished=complete))
        return views
    def wait(check,label,seconds=20):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            result=check(tick())
            if result:return result
            time.sleep(.05)
        raise AssertionError(label)
    def hold(seconds,check=None):
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            views=tick()
            if check:check(views)
            time.sleep(min(.2,max(0,until-time.monotonic())))
    def attach(i):
        sdk=ContinuousRemux(clients[i],str(directory),mode=mode);owned.append(sdk);sdk.prepare(model,'clip')
        end=time.monotonic()+20
        while True:
            value=sdk.status();assert not value['errorCode'],value
            if value['playbackReady']:break
            assert time.monotonic()<end,'Preparation not ready';sdk.heartbeat();time.sleep(.05)
        sdk.play(title='Sustained growing writer')
        while True:
            value=sdk.playback_status();assert value['state'] not in ('error','closed','finished'),value
            if value['state']=='playing' and value['positionSeconds']>.2 and value['decoderReaders'] and audible_decoder_ready(value,peers[i].edition,mode):break
            assert time.monotonic()<end,'Actual playback not ready';sdk.heartbeat();time.sleep(.05)
        job=sdk.status();watcher=WorkerExit(job['workerId']);watchers.append(watcher)
        expected[i]=dict(workerId=job['workerId'],playbackId=value['playbackId'],readerIds=value['readerIds'],watcher=watcher)
        jobs[i]=sdk
        return value
    def publish(next_prefix,finished=False,gap=0):
        nonlocal prefix,model,complete
        previous=model;model=copy.deepcopy(model);old_prefix=prefix
        assert path.stat().st_size==prefix
        with path.open('ab') as out:assert out.write(raw[prefix:next_prefix])==next_prefix-prefix
        assert path.stat().st_size==next_prefix
        if gap:
            def bounded(v):
                assert all(int(p['producer']['maximumReadEnd'])<=old_prefix for p in v if p),v
            hold(gap,bounded)
        model['revision']=str(int(model['revision'])+1);model['resources'][0]['ranges']=[dict(offset='0',length=str(next_prefix))]
        state='complete' if finished else 'downloading'
        model['writer']['state']=model['resources'][0]['state']=state
        for track in model['tracks']:track['state']=state
        model=protect_snapshot(model,directory,previous=previous)
        prefix=next_prefix;complete=finished;started=time.monotonic()
        for job in jobs:
            if job:job.publish(model)
        publications.append(dict(revision=model['revision'],before=old_prefix,after=prefix,complete=finished,seconds=started-began,physicalSha256=sha(path)))
        assert sha(path)==hashlib.sha256(raw[:prefix]).hexdigest()
    def close(i):
        sdk=jobs[i];jobs[i]=None;started=time.monotonic();proof=sdk.close(timeout=2)
        assert proof['released'] and proof['sourceFilesClosed'],proof
        def receipt_view(_):
            try:return clients[i].request(dict(version=1,action='readers',sessionId=model['sessionId'],generation='1'))['readers']
            except ApiError as error:
                if error.code!='reader_registry_busy':raise
                return None
        receipts=wait(receipt_view,'Closed reader receipts',BUDGET['releaseSeconds'])
        assert receipts and all(r['released'] and r['filesClosed'] and r['reason']=='files-closed' for r in receipts),receipts
        forgotten=sdk.forget(timeout=2);assert forgotten['forgotten'],forgotten
        assert not list(Path(str(peers[i].registry)+'.continuous-cache').glob('remux-*'))
        elapsed=time.monotonic()-started;assert elapsed<BUDGET['releaseSeconds'],elapsed
        return dict(seconds=elapsed,proof=proof,receipts=receipts,forgotten=forgotten)
    try:
        initial=[attach(i) for i in range(2)];event('two-viewers-started',initial=initial)
        phase='growing-before-pause'
        for seconds in (16,20,24):hold(4);publish(cuts[seconds],gap=2 if seconds==16 else 0)
        phase='writer-pause-60s';pause_started=time.monotonic();manual=None;stalled=None
        while time.monotonic()-pause_started<60:
            views=tick();other=views[1]['playback']
            if other['state']=='buffering' and other['positionSeconds']>20:stalled=other
            if manual is None and time.monotonic()-pause_started>=30:
                jobs[0].control('pause');manual=wait(lambda v:v[0]['playback'] if v[0]['playback']['state']=='paused' else None,'Manual pause while writer stalled');views=tick()
            if manual:
                assert views[0]['playback']['state']=='paused' and abs(views[0]['playback']['positionSeconds']-manual['positionSeconds'])<.15,views[0]
            time.sleep(.2)
        paused_seconds=time.monotonic()-pause_started
        assert stalled and manual and paused_seconds>=60
        event('writer-pause-complete',elapsedSeconds=paused_seconds,stalled=stalled,manual=manual)
        phase='resume-after-pause';published=time.monotonic();publish(cuts[36])
        resumed=wait(lambda v:v[1]['playback'] if v[1]['playback']['state']=='playing' and v[1]['playback']['positionSeconds']>stalled['positionSeconds']+.2 else None,'Second viewer resumes',2)
        resume_seconds=time.monotonic()-published;assert resume_seconds<2
        def manual_stays(v):
            assert v[0]['playback']['state']=='paused' and abs(v[0]['playback']['positionSeconds']-manual['positionSeconds'])<.15,v[0]
        hold(2,manual_stays);jobs[0].control('play')
        wait(lambda v:v[0]['playback']['state']=='playing','Explicit resume after manual pause',2)
        phase='first-viewer-replaced';before=tick()[1]['playback'];old=expected[0];closed_first=close(0)
        hold(2)
        advanced=wait(lambda v:v[1]['playback'] if v[1]['playback']['positionSeconds']>before['positionSeconds']+.2 else None,'Independent viewer continues',2)
        replacement=attach(0);assert replacement['playbackId']!=old['playbackId'] and not set(replacement['readerIds']) & set(old['readerIds'])
        event('first-viewer-reopened',closed=closed_first,independent=advanced,replacement=replacement)
        phase='growing-after-pause'
        for seconds in range(40,128,4):hold(2);publish(cuts[seconds])
        for job in jobs:job.control('pause')
        wait(lambda v:all(p['playback']['state']=='paused' for p in v),'Pause before writer complete',2)
        phase='writer-complete';publish(len(raw),True)
        wait(lambda v:all(p['producer']['outputComplete'] and p['producer']['sourceFilesClosed'] for p in v),'Confirmed producer completion')
        assert sha(path)==reference_sha
        packet_proofs=[]
        for i,peer in enumerate(peers):
            caches=list(Path(str(peer.registry)+'.continuous-cache').glob('remux-*/output.fmp4'));assert len(caches)==1,caches
            copied=root/('output-%d.fmp4'%i);shutil.copyfile(caches[0],copied);actual=probe(copied);actual_packets=av_packets(copied) if mode==AAC_MODE else packets(actual)
            save('output-%d-packets'%i,actual_packets)
            if mode==AAC_MODE:
                assert actual_packets['video']==reference_packets['video'],'Video payload/PTS/DTS/duration mismatch'
                assert [p[:3] for p in actual_packets['audio']]==[p[:3] for p in reference_packets['audio']],'Audio payload/PTS/DTS mismatch'
                producer=jobs[i].status()
                assert producer['audioWindowReady'] and producer['audioPresentationSamples']==str(48000*128-137),producer
            else:assert actual_packets==reference_packets,'Packet payload/PTS/DTS/duration mismatch'
            packet_proofs.append(dict(peer=i,count={k:len(v) for k,v in actual_packets.items()} if mode==AAC_MODE else len(actual_packets),canonicalSha256=hashlib.sha256(json.dumps(actual_packets).encode()).hexdigest(),copySha256=sha(copied)))
        phase='seek-eof-release';ended=[];closed=[]
        for job in jobs:job.control('seek',seconds=126);job.control('play')
        for i in range(2):ended.append(wait(lambda v,i=i:v[i]['playback'] if v[i]['playback']['state']=='finished' else None,'Confirmed final EOF',10))
        for i in range(2):closed.append(close(i))
        elapsed=time.monotonic()-began;assert elapsed>=120,elapsed
        assert sha(reference)==reference_sha and sha(path)==reference_sha
        assert max(call['seconds'] for c in clients for call in c.calls)<2
        steady=[r for r in samples if 30<=r['seconds']<40];early=samples[:10];late=samples[-10:]
        assert len(steady)>=8 and len(late)==10
        resources=[]
        for i in range(2):
            high=lambda rows,key:max(r['parents'][i][key] for r in rows)
            assert high(late,'handles')<=high(steady,'handles')+BUDGET['handles']
            assert high(late,'threads')<=high(steady,'threads')+BUDGET['threads']
            for key in ('rssBytes','privateBytes'):assert high(late,key)<=high(early,key)+BUDGET['memoryBytes']
            cores=(late[-1]['parents'][i]['cpuSeconds']-late[0]['parents'][i]['cpuSeconds'])/(late[-1]['seconds']-late[0]['seconds'])
            assert 0<=cores<=BUDGET['averageCores']
            resources.append(dict(peer=i,steadyMaxHandles=high(steady,'handles'),lateMaxHandles=high(late,'handles'),steadyMaxThreads=high(steady,'threads'),lateMaxThreads=high(late,'threads'),lastTenAverageCores=cores))
        event('finished',elapsedSeconds=elapsed)
        return dict(result='PASS',count=1,publicMode=mode,sourceSeconds=128,videoOnly=mode==MODE,bFrames=mode==AAC_MODE,wallSeconds=elapsed,writerPauseSeconds=paused_seconds,
            physicalAppendOnly=True,publicationCount=len(publications),twoIndependentEndpoints=True,sameSecondWorkerThroughout=True,
            partialFrameBytes=13,resumeSeconds=resume_seconds,packetProofs=packet_proofs,initial=initial,manualPause=manual,
            reopened=replacement,firstClosed=closed_first,ended=ended,closed=closed,originalSha256=reference_sha,
            maxRequestSeconds=max(call['seconds'] for c in clients for call in c.calls),budgets=BUDGET,resourceBounds=resources,multiHourAcceptance=False)
    except BaseException:
        save('failure',dict(phase=phase,error=traceback.format_exc()));raise
    finally:
        for i,sdk in enumerate(owned):
            if not sdk.forgotten:
                try:sdk.close(timeout=5);sdk.forget(timeout=5)
                except BaseException:save('cleanup-error-%d'%i,dict(error=traceback.format_exc()))
        for watcher in watchers:watcher.close()
        save('samples',samples);save('publications',publications);save('calls',[c.calls for c in clients]);save('events',events)


def main(args):
    report=Path(args.report_directory).resolve();report.mkdir(parents=True,exist_ok=False)
    save_inputs=dict(testSourceSha256=SOURCE_SHA256,budgets=BUDGET,originalPlayerSha256=[sha(p) for p in args.player])
    (report/'inputs.json').write_text(json.dumps(save_inputs,indent=2)+'\n')
    players=list(args.player)
    try:
        with tempfile.TemporaryDirectory(prefix='foxicmp-growing-stage-') as directory:
            stage=Path(directory)
            if args.edition=='classic':
                assert len(players)==2
                args.player=[]
                for i,player in enumerate(players):
                    original=Path(player).resolve();destination=stage/str(i);destination.mkdir()
                    runtime=Path(args.runtime_directory).resolve() if args.runtime_directory else original.parent
                    copy=destination/original.name;shutil.copy2(original,copy)
                    for dll in runtime.glob('*.dll'):shutil.copy2(dll,destination/dll.name)
                    for name in ('foxicmp-demux-worker.exe','foxicmp-remux-stream-worker.exe'):
                        source=original.parent/name
                        if name=='foxicmp-remux-stream-worker.exe' and args.worker:source=Path(args.worker).resolve()
                        elif not source.exists():source=runtime/name
                        shutil.copy2(source,destination/name)
                    copy.with_suffix('.ini').write_text('[Settings]\nLanguage=en\nContextMenuPrompted=1\nContextMenuEnabled=0\nPauseMinimizedVideo=0\nCheckForUpdates=0\n[Audio]\nMute=1\n',encoding='utf-16')
                    args.player.append(str(copy))
            result=run(args,lambda peers,root:exercise(peers,root,report),__file__)
        result.update(stagedCopiesRemoved=not stage.exists(),originalPlayerSha256=save_inputs['originalPlayerSha256'])
        (report/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        assert result['testSourceSha256']==SOURCE_SHA256,'Test source changed while running'
        return result
    finally:
        args.player=players


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--edition',choices=('qt','classic'),required=True)
    parser.add_argument('--player',action='append',required=True);parser.add_argument('--reader-runner',required=True);parser.add_argument('--report-directory',required=True)
    parser.add_argument('--runtime-directory',default='');parser.add_argument('--worker',default='')
    main(parser.parse_args())
