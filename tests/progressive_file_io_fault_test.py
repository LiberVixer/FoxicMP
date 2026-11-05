"""Native parent syscall failures with real FFmpeg/cache and both decoder views.

No filesystem is filled or corrupted. Results are injected for a selected
physical file by a separate test binary, never by a production application.
"""
import argparse
import copy
import json
import os
from pathlib import Path
import tempfile
import time
import traceback
from types import SimpleNamespace

from progressive_stream_io_crash_test import BoundedRpc
from progressive_continuous_avio_crash import ExternalReader
from progressive_continuous_worker_crash_conformance import OwnedWorker
from progressive_stream_remux_worker_test import fixture, probe
from progressive_backend_demux_conformance import snapshot
from progressive_backend_remux_conformance import verify_artifact
from progressive_integrity import protect_snapshot
from progressive_player_crash_recovery import sha


def run(runner, worker, reader_runner, report):
    report.mkdir(parents=True, exist_ok=True); rows = []
    with tempfile.TemporaryDirectory(prefix='foxicmp-file-io-') as temporary:
        root = Path(temporary); reference = fixture(root, 'reference'); raw = reference.read_bytes()
        reference_sha = sha(reference)
        cut = int(next(p['pos'] for p in probe(reference)['packets'] if float(p['pts_time']) >= 12))+13
        registry = root/'registry'; cache = root/'cache'
        rpc = BoundedRpc(runner, registry, worker, report/'native-stderr.txt')
        peer = SimpleNamespace(process=rpc.process, player=str(runner), reader_runner=str(reader_runner), registry=registry)
        watches, helpers = [], []
        def wait(check, label, seconds=2):
            until = time.monotonic()+seconds
            while time.monotonic() < until:
                result = check()
                if result: return result
                time.sleep(.01)
            raise AssertionError(label)
        def status(): return rpc('status')
        def receipts(ids):
            result = []
            for identifier in ids:
                receipt = rpc('readerStatus', version=1, readerId=identifier)['reader']
                assert receipt['released'] and receipt['filesClosed'] and receipt['reason'] == 'files-closed', receipt
                result.append(receipt); rpc('readerForget', version=1, readerId=identifier)
            return result
        negatives = [('read-error', {'resource-io-error'}), ('read-short', {'source-truncated'}),
            ('write-error', {'cache-io-error'}), ('write-full', {'cache-disk-full'}), ('write-partial', {'cache-io-error'})]
        cases = [(mode, codes, held, False) for mode, codes in negatives for held in (False, True)]
        positives = ['control']+(['read-eintr', 'write-eintr', 'write-short'] if os.name != 'nt' else [])
        cases += [(mode, set(), False, True) for mode in positives]
        try:
            for mode, expected_codes, held, positive in cases:
                name = mode+('-external' if held else '-alone')
                (report/'current.json').write_text(json.dumps(dict(profile=name))+'\n')
                directory = root/name; directory.mkdir(); path = directory/'input.part'
                model = snapshot(path, raw, complete=False, prefix=cut)
                model['timeline']['duration'] = '240000000'; initial_sha = sha(path)
                rpc('start', directory=str(directory), root=str(cache), session=model)
                initial = wait(lambda: (lambda s: s if s['sourceCode'] == 'waiting' and s['samples'] > 0 else None)(status()), 'Initial real remux output', 5)
                output = Path(initial['path']); old_size = output.stat().st_size; old_sha = sha(output); raw_output_prefix = output.read_bytes()
                views = rpc('views'); assert views['samples'] > 0
                assert rpc('seek', offset=initial['visible'])['offset'] == initial['visible']
                rpc('beginRead'); wait(lambda: status()['cursorWaiting'], 'Cursor waits for unpublished tail')
                watch = OwnedWorker(peer, initial['workerId'], expected=worker); watches.append(watch)
                reader = None
                if held:
                    reader = ExternalReader(peer, output); helpers.append(reader)
                target = path if mode.startswith('read') else output
                began = time.monotonic()
                if mode != 'control': rpc('fault', path=str(target), mode=mode)
                if positive or mode.startswith('write'):
                    with path.open('ab') as stream:
                        assert stream.write(raw[cut:]) == len(raw)-cut
                        stream.flush(); os.fsync(stream.fileno())
                    updated = copy.deepcopy(model); updated['revision'] = '2'
                    updated['resources'][0]['ranges'] = [dict(offset='0', length=str(len(raw)))]
                    if positive:
                        updated['writer']['state'] = updated['resources'][0]['state'] = updated['tracks'][0]['state'] = 'complete'
                    updated = protect_snapshot(updated, directory, previous=model)
                    rpc('publish', session=updated)
                final = wait(lambda: (lambda s: s if s['done'] else None)(status()), 'Source closes after I/O result', 5 if positive else 2)
                elapsed = time.monotonic()-began
                assert final['filesClosed'] and final['originalsClosed'] and final['producerClosed'] and watch.exited(), final
                injection = rpc('faultStatus')
                if mode != 'control':
                    assert injection['events'] and all(e['mode'] == mode for e in injection['events']), injection
                    assert injection['device'] and injection['fileId']
                rpc('faultClear')
                reply = wait(lambda: (lambda r: None if r.get('pending') else r)(rpc('readResult')), 'Waiting cursor completes')
                classic = rpc('fragmentError')
                verified = None
                if positive:
                    assert final['complete'] and not final['failure'] and final['sourceCode'] == 'data' and final['samples'] == 600, final
                    assert reply['bytes'] > 0 and not reply['error'] and reply['eofs'] == 0, reply
                    assert not classic['error'], classic
                    copied = root/(name+'.mp4'); copied.write_bytes(output.read_bytes())
                    verified = verify_artifact(reference, copied, 50000, True)
                    assert sha(copied) == json.loads(final['sourceResult'])['sha256']
                    assert rpc('seek', offset=final['written'])['offset'] == final['written']
                    rpc('beginRead'); eof = wait(lambda: (lambda r: None if r.get('pending') else r)(rpc('readResult')), 'Only completed output EOF')
                    assert eof['bytes'] == 0 and eof['eofs'] == 1 and not eof['error'], eof
                else:
                    assert final['failure'] in expected_codes and final['sourceCode'] == final['failure'] and not final['complete'] and not final['sourceResult'], final
                    assert elapsed < 2 and reply['bytes'] == -1 and reply['eofs'] == 0 and reply['error'] == final['failure'], reply
                    assert classic['error'] == final['failure'], classic
                    assert final['visible'] == initial['visible'] and final['written'] == initial['written'], (initial, final)
                    if mode == 'write-partial':
                        assert output.stat().st_size > old_size, (output.stat().st_size, old_size)
                        assert output.read_bytes()[:old_size] == raw_output_prefix
                    else: assert output.stat().st_size == old_size and sha(output) == old_sha
                physical_bytes = output.stat().st_size
                source_receipts = receipts(final['sourceReaderIds'])
                assert not rpc('retire')['removed'] and output.exists(), 'Decoder views still hold output'
                rpc('closeViews'); view_receipts = receipts([views['cursorId'], views['classicId']])
                external_proof = None
                if held:
                    pending = rpc('collect'); assert pending['count'] == 1 and pending['bytes'] > 0 and output.exists(), pending
                    alive = rpc('readerStatus', version=1, readerId=reader.reader_id)['reader']; assert not alive['released'], alive
                    helper_exit = reader.finish(); helpers.remove(reader)
                    external_proof = dict(pending=pending, before=alive, receipt=receipts([reader.reader_id])[0], helper=helper_exit)
                cleared = rpc('collect'); after = status()
                assert cleared['count'] == 0 and cleared['bytes'] == 0 and after['removed'] and not output.parent.exists(), (cleared, after)
                assert not rpc('readers', version=1, sessionId=model['sessionId'], generation='1')['readers']
                rpc('drop'); assert rpc.process.poll() is None
                if positive or mode.startswith('write'): assert path.read_bytes() == raw
                else:
                    assert sha(path) == initial_sha
                    with path.open('ab') as stream: assert stream.write(raw[cut:]) == len(raw)-cut
                    assert path.read_bytes() == raw
                assert sha(reference) == reference_sha
                rows.append(dict(profile=name, result='PASS', positiveControl=positive, initial=initial, final=final,
                    injection=injection, waitingCursor=reply, classicView=classic, physicalCacheBytes=physical_bytes,
                    workerExit=watch.proof(), elapsedSeconds=elapsed, sourceReceipts=source_receipts,
                    viewReceipts=view_receipts, externalReader=external_proof, cleared=cleared,
                    originalsUnchangedByParent=True, writerAppendAfterRelease=True, **(verified or {})))
                (report/'progress.json').write_text(json.dumps(dict(completed=len(rows), rows=rows), indent=2)+'\n')
                print(json.dumps(dict(profile=name, result='PASS')), flush=True); watch.close()
            max_rpc = rpc.max_seconds
        finally:
            try: rpc('faultClear')
            except Exception: pass
            for helper in helpers: helper.finish()
            for watch in watches: watch.close()
            exit_code = rpc.close()
    error = (report/'native-stderr.txt').read_text()
    assert not any(t in error for t in ('AddressSanitizer', 'LeakSanitizer', 'runtime error:')), error
    result = dict(result='PASS', count=len(rows), rows=rows, nativeParentExitCode=exit_code, maximumRpcSeconds=max_rpc,
        runnerSha256=sha(runner), workerSha256=sha(worker), readerRunnerSha256=sha(reader_runner), testSourceSha256=sha(__file__),
        fixtureSourceSha256=sha(Path(__file__).with_name('progressive_file_io_fault_cli.cpp')),
        temporaryWorkRemoved=not root.exists(), syscallResultInjection=True, physicalDiskFailure=False,
        realFfmpegWorker=True, realDecoderViews=True, installedPlayerApiAcceptance=False)
    (report/'result.json').write_text(json.dumps(result, indent=2)+'\n'); return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--runner', type=Path, required=True); p.add_argument('--worker', type=Path, required=True)
    p.add_argument('--reader-runner', type=Path, required=True); p.add_argument('--report-directory', type=Path, required=True); a = p.parse_args()
    try:
        r = run(a.runner.resolve(), a.worker.resolve(), a.reader_runner.resolve(), a.report_directory.resolve())
        print(json.dumps(dict(result=r['result'], count=r['count'])))
    except Exception:
        a.report_directory.mkdir(parents=True, exist_ok=True); (a.report_directory/'error.txt').write_text(traceback.format_exc()); raise
