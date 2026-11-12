"""Preserve normal producer EOF across a heartbeat during receipt persistence."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_continuous import AAC_MODE
from progressive_continuous_test_media import source_fixture, source_snapshot, source_cuts
from progressive_integrity import protect_snapshot


def run(runner, worker, report):
    report.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix='foxicmp-source-eof-') as temp:
        root = Path(temp)
        reference = source_fixture(root, 'reference', AAC_MODE)
        raw = reference.read_bytes()
        initial = source_snapshot(root/'input.part', raw, complete=False,
                                  prefix=source_cuts(reference, AAC_MODE)[20], mode=AAC_MODE)
        # Hash the completed fixture and restore the physical initial prefix
        # before starting the native producer. Existing integrity blocks persist.
        complete = copy.deepcopy(initial)
        complete['revision'] = '2'
        complete['resources'][0]['ranges'] = [dict(offset='0', length=str(len(raw)))]
        complete['resources'][0]['state'] = complete['writer']['state'] = 'complete'
        for track in complete['tracks']: track['state']='complete'
        path = root/'input.part'
        path.write_bytes(raw)
        complete = protect_snapshot(complete, root, previous=initial)
        path.write_bytes(raw[:int(initial['resources'][0]['ranges'][0]['length'])])
        for name, value in [('initial', initial), ('complete', complete)]:
            (root/(name+'.json')).write_text(json.dumps(value), encoding='utf-8')
        process = subprocess.run([str(runner), str(root), str(worker)], capture_output=True,
                                 text=True, encoding='utf-8', timeout=30)
        result = dict(nativeExit=process.returncode, stdout=process.stdout, stderr=process.stderr,
                      runnerSha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                      driverSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      nativeSourceSha256=hashlib.sha256(Path(__file__).with_suffix('.cpp').read_bytes()).hexdigest())
        if process.returncode==0: result.update(json.loads(process.stdout))
        else: result['result']='FAIL'
        assert path.read_bytes()==raw and reference.read_bytes()==raw
    result['privateFixtureRemoved'] = not root.exists()
    (report/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    result = run(args.runner.resolve(), args.worker.resolve(), args.report_directory.resolve())
    print(json.dumps(result))
    raise SystemExit(0 if result['result']=='PASS' else 1)
