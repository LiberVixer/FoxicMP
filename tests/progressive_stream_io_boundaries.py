"""Deterministic private-cache I/O barriers; no GUI, mpv or DirectShow graph."""
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
    with tempfile.TemporaryDirectory(prefix='foxicmp-io-boundaries-') as directory:
        root = Path(directory)
        fixture = root / 'source.fmp4'
        command = ['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
                   'testsrc2=size=160x90:rate=25:duration=3', '-an', '-c:v',
                   'libx264', '-threads', '2', '-preset', 'veryfast', '-bf', '0',
                   '-g', '25', '-keyint_min', '25', '-sc_threshold', '0', '-crf',
                   '28', '-movflags',
                   'empty_moov+frag_keyframe+default_base_moof+negative_cts_offsets',
                   '-f', 'mp4', str(fixture)]
        encoded = subprocess.run(command, capture_output=True, timeout=30)
        assert encoded.returncode == 0, (encoded.returncode, encoded.stderr.decode(errors='replace'))
        raw = fixture.read_bytes()
        offset = 0
        fragments = []
        while offset < len(raw):
            length = int.from_bytes(raw[offset:offset + 4], 'big')
            assert length >= 8 and offset + length <= len(raw)
            if raw[offset + 4:offset + 8] == b'mdat':
                fragments.append(offset + length)
            offset += length
        assert offset == len(raw) and len(fragments) == 3 and len(raw) < 65536
        before = sha(fixture)
        result = subprocess.run([str(runner), str(fixture), str(root / 'work'),
                                 str(fragments[0])], capture_output=True, timeout=90,
                                text=True, encoding='utf-8', errors='replace')
        (report / 'native.stdout').write_text(result.stdout, encoding='utf-8')
        (report / 'native.stderr').write_text(result.stderr, encoding='utf-8')
        (report / 'native.exit').write_text(str(result.returncode) + '\n')
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        data = json.loads(result.stdout)
        assert data['result'] == 'PASS' and data['count'] == len(data['rows']) == 25
        assert data['fixtureSha256'] == before == sha(fixture)
        assert all(row['result'] == 'PASS' for row in data['rows'])
    data.update(nativeExit=result.returncode, ffmpegExit=encoded.returncode,
                temporaryWorkRemoved=not root.exists(), runnerSha256=sha(runner),
                driverSha256=sha(__file__),
                nativeSourceSha256=sha(Path(__file__).with_suffix('.cpp')),
                inputUnchanged=True, actualMpvOrDirectShowGraph=False,
                privateHeaderInstrumentation=True)
    (report / 'result.json').write_text(json.dumps(data, indent=2) + '\n')
    print(json.dumps(dict(result='PASS', count=data['count'], checks=data['checks'])))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True, type=Path)
    parser.add_argument('--report-directory', required=True, type=Path)
    args = parser.parse_args()
    run(args.runner.resolve(), args.report_directory.resolve())
