"""Real private registry lock: cache close/status stay live, receipts stay pending.

The deliberate three-second external lock cannot satisfy a two-second durable
release promise. Test prompt native close/status and eventual durable recovery
separately, without claiming that external locking was the historical failure.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import uuid

from progressive_mux_ts_conformance import Rpc
from progressive_stream_remux_worker_test import fixture


def run(runner, worker, report):
    report.mkdir(parents=True, exist_ok=False)
    result = dict(result='FAIL', runnerSha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                  statusBudgetSeconds=.5, closeBudgetSeconds=.5, registryHoldSeconds=3,
                  durableReleaseDuringExternalLockRequired=False)
    with tempfile.TemporaryDirectory(prefix='foxicmp-close-contention-') as temp:
        root = Path(temp)
        original = fixture(root, 'reference')
        fragmented = root/'fragmented.mp4'
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(original), '-c', 'copy',
                        '-movflags', 'empty_moov+frag_keyframe+default_base_moof+negative_cts_offsets',
                        str(fragmented)], check=True, capture_output=True)
        payload = fragmented.read_bytes()
        registry = root/'registry'
        rpc = Rpc([str(runner), str(registry), str(worker)])
        held = None
        unlock = threading.Event()
        thread = None
        try:
            def cmd(action, **fields):
                value = rpc(action, **fields)
                assert value['ok'], value
                return value
            cmd('store', root=str(root/'cache'), sessionId=uuid.uuid4().hex)
            for at in range(0, len(payload), 65536):
                cmd('append', offset=str(at), hex=payload[at:at+65536].hex())
            ids = [cmd('reader', name=name)['readerId'] for name in ('one', 'two')]
            cmd('producerClosed', error='cancelled')
            media = Path(cmd('status')['path'])
            held = (registry/'.guard').open('r+b', buffering=0)
            def lock(take):
                if os.name == 'nt':
                    import msvcrt
                    held.seek(65536)
                    msvcrt.locking(held.fileno(), msvcrt.LK_NBLCK if take else msvcrt.LK_UNLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB if take else fcntl.LOCK_UN)
            lock(True)
            began = time.monotonic()
            def release():
                unlock.wait(3)
                lock(False)
                result['unlockedSeconds'] = time.monotonic()-began
            thread = threading.Thread(target=release)
            thread.start()
            cmd('beginCloseAll', names=['one', 'two'])
            time.sleep(.05)
            if os.name != 'nt':
                def media_fds():
                    matches = []
                    for fd in (Path('/proc')/str(rpc.p.pid)/'fd').iterdir():
                        try:
                            if os.readlink(fd) == str(media): matches.append(int(fd.name))
                        except FileNotFoundError: pass
                    return matches
                result['mediaFdsDuringLock'] = media_fds()
            result['rawReceiptsDuringLock'] = [json.loads((registry/(rid+'.lease')).read_text()) for rid in ids]
            at = time.monotonic()
            result['statusDuringLock'] = cmd('status')
            result['statusSeconds'] = time.monotonic()-at
            result['closeResultDuringLock'] = cmd('readResult')
            result['observedAtSeconds'] = time.monotonic()-began
            thread.join(4)
            assert not thread.is_alive()
            at = time.monotonic()
            assert cmd('retire')['removed']
            result['releaseAfterUnlockSeconds'] = time.monotonic()-at
            result['finalReceipts'] = [cmd('readerStatus', version=1, readerId=rid)['reader'] for rid in ids]
            for rid in ids: cmd('readerForget', version=1, readerId=rid)
            assert all(v['released'] and v['filesClosed'] and v['reason']=='files-closed' for v in result['finalReceipts'])
            assert not media.exists() and cmd('budgetStatus')['bytes']==0
            failures = []
            if result['statusSeconds'] >= .5: failures.append('status blocked behind registry lock')
            if result['observedAtSeconds'] >= .5 or not result['closeResultDuringLock'].get('closed'):
                failures.append('native close did not finish promptly')
            if os.name != 'nt' and result['mediaFdsDuringLock']: failures.append('native media handles still open')
            if any(v['filesClosed'] for v in result['rawReceiptsDuringLock']): failures.append('premature receipt')
            if result['statusDuringLock']['removed']: failures.append('cache removed before durable receipts')
            if result['releaseAfterUnlockSeconds'] >= .5: failures.append('receipt recovery too slow')
            result.update(result='FAIL' if failures else 'PASS', failures=failures)
        finally:
            unlock.set()
            if thread is not None: thread.join(4)
            if held is not None: held.close()
            rpc.close()
            result['nativeRunnerExit'] = rpc.p.returncode
            (report/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    value = run(args.runner.resolve(), args.worker.resolve(), args.report_directory.resolve())
    print(json.dumps(value))
    raise SystemExit(0 if value['result']=='PASS' else 1)
