"""HEVC continuous worker: growing fragments, exact packets/clocks/pixels and errors."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from progressive_hevc_remux_preflight import PROFILES
from progressive_stream_remux_worker_test import probe, serve
from progressive_stream_reorder_test import media_prefix

MODE = 'continuous-hevc-reordered-video-v1'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fixture(root, name, rate='25', pix_fmt='yuv420p', tag='hvc1', repeat=False):
    path = Path(root) / (name + '.mp4')
    subprocess.run([
        'ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
        f'testsrc2=size=160x90:rate={rate}:duration=24', '-an',
        '-c:v', 'libx265', '-threads', '1', '-filter_threads', '1',
        '-preset', 'fast', '-pix_fmt', pix_fmt, '-tag:v', tag,
        '-x265-params', 'pools=1:frame-threads=1:log-level=error:'
        'keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes=2'
        + (':repeat-headers=1' if repeat else ''),
        '-movflags', '+faststart', str(path),
    ], capture_output=True, check=True, timeout=90)
    return path


def packets(path):
    value = probe(path)
    assert len(value['streams']) == 1 and value['streams'][0]['codec_name'] == 'hevc'
    clock = Fraction(value['streams'][0]['time_base'])
    return [[p['data_hash'], *[str(int(p[k]) * clock) for k in ('pts', 'dts', 'duration')]]
            for p in value['packets']]


def frames(path, pix_fmt):
    result = subprocess.run([
        'ffmpeg', '-v', 'error', '-xerror', '-err_detect', 'explode', '-threads', '1',
        '-i', str(path), '-map', '0:v:0', '-threads', '1', '-pix_fmt', pix_fmt,
        '-fps_mode', 'passthrough', '-f', 'framemd5', '-',
    ], capture_output=True, timeout=90)
    assert result.returncode == 0 and not result.stderr, (result.returncode, result.stderr)
    return [line.rsplit(b',', 1)[-1].strip().decode() for line in result.stdout.splitlines()
            if line and not line.startswith(b'#')]


def run(worker, report):
    report = Path(report).resolve()
    report.mkdir(parents=True, exist_ok=True)
    rows = []
    info = json.loads(subprocess.check_output([str(worker), '--info']))
    contract = info['contract']['profiles'][MODE]
    assert contract['maxParameterSetsPerType'] == 1 and contract['firstPicture'] == 'irap'
    with tempfile.TemporaryDirectory(prefix='foxicmp-hevc-stream-') as temporary:
        root = Path(temporary)
        sources = []
        for name, rate, pix_fmt, tag in PROFILES:
            source = fixture(root, name, rate, pix_fmt, tag)
            sources.append(source)
            before = sha(source)
            reference, decoded = packets(source), frames(source, pix_fmt)
            partials = []

            def checkpoint(raw, state):
                raw = media_prefix(raw)
                if not raw or state['available'] >= source.stat().st_size:
                    return
                digest = hashlib.sha256(raw).hexdigest()
                if any(p['sha256'] == digest for p in partials):
                    return
                path = root / (name + '-partial.mp4')
                path.write_bytes(raw)
                actual, pixels = packets(path), frames(path, pix_fmt)
                assert 0 < len(actual) < len(reference) and actual == reference[:len(actual)]
                assert pixels == decoded[:len(pixels)] and len(pixels) == len(actual)
                partials.append(dict(packets=len(actual), decodedFrames=len(pixels),
                                     sourceAvailable=state['available'], outputBytes=len(raw),
                                     sha256=digest, exactPacketClocks=True, exactDecodedPrefix=True))

            row, data = serve(worker, source, mode=MODE, short_reads=True, on_wait=checkpoint)
            assert row['exitCode'] == 0 and row['result']['code'] == 'data', row
            assert row['result']['mode'] == MODE and row['result']['sourceComplete']
            assert row['result']['sourceEof'] and not row['result']['transcoded']
            assert row['firstCompleteFragmentBeforeSourceComplete'] and len(partials) >= 3, row
            output = root / (name + '-output.mp4')
            output.write_bytes(data)
            stream = probe(output)['streams'][0]
            assert stream['codec_tag_string'] == tag and stream['pix_fmt'] == pix_fmt
            assert packets(output) == reference and frames(output, pix_fmt) == decoded
            assert sha(source) == before and sha(output) == row['result']['sha256']
            row.update(profile=name, sourceSha256=before, pixelFormat=pix_fmt,
                       exactRationalPacketClocks=True, exactDecodedFrames=True, partials=partials)
            rows.append(row)
            (report / 'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in rows])))
        for legacy in ('continuous-h264-video-v1', 'continuous-h264-reordered-video-v1'):
            row, data = serve(worker, sources[0], growing=False, mode=legacy)
            assert row['exitCode'] == 1 and row['result']['code'] == 'unsupported-stream-codec' and not data
            row.update(profile='unchanged-' + legacy)
            rows.append(row)
        for fault, expected in [('cancel-wait', 'cancelled'), ('cancel-write', 'cancelled'),
                                ('unconfirmed-eof', 'unconfirmed-eof'), ('integrity', 'integrity-mismatch')]:
            row, _ = serve(worker, sources[0], mode=MODE, fault=fault)
            assert row['exitCode'] == 1 and row['result']['code'] == expected, row
            row.update(profile=fault)
            rows.append(row)
        raw = sources[0].read_bytes()
        config = raw.index(b'hvcC') + 4
        for name, at, value in [('nal-length', config + 21, raw[config+21] & ~3),
                                ('chroma', config + 16, raw[config+16] | 2),
                                ('reserved', config + 13, raw[config+13] & 15)]:
            mutated = bytearray(raw)
            mutated[at] = value
            source = root / (name + '.mp4')
            source.write_bytes(mutated)
            row, data = serve(worker, source, growing=False, mode=MODE)
            assert row['exitCode'] == 1 and row['result']['code'] == 'unsupported-stream-hevc-configuration' and not data, row
            row.update(profile='reject-' + name)
            rows.append(row)
        first = probe(sources[0])['packets'][0]
        truncated = root / 'truncated-at-packet.mp4'
        truncated.write_bytes(raw[:int(probe(sources[0])['packets'][-1]['pos'])])
        row, _ = serve(worker, truncated, growing=False, mode=MODE)
        assert row['exitCode'] == 1 and row['result']['code'] == 'incomplete-stream-hevc-samples', row
        assert not row['result']['sourceComplete']
        row.update(profile='reject-complete-truncated-at-packet-boundary')
        rows.append(row)
        for name, kind in [('packet-nal-length', 'length'), ('first-picture', 'irap')]:
            mutated = bytearray(raw)
            at, end = int(first['pos']), int(first['pos'])+int(first['size'])
            while at < end:
                length = int.from_bytes(mutated[at:at+4], 'big')
                assert 2 <= length <= end-at-4
                if (mutated[at+4] >> 1) & 63 <= 21:
                    if kind == 'length':
                        mutated[at:at+4] = (0x7fffffff).to_bytes(4, 'big')
                    else:
                        mutated[at+4] &= 0x81
                    break
                at += 4+length
            assert at < end
            source = root / (name+'.mp4')
            source.write_bytes(mutated)
            row, _ = serve(worker, source, growing=False, mode=MODE)
            assert row['exitCode'] == 1 and row['result']['code'] == 'unsupported-stream-hevc-packet', row
            row.update(profile='reject-'+name)
            rows.append(row)
        repeated = fixture(root, 'repeat-headers', tag='hev1', repeat=True)
        row, data = serve(worker, repeated, growing=False, mode=MODE)
        assert row['exitCode'] == 0, row
        output = root / 'repeat-output.mp4'
        output.write_bytes(data)
        assert packets(output) == packets(repeated)
        row.update(profile='unchanged-in-band-parameter-sets')
        rows.append(row)
        repeated_raw = bytearray(repeated.read_bytes())
        first = probe(repeated)['packets'][0]
        at, end = int(first['pos']), int(first['pos']) + int(first['size'])
        found = False
        while at < end:
            size = int.from_bytes(repeated_raw[at:at+4], 'big')
            at += 4
            assert 2 <= size <= end-at
            if (repeated_raw[at] >> 1) & 63 == 34:
                repeated_raw[at+size-1] ^= 1
                found = True
                break
            at += size
        assert found, 'repeat-headers fixture lacks in-band PPS'
        repeated.write_bytes(repeated_raw)
        row, _ = serve(worker, repeated, growing=False, mode=MODE)
        assert row['exitCode'] == 1 and row['result']['code'] == 'unsupported-stream-configuration-change', row
        row.update(profile='reject-changed-in-band-pps')
        rows.append(row)
    result = dict(result='PASS', mode=MODE, count=len(rows), rows=rows,
                  workerSha256=sha(worker), testSourceSha256=sha(__file__),
                  publicPlayerTested=False, workerInfo=info)
    (report / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--worker', required=True)
    parser.add_argument('--report-directory', required=True)
    args = parser.parse_args()
    result = run(Path(args.worker).resolve(), args.report_directory)
    print(json.dumps(dict(result=result['result'], count=result['count'])))
