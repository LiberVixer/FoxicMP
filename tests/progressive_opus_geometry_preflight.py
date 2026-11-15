"""Variable Opus packet durations: preparation, not progressive playback.

The study sees confirmed byte chunks. The file decoder sees a completed source;
its success must never be reported as new growing/ordinary-main support.
"""
import argparse
from array import array
import hashlib
import json
from pathlib import Path
import struct
import traceback

from progressive_audio_count_preflight import rewrite_audio
from progressive_audio_count_prototype import packet_samples
from progressive_opus_geometry_prototype import OpusGeometryPrototype
from progressive_simple_vfr_preflight import head_without_default, decorate
from progressive_simple_vfr_prototype import Invalid
from progressive_stream_remux_worker_test import serve
from progressive_webm_fixtures import width
from progressive_webm_opus_remux_preflight import configuration, padding_map, packets, pcm, probe
from progressive_webm_remux_preflight import scan, sha, command, ok, decoded
from progressive_webm_vfr_preflight import ebml, unsigned

PROFILES = [('fixed-'+ms, ms, 1 if i % 2 == 0 else 2)
            for i, ms in enumerate(('2.5', '5', '10', '20', '40', '60', '80', '100', '120'))]
PROFILES += [('mixed-mono', 'mixed', 1), ('mixed-stereo', 'mixed', 2)]
MODE = 'continuous-webm-vp9-opus-simple-vfr-derived-av-v1'
END = 120000000
SAMPLES = 575983


def repacketize(raw):
    """Build a new test source from consecutive single-frame 20ms Opus packets.

    This happens before hashes/oracles. Compressed frames are unmodified, but
    packet boundaries deliberately change; no product code uses this generator.
    """
    layout = scan(raw);audio = configuration(raw)[2]['number'];pads = padding_map(raw, layout)
    sound = [b for b in layout['blocks'] if b['track'] == audio]
    rows = [(b['ticks'], b['track'], raw[b['payload']:b['end']], b['flags'], 0)
            for b in layout['blocks'] if b['track'] != audio]
    at = 0;pattern = (1, 2, 3, 4, 5, 6);turn = 0
    while at < len(sound):
        count = min(pattern[turn % len(pattern)], len(sound)-at);turn += 1
        group = sound[at:at+count];data = [raw[b['payload']:b['end']] for b in group]
        assert all((p[0] & 3) == 0 and p[0] == data[0][0] and packet_samples(p) == 960 for p in data)
        assert not any(pads.get(b['offset'], 0) for b in group[:-1])
        payload = data[0]
        if count > 1:
            lengths = bytearray()
            for p in data[:-1]:
                n = len(p)-1;assert n <= 1275
                lengths += bytes([n]) if n < 252 else bytes([252+(n & 3), (n-252)//4])
            payload = bytes([(data[0][0] & 0xfc) | 3, 0x80 | count])+lengths+b''.join(p[1:] for p in data)
        assert packet_samples(payload) == count*960
        rows.append((group[0]['ticks'], audio, payload, 0x80, pads.get(group[-1]['offset'], 0)))
        at += count
    output = bytearray(raw[:layout['headBytes']]);body = bytearray();clock = None
    for ticks, track, data, flags, padding in sorted(rows, key=lambda r: (r[0], r[1])):
        if clock is None or ticks-clock >= 1000:
            if body:output += ebml(0x1f43b675, body)
            clock = ticks;body = bytearray(unsigned(0xe7, clock))
        assert track < 127
        block = bytes([0x80 | track])+(ticks-clock).to_bytes(2, 'big', signed=True)+bytes([flags])+data
        body += (ebml(0xa0, ebml(0xa1, block)+ebml(0x75a2, padding.to_bytes(8, 'big', signed=True)))
                 if padding else ebml(0xa3, block))
    return bytes(output+ebml(0x1f43b675, body))


def fixture(folder, profile, video):
    name, ms, channels = profile;folder.mkdir(parents=True, exist_ok=True)
    source = folder/'encoded.webm'
    ok(['ffmpeg', '-v', 'error', '-y', '-i', video, '-f', 'lavfi', '-i',
        'sine=frequency=997:sample_rate=48000:duration=12', '-map', '0:v', '-map', '1:a',
        '-c:v', 'copy', '-c:a', 'libopus', '-ac', str(channels), '-frame_duration',
        '20' if ms == 'mixed' else ms, '-application', 'audio', '-af',
        'atrim=end_sample='+str(SAMPLES), '-cluster_time_limit', '1000', '-live', '1', source],
        folder, 'encode-mux')
    raw = head_without_default(source.read_bytes())
    if ms == 'mixed':raw = repacketize(raw)
    source = folder/'source.webm';source.write_bytes(raw)
    return source


def conversion(raw, chunk, **limits):
    p = OpusGeometryPrototype(len(raw), 300, END, **limits);output = bytearray();safe = 0
    for at in range(0, len(raw), chunk):
        output += p.feed(raw[at:at+chunk])
        assert not p.audio_sample_count_final and p.audio_samples is None
        assert safe <= p.indexed_audio_samples == max(0, p.decoded_samples-p.last_duration-p.pre_skip)
        safe = p.indexed_audio_samples
        assert p.at <= p.supplied <= len(raw)
    assert p.feed() == b'' and p.waiting == 'source-complete'
    output += p.feed(complete=True)
    assert p.audio_sample_count_final and p.audio_samples >= safe
    return bytes(output), p, dict(chunk=chunk, indexedBeforeEof=safe,
        audioCountFinalOnlyAfterWriterEof=True, confirmedBytesOnly=True,
        peakInputBufferBytes=p.peak_input, peakPendingBytes=p.peak_pending)


def negatives(raw):
    rows = []
    def refuse(label, value, **limits):
        try:conversion(value, 4093, **limits)
        except (Invalid, ValueError, AssertionError) as error:
            # Assertions in the harness are not parser refusals.
            if isinstance(error, AssertionError):raise
            rows.append(dict(case=label, reason=str(error)));return
        raise AssertionError(('Unexpected admission', label))
    def trim(block, value):
        return ebml(0xa0, ebml(0xa1, block)+ebml(0x75a2, value.to_bytes(8, 'big', signed=True)))
    for name, value in [('negative-trim', -20833), ('fractional-trim', 1), ('excessive-trim', 120000000)]:
        refuse(name, rewrite_audio(raw, lambda i, b, original: trim(b, value) if i == 0 else original))
    refuse('audio-after-trim', rewrite_audio(raw, lambda i, b, original: trim(b, 20833) if i == 0 else original))
    def zero_clock(i, b, original):
        if i != 1:return original
        value = bytearray(b);w = width(value[0]);value[w:w+2] = b'\0\0'
        return ebml(0xa3, value)
    refuse('regressing-clock', rewrite_audio(raw, zero_clock))
    refuse('packet-budget', raw, packet_limit=2)
    refuse('sample-budget', raw, sample_limit=1000)
    p = OpusGeometryPrototype(len(raw), 300, END)
    for at in range(0, len(raw)-1, 4093):p.feed(raw[at:min(at+4093, len(raw)-1)])
    try:p.feed(complete=True)
    except Invalid as error:rows.append(dict(case='premature-writer-eof', reason=str(error)))
    else:raise AssertionError('Premature writer EOF accepted')
    return rows


def file_decoder(runner, edition, source, channels, tail, folder):
    output = folder/'decoder.pcm'
    args = ([runner, source, output, '48000', str(channels)] if edition == 'qt' else
            [runner, source, output, str(channels)])
    p = command(args, folder, 'file-decoder')
    assert p.returncode == 0, (p.returncode, p.stdout[-500:], p.stderr[-1000:])
    status = json.loads(p.stdout.strip().splitlines()[-1])
    fmt, code, typ, stride, limit = (('s16le', 'pcm_s16le', 'h', channels*2, 2)
                                   if edition == 'qt' else ('f32le', 'pcm_f32le', 'f', channels*4, 2e-6))
    reference = ok(['ffmpeg', '-v', 'error', '-threads', '1', '-i', source, '-map', '0:a:0',
                    '-threads', '1', '-c:a', code, '-f', fmt, '-'], folder, 'file-decoder-reference')
    (folder/'file-decoder-reference.stdout').unlink()
    actual = output.read_bytes();assert len(reference) == SAMPLES*stride
    # The ordinary Classic Matroska path currently ignores DiscardPadding.
    # Record its exact excess, rather than relabeling this as precise playback.
    expected_count = SAMPLES+(tail if edition == 'classic' else 0)
    assert len(actual) == expected_count*stride, (len(actual)//stride, expected_count)
    a, b = array(typ), array(typ);a.frombytes(actual[:len(reference)]);b.frombytes(reference)
    error = max(abs(x-y) for x, y in zip(a, b));assert error <= limit, error
    output.unlink()
    return dict(exitCode=p.returncode, status=status, samples=len(actual)//stride, expectedPresentationSamples=SAMPLES,
        exactSampleCount=len(actual)==len(reference), untrimmedTailSamples=(len(actual)-len(reference))//stride,
        classicDiscardPaddingGap=edition=='classic' and tail>0, format=fmt,
        maxAbsolutePcmError=error, audioDeviceOpened=False, completedFileOnly=True,
        progressivePlaybackTested=False, pcmSha256=hashlib.sha256(actual).hexdigest())


def native_controls(runner, folder):
    folder.mkdir(parents=True, exist_ok=True)
    valid = [bytes([config << 3]) for config in range(32)]
    valid += [b'\x83\x30', b'\x9b\x06', b'\x99\x01\x02', b'\x9a\x01\x01\x02']
    path = folder/'valid-framing.bin'
    path.write_bytes(b''.join(struct.pack('<I', len(p))+p for p in valid))
    result = command([runner, path], folder, 'valid-framing')
    assert result.returncode == 0 and list(map(int, result.stdout.splitlines())) == [packet_samples(p) for p in valid]
    negatives = []
    for name, value in [('empty', b''), ('count-missing', b'\x9b'), ('count-zero', b'\x9b\0'),
                        ('duration-over-120ms', b'\x9b\x07'), ('padding-overrun', b'\x9b\x41\xff'),
                        ('vbr-truncated-length', b'\x9a\xfc'), ('vbr-overrun', b'\x9a\x05\x01'),
                        ('cbr-unequal-frames', b'\x99\x01')]:
        try:packet_samples(value)
        except Invalid:pass
        else:raise AssertionError(('Python admitted bad framing', name))
        path = folder/(name+'.bin');path.write_bytes(struct.pack('<I', len(value))+value)
        refused = command([runner, path], folder, name)
        assert refused.returncode == 1, (name, refused.returncode)
        negatives.append(dict(case=name, exitCode=refused.returncode,
                              reason=refused.stderr.decode(errors='replace').strip()))
    return dict(validPackets=len(valid), validExitCode=result.returncode, negativeControls=negatives)


def run(worker, runner, duration_runner, edition, report, selected):
    report.mkdir(parents=True, exist_ok=True);rows = []
    result = dict(result='RUNNING', scope='variable-opus-geometry-preflight-only',
        sdkVersionUnchanged='1.105', productionModeAdded=False, ordinaryMainTested=False,
        physicalAvSyncMeasured=False, edition=edition, workerSha256=sha(worker),
        decoderSha256=sha(runner), durationParserSha256=sha(duration_runner), rows=rows)
    try:
        result['nativeFramingControls'] = native_controls(duration_runner, report/'native-framing')
        video = report/'video.webm'
        ok(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
            'testsrc2=size=160x90:rate=25:duration=12', '-an', '-c:v', 'libvpx-vp9',
            '-threads', '2', '-pix_fmt', 'yuv420p', '-g', '25', '-cpu-used', '8',
            '-lag-in-frames', '0', '-b:v', '300k', '-live', '1', video], report, 'encode-video')
        for profile in PROFILES:
            if selected and profile[0] not in selected:continue
            folder = report/profile[0];source = fixture(folder, profile, video)
            raw = source.read_bytes();meta = probe(source, folder, 'original-probe')
            reference = pcm(source, folder, 'original-pcm');images = decoded(source, 'yuv420p', folder, 'original-video')
            assert len(reference) == SAMPLES*profile[2]*4 and len(images) == 300
            sound = configuration(raw)[2];blocks = [b for b in scan(raw)['blocks'] if b['track'] == sound['number']]
            payloads = [raw[b['payload']:b['end']] for b in blocks]
            binary = folder/'packets.bin';binary.write_bytes(b''.join(struct.pack('<I', len(p))+p for p in payloads))
            native = command([duration_runner, binary], folder, 'native-duration-parser')
            expected = [packet_samples(p) for p in payloads]
            assert native.returncode == 0 and list(map(int, native.stdout.splitlines())) == expected
            if profile[1] == 'mixed':assert set(expected) == {960, 1920, 2880, 3840, 4800, 5760}
            else:assert set(expected) == {int(float(profile[1])*48)}
            for unknown in (False, True):
                case = folder/('unknown' if unknown else 'finite');case.mkdir(exist_ok=True)
                value, crcs = decorate(raw, unknown);path = case/'source.webm';path.write_bytes(value)
                converted = [conversion(value, chunk) for chunk in (257, 8191, 65536)]
                output, clock, _ = converted[0]
                assert all(o == output and c.audio_samples == SAMPLES for o, c, _ in converted)
                assert clock.audio_samples == SAMPLES and clock.pre_skip == sound['preSkip']
                canonical = case/'canonical.webm';canonical.write_bytes(output)
                canonical_meta = probe(canonical, case, 'canonical-probe')
                for kind in ('video', 'audio'):assert packets(meta, kind) == packets(canonical_meta, kind)
                assert pcm(canonical, case, 'canonical-pcm') == reference
                assert decoded(canonical, 'yuv420p', case, 'canonical-video') == images
                first = next(b for b in scan(value)['blocks'] if b['track'] == sound['number'])
                cut = first['end']-1;p = OpusGeometryPrototype(len(value), 300, END)
                for at in range(0, cut, 4093):p.feed(value[at:min(at+4093, cut)])
                assert p.audio_packets == 0;p.feed(value[cut:cut+1]);assert p.audio_packets == 1
                current, _ = serve(worker, path, mode=MODE, format_hint='webm',
                                   video_duration=END, growing=False)
                (case/'current-worker.json').write_text(json.dumps(current, indent=2)+'\n')
                legacy = profile[0] == 'fixed-20'
                assert current['exitCode'] == (0 if legacy else 1), current
                if not legacy:
                    assert current['result']['code'] in ('unsupported-stream-opus-configuration',
                        'unsupported-stream-opus-packet', 'unsupported-stream-webm-configuration'), current
                sound_result = file_decoder(runner, edition, canonical, profile[2], clock.end_trim_samples, case)
                rows.append(dict(profile=profile[0], unknownClusters=unknown, sourceSha256=sha(path),
                    canonicalSha256=sha(canonical), preSkip=clock.pre_skip, channels=profile[2],
                    audioPackets=clock.audio_packets, packetSamples=sorted(clock.durations),
                    audioSamples=clock.audio_samples, endTrimSamples=clock.end_trim_samples,
                    readPatterns=[s for _, _, s in converted], validInputCrcs=crcs,
                    partialPacketNotCounted=True, lastPayloadByteRequired=True,
                    exactPacketsAndClocks=True, exactDecodedVideo=True, exactPcm=True,
                    nativeDurationParserExit=native.returncode, fileDecoder=sound_result,
                    currentWorker=dict(exitCode=current['exitCode'], reason=current['result']['code'],
                                       legacy20msControl=legacy),
                    negativeControls=negatives(value) if not unknown else []))
                assert source.read_bytes() == raw and sha(path) == rows[-1]['sourceSha256']
        result.update(result='PASS-preflight-only', count=len(rows))
    except Exception:
        result.update(result='FAIL', traceback=traceback.format_exc());raise
    finally:
        result['sourceSha256'] = {name:sha(Path(__file__).with_name(name)) for name in
            ('progressive_opus_geometry_preflight.py', 'progressive_opus_geometry_prototype.py',
             'progressive_audio_count_prototype.py', 'progressive_simple_vfr_prototype.py')}
        (report/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser();p.add_argument('--worker', type=Path, required=True)
    p.add_argument('--runner', type=Path, required=True);p.add_argument('--duration-runner', type=Path, required=True)
    p.add_argument('--edition', choices=('qt', 'classic'), required=True)
    p.add_argument('--report-directory', type=Path, required=True)
    p.add_argument('--profile', action='append', choices=[x[0] for x in PROFILES]);a = p.parse_args()
    print(json.dumps(run(a.worker.resolve(), a.runner.resolve(), a.duration_runner.resolve(),
                         a.edition, a.report_directory.resolve(), a.profile)))
