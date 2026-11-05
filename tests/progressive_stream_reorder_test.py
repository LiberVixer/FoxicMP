"""Private worker-only H.264 reorder admission; public v1 stays video/no-B only."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import traceback

from progressive_stream_remux_worker_test import fixture, probe, serve, verify

MODE = 'continuous-h264-reordered-video-v1'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def projection(path):
    value = probe(path)
    assert len(value['streams']) == 1 and value['streams'][0]['codec_type'] == 'video'
    base = Fraction(value['streams'][0]['time_base'])
    return [[p['data_hash'], *[str(int(p[k]) * base) for k in ('pts', 'dts', 'duration')]]
            for p in value['packets']]


def frames(path):
    result = subprocess.run(['ffmpeg', '-v', 'error', '-xerror', '-err_detect', 'explode',
                             '-threads', '2', '-i', str(path), '-map', '0:v',
                             '-threads', '2', '-fps_mode', 'passthrough', '-f', 'framemd5', '-'],
                            capture_output=True)
    assert result.returncode == 0, (result.returncode, result.stderr.decode(errors='replace'))
    assert not result.stderr, result.stderr.decode(errors='replace')
    return [line.rsplit(b',', 1)[-1].strip().decode() for line in result.stdout.splitlines()
            if line and not line.startswith(b'#')]


def media_prefix(raw):
    """Only expose a prefix ending at a complete moof/mdat pair, never a partial box."""
    at = end = 0
    moof = False
    while at + 8 <= len(raw):
        size = int.from_bytes(raw[at:at+4], 'big')
        kind = raw[at+4:at+8]
        header = 8
        if size == 1:
            if at + 16 > len(raw):
                break
            size = int.from_bytes(raw[at+8:at+16], 'big')
            header = 16
        if size < header or at + size > len(raw):
            break
        if kind == b'moof':
            assert not moof
            moof = True
        elif kind == b'mdat' and moof:
            end = at + size
            moof = False
        at += size
    return raw[:end]


def run(worker, report):
    report.mkdir(parents=True, exist_ok=False)
    source_sha = sha(__file__)
    helper = Path(__file__).with_name('progressive_stream_remux_worker_test.py')
    inputs = dict(testSourceSha256=source_sha, helperSourceSha256=sha(helper),
                  workerSha256=sha(worker), mode=MODE,
                  runtime=json.loads(subprocess.check_output([str(worker), '--info'])),
                  ffmpegVersion=subprocess.check_output(['ffmpeg', '-version']).decode().splitlines()[0])
    (report/'inputs.json').write_text(json.dumps(inputs, indent=2)+'\n')
    rows = []
    try:
        with tempfile.TemporaryDirectory(prefix='foxicmp-stream-reorder-') as directory:
            root = Path(directory)
            profiles = [('b2-25', '25', 2), ('b3-fractional', '30000/1001', 3),
                        ('b16-60', '60000/1001', 16), ('b2-vfr', '25', 2)]
            sources = []
            for name, rate, reorder in profiles:
                source = fixture(root, name, rate=rate, bframes=reorder)
                if name.endswith('vfr'):
                    original = root/'before-vfr.mp4'
                    source.rename(original)
                    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(original),
                                    '-vf', r'select=not(eq(mod(n\,5)\,4))', '-fps_mode', 'vfr',
                                    '-c:v', 'libx264', '-threads', '2', '-preset', 'veryfast',
                                    '-bf', '2', '-g', '25', '-sc_threshold', '0',
                                    '-movflags', '+faststart', str(source)], capture_output=True, check=True)
                sources.append(source)
                source_hash = sha(source)
                reference = projection(source)
                reference_frames = frames(source)
                assert reference[0][1] == '0' and Fraction(reference[0][2]) < 0
                assert any(Fraction(reference[i][1]) < Fraction(reference[i-1][1])
                           for i in range(1, len(reference))), 'Fixture lacks actual PTS reorder'
                if name.endswith('vfr'):
                    display = sorted(Fraction(p[1]) for p in reference)
                    assert len({b-a for a, b in zip(display, display[1:])}) > 1
                partials = []

                def checkpoint(raw, state):
                    raw = media_prefix(raw)
                    digest = hashlib.sha256(raw).hexdigest()
                    if (not raw or state['available'] >= source.stat().st_size
                            or any(p['sha256'] == digest for p in partials)):
                        return
                    assert state['beforeSourceComplete'] and state['available'] < source.stat().st_size
                    target = root/(name+'-partial.mp4')
                    target.write_bytes(raw)
                    actual = projection(target)
                    assert actual == reference[:len(actual)] and 0 < len(actual) < len(reference)
                    decoded = frames(target)
                    assert decoded == reference_frames[:len(decoded)] and len(decoded) == len(actual)
                    partials.append(dict(packets=len(actual), decodedFrames=len(decoded),
                                         sourceAvailable=state['available'], outputBytes=len(raw), sha256=digest,
                                         exactPayloadPtsDtsDuration=True, exactDecodedPrefix=True))

                row, data = serve(worker, source, short_reads=True, mode=MODE, on_wait=checkpoint)
                assert row['exitCode'] == 0 and row['result']['code'] == 'data', row
                assert row['result']['mode'] == MODE and not row['result']['playbackSupported']
                assert row['result']['sourceComplete'] and row['result']['sourceEof']
                assert not row['result']['transcoded'] and len(row['waits']) >= 4 and len(partials) >= 3
                assert row['firstCompleteFragmentBeforeSourceComplete']
                output = root/(name+'-output.mp4')
                output.write_bytes(data)
                actual = projection(output)
                assert actual == reference, 'Packet payload or rational PTS/DTS/duration changed'
                assert frames(output) == reference_frames
                assert row['result']['sha256'] == sha(output)
                assert int(row['result']['bytesWritten']) == len(data)
                row.update(profile=name, sourceSha256=source_hash, partialProofs=partials,
                           referencePackets=len(reference), decodedFrames=len(reference_frames),
                           exactRationalPacketClocks=True, **verify(source, output))
                assert sha(source) == source_hash
                (report/(name+'-packets.json')).write_text(json.dumps(reference)+'\n')
                rows.append(row)
                (report/'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in rows])))

            for fault, expected in [('cancel-wait', 'cancelled'), ('cancel-write', 'cancelled'),
                                    ('unconfirmed-eof', 'unconfirmed-eof'),
                                    ('integrity', 'integrity-mismatch'), ('kill-wait', None)]:
                row, _ = serve(worker, sources[0], fault=fault, mode=MODE)
                assert expected is None or (row['exitCode'] == 1 and row['result']['code'] == expected
                                             and not row['result']['sourceComplete']), row
                row['profile'] = fault
                rows.append(row)
            for name, source, mode, expected in [
                    ('legacy-reorder-rejected', sources[0], 'continuous-h264-video-v1', 'unsupported-stream-reorder'),
                    ('audio-rejected', fixture(root, 'audio', bframes=2, audio=True), MODE, 'unsupported-stream-count')]:
                row, data = serve(worker, source, growing=False, mode=mode)
                assert row['exitCode'] == 1 and row['result']['code'] == expected and not data, row
                row['profile'] = name
                rows.append(row)
            assert sha(__file__) == source_sha and sha(helper) == inputs['helperSourceSha256']
        result = dict(result='PASS', **inputs, rows=rows, count=len(rows), engineOnly=True,
                      playerPlaybackTested=False, audioSupported=False, publicCapabilitiesChanged=False,
                      temporaryWorkRemoved=not root.exists())
        (report/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        return result
    except BaseException:
        (report/'failure.json').write_text(json.dumps(dict(completedRows=rows, error=traceback.format_exc()), indent=2)+'\n')
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    run(args.worker.resolve(), args.report_directory.resolve())
