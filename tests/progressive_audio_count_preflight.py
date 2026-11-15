"""Audio-count preparation: incremental test prototype, existing known-plan consumers.

No automatic-audio-count production mode exists or is advertised by this test.
The existing worker/decoder receives the count only AFTER the prototype's EOF.
This oracle is explicitly not a way to start unknown-count progressive playback.
"""
import argparse
from array import array
import copy
from fractions import Fraction
import json
from pathlib import Path
import tempfile
import traceback

from progressive_audio_count_prototype import AudioCountPrototype
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_simple_vfr_preflight import head_without_default, decorate
from progressive_simple_vfr_prototype import Invalid
from progressive_stream_remux_worker_test import serve
from progressive_webm_fixtures import width
from progressive_webm_opus_remux_preflight import (
    fixture, configuration, children, pcm, probe, packets)
from progressive_webm_remux_preflight import scan, sha, command, ok
from progressive_webm_vfr_preflight import (
    decoded, ebml, fixture as vfr_fixture, PROFILES as VFR_PROFILES)


PROFILES = [
    ('mono-full', False, 1, '20', 'audio', 1152000, False),
    ('stereo-short', False, 2, '20', 'audio', 95983, False),
    ('p2-stereo-zero-trim', True, 2, '20', 'audio', 1151688, False),
    ('mono-649-audio-first', False, 1, '20', 'audio', 649, True),
    ('vfr-mono', False, 1, '20', 'audio', 1152000, False),
]
MODE = 'continuous-webm-vp9-opus-vfr-v1'


def conversion(raw, frames, end, chunk):
    p = AudioCountPrototype(len(raw), frames, end)
    output = bytearray();safe = []
    for at in range(0, len(raw), chunk):
        output += p.feed(raw[at:at+chunk])
        assert not p.audio_sample_count_final and p.audio_samples is None
        assert p.indexed_audio_samples <= max(0, p.decoded_samples-960-312)
        assert p.at <= p.supplied <= len(raw)
        if not safe or p.indexed_audio_samples != safe[-1]:safe.append(p.indexed_audio_samples)
    before = p.indexed_audio_samples
    assert p.feed() == b'' and p.waiting == 'source-complete'
    assert not p.audio_sample_count_final and p.audio_samples is None
    output += p.feed(complete=True)
    assert p.finished and p.audio_sample_count_final
    assert p.audio_samples == p.decoded_samples-312-p.end_trim_samples
    assert p.audio_samples >= before and all(a <= b for a, b in zip(safe, safe[1:]))
    return bytes(output), p, dict(chunk=chunk, confirmedBytesOnly=True,
        audioCountFinalOnlyAfterWriterEof=True, indexedBeforeEof=before,
        decodedSamples=p.decoded_samples, endTrimSamples=p.end_trim_samples,
        audioSamples=p.audio_samples, audioPackets=p.audio_packets,
        peakInputBufferBytes=p.peak_input, peakPendingBytes=p.peak_pending)


def partial_packet(raw, frames, end):
    audio = configuration(raw)[2]['number']
    first = next(b for b in scan(raw)['blocks'] if b['track'] == audio)
    cut = first['end']-1;p = AudioCountPrototype(len(raw), frames, end)
    for at in range(0, cut, 4093):p.feed(raw[at:min(at+4093, cut)])
    assert p.audio_packets == 0 and not p.audio_sample_count_final
    p.feed(raw[cut:cut+1])
    assert p.audio_packets == 1 and p.indexed_audio_samples == 0
    return dict(partialAudioPacketNotCounted=True, lastBodyByteRequired=True,
                firstPacketEnd=first['end'])


def rewrite_audio(raw, change):
    """Finite controlled fixtures only. Rebuild sizes, drop stale Cluster CRC/offsets."""
    layout = scan(raw);audio = configuration(raw)[2]['number']
    output = bytearray(raw[:layout['headBytes']]);index = 0
    for cluster in layout['clusters']:
        value = bytearray()
        for tag, at, body, stop in children(raw, cluster['body'], cluster['end']):
            if tag in (0xbf, 0xa7, 0xab):continue
            packet = raw[body:stop]
            if tag == 0xa0:
                packet = next(raw[a:z] for key, _, a, z in children(raw, body, stop) if key == 0xa1)
            if tag in (0xa0, 0xa3):
                w = width(packet[0]);track = int.from_bytes(packet[:w], 'big') & ((1 << (7*w))-1)
                if track == audio:
                    value += change(index, packet, raw[at:stop]);index += 1;continue
            value += raw[at:stop]
        output += ebml(0x1f43b675, value)
    return bytes(output)


def negative_controls(raw, frames, end, folder):
    rows = []
    def refuse(label, value, **limits):
        try:
            p = AudioCountPrototype(len(value), frames, end, **limits)
            for at in range(0, len(value), 4093):p.feed(value[at:at+4093])
            p.feed(complete=True)
        except (Invalid, ValueError) as error:
            rows.append(dict(case=label, reason=str(error)));return
        raise AssertionError(('Unexpected admission', label))
    def group(block, padding):return ebml(0xa0, ebml(0xa1, block)+padding)
    def signed(value):return ebml(0x75a2, value.to_bytes(8, 'big', signed=True))
    for label, padding in [('negative-trim', signed(-20833)),
                           ('fractional-sample-trim', signed(1)),
                           ('excessive-trim', signed(20000000)),
                           ('duplicate-trim', signed(0)+signed(0))]:
        refuse(label, rewrite_audio(raw, lambda i, block, original:
            group(block, padding) if i == 0 else original))
    refuse('audio-after-trim', rewrite_audio(raw, lambda i, block, original:
        group(block, signed(1250000)) if i == 0 else original))
    def bad_clock(i, block, original):
        if i != 1:return original
        value = bytearray(block);w = width(value[0]);value[w:w+2] = b'\0\0'
        return ebml(0xa3, value)
    refuse('regressing-audio-clock', rewrite_audio(raw, bad_clock))
    refuse('audio-sample-budget', raw, sample_limit=1000)
    refuse('audio-packet-budget', raw, packet_limit=2)
    layout = scan(raw)
    first_audio = next(b for b in layout['blocks'] if b['track'] == configuration(raw)[2]['number'])
    for cut in (1, layout['headBytes']-1, first_audio['body']+3, len(raw)-1):
        p = AudioCountPrototype(len(raw), frames, end)
        try:
            for at in range(0, cut, 4093):p.feed(raw[at:min(at+4093, cut)])
            p.feed(complete=True)
        except Invalid as error:rows.append(dict(case='premature-eof-'+str(cut), reason=str(error)))
        else:raise AssertionError('Premature writer EOF accepted')
    # A whole omitted final audio packet has no structural trace. With no external
    # plan it is indistinguishable from a valid shorter track. Preserve this limit.
    audio_track = configuration(raw)[2]['number']
    count = sum(b['track'] == audio_track for b in layout['blocks'])
    shorter = rewrite_audio(raw, lambda i, block, original: b'' if i == count-1 else original)
    path = folder/'whole-packet-omitted.webm';path.write_bytes(shorter)
    _, p, _ = conversion(shorter, frames, end, 8191)
    oracle = len(pcm(path, folder, 'omitted-packet-pcm'))//(4*configuration(raw)[2]['channels'])
    assert p.audio_samples == oracle
    return rows, dict(wholeLastAudioPacketOmitted=True, structurallyValidShorterTrack=True,
        derivedSamples=p.audio_samples, decodedOracleSamples=oracle,
        requiresIndependentWriterIdentityForSemanticTruncation=True)


def consumer(runner, worker, edition, raw, source, frames, end, channels, samples, folder):
    with tempfile.TemporaryDirectory(prefix='fox-audio-count-consumer-') as t:
        root = Path(t);path = root/'video.part'
        model = snapshot(path, raw, complete=True);model['timeline']['duration'] = str(end)
        model['tracks'][0].update(codecHint='vp9', containerHint='webm')
        sound = copy.deepcopy(model['tracks'][0]);sound.update(id='sound', kind='audio', codecHint='opus')
        model['tracks'].append(sound);model = protect_snapshot(model, root)
        manifest = root/'session.json';manifest.write_text(json.dumps(model));output = root/'actual.pcm'
        result = command([runner, manifest, worker, root/'registry', root/'cache', output,
            str(channels), str(samples), 'vfr', str(frames), str(end)], folder, 'decoder')
        assert result.returncode == 0, (result.returncode, result.stderr[-1600:])
        fmt, codec = ('s16le', 'pcm_s16le') if edition == 'qt' else ('f32le', 'pcm_f32le')
        reference = ok(['ffmpeg', '-v', 'error', '-threads', '1', '-i', source,
            '-map', '0:a:0', '-c:a', codec, '-f', fmt, '-'], folder, 'decoder-reference')
        (folder/'decoder-reference.stdout').unlink()
        actual = output.read_bytes();stride = channels*(2 if edition == 'qt' else 4)
        assert len(actual) == len(reference) == samples*stride
        left, right = array('h' if edition == 'qt' else 'f'), array('h' if edition == 'qt' else 'f')
        left.frombytes(actual);right.frombytes(reference)
        error = max(abs(a-b) for a, b in zip(left, right))
        assert error <= (2 if edition == 'qt' else 2e-6), error
        assert not list((root/'cache').glob('remux-*')) and path.read_bytes() == raw
        return dict(exitCode=result.returncode, samples=samples, maxAbsolutePcmError=error,
            sourceUnchanged=True, lastOwnerReleased=True, audioDeviceOpened=False,
            knownPlanProvidedAfterPrototypeEof=True, unknownCountAdapterTested=False)


def run(worker, runner, edition, report):
    report.mkdir(parents=True, exist_ok=True);rows = []
    result = dict(result='RUNNING', scope='audio-count-preflight-only', sdkVersionUnchanged='1.104',
        applicationVersionUnchanged='0.2.0', workerSha256=sha(worker), decoderSha256=sha(runner),
        testSourceSha256=sha(__file__), prototypeSha256=sha(Path(__file__).with_name('progressive_audio_count_prototype.py')),
        ordinaryUnknownAudioCountPlaybackTested=False, physicalAvSyncMeasured=False, rows=rows)
    try:
        for profile in PROFILES:
            folder = report/profile[0]
            source, pixels = (vfr_fixture(folder, VFR_PROFILES[0]) if profile[0] == 'vfr-mono'
                              else fixture(folder, profile))
            original_sha = sha(source);original = source.read_bytes();meta = probe(source, folder, 'original-probe')
            video = packets(meta, 'video');frames = len(video)
            period = scan(original)['defaultDurationNs']
            end = int(Fraction(video[-1][1])*10000000)+round(Fraction(period,1000000))*10000
            reference_pcm = pcm(source, folder, 'original-pcm');reference_video = decoded(source, pixels, folder, 'original-video')
            assert len(reference_pcm) == profile[5]*profile[2]*4
            for unknown in (False, True):
                case = folder/('unknown' if unknown else 'finite');case.mkdir(exist_ok=True)
                raw, crcs = decorate(head_without_default(original), unknown)
                path = case/'source.webm';path.write_bytes(raw);digest = sha(path)
                converted = [conversion(raw, frames, end, chunk) for chunk in (257, 8191, 65536)]
                output, p, _ = converted[0]
                assert all(data == output and clock.audio_samples == p.audio_samples for data, clock, _ in converted)
                assert p.audio_samples == len(reference_pcm)//(4*profile[2])
                assert (p.end_trim_samples == 0) == (profile[0] == 'p2-stereo-zero-trim')
                sealed = case/'canonical.webm';sealed.write_bytes(output)
                actual_meta = probe(sealed, case, 'canonical-probe')
                for kind in ('video', 'audio'):assert packets(meta, kind) == packets(actual_meta, kind)
                assert pcm(sealed, case, 'canonical-pcm') == reference_pcm
                assert decoded(sealed, pixels, case, 'canonical-video') == reference_video
                refused, _ = serve(worker, sealed, mode=MODE, format_hint='webm',
                    video_samples=frames, video_duration=end, growing=False)
                assert refused['exitCode'] == 1 and refused['result']['code'] == 'unsupported-stream-start'
                accepted, _ = serve(worker, sealed, mode=MODE, format_hint='webm',
                    video_samples=frames, video_duration=end, audio_samples=p.audio_samples)
                assert accepted['exitCode'] == 0 and accepted['firstCompleteFragmentBeforeSourceComplete']
                sound = consumer(runner, worker, edition, output, path, frames, end,
                    profile[2], p.audio_samples, case)
                negatives, ambiguity = negative_controls(raw, frames, end, case) if not rows else ([], None)
                assert sha(source) == original_sha and sha(path) == digest
                rows.append(dict(profile=profile[0], unknownClusters=unknown, frames=frames,
                    audioSamples=p.audio_samples, audioPackets=p.audio_packets, endTrimSamples=p.end_trim_samples,
                    readPatterns=[stats for _, _, stats in converted], validInputCrcs=crcs,
                    partialAudioPacket=partial_packet(raw, frames, end),
                    exactPacketsAndClocks=True, exactDecodedVideo=True, exactPcm=True, sourceUnchanged=True,
                    currentWorkerMissingAudioPlanRefused=dict(exitCode=refused['exitCode'], reason=refused['result']['code']),
                    knownPlanWorker=dict(exitCode=accepted['exitCode'], fragmentBeforeComplete=True),
                    decoder=sound, negativeControls=negatives, semanticEofLimit=ambiguity))
                (report/'progress.json').write_text(json.dumps(dict(count=len(rows))))
        result.update(result='PASS-preflight-only', count=len(rows))
        (report/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        print('Audio-count preflight PASS', len(rows));return result
    except BaseException:
        result.update(result='FAIL', error=traceback.format_exc())
        (report/'failure.json').write_text(json.dumps(result, indent=2)+'\n');raise


if __name__ == '__main__':
    p = argparse.ArgumentParser();p.add_argument('--worker', type=Path, required=True)
    p.add_argument('--runner', type=Path, required=True);p.add_argument('--edition', choices=('qt', 'classic'), required=True)
    p.add_argument('--report-directory', type=Path, required=True);a = p.parse_args()
    run(a.worker.resolve(), a.runner.resolve(), a.edition, a.report_directory.resolve())
