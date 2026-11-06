"""Bounded continuous-remux lifecycle stress plus real paused heartbeat repeats.

Thresholds are inherited from the existing 100-cycle player lifecycle test.
Records existing RPCs without adding queries to the heartbeat release path.
"""
import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time
import traceback

from progressive_continuous_api_test import run
from progressive_continuous_heartbeat_conformance import exercise as heartbeat_exercise, WorkerExit
from progressive_player_crash_recovery import sha, wait, ApiError
from progressive_integrity import protect_snapshot
from progressive_continuous import ContinuousRemux, MODE, AAC_MODE
from progressive_continuous_test_media import source_fixture, source_snapshot, source_cuts, audible_decoder_ready
from progressive_process_metrics import sample

BUDGET = dict(steadyHandleGrowth=8, steadyThreadGrowth=2, initialThreadGrowth=8,
    rssPrivateGrowthBytes=256*1024**2, parentAverageCores=1.0,
    requestSeconds=2, readyToPlaySeconds=3, releaseSeconds=2)


def bounds(rows):
    early = rows[:10]; steady = rows[30:40]; late = rows[-10:]
    assert len(steady) == len(late) == 10
    result = []
    for peer in range(2):
        metric = lambda rows, key: max(row['metrics'][peer][key] for row in rows)
        assert metric(late, 'handles') <= metric(steady, 'handles')+BUDGET['steadyHandleGrowth']
        assert metric(late, 'threads') <= metric(steady, 'threads')+BUDGET['steadyThreadGrowth']
        assert metric(late, 'threads') <= metric(early, 'threads')+BUDGET['initialThreadGrowth']
        for key in ('rssBytes', 'privateBytes'):
            assert metric(late, key) <= metric(early, key)+BUDGET['rssPrivateGrowthBytes']
        cpu = (late[-1]['metrics'][peer]['cpuSeconds']-late[0]['metrics'][peer]['cpuSeconds'])/(late[-1]['wallSeconds']-late[0]['wallSeconds'])
        assert 0 <= cpu <= BUDGET['parentAverageCores']
        result.append(dict(peer=peer, steady={k:metric(steady,k) for k in ('handles','threads','rssBytes','privateBytes')},
            late={k:metric(late,k) for k in ('handles','threads','rssBytes','privateBytes')}, lastTenAverageCores=cpu))
    return result


def exercise(peers, root, report, cycles, repeats, mode=MODE):
    assert mode == MODE or (mode == AAC_MODE and repeats == 0), "AAC expiry uses a separate suite"
    began = time.monotonic(); rows = []; warmup = []; traces = []; heartbeat_rows = []
    phase = ['setup']; originals = []; owned = []; watches = []
    peer = peers[0]; pids = [p.process.pid for p in peers]; old_ids = set()
    for index, item in enumerate(peers):
        original = item.client.request; originals.append(original)
        def traced(message, timeout=5, *, original=original, index=index):
            started = time.monotonic(); cpu = time.process_time()
            entry = dict(peer=index, phase=phase[0], action=message['action'], startedSeconds=started-began,
                **{key:message[key] for key in ('jobId','playbackId','readerId','command') if key in message})
            try:
                value = original(message, timeout=timeout)
                entry['reply'] = {key:value[key] for key in ('ok','code','state','errorCode','error','producerCode','cleanupCode','released','sourceFilesClosed','acceptedHeartbeat') if key in value}
                return value
            except ApiError as error:
                entry['reply'] = dict(code=error.code); raise
            finally:
                entry.update(finishedSeconds=time.monotonic()-began, seconds=time.monotonic()-started, clientCpuSeconds=time.process_time()-cpu)
                traces.append(entry)
        item.client.request = traced
    def create(client, directory, model):
        sdk = ContinuousRemux(client, str(directory), mode=mode); owned.append(sdk)
        sdk.prepare(model, 'clip'); return sdk
    def ready(sdk, seconds=20):
        def poll():
            s = sdk.status(); assert not s['errorCode'], s
            if s['playbackReady']: return s
            sdk.heartbeat()
        return wait(poll, 'stress source ready', seconds)
    def playing(sdk):
        def poll():
            s = sdk.playback_status(); assert s['state'] not in ('closed','error','finished'), s
            return s if s['state']=='playing' and s['positionSeconds']>.2 and s['decoderReaders'] and audible_decoder_ready(s, peer.edition, mode) else None
        return wait(poll, 'stress actual playback')
    def finish(sdk):
        proof = sdk.close(timeout=2); assert proof['released'] and proof['sourceFilesClosed'], proof
        receipts = sdk.client.readers(sdk.identity['sessionId'], 1)
        assert receipts and all(r['released'] and r['filesClosed'] and r['reason']=='files-closed' for r in receipts), receipts
        forgotten = sdk.forget(timeout=2); assert forgotten['forgotten']
        return dict(proof=proof, forgotten=forgotten, receipts=receipts)
    try:
        reference = source_fixture(root, 'reference', mode); raw = reference.read_bytes()
        cut = source_cuts(reference, mode)[12]
        control_seconds = 600 if mode == AAC_MODE else 900
        other_file = source_fixture(root, 'independent-long', mode, seconds=control_seconds); other_raw = other_file.read_bytes(); other_sha = sha(other_file)
        other_model = source_snapshot(other_file, other_raw, complete=True, mode=mode)
        other_model['timeline']['duration'] = str(control_seconds*10000000+(3000000 if mode==AAC_MODE else 0))
        other = create(peers[1].client, root, other_model); ready(other, 60)
        wait(lambda: other.status()['outputComplete'], 'long independent remux complete', 60)
        other.play(title='Continuous lifecycle independent control'); independent = playing(other)
        for index in range(cycles+4):
            number = index-3; phase[0] = 'cycle-%03d'%number
            directory = root/('cycle-%03d'%index); directory.mkdir(); path = directory/'input.part'
            model = source_snapshot(path, raw, complete=False, prefix=cut, mode=mode)
            initial_sha = sha(path); started = time.monotonic(); sdk = create(peer.client, directory, model); ready(sdk)
            prepared = time.monotonic(); sdk.play(title='Continuous lifecycle %d'%number); actual = playing(sdk)
            play_seconds = time.monotonic()-prepared; assert play_seconds < BUDGET['readyToPlaySeconds']
            initial = sdk.status(); watch = WorkerExit(initial['workerId']); watches.append(watch)
            worker_metrics = sample(initial['workerId'])
            assert not old_ids.intersection(initial['readerIds']); old_ids.update(initial['readerIds'])
            if index%2:
                sdk.control('pause'); wait(lambda: sdk.playback_status()['state']=='paused','lifecycle manual pause')
            operation = ('stop','cancel','reader-release','complete')[index%4]
            expected_sha = initial_sha
            if operation=='complete':
                with path.open('ab') as stream:
                    assert stream.write(raw[cut:])==len(raw)-cut
                    stream.flush(); os.fsync(stream.fileno())
                updated=copy.deepcopy(model); updated['revision']='2'
                updated['writer']['state']=updated['resources'][0]['state']='complete'
                for track in updated['tracks']:track['state']='complete'
                updated['resources'][0]['ranges']=[dict(offset='0',length=str(len(raw)))]
                updated=protect_snapshot(updated,directory,previous=model); sdk.publish(updated)
                wait(lambda: sdk.status()['outputComplete'],'stress complete output')
                sdk.control('seek',seconds=2); sdk.control('play')
                wait(lambda: sdk.playback_status()['positionSeconds']>2.2,'complete seek/resume')
                expected_sha=sha(reference)
            release_began=time.monotonic()
            if operation in ('stop','complete'): sdk.control('stop')
            elif operation=='cancel': peer.client.request(dict(version=1,action='continuousRemuxCancel',jobId=sdk.job_id,**sdk.identity),timeout=2)
            else:
                identifiers=sdk.playback_status()['readerIds']; assert identifiers
                peer.client.request(dict(version=1,action='readerRelease',readerId=identifiers[0]),timeout=2)
            def released():
                s=sdk.status(); return s if s['released'] and s['sourceFilesClosed'] else None
            release=wait(released,'cycle actual source/cache release',2); release_seconds=time.monotonic()-release_began
            assert release_seconds < BUDGET['releaseSeconds'] and watch.exited()
            stopped=wait(lambda: (lambda s:s if s['decoderReaders']==0 and not s['active'] else None)(sdk.playback_status()),'cycle decoder closed',2)
            final=finish(sdk); exit_proof=watch.proof(); watch.close()
            assert not list(Path(str(peer.registry)+'.continuous-cache').glob('remux-*'))
            assert sha(path)==expected_sha
            shutil.rmtree(directory)
            control=other.playback_status()
            assert control['state']=='playing' and control['playbackId']==independent['playbackId'] and control['readerIds']==independent['readerIds'],control
            assert control['positionSeconds']>independent['positionSeconds']
            assert all(p.process.poll() is None and p.process.pid==pid for p,pid in zip(peers,pids))
            row=dict(cycle=number,result='PASS',mode=operation,prepareSeconds=prepared-started,
                readyToPlaySeconds=play_seconds,releaseSeconds=release_seconds,
                actual=actual,terminal=release,decoderTerminal=stopped,final=final,
                workerMetrics=worker_metrics,workerExit=exit_proof,independentPositionSeconds=control['positionSeconds'],
                metrics=[sample(p.process.pid) for p in peers],wallSeconds=time.monotonic()-began,
                originalSha256=expected_sha,sourceRemovedAfterClose=True,samePlayerProcesses=True)
            (warmup if index<4 else rows).append(row)
            (report/'progress.json').write_text(json.dumps(dict(phase=phase[0],completedCycles=len(rows),completedHeartbeats=len(heartbeat_rows),last=row),indent=2)+'\n')
            if index>=4 and number%10==0: print(json.dumps(dict(cycles=number,result='PASS',handles=[m['handles'] for m in row['metrics']])),flush=True)
        lifecycle=dict(rows=rows,warmup=warmup)
        (report/'lifecycle.json').write_text(json.dumps(lifecycle,indent=2)+'\n')
        budgets=bounds(rows)
        independent_final=other.playback_status(); finish(other); assert sha(other_file)==other_sha
        (report/'lifecycle.json').write_text(json.dumps(dict(rows=rows,warmup=warmup,bounds=budgets),indent=2)+'\n')
        lifecycle_metrics=[sample(p.process.pid) for p in peers]
        (report/'heartbeat-baseline.json').write_text(json.dumps(lifecycle_metrics,indent=2)+'\n')
        for index in range(repeats):
            phase[0]='heartbeat-%02d'%(index+1); directory=root/phase[0]; directory.mkdir(); output=report/phase[0]; output.mkdir()
            first_sequence=len(traces)
            result=heartbeat_exercise(peers,directory,output,profiles=('paused',),completed=False)
            assert result['result']=='PASS' and result['count']==1
            row=result['rows'][0]; assert row['automaticReleaseSeconds']<BUDGET['releaseSeconds']
            events=[t for t in traces[first_sequence:] if t['peer']==0]
            first_error=next(t for t in events if t.get('reply',{}).get('errorCode')=='writer-heartbeat-timeout')
            release=next(t for t in events if t['startedSeconds']>=first_error['startedSeconds'] and t.get('reply',{}).get('released') and t.get('reply',{}).get('sourceFilesClosed'))
            observation=release['finishedSeconds']-first_error['finishedSeconds']; assert 0<=observation<2,observation
            metrics=[sample(p.process.pid) for p in peers]
            heartbeat_rows.append(dict(attempt=index+1,**row,firstErrorToReleasedSeconds=observation,
                traceStart=first_sequence,traceEnd=len(traces),metrics=metrics))
            (report/'heartbeat-metrics.json').write_text(json.dumps(dict(baseline=lifecycle_metrics,rows=heartbeat_rows),indent=2)+'\n')
            try:
                for peer_index,(current,baseline) in enumerate(zip(metrics,lifecycle_metrics)):
                    for key,growth in (('handles',BUDGET['steadyHandleGrowth']),('threads',BUDGET['steadyThreadGrowth']),
                                       ('rssBytes',BUDGET['rssPrivateGrowthBytes']),('privateBytes',BUDGET['rssPrivateGrowthBytes'])):
                        assert current[key]<=baseline[key]+growth, dict(peer=peer_index,metric=key,current=current[key],baseline=baseline[key],growth=growth)
            except AssertionError:
                # Observe eventual teardown only after preserving the failing measurement.
                # These samples cannot turn a failed budget into a pass.
                followup=[]; observed=time.monotonic()
                for _ in range(20):
                    time.sleep(.1)
                    followup.append(dict(seconds=time.monotonic()-observed,metrics=[sample(p.process.pid) for p in peers]))
                (report/'resource-failure-followup.json').write_text(json.dumps(dict(initial=metrics,baseline=lifecycle_metrics,samples=followup),indent=2)+'\n')
                raise
            (report/'progress.json').write_text(json.dumps(dict(phase=phase[0],completedCycles=len(rows),completedHeartbeats=len(heartbeat_rows),lastHeartbeat=heartbeat_rows[-1]),indent=2)+'\n')
            print(json.dumps(dict(heartbeat=index+1,result='PASS',releaseSeconds=observation)),flush=True)
        assert max(t['seconds'] for t in traces)<BUDGET['requestSeconds']
        return dict(result='PASS',count=cycles,publicMode=mode,controlSourceSeconds=control_seconds,cycles=cycles,warmupCycles=4,heartbeatRepeats=repeats,rows=rows,warmup=warmup,
            heartbeatRows=heartbeat_rows,budget=BUDGET,bounds=budgets,independentFinal=independent_final,
            maximumRequestSeconds=max(t['seconds'] for t in traces),wallSeconds=time.monotonic()-began,
            samePrivatePlayers=True,realHeartbeatMilliseconds=30000,clockNotMocked=True,
            singleLongGrowingDownload=False,fullStressAcceptance=False)
    finally:
        primary_failure=sys.exc_info()[0] is not None
        errors=[]
        for sdk in reversed(owned):
            if not sdk.forgotten:
                try: finish(sdk)
                except Exception: errors.append(traceback.format_exc())
        for watch in watches:watch.close()
        (report/'rpc-trace.json').write_text(json.dumps(traces,indent=2)+'\n')
        for peer,original in zip(peers,originals):peer.client.request=original
        if errors:
            (report/'cleanup-errors.txt').write_text('\n'.join(errors))
            if not primary_failure:raise AssertionError('Stress cleanup failed')


def staged(args, report):
    players=list(args.player)
    with tempfile.TemporaryDirectory(prefix='foxicmp-continuous-stress-') as temporary:
        stage=Path(temporary);copies=[];dlls={}
        for i,name in enumerate(players):
            original=Path(name).resolve();destination=stage/str(i);destination.mkdir()
            runtime=Path(args.runtime_directory).resolve() if args.runtime_directory else original.parent
            if os.name=='nt':
                for dll in runtime.glob('*.dll'):shutil.copy2(dll,destination/dll.name)
                if args.edition=='qt':
                    pthread=destination/'libwinpthread-1.dll'
                    assert sha(pthread)=='4118e2fc18e5edb2a2074cdf27ada9bdc42645d6659c43f7ae2ea7baa215a123'
                    dlls[pthread.name]=sha(pthread)
                    for folder in ('platforms','styles','imageformats','iconengines','tls','networkinformation'):
                        if (runtime/folder).is_dir():shutil.copytree(runtime/folder,destination/folder)
            copy=destination/original.name;shutil.copy2(original,copy)
            for base in ('foxicmp-demux-worker','foxicmp-remux-stream-worker'):
                filename=base+('.exe' if os.name=='nt' else '');source=original.parent/filename
                if base=='foxicmp-remux-stream-worker' and args.worker:source=Path(args.worker).resolve()
                elif not source.exists():source=runtime/filename
                shutil.copy2(source,destination/filename)
            if args.edition=='classic':copy.with_suffix('.ini').write_text('[Settings]\nLanguage=en\nContextMenuPrompted=1\nContextMenuEnabled=0\nPauseMinimizedVideo=0\nCheckForUpdates=0\n[Audio]\nMute=1\n',encoding='utf-16')
            copies.append(str(copy))
        args.player=copies
        result=run(args,lambda peers,root:exercise(peers,root,report,args.cycles,args.heartbeat_repeats),__file__)
    result.update(testPlayersRemoved=not stage.exists(),originalPlayerSha256=[sha(p) for p in players],runtimeDllSha256=dlls,
        heartbeatHelperSha256=sha(Path(__file__).with_name('progressive_continuous_heartbeat_conformance.py')))
    (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--player',action='append',required=True)
    p.add_argument('--reader-runner',required=True);p.add_argument('--report-directory',required=True);p.add_argument('--runtime-directory',default='')
    p.add_argument('--worker',default='');p.add_argument('--cycles',type=int,default=100);p.add_argument('--heartbeat-repeats',type=int,default=12);a=p.parse_args()
    assert 100<=a.cycles<=200 and 6<=a.heartbeat_repeats<=20
    report=Path(a.report_directory).resolve();report.mkdir(parents=True,exist_ok=True)
    try:
        r=staged(a,report);print(json.dumps(dict(result=r['result'],cycles=r['cycles'],heartbeatRepeats=r['heartbeatRepeats'])))
    except Exception:
        (report/'error.txt').write_text(traceback.format_exc());raise
