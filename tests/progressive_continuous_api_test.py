"""Continuous preparation over private test Qt/Classic IPC endpoints.

This test covers preparation; SDK and actual playback have separate suites.
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
import uuid

from progressive_player_crash_recovery import Peer, sha, wait, ApiError
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_stream_remux_worker_test import fixture


def exercise(peers, root):
    rows = []
    source = fixture(root, 'reference')
    raw = source.read_bytes()
    active = [None, None]
    elapsed = []

    def command(i, action, **fields):
        began = time.monotonic()
        result = peers[i].client.request(dict(version=1, action=action, **fields))
        elapsed.append(time.monotonic()-began)
        return result

    def reject(i, action, code, **fields):
        try:
            command(i, action, **fields)
        except ApiError as error:
            assert error.code == code, error.reply
        else:
            raise AssertionError(('Unexpected command success', action))

    def status(i):
        return command(i, 'continuousRemuxStatus', jobId=active[i]['jobId'])

    def fenced(i, action, **changes):
        fields = {key: active[i][key] for key in ('jobId', 'sessionId', 'generation', 'writerInstanceId')}
        fields.update(changes)
        return command(i, action, **fields)

    def until(i, predicate):
        def poll():
            value = status(i)
            if predicate(value):
                return value
            if value['state'] not in ('error', 'cancelled', 'forgotten', 'forgetting', 'cancelling') and not value['outputComplete']:
                try:
                    fenced(i, 'continuousRemuxHeartbeat')
                except ApiError as error:
                    # A terminal transition can occur after this status reply.
                    # Re-read it next iteration; never retry/revive a closed job.
                    if error.code != 'continuous-closed':
                        raise
            return None
        return wait(poll, 'continuous API state', 20)

    def start(i, directory, model, request=None):
        fields = dict(requestId=request or uuid.uuid4().hex, sessionDirectory=str(directory), session=model, resourceId='clip')
        active[i] = command(i, 'continuousRemuxStart', **fields)
        return fields

    def forget(i):
        fenced(i, 'continuousRemuxForget')
        result = until(i, lambda s: s['forgotten'])
        assert result['released'] and result['sourceFilesClosed'] and not result['playbackReady'], result
        assert not list(Path(str(peers[i].registry)+'.continuous-cache').glob('remux-*'))
        for identifier in result['readerIds']:
            reject(i, 'readerStatus', 'unknown_reader', readerId=identifier)
        return result

    def model_for(name, prefix=4):
        directory = root/name
        directory.mkdir()
        path = directory/'input.part'
        model = snapshot(path, raw, complete=False, prefix=prefix)
        model['timeline']['duration'] = '240000000'
        # The existing physical tail must not grant access beyond published ranges.
        path.write_bytes(raw)
        return directory, path, model

    try:
        for i in range(2):
            reject(i, 'continuousRemuxStatus', 'unknown-continuous-job', requestId=uuid.uuid4().hex)
        directory, path, model = model_for('growing')
        original_sha = sha(path)
        fields = start(0, directory, model)
        job = active[0]['jobId']
        assert command(0, 'continuousRemuxStart', **fields)['jobId'] == job
        assert command(0, 'continuousRemuxStatus', requestId=fields['requestId'])['jobId'] == job
        conflict = dict(fields, sessionDirectory=str(directory/'other'))
        reject(0, 'continuousRemuxStart', 'continuous-conflict', **conflict)
        reject(1, 'continuousRemuxStatus', 'unknown-continuous-job', jobId=job)
        waiting = until(0, lambda s: s['producerCode'] == 'waiting')
        assert waiting['maximumReadEnd'] == '4' and not waiting['playbackReady'], waiting
        pid = waiting['workerId']
        for revision, prefix, complete in ((2, len(raw)//3, False), (3, len(raw)*2//3, False), (4, len(raw), False), (5, len(raw), True)):
            previous = model
            model = copy.deepcopy(model)
            model['revision'] = str(revision)
            model['resources'][0]['ranges'] = [dict(offset='0', length=str(prefix))]
            phase = 'complete' if complete else 'downloading'
            model['writer']['state'] = model['resources'][0]['state'] = model['tracks'][0]['state'] = phase
            model = protect_snapshot(model, directory, previous=previous)
            accepted = command(0, 'continuousRemuxUpdate', jobId=job, session=model)
            assert accepted['acceptedRevision'] == str(revision), accepted
            current = until(0, lambda s: s['appliedRevision'] == str(revision) and (s['outputComplete'] if complete else s['playbackReady']))
            assert current['workerId'] == pid and not current['errorCode'], current
            assert current['outputComplete'] == complete, current
            assert len(current['readerIds']) == 1, current  # Source lease only, no decoder commit.
        completed = until(0, lambda s: s['outputComplete'] and s['sourceFilesClosed'])
        assert completed['samples'] == 600 and completed['playbackReady'], completed
        # Preparation does not create decoder/output reader leases or start a GUI graph.
        receipts = command(0, 'readers', sessionId=model['sessionId'], generation='1')['readers']
        assert receipts and all(r['released'] and r['filesClosed'] for r in receipts), receipts
        assert sha(path) == original_sha
        final = forget(0)
        assert command(0, 'continuousRemuxStart', **fields)['forgotten']
        rows.append(dict(profile='growing-complete-replay-private-endpoint', result='PASS', workerId=pid, before=waiting, complete=completed, final=final, physicalTailIgnored=True, originalUnchanged=True, sourceReceiptsClosed=receipts))

        for failure in ('cancel', 'required-track', 'resource', 'late-resource'):
            directory, path, model = model_for(failure)
            if failure == 'late-resource':
                path.unlink()
            start(0, directory, model)
            initial = until(0, lambda s: s['producerCode'] == 'waiting')
            assert not initial['errorCode'], initial
            pid = initial['workerId']
            bad = dict(jobId=active[0]['jobId'], sessionId=model['sessionId'], generation='2', writerInstanceId=model['writer']['instanceId'])
            reject(0, 'continuousRemuxCancel', 'generation_changed', **bad)
            reject(0, 'continuousRemuxUpdate', 'stale_revision', jobId=active[0]['jobId'], session=model)
            reject(1, 'continuousRemuxUpdate', 'unknown-continuous-job', jobId=active[0]['jobId'], session=model)
            if failure == 'cancel':
                fenced(0, 'continuousRemuxCancel')
                final = until(0, lambda s: s['released'])
                assert final['state'] == 'cancelled' and not final['outputComplete'], final
                reject(0, 'continuousRemuxHeartbeat', 'continuous-closed', **{k:active[0][k] for k in ('jobId','sessionId','generation','writerInstanceId')})
            elif failure == 'late-resource':
                path.write_bytes(raw)
                previous = model
                model = copy.deepcopy(model)
                model['revision'] = '2'
                model['writer']['state'] = model['resources'][0]['state'] = model['tracks'][0]['state'] = 'complete'
                model['resources'][0]['ranges'] = [dict(offset='0', length=str(len(raw)))]
                model = protect_snapshot(model, directory, previous=previous)
                command(0, 'continuousRemuxUpdate', jobId=active[0]['jobId'], session=model)
                final = until(0, lambda s: s['outputComplete'] and s['sourceFilesClosed'])
                assert final['samples'] == 600 and final['workerId'] == pid, final
            else:
                model = copy.deepcopy(model)
                model['revision'] = '2'
                model['tracks' if failure == 'required-track' else 'resources'][0]['state'] = 'failed'
                command(0, 'continuousRemuxUpdate', jobId=active[0]['jobId'], session=model)
                final = until(0, lambda s: s['state'] == 'error' and s['released'])
                assert final['errorCode'] == ('writer-failed' if failure == 'required-track' else 'resource-failed'), final
            forgotten = forget(0)
            rows.append(dict(profile=failure, result='PASS', initial=initial, final=final, forgotten=forgotten))

        directory, path, model = model_for('rejected')
        model['tracks'][0]['kind'] = 'audio'
        reject(1, 'continuousRemuxStart', 'unsupported-stream-source', requestId=uuid.uuid4().hex, sessionDirectory=str(directory), session=model, resourceId='clip')
        assert not Path(str(peers[1].registry)+'.continuous-cache').exists()
        rows.append(dict(profile='unsupported-before-cache-or-worker', result='PASS'))
        assert max(elapsed) < 2, elapsed
        return dict(result='PASS', count=len(rows), rows=rows, maxRequestSeconds=max(elapsed), ordinaryPlayerIpc=True, playbackCommit=False, publicCapability=True)
    finally:
        for i, value in enumerate(active):
            if value is not None:
                try:
                    forget(i)
                except Exception:
                    pass


def run(args, exercise_case=None, source_file=None):
    report = Path(args.report_directory).resolve()
    report.mkdir(parents=True, exist_ok=True)
    registration = None
    if args.edition == 'classic':
        from windows.progressive_family_test import registration_snapshot
        registration = registration_snapshot()
        assert len(args.player) == 2
    else:
        assert len(args.player) == 1
    peers = []
    with tempfile.TemporaryDirectory(prefix='foxicmp-continuous-api-') as name:
        root = Path(name)
        try:
            for i in range(2):
                logs = root/('peer-%d' % i)
                logs.mkdir()
                player = args.player[i] if args.edition == 'classic' else args.player[0]
                peer = Peer(args.edition, player, Path(player).parent if args.edition == 'classic' else root/('profile-%d' % i), logs, args.reader_runner)
                peers.append(peer)
                peer.launch()
            result = (exercise_case or exercise)(peers, root)
        finally:
            # A cache assertion must not skip the other peer's process/log close
            # or the installed-registration check, and must retain the root cause.
            primary_failure = sys.exc_info()[1]
            cleanup_errors = []
            for peer in peers:
                try:
                    if peer.process is not None and peer.process.poll() is None:
                        peer.kill('cleanup-owned-idle-process')
                except Exception:
                    cleanup_errors.append(traceback.format_exc())
            for peer in peers:
                try:
                    peer.cleanup()
                    cache = Path(str(peer.registry)+'.continuous-cache')
                    if cache.exists():
                        assert not list(cache.glob('remux-*')), ('Unreleased output after test', str(cache))
                        shutil.rmtree(cache)
                except Exception:
                    cleanup_errors.append(traceback.format_exc())
            if registration is not None:
                try:
                    assert registration_snapshot() == registration, 'Installed Classic registration changed'
                except Exception:
                    cleanup_errors.append(traceback.format_exc())
            if cleanup_errors:
                (report/'cleanup-errors.json').write_text(json.dumps(cleanup_errors, indent=2), encoding='utf-8')
                if primary_failure is None:
                    raise AssertionError(cleanup_errors)
    worker = 'foxicmp-remux-stream-worker'+('.exe' if os.name == 'nt' else '')
    result.update(edition=args.edition, temporaryWorkRemoved=not root.exists(), processHistory=[p.history for p in peers], playerSha256=[sha(p.player) for p in peers], workerSha256=[sha(Path(p.player).parent/worker) for p in peers], testSourceSha256=sha(source_file or __file__), readerRunnerSha256=sha(args.reader_runner))
    (report/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--edition', choices=('qt', 'classic'), required=True)
    parser.add_argument('--player', action='append', required=True)
    parser.add_argument('--reader-runner', required=True)
    parser.add_argument('--report-directory', required=True)
    args = parser.parse_args()
    try:
        value = run(args)
        print(json.dumps(dict(result=value['result'], count=value['count'])))
    except Exception:
        Path(args.report_directory).mkdir(parents=True, exist_ok=True)
        (Path(args.report_directory)/'error.txt').write_text(traceback.format_exc(), encoding='utf-8')
        raise
