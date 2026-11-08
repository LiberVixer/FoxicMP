"""Observe Qt's TMP/TEMP/USERPROFILE precedence without changing parent env."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    args.report_directory.mkdir(parents=True, exist_ok=False)
    original = dict(os.environ)
    rows = []
    with tempfile.TemporaryDirectory(prefix='foxicmp-qt-temp-policy-') as temporary:
        root = Path(temporary)
        long = root/'Длинный каталог'
        while len(str(long)) < 320:
            long /= 'Временные файлы '+'x'*25
        long.mkdir(parents=True)
        relative = os.path.relpath(long, root)
        cases = [
            ('short-tmp-wins', dict(TMP=str(root), TEMP=str(long)), root),
            ('empty-tmp-long-temp', dict(TMP='', TEMP=str(long)), long),
            ('long-userprofile', dict(USERPROFILE=str(long)), long),
            ('relative-long-tmp', dict(TMP=relative, TEMP=str(root)), long),
        ]
        for label, changes, expected in cases:
            env = dict(os.environ)
            for key in ('TMP', 'TEMP', 'USERPROFILE'):
                env.pop(key, None)
            env.update(changes)
            result = subprocess.run([str(args.runner.resolve()), '--temp-info'], env=env,
                                    cwd=root, capture_output=True, timeout=5)
            assert result.returncode == 0, (label, result.returncode, result.stderr)
            actual = Path(json.loads(result.stdout)['actualTemp'])
            assert actual.resolve() == expected.resolve(), (label, actual, expected)
            rows.append(dict(result='PASS', case=label, nativeExit=result.returncode,
                             actualTempDirectory=str(actual)))
            (args.report_directory/'progress.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    assert dict(os.environ) == original
    report = dict(result='PASS', count=len(rows), rows=rows, privateRootRemoved=not root.exists(),
                  parentEnvironmentUnchanged=True,localSystemTested=False)
    (args.report_directory/'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(result='PASS', count=len(rows))))


if __name__=='__main__':
    main()
