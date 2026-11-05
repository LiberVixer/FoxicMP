"""Managed parent faults after proven progress in incomplete pipe operations.

Private synthetic workers exercise the real Process/StreamRemuxSession transport.
This is not a FFmpeg instruction-level or end-to-end player/cache acceptance test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import time
import traceback
from types import SimpleNamespace

from progressive_backend_demux_conformance import snapshot
from progressive_continuous_worker_crash_conformance import OwnedWorker
from progressive_stream_remux_worker_test import fixture


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class BoundedRpc:
    def __init__(self, runner, registry, worker, log):
        self.error = log.open('wb')
        self.process = subprocess.Popen([str(runner), str(registry), str(worker)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.error,
            text=True, encoding='utf-8')
        self.replies = queue.Queue()
        def read():
            try:
                for line in self.process.stdout:
                    self.replies.put(json.loads(line))
            except Exception as error:
                self.replies.put(error)
            finally:
                self.replies.put(EOFError('Native fixture stdout closed'))
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()
        self.max_seconds = 0

    def __call__(self, action, **fields):
        began = time.monotonic()
        self.process.stdin.write(json.dumps(dict(action=action, **fields))+'\n')
        self.process.stdin.flush()
        answer = self.replies.get(timeout=2)
        if isinstance(answer, Exception):
            raise answer
        seconds = time.monotonic()-began
        self.max_seconds = max(seconds, self.max_seconds)
        assert seconds < 2 and answer['ok'], (seconds, answer)
        return answer

    def close(self):
        self.process.stdin.close()
        try:
            code = self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=2)
            raise
        finally:
            self.reader.join(timeout=2)
            self.process.stdout.close()
            self.error.close()
        assert code == 0, code
        return code


def run(runner, worker, report):
    report.mkdir(parents=True, exist_ok=True)
    rows = []
    with tempfile.TemporaryDirectory(prefix='foxicmp-stream-io-') as temporary:
        root = Path(temporary)
        original = fixture(root, 'reference')
        raw = original.read_bytes()
        original_sha = sha(original)
        assert len(raw) > 65536
        registry = root/'registry'
        rpc = BoundedRpc(runner, registry, worker, report/'native-stderr.txt')
        peer = SimpleNamespace(process=rpc.process, player=str(runner))
        watches = []
        def wait(predicate, label, timeout=2):
            until = time.monotonic()+timeout
            while time.monotonic() < until:
                value = predicate()
                if value:
                    return value
                time.sleep(.01)
            raise AssertionError(label)
        try:
            for mode in ('pipe-receive', 'pipe-send'):
                for fault in ('worker-death', 'cancel', 'operation-timeout'):
                    for iteration in range(2):
                        name = '%s-%s-%d' % (mode, fault, iteration)
                        (report/'current.json').write_text(json.dumps(dict(profile=name))+'\n')
                        directory = root/name
                        directory.mkdir()
                        path = directory/'input.part'
                        model = snapshot(path, raw, complete=False, prefix=65536)
                        before = sha(path)
                        marker = directory/'ready.json'
                        output = directory/'output.mp4'
                        rpc('start', directory=str(directory), session=model,
                            output=str(output), childMode=mode, childMarker=str(marker),
                            operation=3000 if fault == 'operation-timeout' else 10000,
                            lifetime=30000, heartbeat=30000)
                        wait(marker.exists, 'Child never proved partial pipe progress')
                        ready = json.loads(marker.read_text())
                        initial = rpc('status')
                        assert ready['mode'] == mode and ready['pid'] == initial['workerId']
                        assert not initial['done'] and not initial['sourceComplete'] and initial['readerIds'], initial
                        count = 65536 if mode == 'pipe-send' else 4096
                        assert initial['bytesRead'] == initial['maximumReadEnd'] == count, initial
                        reply = ('{"code":"data","hex":"'+raw[:count].hex()+'"}').encode()
                        expected = reply[:4096] if mode == 'pipe-send' else reply
                        assert ready['prefixSha256'] == hashlib.sha256(expected).hexdigest(), ready
                        if mode == 'pipe-send':
                            assert ready['transferredBytes'] == 4096
                            assert len(reply)+1-ready['transferredBytes'] > ready['channelCapacityBytes'] > 0
                        else:
                            assert ready['transferredBytes'] > ready['channelCapacityBytes'] > 0
                        assert output.stat().st_size == initial['bytesWritten'] == 0
                        watch = OwnedWorker(peer, initial['workerId'], expected=runner)
                        watches.append(watch)
                        began = time.monotonic()
                        kill = None
                        if fault == 'worker-death':
                            kill = watch.terminate()
                        elif fault == 'cancel':
                            rpc('cancel')
                        trace = []
                        def terminal():
                            state = rpc('status')
                            trace.append(dict(seconds=time.monotonic()-began, status=state))
                            return state if state['done'] else None
                        try:
                            final = wait(terminal, 'Managed parent did not release after pipe fault',
                                timeout=5 if fault == 'operation-timeout' else 2)
                        finally:
                            (report/('trace-'+name+'.json')).write_text(json.dumps(trace, indent=2)+'\n')
                        elapsed = time.monotonic()-began
                        expected_codes = {'backend-worker-exited', 'backend-pipe-error'} if fault == 'worker-death' else {'cancelled'} if fault == 'cancel' else {'backend-operation-time-limit'}
                        assert final['code'] in expected_codes and not final['result'] and final['filesClosed'], final
                        if fault != 'operation-timeout':
                            assert elapsed < 2
                        assert watch.exited() and final['workerId'] == initial['workerId']
                        assert final['bytesWritten'] == output.stat().st_size == 0
                        assert final['bytesRead'] == final['maximumReadEnd'] == count
                        receipts = []
                        for identifier in final['readerIds']:
                            receipt = rpc('readerStatus', version=1, readerId=identifier)['reader']
                            assert receipt['released'] and receipt['filesClosed'] and receipt['reason'] == 'files-closed', receipt
                            receipts.append(receipt)
                            rpc('readerForget', version=1, readerId=identifier)
                        assert receipts
                        assert not rpc('readers', version=1, sessionId=model['sessionId'], generation='1')['readers']
                        rpc('drop')
                        assert sha(path) == before and sha(original) == original_sha
                        # A closed source must remain writable by its owner.
                        with path.open('ab') as stream:
                            assert stream.write(raw[65536:65536+128]) == 128
                        assert path.read_bytes() == raw[:65536+128]
                        assert rpc.process.poll() is None
                        rows.append(dict(profile=name, result='PASS', marker=ready,
                            initial=initial, final=final, elapsedSeconds=elapsed,
                            killedWorker=kill, childExit=watch.proof(), receipts=receipts,
                            originalsUnchangedByParent=True, writerAppendAfterRelease=True,
                            incompleteOutputRejected=True, sourceReadBounded=True,
                            sameParentProcess=True))
                        (report/'progress.json').write_text(json.dumps(dict(completed=len(rows), rows=rows), indent=2)+'\n')
                        print(json.dumps(dict(profile=name, result='PASS')), flush=True)
                        watch.close()
            maximum_rpc = rpc.max_seconds
        finally:
            for watch in watches:
                watch.close()
            exit_code = rpc.close()
    error = (report/'native-stderr.txt').read_text()
    assert not any(text in error for text in ('AddressSanitizer', 'LeakSanitizer', 'runtime error:')), error
    result = dict(result='PASS', count=len(rows), rows=rows, parentExitCode=exit_code,
        maximumRpcSeconds=maximum_rpc, runnerSha256=sha(runner), workerSha256=sha(worker),
        testSourceSha256=sha(__file__), fixtureSourceSha256=sha(Path(__file__).with_name('progressive_stream_parent_cli.cpp')),
        identityHelperSourceSha256=sha(Path(__file__).with_name('progressive_continuous_worker_crash_conformance.py')),
        temporaryWorkRemoved=not root.exists(), syntheticWorker=True,
        actualFfmpegWorkerInstructionDeath=False, playerCacheAcceptance=False)
    (report/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = run(args.runner.resolve(), args.worker.resolve(), args.report_directory.resolve())
        print(json.dumps(dict(result=result['result'], count=result['count'])))
    except Exception:
        args.report_directory.mkdir(parents=True, exist_ok=True)
        (args.report_directory/'error.txt').write_text(traceback.format_exc())
        raise
