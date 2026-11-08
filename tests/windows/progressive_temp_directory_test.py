"""Resolve exact private TEMP roots with/without longPathAware; no desktop I/O."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from owned_process_job import OwnedProcessJob


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--aware-runner', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path)
    args = parser.parse_args()
    owned_report = None
    if args.report_directory is None:
        owned_report = tempfile.TemporaryDirectory(prefix='foxicmp-temp-directory-report-')
        args.report_directory = Path(owned_report.name)
    else:
        args.report_directory.mkdir(parents=True, exist_ok=False)
    job = OwnedProcessJob()
    before = dict(os.environ)
    rows = []
    with tempfile.TemporaryDirectory(prefix='foxicmp-temp-resolver-') as name:
        root = Path(name)
        for length in (120, 240, 320, 1100):
            directory = root / str(length)
            while len(str(directory)) < length:
                component = ('Временные файлы ' + 'x' * 25)[:max(1, length-len(str(directory))-1)]
                directory /= component[:-1]+'x' if component.endswith(' ') else component
            directory.mkdir(parents=True)
            for aware, exe in ((False, args.runner), (True, args.aware_runner)):
                env = dict(os.environ)
                # A conflicting TEMP verifies that TMP remains authoritative.
                env.update(TMP=str(directory), TEMP=str(root), TMPDIR=str(root))
                probe = subprocess.run([str(exe.resolve()), '--temp-info-safe'], env=env,
                                       capture_output=True, timeout=5)
                assert probe.returncode == 0, (probe.returncode, probe.stderr)
                actual = Path(json.loads(probe.stdout)['progressiveTempDirectory']['path'])
                assert actual.resolve() == directory.resolve(), (actual, directory)
                rows.append(dict(result='PASS', manifestLongPathAware=aware, targetLength=length,
                                 actualDirectory=str(actual), nativeExit=probe.returncode))
        # No filesystem writes by the native helper: verify documented fallback
        # precedence independently of whether the selected directory is writable.
        for source in ('TEMP', 'USERPROFILE', 'Windows'):
            env = dict(os.environ)
            for key in ('TMP', 'TEMP', 'USERPROFILE'):
                env.pop(key, None)
            expected = root if source != 'Windows' else Path(os.environ['SystemRoot'])
            if source != 'Windows':
                env[source] = str(root)
            probe = subprocess.run([str(args.aware_runner.resolve()), '--temp-info-safe'],
                                   env=env, capture_output=True, timeout=5)
            assert probe.returncode == 0, (probe.returncode, probe.stderr)
            actual = Path(json.loads(probe.stdout)['progressiveTempDirectory']['path'])
            assert actual.resolve() == expected.resolve(), (source, actual, expected)
            rows.append(dict(result='PASS', precedenceSource=source, actualDirectory=str(actual), nativeExit=probe.returncode))
    assert dict(os.environ) == before
    result = dict(result='PASS', count=len(rows), rows=rows, privateRootRemoved=not root.exists(),
                  parentEnvironmentUnchanged=True)
    (args.report_directory/'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(result='PASS', count=len(rows))))
    job.finish()
    if owned_report is not None:
        owned_report.cleanup()


if __name__ == '__main__':
    main()
