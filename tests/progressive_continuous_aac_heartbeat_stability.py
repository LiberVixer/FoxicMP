"""Classic AAC heartbeat stability after a fixed 120-second cleanup interval.

Instant thread overruns remain explicit failures of the original instant gate.
Handles, memory, CPU and IPC/release limits remain immediate. This additional
profile checks whether total thread growth persists after all jobs are closed.
"""
import json
import os
from pathlib import Path
import time
import traceback

from progressive_continuous import AAC_MODE
from progressive_continuous_heartbeat_conformance import exercise as heartbeat_exercise
from progressive_continuous_lifecycle_stress import BUDGET
from progressive_player_crash_recovery import ApiError, sha
from progressive_process_metrics import sample
from progressive_thread_observation import windows_thread_inventory


def exercise(peers, root, report, mode=AAC_MODE):
    assert mode == AAC_MODE
    assert os.name == "nt" and all(p.edition == "classic" for p in peers)
    began = time.monotonic()
    pids = [p.process.pid for p in peers]
    originals, traces, repeats = [], [], []
    phase = ['functional']
    instant_thread_failures = []

    def save(name, value):
        (report/(name+'.json')).write_text(json.dumps(value, indent=2)+'\n', encoding='utf-8')

    for index, peer in enumerate(peers):
        original = peer.client.request
        originals.append(original)
        def traced(message, timeout=5, *, original=original, index=index):
            started = time.monotonic()
            entry = dict(peer=index, phase=phase[0], action=message['action'], startedSeconds=started-began,
                         **{k:message[k] for k in ('jobId', 'playbackId', 'command') if k in message})
            try:
                value = original(message, timeout=timeout)
                entry['reply'] = {k:value[k] for k in ('ok', 'code', 'state', 'errorCode', 'error', 'producerCode',
                                 'cleanupCode', 'released', 'sourceFilesClosed', 'acceptedHeartbeat') if k in value}
                return value
            except ApiError as error:
                entry['reply'] = dict(code=error.code)
                raise
            finally:
                entry.update(finishedSeconds=time.monotonic()-began, seconds=time.monotonic()-started)
                traces.append(entry)
        peer.client.request = traced

    def same_players():
        assert all(p.process.poll() is None and p.process.pid == pid for p, pid in zip(peers, pids))

    def run_case(name, **options):
        directory, output = root/name, report/name
        directory.mkdir(); output.mkdir()
        result = heartbeat_exercise(peers, directory, output, media_mode=mode, **options)
        assert result['result'] == 'PASS'
        same_players()
        save(name+'/result', result)
        return result

    try:
        functional = run_case('functional')
        assert functional['count'] == 5
        baseline = [sample(pid) for pid in pids]
        baseline_time = time.monotonic()
        baseline_inventory = [windows_thread_inventory(pid) for pid in pids]
        save('baseline', dict(pids=pids, metrics=baseline, budgets=BUDGET, threadInventories=baseline_inventory))
        for index in range(12):
            name = phase[0] = 'paused-expiry-%02d' % (index+1)
            first_trace = len(traces)
            result = run_case(name, profiles=('paused',), completed=False)
            assert result['count'] == 1
            events = [t for t in traces[first_trace:] if t['peer'] == 0]
            error = next(t for t in events if t.get('reply', {}).get('errorCode') == 'writer-heartbeat-timeout')
            released = next(t for t in events if t['startedSeconds'] >= error['startedSeconds']
                            and t.get('reply', {}).get('released') and t.get('reply', {}).get('sourceFilesClosed'))
            observation = released['finishedSeconds']-error['finishedSeconds']
            assert 0 <= observation < BUDGET['releaseSeconds'], observation
            now = time.monotonic()
            metrics = [sample(pid) for pid in pids]
            row = dict(attempt=index+1, result=result, metrics=metrics, secondsSinceBaseline=now-baseline_time,
                       firstErrorToReleasedSeconds=observation, errorResponse=error, releasedResponse=released,
                       threadInventories=[windows_thread_inventory(pid) for pid in pids])
            repeats.append(row)
            # Save the actual failing sample before asserting or observing later cleanup.
            save('repeats', dict(baseline=baseline, rows=repeats))
            try:
                for peer_index, (current, initial) in enumerate(zip(metrics, baseline)):
                    for key, growth in (('handles', BUDGET['steadyHandleGrowth']), ('threads', BUDGET['steadyThreadGrowth']),
                                        ('rssBytes', BUDGET['rssPrivateGrowthBytes']), ('privateBytes', BUDGET['rssPrivateGrowthBytes'])):
                        violation = dict(attempt=index+1, peer=peer_index, metric=key, current=current[key], baseline=initial[key], growth=growth)
                        if key == 'threads':
                            if current[key] > initial[key]+growth:
                                instant_thread_failures.append(violation)
                                save('instant-thread-failures', instant_thread_failures)
                        else:
                            assert current[key] <= initial[key]+growth, violation
                    cores = (current['cpuSeconds']-initial['cpuSeconds'])/(now-baseline_time)
                    assert 0 <= cores <= BUDGET['parentAverageCores'], dict(peer=peer_index, averageCores=cores)
            except AssertionError:
                followup = []
                observed = time.monotonic()
                try:
                    for _ in range(20):
                        time.sleep(.1)
                        followup.append(dict(seconds=time.monotonic()-observed, metrics=[sample(pid) for pid in pids]))
                except Exception:
                    save('followup-error', dict(error=traceback.format_exc()))
                save('resource-failure-followup', dict(baseline=baseline, failed=metrics, samples=followup))
                raise
            save('progress', dict(phase=name, completedFunctionalCases=5, completedPausedExpiries=len(repeats), metrics=metrics))
            print(json.dumps(dict(phase=name, result='PASS', firstErrorToReleasedSeconds=observation, instantThreadOverruns=len(instant_thread_failures))), flush=True)
        phase[0] = 'post-release-idle'
        idle_began = time.monotonic()
        idle = []
        for index in range(25):
            if index:
                time.sleep(5)
            same_players()
            metrics = [sample(pid) for pid in pids]
            idle.append(dict(seconds=time.monotonic()-idle_began, metrics=metrics,
                             threadInventories=[windows_thread_inventory(pid) for pid in pids]))
            save('post-release-idle', idle)
            save('progress', dict(phase=phase[0], completedFunctionalCases=5, completedPausedExpiries=12,
                                  idleSeconds=idle[-1]['seconds'], metrics=metrics))
            for peer_index, (current, initial) in enumerate(zip(metrics, baseline)):
                for key, growth in (('handles', BUDGET['steadyHandleGrowth']),
                                    ('rssBytes', BUDGET['rssPrivateGrowthBytes']), ('privateBytes', BUDGET['rssPrivateGrowthBytes'])):
                    assert current[key] <= initial[key]+growth, dict(peer=peer_index, metric=key, current=current[key], baseline=initial[key], growth=growth)
                cores = (current['cpuSeconds']-initial['cpuSeconds'])/(time.monotonic()-baseline_time)
                assert 0 <= cores <= BUDGET['parentAverageCores'], dict(peer=peer_index, averageCores=cores)
        assert idle[-1]['seconds'] >= 120
        for peer_index, (current, initial) in enumerate(zip(idle[-1]['metrics'], baseline)):
            assert current['threads'] <= initial['threads']+BUDGET['steadyThreadGrowth'], dict(peer=peer_index, metric='threads', current=current['threads'], baseline=initial['threads'], growth=BUDGET['steadyThreadGrowth'])
        maximum = max(t['seconds'] for t in traces)
        assert maximum < BUDGET['requestSeconds'], maximum
        return dict(result='PASS', count=17, publicMode=mode, functional=functional, repeatedPausedExpiries=repeats,
                    resourceBaseline=baseline, baselineThreadInventories=baseline_inventory, budgets=BUDGET,
                    resourcePolicy='threads-after-120s-idle; other-limits-instant',
                    instantThreadGate='FAIL' if instant_thread_failures else 'PASS',
                    instantThreadFailures=instant_thread_failures, settledThreadGate='PASS', postReleaseIdle=idle, maxRequestSeconds=maximum, wallSeconds=time.monotonic()-began,
                    samePrivatePlayerPids=pids, actualHeartbeatMilliseconds=30000, deadlinesNotShortened=True,
                    helperSha256=sha(Path(__file__).with_name('progressive_continuous_heartbeat_conformance.py')),
                    budgetSourceSha256=sha(Path(__file__).with_name('progressive_continuous_lifecycle_stress.py')),
                    independentEndpointPreparing=True, independentEndpointPlaying=False,
                    physicalAvNotMeasured=True, fullStressAcceptance=False)
    except BaseException:
        save('failure', dict(phase=phase[0], error=traceback.format_exc()))
        raise
    finally:
        save('rpc-trace', traces)
        for peer, original in zip(peers, originals):
            peer.client.request = original
