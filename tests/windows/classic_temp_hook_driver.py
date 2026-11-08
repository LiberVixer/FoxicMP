"""Actual process-local Detours adapter tests under short/long child-only TMP."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from owned_process_job import OwnedProcessJob


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    args.report_directory.mkdir(parents=True, exist_ok=False)
    job = OwnedProcessJob()
    before = dict(os.environ)
    rows = []
    with tempfile.TemporaryDirectory(prefix='foxicmp-classic-temp-hook-') as name:
        root = Path(name)
        for length, empty in ((120,False),(240,False),(320,False),(1100,False),(320,True),(1100,True)):
            label = str(length)+('-empty' if empty else '')
            folder = root / label
            while len(str(folder)) < length:
                component = ('Временные файлы ' + 'x'*25)[:max(1, length-len(str(folder))-1)]
                folder /= component[:-1]+'x' if component.endswith(' ') else component
            folder.mkdir(parents=True)
            env = dict(os.environ)
            env.update(TMP='' if empty else str(folder), TEMP=str(folder), TMPDIR=str(folder))
            proc = subprocess.run([str(args.runner.resolve()), '--long' if length>=320 else '--short'],
                                  env=env, capture_output=True, timeout=10)
            (args.report_directory/(label+'.stdout')).write_bytes(proc.stdout)
            (args.report_directory/(label+'.stderr')).write_bytes(proc.stderr)
            assert proc.returncode == 0, (length, proc.returncode, proc.stdout, proc.stderr)
            result = json.loads(proc.stdout)
            assert result['result']=='PASS' and result['longCase']==(length>=320), result
            rows.append(dict(targetLength=length, emptyTmp=empty, nativeExit=proc.returncode, result=result))
    assert dict(os.environ)==before
    report = dict(result='PASS', count=len(rows), rows=rows, privateRootRemoved=not root.exists(),
                  parentEnvironmentUnchanged=True)
    (args.report_directory/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(result='PASS', count=len(rows))))
    job.finish()


if __name__=='__main__':
    main()
