"""Private H.264/AAC packet-copy worker; aligned AAC endings, no public playback."""
import argparse
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile
import traceback

from progressive_stream_remux_worker_test import probe, serve
from progressive_stream_reorder_test import frames, media_prefix, sha
from progressive_mux_mp4_fixture import boxes

MODE = 'continuous-h264-aac-aligned-v1'


def fixture(root, name, rate=48000, channels=1, fps='25', bframes=2,
            audio_first=False, aligned=True, codec='aac'):
    path = root/(name+'.mp4')
    samples = 24*rate//1024*1024 if aligned else 24*rate-137
    args = ['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
            'testsrc2=size=160x90:rate='+fps+':duration=24', '-f', 'lavfi', '-i',
            'sine=sample_rate='+str(rate)+':duration=24', '-map',
            '1:a' if audio_first else '0:v', '-map', '0:v' if audio_first else '1:a',
            '-c:v', 'libx264', '-threads', '2', '-preset', 'veryfast', '-bf', str(bframes),
            '-g', '25', '-sc_threshold', '0', '-c:a', codec, '-ac', str(channels),
            '-af', 'atrim=end_sample='+str(samples), '-video_track_timescale', '60000',
            '-movie_timescale', str(math.lcm(60000, rate)), '-movflags', '+faststart', str(path)]
    subprocess.run(args, check=True, capture_output=True)
    return path, samples


def fragment_durations(raw):
    """Independent ISO sample durations; older ffprobe omits some packet values."""
    defaults, result = {}, {}
    for at, size, kind, header in boxes(raw):
        body = raw[at+header:at+size]
        if kind == b'moov':
            for a, n, k, h in boxes(body):
                if k != b'mvex':
                    continue
                for x, length, tag, head in boxes(body[a+h:a+n]):
                    if tag == b'trex':
                        trex = body[a+h+x+head:a+h+x+length]
                        defaults[int.from_bytes(trex[4:8], 'big')] = int.from_bytes(trex[12:16], 'big')
        if kind != b'moof':
            continue
        for a, n, k, h in boxes(body):
            if k != b'traf':
                continue
            children = body[a+h:a+n]
            parts = [(tag, children[x+head:x+length]) for x, length, tag, head in boxes(children)]
            tfhd = next(value for tag, value in parts if tag == b'tfhd')
            flags, track = int.from_bytes(tfhd[:4], 'big') & 0xffffff, int.from_bytes(tfhd[4:8], 'big')
            pos = 8+(8 if flags & 1 else 0)+(4 if flags & 2 else 0)
            duration = int.from_bytes(tfhd[pos:pos+4], 'big') if flags & 8 else defaults[track]
            for tag, value in parts:
                if tag != b'trun':
                    continue
                f, count = int.from_bytes(value[:4], 'big') & 0xffffff, int.from_bytes(value[4:8], 'big')
                assert 0 < count <= 50000
                pos = 8+(4 if f & 1 else 0)+(4 if f & 4 else 0)
                for _ in range(count):
                    d = int.from_bytes(value[pos:pos+4], 'big') if f & 0x100 else duration
                    assert d > 0
                    result.setdefault(track, []).append(d)
                    pos += sum(4 for flag in (0x100, 0x200, 0x400, 0x800) if f & flag)
                assert pos == len(value)
    return result


def clocks(path):
    p = probe(path)
    durations = fragment_durations(path.read_bytes())
    result = {}
    for s in p['streams']:
        base = Fraction(s['time_base'])
        packets = [x for x in p['packets'] if x['stream_index'] == s['index']]
        table = durations.get(int(s['id'], 0))
        if table is not None:
            assert len(table) == len(packets)
        result[s['codec_type']] = []
        for index, packet in enumerate(packets):
            duration = int(packet['duration']) if 'duration' in packet else table[index]
            if table is not None:
                assert duration == table[index], (duration, table[index])
            result[s['codec_type']].append([packet['data_hash'], str(int(packet['pts'])*base),
                str(int(packet['dts'])*base), str(duration*base)])
    return result


def pcm(path):
    p = subprocess.run(['ffmpeg', '-v', 'error', '-xerror', '-threads', '2', '-i', str(path),
                        '-map', '0:a', '-threads', '2', '-f', 's16le', '-'], capture_output=True)
    assert p.returncode == 0 and not p.stderr, (p.returncode, p.stderr.decode(errors='replace'))
    return p.stdout


def run(worker, report):
    report.mkdir(parents=True, exist_ok=False)
    source_hash = sha(__file__)
    rows = []
    inputs = dict(workerSha256=sha(worker), testSourceSha256=source_hash,
                  helperSourceSha256=sha(Path(__file__).with_name('progressive_stream_remux_worker_test.py')),
                  runtime=json.loads(subprocess.check_output([str(worker), '--info'])),
                  ffmpegVersion=subprocess.check_output(['ffmpeg', '-version']).decode().splitlines()[0],
                  ffprobeVersion=subprocess.check_output(['ffprobe', '-version']).decode().splitlines()[0])
    assert inputs['runtime']['contract']['audio'] is False
    try:
        with tempfile.TemporaryDirectory(prefix='foxicmp-aligned-aac-') as name:
            root = Path(name)
            profiles = [('48000-mono', 48000, 1, '25', 2, False),
                        ('48000-stereo-fractional', 48000, 2, '30000/1001', 3, False),
                        ('44100-mono', 44100, 1, '25', 2, False),
                        ('44100-stereo-fractional', 44100, 2, '30000/1001', 3, False),
                        ('audio-first', 48000, 1, '25', 2, True),
                        ('requested-b16-60', 48000, 1, '60000/1001', 16, False)]
            sources = []
            for label, rate, channels, fps, bframes, audio_first in profiles:
                original, samples = fixture(root, label, rate, channels, fps, bframes, audio_first)
                sources.append(original)
                before = sha(original)
                reference, reference_frames, reference_pcm = clocks(original), frames(original), pcm(original)
                assert len(reference_pcm) == samples*channels*2
                assert samples % 1024 == 0 and Fraction(reference['audio'][0][1]) < 0
                assert Fraction(reference['video'][0][2]) < 0
                partials = []

                def checkpoint(raw, state):
                    content = media_prefix(raw)
                    if not content or state['available'] >= original.stat().st_size:
                        return
                    digest = hashlib.sha256(content).hexdigest()
                    if any(p['sha256'] == digest for p in partials):
                        return
                    path = root/'partial.mp4'
                    path.write_bytes(content)
                    actual, video, audio = clocks(path), frames(path), pcm(path)
                    assert set(actual) == set(reference)
                    for kind in actual:
                        assert 0 < len(actual[kind]) < len(reference[kind])
                        assert actual[kind] == reference[kind][:len(actual[kind])]
                    assert video == reference_frames[:len(video)]
                    assert audio == reference_pcm[:len(audio)]
                    assert state['beforeSourceComplete']
                    partials.append(dict(sha256=digest, videoPackets=len(actual['video']),
                        audioPackets=len(actual['audio']), decodedVideoFrames=len(video),
                        decodedPcmFrames=len(audio)//channels//2, exactPacketClocks=True,
                        exactDecodedVideoPrefix=True, exactDecodedPcmPrefix=True))

                row, data = serve(worker, original, mode=MODE, short_reads=True, on_wait=checkpoint)
                assert row['exitCode'] == 0 and row['result']['code'] == 'data', row
                result = row['result']
                assert result['mode'] == MODE and result['sourceComplete'] and result['sourceEof']
                assert not result['playbackSupported'] and not result['aacEndTrimRequired']
                assert int(result['audioPresentationSamples']) == samples and result['aacInitialSkipSamples'] == 1024
                assert result['audioPackets'] == len(reference['audio']) and result['videoPackets'] == len(reference['video'])
                output = root/(label+'-output.mp4')
                output.write_bytes(data)
                assert clocks(output) == reference and frames(output) == reference_frames
                assert pcm(output) == reference_pcm
                assert len(partials) >= 3 and row['firstCompleteFragmentBeforeSourceComplete']
                assert sha(original) == before and sha(output) == result['sha256']
                row.update(profile=label, sampleRate=rate, channels=channels, fps=fps,
                           requestedBFrames=bframes, sourceSha256=before, audioFirst=audio_first,
                           pcmFrames=samples, pcmSha256=hashlib.sha256(reference_pcm).hexdigest(),
                           videoFrames=len(reference_frames), exactPayloadPtsDtsDuration=True,
                           exactDecodedVideo=True, exactDecodedPcmWithoutTrimFilters=True,
                           partialProofs=partials)
                rows.append(row)
                (report/'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in rows])))
            for label, rate, codec, expected in [('tail-44100', 44100, 'aac', 'unsupported-stream-aac-end-trim'),
                    ('tail-48000', 48000, 'aac', 'unsupported-stream-aac-end-trim'),
                    ('rate-32000', 32000, 'aac', 'unsupported-stream-aac-config'),
                    ('non-aac', 48000, 'libmp3lame', 'unsupported-stream-codec')]:
                path, _ = fixture(root, label, rate, aligned=False, codec=codec)
                row, data = serve(worker, path, mode=MODE, growing=False)
                assert row['exitCode'] == 1 and row['result']['code'] == expected and not data, row
                row['profile'] = label
                rows.append(row)
            for fault, expected in [('cancel-wait', 'cancelled'), ('cancel-write', 'cancelled'),
                    ('unconfirmed-eof', 'unconfirmed-eof'), ('integrity', 'integrity-mismatch'), ('kill-wait', None)]:
                row, _ = serve(worker, sources[0], mode=MODE, fault=fault)
                assert expected is None or (row['exitCode'] == 1 and row['result']['code'] == expected
                                            and not row['result']['sourceComplete']), row
                row['profile'] = fault
                rows.append(row)
            for old_mode in ('continuous-h264-video-v1', 'continuous-h264-reordered-video-v1'):
                row, data = serve(worker, sources[0], mode=old_mode, growing=False)
                assert row['exitCode'] == 1 and row['result']['code'] == 'unsupported-stream-count' and not data, row
                row['profile'] = old_mode+'-still-rejects-audio'
                rows.append(row)
            assert sha(__file__) == source_hash
        result = dict(result='PASS', **inputs, count=len(rows), rows=rows, privateMode=MODE,
                      engineOnly=True, playerPlaybackTested=False, parentAndCacheWired=False,
                      publicAudioCapability=False, nonAlignedAacSupported=False,
                      temporaryWorkRemoved=not root.exists())
        (report/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        return result
    except BaseException:
        (report/'failure.json').write_text(json.dumps(dict(rows=rows, error=traceback.format_exc()), indent=2)+'\n')
        raise


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--worker', type=Path, required=True)
    p.add_argument('--report-directory', type=Path, required=True)
    a = p.parse_args()
    r = run(a.worker.resolve(), a.report_directory.resolve())
    print(json.dumps(dict(result=r['result'], count=r['count'])))
