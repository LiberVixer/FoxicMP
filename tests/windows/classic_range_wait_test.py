"""Classic missing-range, sample and completion waits with real publication."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(runner, report, pre_fix):
    report.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='foxicmp-range-wait-') as directory:
        root = Path(directory)
        video, audio = root / 'video.mp4', root / 'audio.m4a'
        common = ['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i']
        commands = [common + ['testsrc2=size=160x90:rate=25:duration=24', '-an',
                    '-c:v', 'libx264', '-threads', '2', '-preset', 'veryfast',
                    '-bf', '0', '-g', '25', '-crf', '20'],
                    common + ['sine=frequency=997:sample_rate=48000:duration=24',
                    '-vn', '-c:a', 'aac', '-b:a', '128k']]
        for command, path in zip(commands, (video, audio)):
            value = subprocess.run(command + ['-movflags',
                'empty_moov+frag_keyframe+default_base_moof', '-frag_duration', '1000000', str(path)],
                capture_output=True, timeout=45)
            assert value.returncode == 0, (value.returncode, value.stderr.decode(errors='replace'))
        inputs = {path.name: sha(path) for path in (video, audio)}
        value = subprocess.run([str(runner), str(video), str(audio), str(root / 'work')],
            capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=90)
        (report / 'native.stdout').write_text(value.stdout, encoding='utf-8')
        (report / 'native.stderr').write_text(value.stderr, encoding='utf-8')
        (report / 'native.exit').write_text(str(value.returncode) + '\n')
        assert value.returncode in (0, 1), (value.returncode, value.stdout, value.stderr)
        data = json.loads(value.stdout)
        assert data['count'] == len(data['rows']) == 38
        failed = [row for row in data['rows'] if row['result'] != 'PASS']
        assert len(failed) == data['failures']
        if pre_fix:
            assert value.returncode == 1 and data['result'] == 'FAIL'
            expected = {'syncread-waiting-pulse', 'completion-waiting-pulse',
                        'sample-waiting-pulse', 'completion-waiting-pulse-publish',
                        'sample-waiting-pulse-publish'}
            expected.update(operation + '-ready-' + signal
                            for operation in ('completion', 'sample', 'sample-eof')
                            for signal in ('cancel', 'flush', 'breaker', 'pulse'))
            assert {row['profile'] for row in failed} == expected
            verdict = 'EXPECTED-PRE-FIX-FAILURE'
        else:
            assert value.returncode == 0 and data['result'] == 'PASS' and not failed
            assert not any(row['rescuePublicationRequired'] for row in data['rows'])
            verdict = 'PASS'
        for row in data['rows']:
            assert row['callbacks'] == 1 and row['closedAndForgottenReaders'] == 2
            assert row['completionCounterBalanced']
            if not pre_fix and row['expectedHresult'] == -2147467260:
                assert row['sampleUnchanged'] and row['readBufferUnchanged']
                assert row['nativeReadSubmissions'] == 0
        assert {path.name: sha(path) for path in (video, audio)} == inputs
    data.update(validationResult=verdict, nativeExit=value.returncode,
        actualSyncRead=True, actualWaitForSample=True, actualWaitForCompletion=True,
        actualRegularFiles=True, actualPublication=True, actualPublicIpc=False,
        actualDiskDriverStall=False, privateHandleSubstitution=False,
        inputSha256=inputs, inputsUnchanged=True, temporaryWorkRemoved=not root.exists(),
        runnerSha256=sha(runner), driverSha256=sha(__file__),
        sourceSha256=sha(Path(__file__).with_name('classic_range_wait_probe.cpp')))
    (report / 'result.json').write_text(json.dumps(data, indent=2) + '\n')
    print(json.dumps(dict(result=verdict, count=data['count'], failures=data['failures'],
                         nativeExit=value.returncode)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    parser.add_argument('--expect-pre-fix', action='store_true')
    args = parser.parse_args()
    run(args.runner.resolve(), args.report_directory.resolve(), args.expect_pre_fix)
