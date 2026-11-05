"""Actual FFmpeg AVIO checkpoints through public SDK and real player/cache.

Only the separately built test worker contains the checkpoint implementation.
File read/write belongs to the parent; this does not inject an OS disk failure.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import time
import traceback
import uuid

from progressive_continuous_api_test import run
from progressive_continuous_worker_crash_conformance import OwnedWorker
from progressive_player_crash_recovery import ApiError, sha, wait
from progressive_stream_remux_worker_test import fixture, probe
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_continuous import ContinuousRemux


class ExternalReader:
    def __init__(self, peer, path):
        self.process = subprocess.Popen([peer.reader_runner, '--hold', str(peer.registry),
            uuid.uuid4().hex, str(path)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True)
        self.replies = queue.Queue()
        def read():
            for line in self.process.stdout:
                self.replies.put(json.loads(line))
        self.thread = threading.Thread(target=read, daemon=True); self.thread.start()
        self.reader_id = self.replies.get(timeout=2)['readerId']
        self.closed = False

    def close(self):
        if not self.closed:
            self.process.stdin.write('close\n'); self.process.stdin.flush()
            assert self.replies.get(timeout=2)['ok']
            self.closed = True

    def finish(self):
        self.close(); self.process.stdin.write('quit\n'); self.process.stdin.flush()
        self.process.stdin.close(); code = self.process.wait(timeout=2)
        self.thread.join(timeout=2); error = self.process.stderr.read()
        self.process.stdout.close(); self.process.stderr.close()
        assert code == 0, error
        return dict(exitCode=code, readerId=self.reader_id)


def exercise(peers, root, report):
    reference = fixture(root, 'reference'); raw = reference.read_bytes()
    cuts = {s: int(next(p['pos'] for p in probe(reference)['packets'] if float(p['pts_time']) >= s))+13 for s in (12, 20)}
    peer = peers[0]; pid = peer.process.pid
    worker_dir = Path(peer.player).parent
    control = worker_dir/'avio-fault-control.json'; marker = worker_dir/'avio-fault-ready.json'
    assert not control.exists() and not marker.exists()
    cache = Path(str(peer.registry)+'.continuous-cache')
    owned, watches, external, rows = [], [], [], []

    def model_for(name, prefix, complete=False):
        directory = root/name; directory.mkdir(); path = directory/'input.part'
        model = snapshot(path, raw, complete=complete, prefix=prefix)
        model['timeline']['duration'] = '240000000'
        return directory, path, model

    def create(client, directory, model):
        sdk = ContinuousRemux(client, str(directory)); owned.append(sdk)
        sdk.prepare(model, 'clip'); return sdk

    def ready(sdk):
        def poll():
            s = sdk.status(); assert not s['errorCode'], s
            if s['playbackReady']: return s
            sdk.heartbeat()
        return wait(poll, 'AVIO source ready')

    def playing(sdk):
        def poll():
            s = sdk.playback_status(); assert s['state'] not in ('closed', 'error', 'finished'), s
            return s if s['state'] == 'playing' and s['positionSeconds'] > .2 and s['decoderReaders'] else None
        return wait(poll, 'AVIO actual playback')

    def finish(sdk):
        proof = sdk.close(timeout=2); assert proof['released'] and proof['sourceFilesClosed'], proof
        forgotten = sdk.forget(timeout=2); assert forgotten['forgotten'], forgotten
        return dict(proof=proof, forgotten=forgotten)

    try:
        directory, other_path, other_model = model_for('independent', len(raw), True)
        other = create(peers[1].client, directory, other_model); ready(other)
        wait(lambda: other.status()['outputComplete'], 'independent output complete')
        other.play(title='Independent AVIO control'); other_initial = playing(other); other.control('pause')
        for point in ('read', 'write'):
            for paused in (False, True):
                for held in (False, True):
                    name = '%s-%s-%s' % (point, 'paused' if paused else 'playing', 'external' if held else 'alone')
                    (report/'current.json').write_text(json.dumps(dict(profile=name))+'\n')
                    directory, path, model = model_for(name, cuts[12])
                    sdk = create(peer.client, directory, model); ready(sdk)
                    sdk.play(title='AVIO fault '+name); actual = playing(sdk)
                    if paused:
                        sdk.control('pause')
                        actual = wait(lambda: (lambda s: s if s['state'] == 'paused' else None)(sdk.playback_status()), 'AVIO manual pause')
                    initial = wait(lambda: (lambda s: s if s['producerCode'] == 'waiting' else None)(sdk.status()), 'producer waiting before arm')
                    watch = OwnedWorker(peer, initial['workerId']); watches.append(watch)
                    temporary = control.with_suffix('.tmp')
                    temporary.write_text(json.dumps(dict(pid=initial['workerId'], point=point)))
                    temporary.replace(control)
                    with path.open('ab') as file:
                        assert file.write(raw[cuts[12]:cuts[20]]) == cuts[20]-cuts[12]
                        file.flush(); os.fsync(file.fileno())
                    updated = copy.deepcopy(model); updated['revision'] = '2'
                    updated['resources'][0]['ranges'] = [dict(offset='0', length=str(cuts[20]))]
                    updated = protect_snapshot(updated, directory, previous=model); appended_sha = sha(path)
                    sdk.publish(updated)
                    wait(marker.exists, 'Real FFmpeg never entered targeted AVIO callback', 5)
                    checkpoint = json.loads(marker.read_text())
                    assert checkpoint['insideActualFfmpegAvio'] and checkpoint['pid'] == initial['workerId'] and checkpoint['point'] == point
                    before_kill = sdk.status()
                    assert not before_kill['errorCode'] and not before_kill['outputComplete']
                    if point == 'read':
                        offset = int(checkpoint['offset']); length = checkpoint['bytes']
                        assert offset+length <= cuts[20] and checkpoint['dataSha256'] == hashlib.sha256(raw[offset:offset+length]).hexdigest()
                        assert checkpoint['partialWireBytes'] == 0
                    else:
                        assert checkpoint['partialWireBytes'] > 0
                        assert int(before_kill['writtenBytes']) == int(checkpoint['offset']), (before_kill, checkpoint)
                    outputs = list(cache.glob('remux-*/output.fmp4')); assert len(outputs) == 1, outputs
                    output = outputs[0]; cache_sha = sha(output); cache_size = output.stat().st_size
                    reader = None
                    if held:
                        reader = ExternalReader(peer, output); external.append(reader)
                    other.control('seek', seconds=0); other.control('play'); independent_before = playing(other)
                    began = time.monotonic(); killed = watch.terminate(); trace = []
                    def stopped_source():
                        s = sdk.status(); trace.append(dict(seconds=time.monotonic()-began, producer=s))
                        assert not s['outputComplete']
                        if s['errorCode']:
                            assert s['errorCode'] in ('backend-worker-exited', 'backend-pipe-error'), s
                        return s if s['errorCode'] and s['sourceFilesClosed'] else None
                    try:
                        terminal = wait(stopped_source, 'AVIO error closes source', 2)
                        stopped = wait(lambda: (lambda s: s if s['state'] == 'error' and s['decoderReaders'] == 0 else None)(sdk.playback_status()), 'AVIO error closes decoder', 2)
                        assert stopped['error'] == terminal['errorCode'], stopped
                        external_proof = None
                        if held:
                            assert not terminal['released'] and output.exists()
                            receipt = peer.client.reader_status(reader.reader_id)
                            assert not receipt['released'] and not receipt['filesClosed'], receipt
                            for _ in range(3):
                                s = sdk.status(); assert not s['released'] and output.exists(), s
                                time.sleep(.05)
                            assert sha(output) == cache_sha and output.stat().st_size == cache_size
                            closed_at = time.monotonic(); external_exit = reader.finish(); external.remove(reader)
                            closed_receipt = peer.client.reader_status(reader.reader_id)
                            assert closed_receipt['released'] and closed_receipt['filesClosed'] and closed_receipt['reason'] == 'files-closed'
                            assert peer.client.forget_reader(reader.reader_id)['forgotten']
                            external_proof = dict(before=receipt, after=closed_receipt, process=external_exit)
                        else:
                            closed_at = began
                        def released():
                            s = sdk.status(); trace.append(dict(seconds=time.monotonic()-began, producer=s))
                            return s if s['released'] and s['sourceFilesClosed'] else None
                        terminal = wait(released, 'AVIO cache released after actual last close', 2)
                        release_seconds = time.monotonic()-closed_at
                        assert release_seconds < 2 and not list(cache.glob('remux-*'))
                    finally:
                        (report/('trace-'+name+'.json')).write_text(json.dumps(trace, indent=2)+'\n')
                        control.unlink(missing_ok=True); marker.unlink(missing_ok=True)
                    receipts = peer.client.readers(model['sessionId'], 1)
                    assert receipts and all(r['released'] and r['filesClosed'] and r['reason'] == 'files-closed' for r in receipts), receipts
                    def advances():
                        s = other.playback_status()
                        assert s['state'] == 'playing' and s['playbackId'] == other_initial['playbackId'] and s['readerIds'] == other_initial['readerIds'], s
                        return s if s['positionSeconds'] > independent_before['positionSeconds']+.1 else None
                    independent_after = wait(advances, 'independent playback survives AVIO fault', 2); other.control('pause')
                    for action in (sdk.heartbeat, lambda: sdk.publish(updated)):
                        try: action()
                        except ApiError as error: assert error.code == 'continuous-closed', error.reply
                        else: raise AssertionError('Failed attempt accepted late writer event')
                    final = finish(sdk); assert sha(path) == appended_sha
                    with path.open('ab') as file: assert file.write(raw[cuts[20]:]) == len(raw)-cuts[20]
                    assert path.read_bytes() == raw
                    retried_model = copy.deepcopy(updated); retried_model['revision'] = '3'
                    retried_model['resources'][0]['ranges'] = [dict(offset='0', length=str(len(raw)))]
                    retried_model = protect_snapshot(retried_model, directory, previous=updated)
                    retry = create(peer.client, directory, retried_model); retried = ready(retry)
                    assert retried['jobId'] != initial['jobId']
                    retry.play(title='Explicit AVIO retry'); restarted = playing(retry)
                    assert restarted['playbackId'] != sdk.playback_id and not set(restarted['readerIds']) & set(initial['readerIds'])
                    retry_final = finish(retry)
                    assert sha(path) == sha(reference) and peer.process.poll() is None and peer.process.pid == pid
                    rows.append(dict(profile=name, result='PASS', initialProducer=initial,
                        initialPlayback=actual, checkpoint=checkpoint, beforeKill=before_kill,
                        killedWorker=killed, terminalProducer=terminal, terminalPlayback=stopped,
                        sourceReaderReceipts=receipts, cachePrefixSha256=cache_sha, cachePrefixBytes=cache_size,
                        externalReader=external_proof, releaseAfterLastCloseSeconds=release_seconds,
                        independentBefore=independent_before, independentAfter=independent_after,
                        final=final, explicitRetry=dict(producer=retried, playback=restarted, **retry_final),
                        originalsUnchangedByPlayer=True, writerAppendAfterRelease=True,
                        lateEventsRejected=True, samePlayerProcess=True))
                    (report/'progress.json').write_text(json.dumps(dict(completed=len(rows), rows=rows), indent=2)+'\n')
                    print(json.dumps(dict(profile=name, result='PASS')), flush=True)
        other_final = finish(other); assert sha(other_path) == sha(reference)
        return dict(result='PASS', count=len(rows), rows=rows, otherFinal=other_final,
            actualFfmpegAvio=True, instrumentedTestWorker=True,
            osDiskInstructionInjection=False, productionWorkerCheckpoint=False)
    finally:
        control.unlink(missing_ok=True); marker.unlink(missing_ok=True)
        errors = []
        for reader in external:
            try: reader.finish()
            except Exception: errors.append(traceback.format_exc())
        for sdk in reversed(owned):
            if not sdk.forgotten:
                try: finish(sdk)
                except Exception: errors.append(traceback.format_exc())
        for watch in watches: watch.close()
        if errors:
            (report/'cleanup-errors.txt').write_text('\n'.join(errors))
            raise AssertionError('AVIO fault cleanup failed')


def staged_run(args, report):
    original_players = list(args.player)
    with tempfile.TemporaryDirectory(prefix='foxicmp-avio-players-') as name:
        stage = Path(name); players = []
        for i, original in enumerate(original_players):
            original = Path(original).resolve(); destination = stage/str(i); destination.mkdir()
            runtime = Path(args.runtime_directory).resolve() if args.runtime_directory else original.parent
            if os.name == 'nt':
                for dll in runtime.glob('*.dll'): shutil.copy2(dll, destination/dll.name)
                if args.edition == 'qt':
                    for folder in ('platforms', 'styles', 'imageformats', 'iconengines', 'tls', 'networkinformation'):
                        if (runtime/folder).is_dir(): shutil.copytree(runtime/folder, destination/folder)
            player = destination/original.name; shutil.copy2(original, player)
            normal_worker = original.parent/('foxicmp-demux-worker'+('.exe' if os.name == 'nt' else ''))
            if not normal_worker.exists(): normal_worker = runtime/normal_worker.name
            shutil.copy2(normal_worker, destination/normal_worker.name)
            shutil.copy2(args.test_worker, destination/('foxicmp-remux-stream-worker'+('.exe' if os.name == 'nt' else '')))
            if args.edition == 'classic':
                player.with_suffix('.ini').write_text('[Settings]\nLanguage=en\nContextMenuPrompted=1\nContextMenuEnabled=0\nPauseMinimizedVideo=0\nCheckForUpdates=0\n[Audio]\nMute=1\n', encoding='utf-16')
            players.append(str(player))
        args.player = players
        result = run(args, lambda peers, root: exercise(peers, root, report), __file__)
    result.update(testPlayersRemoved=not stage.exists(), originalPlayerSha256=[sha(p) for p in original_players],
        checkpointWorkerSha256=sha(args.test_worker), checkpointSourceSha256=sha(Path(__file__).with_name('progressive_ffmpeg_fault_worker.cpp')))
    (report/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--edition', choices=('qt', 'classic'), required=True)
    parser.add_argument('--player', action='append', required=True)
    parser.add_argument('--reader-runner', required=True)
    parser.add_argument('--test-worker', required=True)
    parser.add_argument('--runtime-directory', default='')
    parser.add_argument('--report-directory', required=True)
    args = parser.parse_args(); report = Path(args.report_directory).resolve(); report.mkdir(parents=True, exist_ok=True)
    try:
        result = staged_run(args, report)
        print(json.dumps(dict(result=result['result'], count=result['count'])))
    except Exception:
        (report/'error.txt').write_text(traceback.format_exc())
        raise
