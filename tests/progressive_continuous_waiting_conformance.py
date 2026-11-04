"""Public continuous SDK: real decoder starvation, writer pauses and request replay.

These are wall-clock 10/30/60 second waits, not shortened simulated timeouts.
Run in private Qt profiles / guarded Classic copies. No downloader is modified.
"""
import argparse
import copy
import json
from pathlib import Path
import time
import traceback

from progressive_continuous_api_test import run
from progressive_player_crash_recovery import ApiError, sha, wait
from progressive_stream_remux_worker_test import fixture, probe
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_continuous import ContinuousRemux
from progressive_process_metrics import sample


class MeasuredClient:
    def __init__(self, client):
        self.client = client
        self.calls = []

    def request(self, message, timeout=5):
        began = time.monotonic()
        try:
            return self.client.request(message, timeout=timeout)
        finally:
            self.calls.append(dict(action=message['action'], seconds=time.monotonic()-began))


def exercise(peers, root, report):
    owned = []
    try:
        return exercise_owned(peers, root, report, owned)
    except Exception:
        (report/'scenario-error.txt').write_text(traceback.format_exc(), encoding='utf-8')
        raise
    finally:
        # A failed assertion must also release the jobs before the driver kills
        # its idle players. Keep the original failure separate from cleanup.
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
        if errors:
            (report/'job-cleanup-errors.txt').write_text('\n'.join(errors), encoding='utf-8')
            raise AssertionError('Test job cleanup failed; see job-cleanup-errors.txt')


def exercise_owned(peers, root, report, owned):
    source = fixture(root, 'reference')
    raw = source.read_bytes()
    packets = probe(source)['packets']
    # Include only part of the frame at 12 s / 20 s. Its bytes must not become a
    # decoded frame or an EOF merely because the physical file has a full tail.
    cuts = {second: int(next(p['pos'] for p in packets if float(p['pts_time']) >= second))+13
            for second in (12, 20)}
    transport = MeasuredClient(peers[0].client)
    rows = []
    events = []

    def event(kind, **fields):
        events.append(dict(event=kind, **fields))
        (report/'events.json').write_text(json.dumps(events, indent=2)+'\n', encoding='utf-8')

    def make(name, prefix):
        directory = root/name
        directory.mkdir()
        path = directory/'input.part'
        model = snapshot(path, raw, complete=False, prefix=prefix)
        model['timeline']['duration'] = '240000000'
        path.write_bytes(raw)
        sdk = ContinuousRemux(transport, str(directory))
        owned.append(sdk)
        sdk.prepare(model, 'clip')
        return directory, path, model, sdk

    def publish(sdk, model, directory, prefix, complete=False):
        next_model = copy.deepcopy(model)
        next_model['revision'] = str(int(model['revision'])+1)
        phase = 'complete' if complete else 'downloading'
        next_model['writer']['state'] = next_model['resources'][0]['state'] = next_model['tracks'][0]['state'] = phase
        next_model['resources'][0]['ranges'] = [dict(offset='0', length=str(prefix))]
        next_model = protect_snapshot(next_model, directory, previous=model)
        sdk.publish(next_model)
        return next_model

    def preparation(sdk):
        value = sdk.status()
        assert not value['errorCode'], value
        return value

    def playback(sdk):
        sdk.heartbeat()
        value = sdk.playback_status()
        assert value['active'] and value['state'] not in ('error', 'closed', 'finished'), value
        if value['state'] != 'opening':
            assert value['decoderReaders'] > 0, value
        return value

    def ready(sdk):
        return wait(lambda: (lambda s: s if s['playbackReady'] else None)(preparation(sdk)), 'wait-suite preparation')

    def playing(sdk, minimum=.2):
        return wait(lambda: (lambda s: s if s['state']=='playing' and s['positionSeconds']>minimum else None)(playback(sdk)), 'wait-suite actual playback')

    def tail(sdk, maximum):
        position = -1
        changed = time.monotonic()

        def stalled():
            nonlocal position, changed
            value = playback(sdk)
            assert not value['outputComplete'] and value['positionSeconds'] < maximum, value
            if abs(value['positionSeconds']-position) > .02:
                position = value['positionSeconds']
                changed = time.monotonic()
            state = preparation(sdk)
            assert int(state['maximumReadEnd']) <= cuts[12], state
            return value if position > 1 and time.monotonic()-changed >= .75 and state['producerCode']=='waiting' else None

        return wait(stalled, 'actual decoder at partial unpublished frame', 15)

    def finish(sdk, path, original):
        began = time.monotonic()
        proof = sdk.close(timeout=2)
        assert proof['released'] and proof['sourceFilesClosed'], proof
        forgotten = sdk.forget(timeout=2)
        assert forgotten['forgotten'] and forgotten['released'], forgotten
        assert sha(path)==original
        assert not list(Path(str(peers[0].registry)+'.continuous-cache').glob('remux-*'))
        return dict(closeSeconds=time.monotonic()-began, proof=proof, forgotten=forgotten, originalUnchanged=True)

    # Keep an unrelated endpoint alive during every long stall and cancellation.
    other_dir = root/'independent'
    other_dir.mkdir()
    other_path = other_dir/'input.part'
    other_model = snapshot(other_path, raw, complete=False, prefix=4)
    other_model['timeline']['duration'] = '240000000'
    other = ContinuousRemux(peers[1].client, str(other_dir))
    owned.append(other)
    other.prepare(other_model, 'clip')

    for seconds in (10, 30, 60):
        name = 'writer-pause-%ds'%seconds
        event('begin', profile=name)
        directory, path, model, sdk = make(name, cuts[12])
        original = sha(path)
        ready(sdk)
        sdk.play(title=name)
        initial = playing(sdk)
        blocked = tail(sdk, 12.1)
        job = preparation(sdk)
        identities = dict(playbackId=sdk.playback_id, readerIds=blocked['readerIds'], workerId=job['workerId'])
        started = time.monotonic()
        before = sample(peers[0].process.pid)
        positions = []
        states = set()
        while time.monotonic()-started < seconds:
            state = playback(sdk)
            producer = preparation(sdk)
            assert not producer['outputComplete'] and producer['producerCode']=='waiting', producer
            assert producer['workerId']==identities['workerId'] and int(producer['maximumReadEnd'])<=cuts[12], producer
            assert state['readerIds']==identities['readerIds'] and state['playbackId']==identities['playbackId'], state
            assert state['state']=='buffering', state
            if peers[0].edition=='qt':
                assert state['reasonCode']=='waiting-range', state
            positions.append(state['positionSeconds'])
            states.add(state['state'])
            assert abs(state['positionSeconds']-blocked['positionSeconds']) < .15, state
            other.heartbeat()
            assert not other.status()['released']
            time.sleep(min(.25, max(0, seconds-(time.monotonic()-started))))
        elapsed = time.monotonic()-started
        after = sample(peers[0].process.pid)
        event('wait-complete', profile=name, elapsedSeconds=elapsed, states=sorted(states))
        pause = None
        if seconds == 30:
            sdk.control('pause')
            pause = wait(lambda: (lambda s: s if s['state']=='paused' else None)(sdk.playback_status()), 'manual pause at waiting tail')
            if peers[0].edition=='qt':
                assert pause['reasonCode']=='paused', pause
        published = time.monotonic()
        model = publish(sdk, model, directory, cuts[20])
        if pause:
            deadline = time.monotonic()+1
            while time.monotonic()<deadline:
                state = playback(sdk)
                assert state['state']=='paused' and abs(state['positionSeconds']-pause['positionSeconds'])<.15, state
                time.sleep(.05)
            published = time.monotonic()
            sdk.control('play')
        resumed = playing(sdk, blocked['positionSeconds']+.2)
        if peers[0].edition=='qt':
            assert resumed['reasonCode']=='', resumed
        resume_seconds = time.monotonic()-published
        assert resume_seconds < 2, (resume_seconds, resumed)
        assert sdk.playback_id==identities['playbackId'] and resumed['readerIds']==identities['readerIds'], resumed
        assert preparation(sdk)['workerId']==identities['workerId']
        model = publish(sdk, model, directory, len(raw), True)
        wait(lambda: preparation(sdk)['outputComplete'], 'final remux before reverse seek')
        sdk.control('pause')
        seeked = []
        for target in (18, 1, 22):
            sdk.control('seek', seconds=target)
            value = wait(lambda: (lambda s: s if s['state']=='paused' and abs(s['positionSeconds']-target)<.2 else None)(sdk.playback_status()), 'forward/reverse complete-source seek')
            seeked.append(value)
        sdk.control('play')
        ended = wait(lambda: (lambda s: s if s['state']=='finished' else None)(sdk.playback_status()), 'writer-confirmed EOF after wait', 10)
        assert ended['outputComplete'] and not ended['active'], ended
        rows.append(dict(profile=name, result='PASS', requiredWaitSeconds=seconds, actualWaitSeconds=elapsed,
            initialPublishedSeconds=12, resumedPublishedSeconds=20, partialPacketBytes=13, physicalUnpublishedTailIgnored=True, initial=initial, stalled=blocked,
            sameIdentities=identities, heldPositionMin=min(positions), heldPositionMax=max(positions),
            waitStates=sorted(states), processBeforeWait=before, processAfterWait=after,
            manualPause=pause, resumed=resumed, resumeSeconds=resume_seconds, seeked=seeked, confirmedEof=ended,
            **finish(sdk, path, original)))
        (report/'progress.json').write_text(json.dumps(dict(completed=len(rows), rows=rows), indent=2)+'\n', encoding='utf-8')

    # Retrying transport messages must not re-commit a cancelled or stopped GUI
    # attempt. Use the actual original wire identities, not a new SDK object.
    for terminal in ('stop', 'cancel'):
        name = 'waiting-'+terminal+'-request-replay'
        directory, path, model, sdk = make(name, cuts[12])
        original = sha(path)
        ready(sdk)
        sdk.play(title=name)
        playing(sdk)
        blocked = tail(sdk, 12.1)
        fields = dict(jobId=sdk.job_id, requestId=sdk.play_request_id, **sdk.identity, title=name, replace=False, activate=False)
        if terminal=='stop':
            sdk.control('stop')
        else:
            sdk.close(timeout=2)
        closed = wait(lambda: (lambda s: s if s['state']=='closed' and s['decoderReaders']==0 else None)(sdk.playback_status()), 'terminal wait closes output')
        if peers[0].edition=='qt':
            assert closed['reasonCode']=='playback-closed', closed
        for _ in range(3):
            replay = transport.request(dict(version=1, action='continuousRemuxPlay', **fields))
            assert replay['state']=='closed' and replay['playbackId']==sdk.playback_id and replay['decoderReaders']==0, replay
            try:
                transport.request(dict(version=1, action='continuousPlaybackControl', playbackId=sdk.playback_id, command='play'))
            except ApiError as error:
                assert error.code=='continuous-playback-closed', error.reply
            else:
                raise AssertionError('Terminal preview restarted by stale control')
        assert not other.status()['released']
        rows.append(dict(profile=name, result='PASS', stalled=blocked, closed=closed, replay=3, staleControlRejected=True,
                         **finish(sdk, path, original)))
    proof = other.close(timeout=2)
    assert proof['released'] and proof['sourceFilesClosed'], proof
    assert other.forget(timeout=2)['forgotten']
    assert max(c['seconds'] for c in transport.calls) < 2, transport.calls
    return dict(result='PASS', count=len(rows), rows=rows, publicSdk=True, ordinaryPlayerIpc=True,
                writerPauseSeconds=[10,30,60], sameWorkerAndDecoderReaders=True,
                maxRequestSeconds=max(c['seconds'] for c in transport.calls), independentEndpointPreserved=True)


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
        result = run(args, lambda peers, root: exercise(peers, root, report), __file__)
        print(json.dumps(dict(result=result['result'], count=result['count'])))
    except Exception:
        (report/'error.txt').write_text(traceback.format_exc(), encoding='utf-8')
        raise
