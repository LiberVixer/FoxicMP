"""Cancel an incomplete remux source while its durable receipt registry is locked."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'qt/tools'))

from progressive_continuous import AAC_MODE
from progressive_continuous_test_media import source_fixture, source_snapshot, source_cuts
from progressive_mux_ts_conformance import Rpc
from progressive_receipt_close_api import RegistryHold


def run(runner, worker, report):
    report.mkdir(parents=True, exist_ok=False)
    result = dict(result='FAIL', runnerSha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                  registryHoldSeconds=3, statusBudgetSeconds=.5, historicalFailureCauseEstablished=False)
    with tempfile.TemporaryDirectory(prefix='foxicmp-source-close-') as temp:
        root = Path(temp)
        reference = source_fixture(root, 'reference', AAC_MODE)
        raw = reference.read_bytes()
        path = root/'input.part'
        model = source_snapshot(path, raw, prefix=source_cuts(reference, AAC_MODE)[20], complete=False, mode=AAC_MODE)
        original_sha = hashlib.sha256(path.read_bytes()).hexdigest()
        registry = root/'registry'
        rpc = Rpc([str(runner), str(registry), str(worker)])
        hold = None
        try:
            def cmd(action, **fields):
                reply = rpc(action, **fields)
                assert reply['ok'], reply
                return reply
            def wait(check, timeout=30):
                deadline = time.monotonic()+timeout
                while time.monotonic()<deadline:
                    value = check()
                    if value: return value
                    time.sleep(.02)
                raise AssertionError('Source cleanup deadline')
            cmd('start', directory=str(root), root=str(root/'cache'), session=model, aacWindow=True)
            initial = wait(lambda: (lambda s: s if s['fragments'] and s['sourceCode']=='waiting' else None)(cmd('status')))
            assert not initial['sourceFilesClosed'] and not initial['done']
            ids = initial['sourceReaderIds']
            assert ids
            hold = RegistryHold(registry)
            at = time.monotonic()
            cmd('cancel')
            result['cancelSeconds'] = time.monotonic()-at
            time.sleep(.15)
            if os.name!='nt':
                fds = []
                for fd in (Path('/proc')/str(rpc.p.pid)/'fd').iterdir():
                    try:
                        if os.readlink(fd)==str(path): fds.append(int(fd.name))
                    except FileNotFoundError: pass
                result['originalMediaFdsDuringLock'] = fds
            result['rawReceiptsDuringLock'] = [json.loads((registry/(rid+'.lease')).read_text()) for rid in ids]
            at = time.monotonic()
            result['statusDuringLock'] = cmd('status')
            result['statusSeconds'] = time.monotonic()-at
            result['observedAtSeconds'] = time.monotonic()-hold.began
            hold.finish()
            final = wait(lambda: (lambda s: s if s['done'] else None)(cmd('status')), 2)
            result['finalStatus'] = final
            assert final['sourceFilesClosed'] and final['sourceCode']=='cancelled', final
            result['finalReceipts'] = [cmd('readerStatus', version=1, readerId=rid)['reader'] for rid in ids]
            assert all(v['released'] and v['filesClosed'] and v['reason']=='files-closed' for v in result['finalReceipts'])
            assert cmd('retire')['removed']
            cmd('drop')
            for rid in ids: cmd('readerForget', version=1, readerId=rid)
            assert cmd('budgetStatus')['bytes']==0 and hashlib.sha256(path.read_bytes()).hexdigest()==original_sha
            failures = []
            if result['cancelSeconds']>=.5 or result['statusSeconds']>=.5: failures.append('source mutex blocked during receipt write')
            if result['observedAtSeconds']>=1: failures.append('observation missed locked window')
            if result.get('originalMediaFdsDuringLock'): failures.append('original native handles still open')
            if any(v['filesClosed'] for v in result['rawReceiptsDuringLock']): failures.append('premature receipt')
            if result['observedAtSeconds']<1 and result['statusDuringLock']['sourceFilesClosed']:
                failures.append('premature source close acknowledgement')
            result.update(result='FAIL' if failures else 'PASS', failures=failures)
        finally:
            if hold is not None: hold.close()
            rpc.close()
            result['nativeRunnerExit'] = rpc.p.returncode
            (report/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    value = run(args.runner.resolve(), args.worker.resolve(), args.report_directory.resolve())
    print(json.dumps(value))
    raise SystemExit(0 if value['result']=='PASS' else 1)
