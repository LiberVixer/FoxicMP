"""Real 30-second writer heartbeat deadlines through public Qt/Classic IPC.

Status reads do not impersonate a writer. Only explicit heartbeat/publication
renews a live attempt; completed input no longer needs writer liveness.
"""
import argparse
import copy
import ctypes
import json
import os
from pathlib import Path
import select
import time
import traceback

from progressive_continuous_api_test import run
from progressive_player_crash_recovery import ApiError, sha, wait
from progressive_continuous_waiting_conformance import MeasuredClient
from progressive_stream_remux_worker_test import fixture, probe
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_continuous import ContinuousRemux


class WorkerExit:
    """Hold OS process identity without terminating a player or its worker."""
    def __init__(self, pid):
        self.pid = pid
        self.fd = self.handle = None
        if os.name == 'nt':
            from ctypes import wintypes as w
            self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            self.kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
            self.kernel.OpenProcess.restype = w.HANDLE
            self.kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
            self.kernel.WaitForSingleObject.restype = w.DWORD
            self.kernel.GetExitCodeProcess.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
            self.kernel.GetExitCodeProcess.restype = w.BOOL
            self.kernel.CloseHandle.argtypes = [w.HANDLE]
            self.handle = self.kernel.OpenProcess(0x101000, False, pid)
            if not self.handle:
                raise ctypes.WinError(ctypes.get_last_error())
        else:
            self.fd = os.pidfd_open(pid)
        assert not self.exited(), 'Worker must be alive when its identity is captured'

    def exited(self):
        if self.handle is not None:
            value = self.kernel.WaitForSingleObject(self.handle, 0)
            assert value in (0, 258), value
            return value == 0
        poll = select.poll()
        poll.register(self.fd, select.POLLIN)
        return bool(poll.poll(0))

    def proof(self):
        assert self.exited()
        result = dict(pid=self.pid, exited=True, method='Windows-held-process-handle' if self.handle else 'Linux-pidfd')
        if self.handle:
            value = ctypes.c_ulong()
            assert self.kernel.GetExitCodeProcess(self.handle, ctypes.byref(value))
            result['workerExitCode'] = value.value
        return result

    def close(self):
        if self.handle is not None:
            self.kernel.CloseHandle(self.handle)
            self.handle = None
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None


def exercise(peers, root, report, profiles=('preparation', 'playing', 'paused', 'renewals'), completed=True):
    owned, watches = [], []
    try:
        return exercise_owned(peers, root, report, owned, watches, profiles, completed)
    except Exception:
        (report/'scenario-error.txt').write_text(traceback.format_exc(), encoding='utf-8')
        raise
    finally:
        errors = []
        for sdk in reversed(owned):
            if sdk.forgotten:
                continue
            try:
                proof = sdk.close(timeout=5)
                assert proof['released'] and proof['sourceFilesClosed'], proof
                sdk.forget(timeout=5)
            except Exception:
                errors.append(traceback.format_exc())
        for watcher in watches:
            watcher.close()
        if errors:
            (report/'job-cleanup-errors.txt').write_text('\n'.join(errors), encoding='utf-8')
            raise AssertionError('Test cleanup failed; see job-cleanup-errors.txt')


def exercise_owned(peers, root, report, owned, watches, profiles, completed):
    raw_path = fixture(root, 'reference')
    raw = raw_path.read_bytes()
    packets = probe(raw_path)['packets']
    cuts = {s: int(next(p['pos'] for p in packets if float(p['pts_time']) >= s))+13 for s in (12, 20)}
    transport = MeasuredClient(peers[0].client)
    rows, events = [], []
    timeout = 30

    def event(kind, **fields):
        events.append(dict(event=kind, **fields))
        (report/'events.json').write_text(json.dumps(events, indent=2)+'\n', encoding='utf-8')

    def model_for(name, prefix, complete=False):
        directory = root/name
        directory.mkdir()
        path = directory/'input.part'
        model = snapshot(path, raw, complete=complete, prefix=prefix)
        model['timeline']['duration'] = '240000000'
        path.write_bytes(raw)
        return directory, path, model

    def create(client, directory, model):
        sdk = ContinuousRemux(client, str(directory))
        assert sdk.capability['writerHeartbeatMilliseconds'] == timeout*1000, sdk.capability
        owned.append(sdk)
        sdk.prepare(model, 'clip')
        return sdk

    other_directory, other_path, other_model = model_for('independent', 4)
    other = create(peers[1].client, other_directory, other_model)

    def keep_other():
        other.heartbeat()
        status = other.status()
        assert not status['errorCode'] and not status['released'] and not status['outputComplete'], status

    def until(check, label, seconds=20):
        def poll():
            keep_other()
            return check()
        return wait(poll, label, seconds)

    def ready(sdk):
        def check():
            value = sdk.status()
            assert not value['errorCode'], value
            return value if value['playbackReady'] else None
        return until(check, 'heartbeat-suite preparation ready')

    def playing(sdk):
        def check():
            value = sdk.playback_status()
            assert value['state'] not in ('error', 'closed', 'finished'), value
            return value if value['state']=='playing' and value['positionSeconds']>.2 and value['decoderReaders'] else None
        return until(check, 'heartbeat-suite actual playback')

    def finish(sdk, path, original):
        began = time.monotonic()
        proof = sdk.close(timeout=2)
        assert proof['released'] and proof['sourceFilesClosed'], proof
        elapsed = time.monotonic()-began
        forgotten = sdk.forget(timeout=2)
        assert forgotten['forgotten'] and forgotten['released'], forgotten
        assert sha(path)==original
        return dict(proof=proof, forgotten=forgotten, closeSeconds=elapsed, originalUnchanged=True)

    for mode in profiles:
        event('begin', profile=mode)
        directory, path, model = model_for(mode, 4 if mode=='preparation' else cuts[12])
        original = sha(path)
        sdk = create(transport, directory, model)
        if mode=='preparation':
            until(lambda: sdk.status()['producerCode']=='waiting', 'incomplete initialization waits')
            actual = None
        else:
            ready(sdk)
            sdk.play(title='Heartbeat '+mode)
            actual = playing(sdk)
            if mode=='paused':
                sdk.control('pause')
                until(lambda: sdk.playback_status()['state']=='paused', 'manual pause before lost heartbeat')
        watcher = WorkerExit(sdk.status()['workerId'])
        watches.append(watcher)
        began = refreshed = time.monotonic()
        sdk.heartbeat()
        expected_sequence = int(sdk.status()['acceptedHeartbeat'])
        updates, milestones, samples = [], set(), []
        terminal = None
        while time.monotonic()-refreshed < timeout+2:
            keep_other()
            elapsed = time.monotonic()-began
            if mode=='renewals' and 'heartbeat' not in milestones and elapsed>=20:
                refreshed = time.monotonic()
                sdk.heartbeat()
                expected_sequence += 1
                milestones.add('heartbeat')
                updates.append(dict(action='heartbeat', seconds=refreshed-began))
            if mode=='renewals' and 'publication' not in milestones and elapsed>=40:
                previous = model
                model = copy.deepcopy(model)
                model['revision'] = '2'
                model['resources'][0]['ranges'] = [dict(offset='0', length=str(cuts[20]))]
                model = protect_snapshot(model, directory, previous=previous)
                refreshed = time.monotonic()
                sdk.publish(model)
                milestones.add('publication')
                updates.append(dict(action='publication', seconds=refreshed-began))
            if mode=='preparation' and 'foreign-heartbeat' not in milestones and elapsed>=20:
                fields = dict(jobId=sdk.job_id, **sdk.identity)
                fields['generation'] = '2'
                try:
                    transport.request(dict(version=1, action='continuousRemuxHeartbeat', **fields))
                except ApiError as error:
                    assert error.code=='generation_changed', error.reply
                else:
                    raise AssertionError('Foreign generation renewed writer liveness')
                milestones.add('foreign-heartbeat')
            producer = sdk.status()
            value = sdk.playback_status() if sdk.play_request_id else None
            assert not producer['outputComplete'] and not producer['sourceComplete'], producer
            assert int(producer['acceptedHeartbeat'])==expected_sequence, producer
            if value:
                assert value['state']!='finished', value
            if producer['errorCode']:
                terminal = producer
                break
            assert producer['workerId']==watcher.pid and not watcher.exited(), producer
            assert not producer['released'], producer
            if mode=='preparation':
                assert not producer['playbackReady'] and producer['maximumReadEnd']=='4', producer
            if mode=='paused':
                assert value['state']=='paused', value
            bucket = int(elapsed//5)
            if not samples or bucket>samples[-1]['bucket']:
                samples.append(dict(bucket=bucket, seconds=elapsed, producerState=producer['state'], producerCode=producer['producerCode'], acceptedHeartbeat=producer['acceptedHeartbeat'], appliedHeartbeat=producer['appliedHeartbeat'], acceptedRevision=producer['acceptedRevision'], playbackState=value['state'] if value else None, positionSeconds=value['positionSeconds'] if value else None))
            time.sleep(.2)
        expired_after = time.monotonic()-refreshed
        assert terminal and terminal['errorCode']=='writer-heartbeat-timeout', terminal
        assert timeout-.1 <= expired_after < timeout+2, expired_after
        release_began = time.monotonic()
        release_trace = []
        def release_view():
            current = sdk.status()
            release_trace.append(dict(secondsAfterError=time.monotonic()-release_began, producer=current))
            return current if current['released'] and current['sourceFilesClosed'] else None
        try:
            released = until(release_view, 'heartbeat timeout closes source/cache', 2)
        except Exception:
            # Persist the unmodified status-only release path before additional
            # diagnostic requests or cleanup can affect the observed state.
            (report/('release-trace-'+mode+'.json')).write_text(json.dumps(release_trace,indent=2)+'\n',encoding='utf-8')
            diagnostic = dict(profile=mode, producer=sdk.status(),
                playback=sdk.playback_status() if sdk.play_request_id else None,
                readers=peers[0].client.readers(model['sessionId'], 1))
            (report/'release-failure.json').write_text(json.dumps(diagnostic,indent=2)+'\n',encoding='utf-8')
            raise
        (report/('release-trace-'+mode+'.json')).write_text(json.dumps(release_trace,indent=2)+'\n',encoding='utf-8')
        release_seconds = time.monotonic()-release_began
        if sdk.play_request_id:
            stopped = until(lambda: (lambda s: s if s['state']=='error' and s['decoderReaders']==0 else None)(sdk.playback_status()), 'heartbeat timeout closes decoder', 2)
            assert stopped['error']=='writer-heartbeat-timeout', stopped
            if peers[0].edition=='qt':
                assert stopped['reasonCode']=='writer-heartbeat-timeout', stopped
        else:
            stopped = dict(playbackNeverRequested=True)
        until(watcher.exited, 'actual worker process exits', 2)
        receipts = peers[0].client.readers(model['sessionId'], 1)
        assert receipts and all(r['released'] and r['filesClosed'] and r['reason']=='files-closed' for r in receipts), receipts
        for action in (sdk.heartbeat, lambda: sdk.publish(model)):
            try:
                action()
            except ApiError as error:
                assert error.code=='continuous-closed', error.reply
            else:
                raise AssertionError('Expired attempt accepted a late writer event')
        final = finish(sdk, path, original)
        reopened = None
        if mode=='playing':
            replacement = create(transport, directory, model)
            ready(replacement)
            assert not sdk.playback_status()['active'], 'Preparation implicitly restarted the old attempt'
            replacement.play(title='Explicit preview after heartbeat loss')
            running = playing(replacement)
            assert running['playbackId']!=actual['playbackId'] and not set(running['readerIds']) & set(actual['readerIds']), running
            reopened = dict(actual=running, explicitNewAttempt=True, sameOriginalBytes=True, **finish(replacement,path,original))
        rows.append(dict(profile=mode, result='PASS', deadlineSeconds=timeout, expiredAfterLastRefreshSeconds=expired_after, initialPlayback=actual, refreshes=updates, observations=samples, terminal=terminal, released=released, playbackTerminal=stopped, workerExit=watcher.proof(), readerReceipts=receipts, lateEventsRejected=True, reopened=reopened, automaticReleaseSeconds=release_seconds, **final))
        event('expired', profile=mode, secondsSinceLastRefresh=expired_after)
        (report/'progress.json').write_text(json.dumps(dict(completed=len(rows),rows=rows),indent=2)+'\n',encoding='utf-8')

    if completed:
        directory, path, model = model_for('completed-paused', len(raw), True)
        original = sha(path)
        sdk = create(transport, directory, model)
        ready(sdk)
        until(lambda: sdk.status()['outputComplete'], 'complete output before heartbeat-free playback')
        sdk.play(title='Completed source without writer heartbeat')
        playing(sdk)
        sdk.control('pause')
        until(lambda: sdk.playback_status()['state']=='paused', 'completed-source manual pause')
        started = time.monotonic()
        initial = sdk.playback_status()
        event('begin', profile='completed-paused-without-heartbeat')
        while time.monotonic()-started < timeout+2:
            keep_other()
            producer, value = sdk.status(), sdk.playback_status()
            assert producer['outputComplete'] and not producer['errorCode'] and not producer['released'], producer
            assert value['state']=='paused' and abs(value['positionSeconds']-initial['positionSeconds'])<.15, value
            assert producer['acceptedHeartbeat']=='0', producer
            time.sleep(.2)
        elapsed = time.monotonic()-started
        sdk.control('seek', seconds=22)
        sdk.control('play')
        ended = until(lambda: (lambda s: s if s['state']=='finished' else None)(sdk.playback_status()), 'completed EOF without writer heartbeat', 10)
        assert ended['outputComplete'] and not ended['active'], ended
        rows.append(dict(profile='completed-paused-without-heartbeat', result='PASS', noHeartbeatSeconds=elapsed, acceptedHeartbeat='0', before=initial, after=value, confirmedEof=ended, **finish(sdk,path,original)))
    assert other.close(timeout=2)['released'] and other.forget(timeout=2)['forgotten']
    assert not list(Path(str(peers[0].registry)+'.continuous-cache').glob('remux-*'))
    assert max(c['seconds'] for c in transport.calls)<2, transport.calls
    return dict(result='PASS', count=len(rows), rows=rows, publicSdk=True, actualHeartbeatMilliseconds=30000, deadlinesNotShortened=True, maxRequestSeconds=max(c['seconds'] for c in transport.calls), independentEndpointPreserved=True, statusDoesNotRenewWriter=True, noImplicitPlaybackRestart=True)


if __name__=='__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--edition', choices=('qt','classic'), required=True)
    parser.add_argument('--player', action='append', required=True)
    parser.add_argument('--reader-runner', required=True)
    parser.add_argument('--report-directory', required=True)
    args = parser.parse_args()
    report = Path(args.report_directory).resolve()
    report.mkdir(parents=True, exist_ok=True)
    try:
        result = run(args, lambda peers, root: exercise(peers,root,report), __file__)
        print(json.dumps(dict(result=result['result'],count=result['count'])))
    except Exception:
        (report/'error.txt').write_text(traceback.format_exc(),encoding='utf-8')
        raise
