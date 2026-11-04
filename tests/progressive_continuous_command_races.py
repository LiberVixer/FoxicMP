"""Concurrent public IPC requests against isolated continuous playback attempts.

Two fresh connections start at a barrier; recorded client intervals describe the
observed scheduling, not deterministic coverage of internal I/O boundaries.
"""
import argparse
import concurrent.futures
import copy
import json
from pathlib import Path
import subprocess
import threading
import time
import traceback

from progressive_continuous_api_test import run
from progressive_player_crash_recovery import ApiError, sha, wait
from progressive_stream_remux_worker_test import fixture, probe
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_continuous import ContinuousRemux


def exercise(peers, root, report):
    raw_path = fixture(root, 'reference'); raw = raw_path.read_bytes()
    packets = probe(raw_path)['packets']
    cuts = {s: int(next(p['pos'] for p in packets if float(p['pts_time']) >= s))+13 for s in (12, 20)}
    ordinary_path = root/'replacement.mp4'
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
        'testsrc2=size=160x90:rate=25:duration=24', '-c:v', 'libx264',
        '-threads', '2', '-bf', '0', '-g', '25', '-movflags',
        '+frag_keyframe+empty_moov+default_base_moof', str(ordinary_path)], check=True, capture_output=True)
    ordinary_bytes = ordinary_path.read_bytes(); ordinary_sha = sha(ordinary_path)
    rows, owned, regular_sessions = [], [], []
    peer = peers[0]; player_pid = peer.process.pid

    def request(action, **fields):
        return peer.client.request(dict(version=1, action=action, **fields), timeout=2)

    def model_for(name, prefix, complete=False):
        directory = root/name; directory.mkdir(); path = directory/'input.part'
        model = snapshot(path, raw, complete=complete, prefix=prefix)
        model['timeline']['duration'] = '240000000'; path.write_bytes(raw[:prefix])
        return directory, path, model

    def create(client, directory, model):
        sdk = ContinuousRemux(client, str(directory)); owned.append(sdk)
        sdk.prepare(model, 'clip')
        return sdk

    def ready(sdk):
        def poll():
            s = sdk.status(); assert not s['errorCode'], s
            if s['playbackReady']: return s
            sdk.heartbeat()
        return wait(poll, 'race source ready')

    def playing(sdk):
        def poll():
            s = sdk.playback_status(); assert s['state'] not in ('closed', 'error', 'finished'), s
            return s if s['state'] == 'playing' and s['positionSeconds'] > .2 and s['decoderReaders'] else None
        return wait(poll, 'race actual playback')

    def finish(sdk):
        proof = sdk.close(timeout=2); assert proof['released'] and proof['sourceFilesClosed'], proof
        forgotten = sdk.forget(timeout=2); assert forgotten['forgotten'], forgotten
        return dict(proof=proof, forgotten=forgotten)

    def stop_regular(model):
        try: reply = request('cancel', sessionId=model['sessionId'])
        except ApiError as error:
            # Failed admission may never have installed this session.
            assert error.code == 'unknown_session', error.reply
            reply = error.reply
        receipts = wait(lambda: (lambda rs: dict(readers=rs) if all(r['released'] and r['filesClosed'] for r in rs) else None)(peer.client.readers(model['sessionId'], 1)), 'ordinary replacement closes', 2)['readers']
        for receipt in receipts: assert peer.client.forget_reader(receipt['readerId'])['forgotten']
        regular_sessions.remove(model)
        return dict(cancel=reply, receipts=receipts)

    def race_requests(left, right, offset):
        barrier = threading.Barrier(3); began = time.monotonic()
        def send(label, message, delay):
            barrier.wait(timeout=5)
            if delay: time.sleep(delay)
            started = time.monotonic()-began
            try: reply = peer.client.request(message, timeout=2)
            except ApiError as error: reply = error.reply
            return dict(label=label, message=message, startedSeconds=started,
                finishedSeconds=time.monotonic()-began, reply=reply)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(send, 'old-attempt', left, max(0, offset)),
                pool.submit(send, 'stop-or-replacement', right, max(0, -offset))]
            barrier.wait(timeout=5)
            result = [future.result(timeout=5) for future in futures]
        assert all(row['finishedSeconds']-row['startedSeconds'] < 2 for row in result)
        return result

    try:
        directory, other_path, other_model = model_for('independent', len(raw), True)
        other = create(peers[1].client, directory, other_model); ready(other)
        wait(lambda: other.status()['outputComplete'], 'independent complete output')
        other.play(title='Independent command-race control'); independent_initial = playing(other); other.control('pause')
        for mode in ('publish-stop', 'publish-replace', 'release-replace', 'cancel-replace'):
            for iteration, offset in enumerate((0, -.003, .003, 0)):
                name = '%s-%d' % (mode, iteration)
                (report/'current.json').write_text(json.dumps(dict(profile=name))+'\n')
                directory, path, model = model_for(name, cuts[12]); sdk = create(peer.client, directory, model); ready(sdk)
                sdk.play(title='Race '+name); actual = playing(sdk)
                if iteration % 2:
                    sdk.control('pause')
                    wait(lambda: sdk.playback_status()['state'] == 'paused', 'paused command race')
                outputs = sdk.playback_status()['readerIds']; assert outputs
                before = sha(path)
                with path.open('ab') as file: assert file.write(raw[cuts[12]:cuts[20]]) == cuts[20]-cuts[12]
                assert path.read_bytes() == raw[:cuts[20]]; published_sha = sha(path)
                updated = copy.deepcopy(model); updated['revision'] = '2'
                updated['resources'][0]['ranges'] = [dict(offset='0', length=str(cuts[20]))]
                updated = protect_snapshot(updated, directory, previous=model)
                if mode.startswith('publish'):
                    left = dict(version=1, action='continuousRemuxUpdate', jobId=sdk.job_id, session=updated)
                elif mode.startswith('release'):
                    left = dict(version=1, action='readerRelease', readerId=outputs[0])
                else:
                    left = dict(version=1, action='continuousRemuxCancel', jobId=sdk.job_id, **sdk.identity)
                ordinary = None
                if mode == 'publish-stop':
                    right = dict(version=1, action='continuousPlaybackControl', playbackId=sdk.playback_id, command='stop')
                else:
                    ordinary = snapshot(ordinary_path, ordinary_bytes, complete=True)
                    ordinary['timeline']['duration'] = '240000000'
                    ordinary['tracks'][0].update(containerHint='fmp4', codecHint='h264', initializationReady=True)
                    right = dict(version=1, action='start', session=ordinary, sessionDirectory=str(root), replace=True)
                    regular_sessions.append(ordinary)
                other.control('seek', seconds=0); other.control('play'); independent_before = playing(other)
                requests = race_requests(left, right, offset)
                (report/('requests-'+name+'.json')).write_text(json.dumps(requests, indent=2)+'\n')
                old_reply, replacement_reply = [r['reply'] for r in requests]
                if not old_reply.get('ok'):
                    assert mode.startswith('publish') and old_reply['code'] == 'continuous-closed', old_reply
                retried = None
                if not replacement_reply.get('ok'):
                    # A close before asynchronous source commit may cancel the
                    # pending intent. It must never commit later on its own.
                    assert ordinary is not None and replacement_reply['code'] in ('preparation_cancelled', 'reader_registry_busy'), replacement_reply
                    retries = []; until = time.monotonic()+2
                    while True:
                        try:
                            retry = request('start', session=ordinary, sessionDirectory=str(root), replace=True)
                        except ApiError as error:
                            retries.append(error.reply)
                            (report/('retry-'+name+'.json')).write_text(json.dumps(retries, indent=2)+'\n')
                            assert error.code in ('preparation_cancelled', 'reader_registry_busy') and time.monotonic()<until, error.reply
                            time.sleep(.02)
                        else:
                            retried = dict(reply=retry, previousErrors=retries)
                            break
                old_terminal = wait(lambda: (lambda s: s if s['state'] == 'closed' and s['decoderReaders'] == 0 else None)(sdk.playback_status()), 'race fences old playback', 2)
                released = wait(lambda: (lambda s: s if s['released'] and s['sourceFilesClosed'] else None)(sdk.status()), 'race releases old source/cache', 2)
                old_receipts = peer.client.readers(model['sessionId'], 1)
                assert old_receipts and all(r['released'] and r['filesClosed'] for r in old_receipts), old_receipts
                cache = Path(str(peer.registry)+'.continuous-cache'); assert not list(cache.glob('remux-*'))
                continued = ordinary_initial = regular_final = None
                if ordinary is not None:
                    def regular():
                        s = request('heartbeat', sessionId=ordinary['sessionId'])
                        return s if s.get('active') and s['state'] == 'playing' and s['positionSeconds'] > .2 else None
                    ordinary_initial = wait(regular, 'replacement actual playback')
                    # All old commands arrive again after the new view commits.
                    request('continuousPlaybackControl', playbackId=sdk.playback_id, command='stop')
                    request('continuousRemuxCancel', jobId=sdk.job_id, **sdk.identity)
                    for identifier in outputs: request('readerRelease', readerId=identifier)
                    try: request('continuousRemuxUpdate', jobId=sdk.job_id, session=updated)
                    except ApiError as error: assert error.code == 'continuous-closed', error.reply
                    else: raise AssertionError('Closed attempt accepted late publication')
                    def advances():
                        s = regular()
                        if s:
                            assert s['readerId'] == ordinary_initial['readerId'], s
                            return s if s['positionSeconds'] > ordinary_initial['positionSeconds']+.15 else None
                    continued = wait(advances, 'late old events leave replacement running', 2)
                    regular_final = stop_regular(ordinary)
                def independent_advances():
                    s = other.playback_status()
                    assert s['state'] == 'playing' and s['playbackId'] == independent_initial['playbackId'] and s['readerIds'] == independent_initial['readerIds'], s
                    return s if s['positionSeconds'] > independent_before['positionSeconds']+.1 else None
                independent_after = wait(independent_advances, 'independent viewing survives race', 2); other.control('pause')
                final = finish(sdk)
                assert sha(path) == published_sha and sha(ordinary_path) == ordinary_sha
                assert peer.process.poll() is None and peer.process.pid == player_pid
                rows.append(dict(profile=name, result='PASS', iteration=iteration, requestedOffsetSeconds=offset,
                    actualPlayback=actual, requests=requests, explicitReplacementRetry=retried,
                    oldTerminal=old_terminal, released=released, oldReceipts=old_receipts,
                    replacementInitial=ordinary_initial, replacementContinued=continued, replacementFinal=regular_final,
                    independentBefore=independent_before, independentAfter=independent_after, final=final,
                    initialPartialSha256=before, appendedPartialSha256=published_sha, originalsUnchangedByPlayer=True,
                    lateEventsIsolated=True, samePlayerProcess=True))
                (report/'progress.json').write_text(json.dumps(dict(completed=len(rows), rows=rows), indent=2)+'\n')
                print(json.dumps(dict(profile=name, result='PASS')), flush=True)
        other_final = finish(other); assert sha(other_path) == sha(raw_path)
        return dict(result='PASS', count=len(rows), rows=rows, otherFinal=other_final,
            realConcurrentIpcConnections=True, independentEndpointPreserved=True,
            deterministicInternalIoSchedule=False, stressAcceptance=False)
    finally:
        errors = []
        for model in list(regular_sessions):
            try: stop_regular(model)
            except Exception: errors.append(traceback.format_exc())
        for sdk in reversed(owned):
            if not sdk.forgotten:
                try: finish(sdk)
                except Exception: errors.append(traceback.format_exc())
        if errors:
            (report/'cleanup-errors.txt').write_text('\n'.join(errors))
            raise AssertionError('Command-race cleanup failed')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--edition', choices=('qt', 'classic'), required=True)
    p.add_argument('--player', action='append', required=True)
    p.add_argument('--reader-runner', required=True)
    p.add_argument('--report-directory', required=True)
    args = p.parse_args(); report = Path(args.report_directory).resolve(); report.mkdir(parents=True, exist_ok=True)
    try:
        result = run(args, lambda peers, root: exercise(peers, root, report), __file__)
        print(json.dumps(dict(result=result['result'], count=result['count'])))
    except Exception:
        (report/'error.txt').write_text(traceback.format_exc(), encoding='utf-8')
        raise
