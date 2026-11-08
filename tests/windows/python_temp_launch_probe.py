"""Bounded observation of an owned Python launcher with long parent/child TMP.

This collects stalls; collector exit 0 is not playback/startup acceptance.
It does not modify Python, Harvester, installed profiles or parent environment.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from owned_process_job import OwnedProcessJob


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-child', type=Path, required=True)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    args.report_directory.mkdir(parents=True, exist_ok=False)
    original = dict(os.environ)
    job = OwnedProcessJob()
    rows = []
    with tempfile.TemporaryDirectory(prefix='foxicmp-python-launch-') as temporary:
        root = Path(temporary)
        for length, scope, short_child in ((120,'child',False),(320,'child',False),
                                           (320,'parent',False),(320,'parent',True),
                                           (1100,'parent',False)):
            label = '%s-%s-%s' % (length,scope,'short-child' if short_child else 'long-child')
            temp = root/label
            while len(str(temp))<length:
                piece = ('Временные файлы '+'x'*25)[:max(1,length-len(str(temp))-1)]
                temp /= piece[:-1]+'x' if piece.endswith(' ') else piece
            temp.mkdir(parents=True)
            case = dict(command=[str(args.native_child.resolve())], temp=str(temp), scope=scope, timeout=4)
            if short_child:
                case['childTemp'] = str(root)
            config = args.report_directory/(label+'.json')
            config.write_text(json.dumps(case))
            log_path = args.report_directory/(label+'.log')
            with log_path.open('wb') as log:
                bootstrap = subprocess.Popen([sys.executable,'-u','-B',str(Path(__file__).with_name('runtime_child_probe.py')),str(config)],
                                             stdout=log,stderr=subprocess.STDOUT)
                row = dict(label=label,case=case,bootstrapPid=bootstrap.pid)
                try:
                    deadline = time.monotonic()+6
                    while bootstrap.poll() is None and time.monotonic()<deadline:
                        time.sleep(.05)
                    if bootstrap.poll() is None:
                        snapshot = subprocess.run([str(args.snapshot.resolve()),'--owned-python-child',
                            str(bootstrap.pid),str(Path(sys.executable).resolve()),str(Path(sys.executable).resolve().parent)],
                            capture_output=True,timeout=20)
                        stack_path = args.report_directory/(label+'-threads.json')
                        stack_path.write_bytes(snapshot.stdout)
                        row['snapshot'] = dict(nativeExit=snapshot.returncode, file=stack_path.name,
                            sha256=hashlib.sha256(snapshot.stdout).hexdigest(),stderr=snapshot.stderr.decode(errors='replace'))
                        row['observedState'] = 'deadline-exceeded'
                    else:
                        row['observedState'] = 'completed'
                finally:
                    row['forcedCleanup'] = bootstrap.poll() is None
                    if bootstrap.poll() is None:
                        bootstrap.kill()
                    bootstrap.wait(timeout=5)
                    row['bootstrapNativeExit'] = bootstrap.returncode
            events = []
            for line in log_path.read_text(errors='replace').splitlines():
                try:
                    events.append(json.loads(line))
                except ValueError:
                    pass
            row['events'] = events
            row['logSha256'] = hashlib.sha256(log_path.read_bytes()).hexdigest()
            rows.append(row)
            (args.report_directory/'progress.json').write_text(json.dumps(rows,indent=2)+'\n')
    assert dict(os.environ)==original
    report = dict(result='OBSERVED',collectorSuccessIsNotAllRowsPass=True,count=len(rows),rows=rows,
                  privateRootRemoved=not root.exists(),parentEnvironmentUnchanged=True,
                  pythonVersion=sys.version,pythonExecutable=str(Path(sys.executable).resolve()))
    (args.report_directory/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(result='OBSERVED',count=len(rows))))
    job.finish()


if __name__=='__main__':
    main()
