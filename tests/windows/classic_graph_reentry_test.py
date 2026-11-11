"""Real DirectShow graph/owned source subclass reentry; no CMainFrame or IPC."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(runner, report):
    report.mkdir(parents=True, exist_ok=True)
    rows = []
    with tempfile.TemporaryDirectory(prefix='foxicmp-graph-reentry-') as folder:
        root = Path(folder)
        for reordered in (False, True):
            fixture = root / ('bf2.fmp4' if reordered else 'bf0.fmp4')
            command = ['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
                       'testsrc2=size=160x90:rate=25:duration=24', '-an', '-c:v',
                       'libx264', '-threads', '2', '-preset', 'veryfast', '-bf',
                       '2' if reordered else '0', '-g', '25', '-keyint_min', '25',
                       '-sc_threshold', '0', '-crf', '20', '-movflags',
                       'empty_moov+frag_keyframe+default_base_moof+negative_cts_offsets',
                       '-f', 'mp4', str(fixture)]
            encoded = subprocess.run(command, capture_output=True, timeout=30)
            assert encoded.returncode == 0, (encoded.returncode, encoded.stderr.decode(errors='replace'))
            raw = fixture.read_bytes()
            offset, cuts = 0, []
            while offset < len(raw):
                length = int.from_bytes(raw[offset:offset + 4], 'big')
                assert length >= 8 and offset + length <= len(raw)
                if raw[offset + 4:offset + 8] == b'mdat':
                    cuts.append(offset + length)
                offset += length
            assert len(cuts) == 24
            reference = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(fixture),
                                        '-an', '-pix_fmt', 'nv12', '-f', 'rawvideo', '-'],
                                       capture_output=True, timeout=30)
            quantum = 160 * 90 * 3 // 2
            assert reference.returncode == 0 and len(reference.stdout) == 600 * quantum
            expected = [hashlib.sha256(reference.stdout[i:i + quantum]).hexdigest()
                        for i in range(250 * quantum, len(reference.stdout), quantum)]
            for mode in ('before-cancel', 'after-cancel', 'before-supersede',
                         'after-supersede', 'nested-poll', 'same-target'):
                name = ('bf2-' if reordered else 'bf0-') + mode
                work = root / name
                work.mkdir()
                result = subprocess.run([str(runner), str(fixture), str(work),
                                         str(cuts[5]), mode, '1' if reordered else '0'],
                                        capture_output=True, text=True, encoding='utf-8',
                                        errors='replace', timeout=90)
                (report / (name + '.stdout')).write_text(result.stdout, encoding='utf-8')
                (report / (name + '.stderr')).write_text(result.stderr, encoding='utf-8')
                (report / (name + '.exit')).write_text(str(result.returncode) + '\n')
                assert result.returncode == 0, (name, result.returncode, result.stdout, result.stderr)
                value = json.loads(result.stdout)
                assert value['result'] == 'PASS' and value['mode'] == mode
                assert value['sourceSeekDepth'] == 1 and value['callbacks'] == 1
                assert value['verificationFrameHashes'] == expected, (name, len(value['verificationFrameHashes']))
                assert 0 < value['framesBeforeWriterComplete'] < 600
                assert value['closedAndForgottenReaders'] == 2
                assert all(code >= 0 for code in value['seekResults'])
                supersedes = mode.endswith('supersede') or mode == 'same-target'
                assert value['acceptedGrowingSeeks'] == (2 if supersedes else 1)
                assert value['seekTargets100ns'] == ([10000000, 10000000 if mode == 'same-target' else 20000000] if supersedes else [10000000])
                assert fixture.read_bytes() == raw
                rows.append(dict(profile=name, nativeExit=0, status=value,
                                 stderr=result.stderr, fixtureSha256=sha(fixture),
                                 expectedNv12SequenceSha256=hashlib.sha256(''.join(expected).encode()).hexdigest()))
    data = dict(result='PASS', count=len(rows), rows=rows, runnerSha256=sha(runner),
                driverSha256=sha(__file__), actualDirectShowGraph=True,
                actualSoftwareH264Decode=True, inputUnchanged=True,
                temporaryWorkRemoved=not root.exists(), hardwareDecoder=False,
                actualCMainFrame=False, actualPublicIpc=False, ownerApartment='MTA')
    (report / 'result.json').write_text(json.dumps(data, indent=2) + '\n')
    print(json.dumps(dict(result='PASS', count=len(rows))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    run(args.runner.resolve(), args.report_directory.resolve())
