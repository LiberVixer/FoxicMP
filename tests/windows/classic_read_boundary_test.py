"""Actual Classic SyncRead/ReadV2, no installed profile or display/audio device."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(runner, report, expect_pre_fix=False):
    report.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='foxicmp-classic-read-') as folder:
        root = Path(folder)
        fixture = root / 'input.fmp4'
        command = ['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
                   'testsrc2=size=160x90:rate=25:duration=24', '-an', '-c:v',
                   'libx264', '-threads', '2', '-preset', 'veryfast', '-bf', '0',
                   '-g', '25', '-keyint_min', '25', '-sc_threshold', '0',
                   '-crf', '20', '-movflags',
                   'empty_moov+frag_keyframe+default_base_moof+negative_cts_offsets',
                   '-f', 'mp4', str(fixture)]
        encoded = subprocess.run(command, capture_output=True, timeout=30)
        assert encoded.returncode == 0, (encoded.returncode, encoded.stderr.decode(errors='replace'))
        original = sha(fixture)
        result = subprocess.run([str(runner), str(fixture), str(root / 'work')],
                                capture_output=True, text=True, encoding='utf-8',
                                errors='replace', timeout=120)
        (report / 'native.stdout').write_text(result.stdout, encoding='utf-8')
        (report / 'native.stderr').write_text(result.stderr, encoding='utf-8')
        (report / 'native.exit').write_text(str(result.returncode) + '\n')
        assert result.returncode in (0, 1), (result.returncode, result.stdout, result.stderr)
        data = json.loads(result.stdout)
        assert data['count'] == len(data['rows']) == 31
        failures = [row for row in data['rows'] if row['result'] == 'FAIL']
        assert len(failures) == data['failures']
        if expect_pre_fix:
            assert result.returncode == 1 and data['result'] == 'FAIL' and len(failures) == 19, data
            verdict = 'EXPECTED-PRE-FIX-FAILURE'
        else:
            assert result.returncode == 0 and data['result'] == 'PASS' and not failures, data
            verdict = 'PASS'
        assert sha(fixture) == original == data['fixtureSha256']
        assert all(row['closedAndForgottenReaders'] == 2 for row in data['rows'])
    data.update(validationResult=verdict, nativeExit=result.returncode,
                inputUnchanged=True, temporaryWorkRemoved=not root.exists(),
                actualSyncRead=True, actualReadV2=True, actualDirectShowGraph=False,
                actualInstalledClassic=False, runnerSha256=sha(runner),
                driverSha256=sha(__file__), nativeSourceSha256=sha(Path(__file__).with_name('classic_read_boundary_probe.cpp')))
    (report / 'result.json').write_text(json.dumps(data, indent=2) + '\n')
    print(json.dumps(dict(result=verdict, nativeExit=result.returncode,
                          count=data['count'], failures=data['failures'])))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    parser.add_argument('--expect-pre-fix', action='store_true')
    args = parser.parse_args()
    run(args.runner.resolve(), args.report_directory.resolve(), args.expect_pre_fix)
