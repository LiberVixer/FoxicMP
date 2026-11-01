"""PCM oracle for a future MP3 output adapter; does not claim player gapless support.

Reads the production indexed-MP3 projection via its decoder API, then compares
an absolute-sample window with native FFmpeg MP3 output. No audio device, user
profile, network source or Harvester is used. Decoder delay is an explicit
FFmpeg-specific input, not a universal MP3 constant.
"""
import argparse
import hashlib
import json
import platform
import subprocess
import tempfile
from pathlib import Path

from progressive_mp3_indexed_fixture import PROFILES, fixture, frames
from progressive_standalone_fixture import publish
from progressive_mux_ts_conformance import Rpc


def digest(data):
    return hashlib.sha256(data).hexdigest()


def decode(path=None, raw=None):
    args = ['ffmpeg', '-v', 'error']
    if raw is not None:
        args += ['-f', 'mp3']
    args += ['-i', str(path) if raw is None else 'pipe:0', '-map', '0:a:0',
             '-c:a', 'pcm_s16le', '-f', 's16le', '-']
    return subprocess.check_output(args, input=raw, stderr=subprocess.PIPE)


def window(coded_samples, delay, padding, decoder_delay):
    """Half-open decoded PCM interval, under the tested decoder-delay contract."""
    if (any(type(n) is not int or n < 0
            for n in (coded_samples, delay, padding, decoder_delay))
            or delay > 4095 or padding > 4095 or padding < decoder_delay
            or delay + padding >= coded_samples):
        raise ValueError('No proven sample window for these metadata/decoder inputs')
    return delay + decoder_delay, coded_samples - padding + decoder_delay


def select_chunks(pcm, frame_bytes, first, end, origin, sizes):
    """Candidate model only: clip absolute decoded positions, never reset on update."""
    assert len(pcm) % frame_bytes == 0 and sizes and all(n > 0 for n in sizes)
    result = bytearray()
    position = 0
    count = len(pcm) // frame_bytes
    index = 0
    while position < count:
        stop = min(count, position + sizes[index % len(sizes)])
        start_in = max(position, first - origin)
        stop_in = min(stop, end - origin)
        if start_in < stop_in:
            result += pcm[start_in * frame_bytes:stop_in * frame_bytes]
        position = stop
        index += 1
    return bytes(result)


def project(runner, root, model, raw):
    data = root / 'data'
    data.mkdir()
    rpc = Rpc([runner, '--registry', str(root / 'registry')])
    try:
        value = publish(model, raw, data, len(raw), complete=True)
        answer = rpc('open', session=value, directory=str(data))
        assert answer['ok'], answer
        status = rpc('status')
        assert status['mp3Index']['gaplessTrimApplied'] is False
        size = int(rpc('layout')['tracks'][0]['available'])
        output = bytearray()
        for at in range(0, size, 65536):
            r = rpc('read', decoder=True, track=0, offset=str(at),
                    length=str(min(65536, size - at)))
            assert r['bytesRead'] == min(65536, size - at), r
            output += bytes.fromhex(r['hex'])
        rpc('release')
        readers = rpc('readers', version=1, sessionId=model['sessionId'], generation='1')
        assert not readers['readers'], readers
        return bytes(output)
    finally:
        rpc.close()


def run(runner, mpv_runner=None):
    checks, rows = [], []
    # VBRI does not carry the LAME delay/padding contract and is not guessed.
    profiles = [p for p in PROFILES if '-vbri-' not in p]
    for profile in profiles:
        with tempfile.TemporaryDirectory(prefix='foxicmp-mp3-gapless-') as temp:
            root = Path(temp)
            model, raw, original, probe = fixture(root, profile, 4)
            projected = root / 'projection.mp4'
            projected.write_bytes(project(runner, root, model, raw))
            coded_pcm, native_pcm = decode(projected), decode(original)
            rate = int(probe['streams'][0]['sample_rate'])
            channels = int(probe['streams'][0]['channels'])
            frame_bytes = channels * 2
            coded_samples = probe['_index']['frames'] * 1152
            first, end = window(coded_samples, probe['_index']['delay'],
                                probe['_index']['padding'], 529)
            assert len(coded_pcm) == coded_samples * frame_bytes
            assert len(native_pcm) == (end - first) * frame_bytes
            assert coded_pcm[first * frame_bytes:end * frame_bytes] == native_pcm
            checks.append(profile + '-native-sample-window-exact')
            for label, sizes in [('one-sample', (1,)), ('frame', (1152,)),
                                 ('irregular-update', (1, 528, 529, 1105, 17, 4096, 3))]:
                assert select_chunks(coded_pcm, frame_bytes, first, end, 0, sizes) == native_pcm
                checks.append(profile + '-' + label + '-partition-invariant')
            seek_rows = []
            packets = frames(raw)[1:]
            # Includes near-start, non-frame-aligned and near-end audible targets.
            for target in (0, 1, 1151, rate * 2 + 1, end - first - 1, end - first):
                absolute = first + target
                start_frame = max(0, (absolute - rate // 4) // 1152)
                at = packets[start_frame][0]
                stop = packets[-1][0] + packets[-1][1]
                restarted = decode(raw=raw[at:stop])
                origin = start_frame * 1152
                # An omitted reservoir-dependent frame must not silently shift PCM.
                assert len(restarted) == (coded_samples - origin) * frame_bytes
                selected = select_chunks(restarted, frame_bytes, absolute, end,
                                         origin, (7, 1152, 1, 4096))
                expected = native_pcm[target * frame_bytes:]
                assert selected == expected, (profile, target, len(selected), len(expected))
                seek_rows.append(dict(targetSample=target, decodeStartFrame=start_frame,
                                      outputSamples=len(selected) // frame_bytes,
                                      pcmSha256=digest(selected)))
                checks.append(profile + '-preroll-seek-' + str(target) + '-exact')
            mpv_result = None
            if mpv_runner:
                outputs = []
                engine_identity = None
                for name, source in [('coded', projected), ('native', original)]:
                    destination = root / (name + '.pcm')
                    captured = subprocess.run([mpv_runner, str(source), str(destination),
                                               str(rate), str(channels)], check=True,
                                              capture_output=True, timeout=40)
                    identity = captured.stdout.decode('utf-8').strip().splitlines()
                    assert len(identity) == 2, identity
                    if engine_identity is not None:
                        assert engine_identity == identity
                    engine_identity = identity
                    outputs.append(destination.read_bytes())
                coded_output, native_output = outputs
                assert len(coded_output) == coded_samples * frame_bytes
                assert len(native_output) == (end - first) * frame_bytes
                assert coded_output[first * frame_bytes:end * frame_bytes] == native_output
                adapter_rows = []
                for target in (0, 1, rate * 2 + 1, end - first - 1):
                    absolute = first + target
                    start_frame = max(0, (absolute - rate // 4) // 1152)
                    destination = root / ('filtered-' + str(target) + '.pcm')
                    subprocess.run([mpv_runner, str(projected), str(destination),
                                    str(rate), str(channels), str(absolute), str(end),
                                    format(start_frame * 1152 / rate, '.17g')],
                                   check=True, capture_output=True, timeout=40)
                    selected_output = destination.read_bytes()
                    expected_output = native_output[target * frame_bytes:]
                    assert selected_output == expected_output, (
                        profile, 'libmpv-filter', target, len(selected_output), len(expected_output))
                    adapter_rows.append(dict(targetSample=target,
                                             outputSamples=len(selected_output) // frame_bytes,
                                             pcmSha256=digest(selected_output)))
                    checks.append(profile + '-libmpv-atrim-seek-' + str(target) + '-exact')
                mpv_result = dict(nativeSamples=len(native_output) // frame_bytes,
                                  codedSamples=len(coded_output) // frame_bytes,
                                  nativePcmSha256=digest(native_output),
                                  windowExact=True, audioDeviceOpened=False,
                                  scope='libmpv PCM file output, not FoxiqMP UI/controller',
                                  engineIdentity=engine_identity,
                                  filter='lavfi atrim with absolute start_pts/end_pts',
                                  filteredSeeks=adapter_rows)
                checks.append(profile + '-libmpv-pcm-output-window-exact')
            assert original.read_bytes() == raw
            rows.append(dict(profile=profile, sampleRate=rate, channels=channels,
                             fixtureSha256=digest(raw),
                             codedSamples=coded_samples, encoderDelay=probe['_index']['delay'],
                             encoderPadding=probe['_index']['padding'], decoderDelay=529,
                             audibleStartSample=first, audibleEndSample=end,
                             audibleSamples=end-first, nativePcmSha256=digest(native_pcm),
                             projectionPcmSha256=digest(coded_pcm), seeks=seek_rows,
                             libmpv=mpv_result))
    for values in [(0, 0, 0, 529), (1152, 576, 576, 529),
                   (4608, 576, 528, 529), (4608, -1, 1200, 529),
                   (4608, 576, 4096, 529), (4608, 4096, 576, 529),
                   (4608, 576, 1200, -1), (4608, True, 1200, 529)]:
        try:
            window(*values)
        except ValueError:
            checks.append('unproven-window-rejected-' + '-'.join(map(str, values)))
        else:
            raise AssertionError(values)
    return dict(result='PASS', count=len(checks), checks=checks, profiles=rows,
                scope='offline PCM oracle and candidate sample-window model; not player output',
                playerGaplessImplemented=False, originalFilesModified=False,
                platform=platform.system(),
                ffmpegVersion=subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True)
    parser.add_argument('--mpv-runner', help='Optional offline libmpv PCM probe')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = run(args.runner, args.mpv_runner)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))
