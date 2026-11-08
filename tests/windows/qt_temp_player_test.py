"""Actual Qt Player legacy-adapter regressions under exact long child TEMP.

The parent environment/desktop profile is untouched. Fixtures and software/null
output are configured by the existing ProgressiveApiTest cases.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from owned_process_job import OwnedProcessJob


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runner', 'temp-probe', 'fixtures', 'report-directory'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    args.report_directory.mkdir(parents=True, exist_ok=False)
    job = OwnedProcessJob()
    original = dict(os.environ)
    rows = []
    with tempfile.TemporaryDirectory(prefix='foxicmp-qt-player-temp-') as name:
        root = Path(name)
        for length in (240, 320, 1100):
            folder = root / str(length)
            while len(str(folder)) < length:
                component = ('Временные файлы '+'x'*25)[:max(1, length-len(str(folder))-1)]
                folder /= component[:-1]+'x' if component.endswith(' ') else component
            folder.mkdir(parents=True)
            env = dict(os.environ)
            env.update(TMP=str(folder), TEMP=str(folder), TMPDIR=str(folder), QT_QPA_PLATFORM='offscreen',
                       FOXICMP_TEST_MEDIA=str(args.fixtures.resolve()), QTEST_FUNCTION_TIMEOUT='60000')
            probe = subprocess.run([str(args.temp_probe.resolve()), '--temp-info'], env=env,
                                   capture_output=True, timeout=5)
            assert probe.returncode == 0, (probe.returncode, probe.stderr)
            actual = Path(json.loads(probe.stdout)['actualTemp'])
            assert actual.resolve() == folder.resolve(), (actual, folder)
            with (args.report_directory/(str(length)+'.log')).open('wb') as output:
                process = subprocess.run([str(args.runner.resolve()), 'legacyProviderPublishesWriterStateAndReleases'],
                                         env=env, stdout=output, stderr=subprocess.STDOUT, timeout=70)
            (args.report_directory/(str(length)+'.exit')).write_text(str(process.returncode)+'\n')
            assert process.returncode == 0, (length, process.returncode)
            log = (args.report_directory/(str(length)+'.log')).read_bytes()
            assert b'7 passed, 0 failed, 0 skipped' in log, log[-2048:]
            rows.append(dict(result='PASS', targetLength=length, actualTempDirectory=str(actual),
                             requestedTempHonored=True, nativeExit=process.returncode,
                             workingRows=5, logSha256=hashlib.sha256(log).hexdigest()))
            (args.report_directory/'progress.json').write_text(json.dumps(rows, indent=2)+'\n')
    assert dict(os.environ) == original
    result = dict(result='PASS', count=len(rows), rows=rows, privateRootRemoved=not root.exists(),
                  parentEnvironmentUnchanged=True)
    (args.report_directory/'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(result='PASS', count=len(rows))))
    job.finish()


if __name__ == '__main__':
    main()
