"""Published reordered SDK: physical growth, rational clocks and real player seek.

Packet/pixel oracles inspect the actual managed cache, not a separately remuxed
file. Playback assertions use ordinary Qt/libmpv or Classic/DirectShow IPC.
They do not measure physical screen presentation or audio synchronization.
"""
import argparse
import copy
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import traceback

from progressive_continuous_api_test import run
from progressive_player_crash_recovery import ApiError, sha, wait
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_stream_remux_worker_test import fixture, probe
from progressive_stream_reorder_test import frames, media_prefix, projection
from progressive_continuous import ContinuousRemux, MODE, REORDERED_MODE


PROFILES = (('b2-25', '25', 2), ('b3-30000-1001', '30000/1001', 3),
            ('requested-b16-60000-1001', '60000/1001', 16), ('b2-vfr', '25', 2))


def exercise(peers, root, report=None):
    peer = peers[0]
    rows, observations, request_times = [], [], []

    def observe(label, **value):
        observations.append(dict(label=label, monotonicSeconds=time.monotonic(), **value))
        if report:
            (report/'observations.json').write_text(json.dumps(observations, indent=2)+'\n')

    for name, rate, bframes in PROFILES:
        folder = root/name
        folder.mkdir()
        source = fixture(folder, 'reference', rate=rate, bframes=bframes)
        if name.endswith('vfr'):
            original = folder/'before-vfr.mp4'
            source.rename(original)
            subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(original),
                            '-vf', r'select=not(eq(mod(n\,5)\,4))', '-fps_mode', 'vfr',
                            '-c:v', 'libx264', '-threads', '2', '-preset', 'veryfast',
                            '-bf', '2', '-g', '25', '-sc_threshold', '0',
                            '-movflags', '+faststart', str(source)], check=True, capture_output=True)
        raw, source_hash = source.read_bytes(), sha(source)
        source_probe = probe(source)
        stream = source_probe['streams'][0]
        reference, reference_frames = projection(source), frames(source)
        assert len(source_probe['streams']) == 1 and stream['codec_name'] == 'h264'
        assert int(stream['has_b_frames']) > 0 and len(reference) == len(reference_frames)
        assert reference[0][1] == '0' and Fraction(reference[0][2]) < 0
        assert any(Fraction(a[1]) > Fraction(b[1]) for a, b in zip(reference, reference[1:]))
        display = sorted(Fraction(p[1]) for p in reference)
        intervals = sorted({b-a for a, b in zip(display, display[1:])})
        assert all(x > 0 for x in intervals)
        assert (len(intervals) > 1) if name.endswith('vfr') else (intervals == [1/Fraction(rate)])
        duration = Fraction(int(stream['duration_ts'])) * Fraction(stream['time_base'])
        duration_ticks = -(-(duration*10000000).numerator // (duration*10000000).denominator)
        advertised_duration = duration_ticks/10000000
        packets = source_probe['packets']
        cuts = [int(next(p['pos'] for p in packets if Fraction(p['pts_time']) >= s))+13
                for s in (4, 8, 12, 16, 20)]
        assert 4 < cuts[0] and all(a < b for a, b in zip(cuts, cuts[1:])) and cuts[-1] < len(raw)
        path = folder/'input.part'
        model = snapshot(path, raw, complete=False, prefix=4)
        model['timeline']['duration'] = str(duration_ticks)
        current, worker_id = 4, None
        publications, seeks = [], []
        sdk = ContinuousRemux(peer.client, str(folder), mode=REORDERED_MODE)

        def timed(fn):
            began = time.monotonic()
            value = fn()
            request_times.append(time.monotonic()-began)
            return value

        def status():
            s = timed(sdk.status)
            assert not s['errorCode'] and s['mode'] == REORDERED_MODE, s
            assert int(s['maximumReadEnd']) <= current, (current, s)
            if worker_id:
                assert s['workerId'] == worker_id, s
            return s

        def until(check, label, timeout=20):
            def poll():
                s = status()
                if not s['outputComplete']:
                    timed(sdk.heartbeat)
                return check()
            return wait(poll, label, timeout)

        def playback():
            s = timed(sdk.playback_status)
            observe(name+':playback', status=s)
            assert s['state'] not in ('closed', 'error'), s
            assert abs(s['durationSeconds']-advertised_duration) <= 1e-6, s
            return s

        def publish(n, complete=False):
            nonlocal model, current
            assert current <= n <= len(raw) and path.stat().st_size == current
            with path.open('ab') as output:
                assert output.write(raw[current:n]) == n-current
                output.flush()
                os.fsync(output.fileno())
            # Physical bytes alone must not grant access before publication.
            for _ in range(3):
                status()
                time.sleep(.05)
            previous = model
            model = copy.deepcopy(model)
            model['revision'] = str(int(model['revision'])+1)
            state = 'complete' if complete else 'downloading'
            model['writer']['state'] = model['resources'][0]['state'] = model['tracks'][0]['state'] = state
            model['resources'][0]['ranges'] = [dict(offset='0', length=str(n))]
            model = protect_snapshot(model, folder, previous=previous)
            current = n
            timed(lambda: sdk.publish(model))
            until(lambda: status()['appliedRevision'] == model['revision'], 'SDK revision applied')
            assert sha(path) == hashlib.sha256(raw[:n]).hexdigest()
            publications.append(dict(revision=model['revision'], physicalBytes=n, complete=complete))
            observe(name+':publish', publication=publications[-1], status=status())

        try:
            timed(lambda: sdk.prepare(model, 'clip'))
            initial = until(lambda: (lambda s:s if s['producerCode'] == 'waiting' else None)(status()), '4-byte input wait')
            worker_id = initial['workerId']
            assert worker_id > 0 and not initial['playbackReady'] and not initial['outputComplete'], initial
            publish(cuts[0])
            ready = until(lambda: (lambda s:s if s['playbackReady'] and s['producerCode'] == 'waiting' else None)(status()), 'partial cache ready')
            assert not ready['outputComplete'] and sdk.play_request_id is None
            cache_root = Path(str(peer.registry)+'.continuous-cache')
            caches = list(cache_root.glob('remux-*/output.fmp4'))
            assert len(caches) == 1, caches
            cache = caches[0]
            partial_path = folder/'partial-cache.mp4'
            partial_path.write_bytes(media_prefix(cache.read_bytes()))
            partial_packets, partial_frames = projection(partial_path), frames(partial_path)
            assert 0 < len(partial_packets) < len(reference)
            assert partial_packets == reference[:len(partial_packets)]
            assert partial_frames == reference_frames[:len(partial_frames)]
            partial = dict(packets=len(partial_packets), frames=len(partial_frames),
                           exactPayloadPtsDtsDuration=True, exactDecodedPrefix=True)
            timed(lambda: sdk.play(title='SDK timing '+name))
            initial_buffer = None
            if peer.edition == 'classic':
                # MainFrm keeps its existing 10 s initial / 5 s resume buffer.
                # Playback-ready preparation does not override that policy.
                first = until(lambda: (lambda s:s if s['state'] == 'buffering' and s['decoderReaders'] else None)(playback()), 'Classic initial buffer')
                publish(cuts[1])
                time.sleep(.2)
                below_threshold = playback()
                assert below_threshold['state'] == 'buffering' and below_threshold['positionSeconds'] < .01, below_threshold
                initial_buffer = dict(initialPublishedSeconds=4, nextPublishedSeconds=8,
                                      configuredInitialBufferSeconds=10, first=first, belowThreshold=below_threshold)
                publish(cuts[2])
            running = until(lambda: (lambda s:s if s['state'] == 'playing' and s['positionSeconds'] > .2 and s['decoderReaders'] else None)(playback()), 'real decoder playback')
            timed(lambda: sdk.control('pause'))
            paused = until(lambda: (lambda s:s if s['state'] == 'paused' else None)(playback()), 'manual pause')
            publish(cuts[3] if peer.edition == 'classic' else cuts[1])
            time.sleep(.25)
            held = playback()
            assert held['state'] == 'paused' and abs(held['positionSeconds']-paused['positionSeconds']) < .15, (paused, held)
            try:
                timed(lambda: sdk.control('seek', seconds=1.375))
            except ApiError as e:
                assert e.code == 'continuous-seek-unavailable', e.reply
            else:
                raise AssertionError('Incomplete output accepted seek')
            timed(lambda: sdk.control('play'))
            resumed = until(lambda: (lambda s:s if s['state'] == 'playing' and s['positionSeconds'] > paused['positionSeconds']+.2 else None)(playback()), 'resume same growing decoder')
            publish(cuts[4] if peer.edition == 'classic' else cuts[2])
            timed(lambda: sdk.control('pause'))
            until(lambda: playback()['state'] == 'paused', 'pause before completion')
            publish(len(raw))
            all_bytes = until(lambda: (lambda s:s if s['producerCode'] == 'waiting' else None)(status()), 'all bytes without complete')
            assert not all_bytes['outputComplete'] and playback()['state'] == 'paused', all_bytes
            publish(len(raw), complete=True)
            complete = until(lambda: (lambda s:s if s['outputComplete'] else None)(status()), 'confirmed complete output')
            assert int(complete['samples']) == len(reference), complete
            assert projection(cache) == reference and frames(cache) == reference_frames
            # These are API/decoder clock checks with an explicit frame-sized
            # tolerance, not claims of pixel-accurate screen presentation.
            tolerance = float(max(intervals))+.01
            for target in (10.125, 1.375, advertised_duration-1.175):
                began = time.monotonic()
                timed(lambda: sdk.control('seek', seconds=target))
                sought = until(lambda: (lambda s:s if s['state'] == 'paused' and abs(s['positionSeconds']-target) <= tolerance else None)(playback()), 'paused fractional seek', 3)
                elapsed = time.monotonic()-began
                assert elapsed < 2, (target, elapsed, sought)
                time.sleep(.12)
                stable = playback()
                assert stable['state'] == 'paused' and abs(stable['positionSeconds']-target) <= tolerance, stable
                seeks.append(dict(targetSeconds=target, observedSeconds=sought['positionSeconds'],
                                  toleranceSeconds=tolerance, elapsedSeconds=elapsed))
            began = time.monotonic()
            timed(lambda: sdk.control('play'))
            finished = until(lambda: (lambda s:s if s['state'] == 'finished' else None)(playback()), 'natural confirmed EOF', 10)
            eof_seconds = time.monotonic()-began
            assert .65 <= eof_seconds < 5, (eof_seconds, finished)
            assert finished['outputComplete'] and not finished['active'], finished
            assert abs(finished['positionSeconds']-advertised_duration) <= tolerance, finished
            began = time.monotonic()
            proof = sdk.close(timeout=2)
            release_seconds = time.monotonic()-began
            assert proof['released'] and proof['sourceFilesClosed'] and release_seconds < 2, proof
            final = sdk.forget(timeout=2)
            assert final['forgotten'] and final['released'] and not cache.exists(), final
            assert sha(path) == sha(source) == source_hash
            row = dict(profile=name, result='PASS', rate=rate, requestedBFrames=bframes,
                       sourceStream={k:stream[k] for k in ('time_base', 'avg_frame_rate', 'r_frame_rate', 'duration_ts', 'has_b_frames')},
                       durationRational=str(duration), duration100ns=str(duration_ticks),
                       presentationIntervals=[str(x) for x in intervals], samples=len(reference),
                       decodedFrames=len(reference_frames), partialProof=partial,
                       exactCachePacketsAndDecodedFrames=True, physicalAppendOnly=True,
                       unpublishedPhysicalBytesNotRead=True, publications=publications,
                       initialBufferPolicyProof=initial_buffer,
                       actualPlayback=running, pause=paused, held=held, resumed=resumed,
                       workerPidUnchanged=worker_id, complete=complete, seeks=seeks,
                       finished=finished, eofWallSeconds=eof_seconds, releaseSeconds=release_seconds,
                       proof=proof, forgotten=final, originalSha256=source_hash)
            rows.append(row)
            observe(name+':passed', summary=row)
        finally:
            if sdk.request_id and not sdk.forgotten:
                try:
                    proof = sdk.close(timeout=2)
                    if proof['released'] and proof['sourceFilesClosed']:
                        sdk.forget(timeout=2)
                except Exception:
                    observe(name+':cleanup-error', error=traceback.format_exc())
    assert max(request_times) < 2, request_times
    return dict(result='PASS', count=len(rows), rows=rows, mode=REORDERED_MODE,
                defaultMode=MODE, publicSdk=True, sourceSeconds=24,
                physicalAppendOnly=True, actualPlayerPlayback=True,
                cachePixelAndPacketOracle=True, physicalScreenTimingMeasured=False,
                audioSupported=False, maxRequestSeconds=max(request_times))


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--edition', choices=('qt', 'classic'), required=True)
    p.add_argument('--player', action='append', required=True)
    p.add_argument('--reader-runner', required=True)
    p.add_argument('--report-directory', type=Path, required=True)
    args = p.parse_args()
    args.report_directory.mkdir(parents=True, exist_ok=True)
    try:
        value = run(args, lambda peers, root: exercise(peers, root, args.report_directory), __file__)
        print(json.dumps(dict(result=value['result'], count=value['count'])))
    except Exception:
        (args.report_directory/'error.txt').write_text(traceback.format_exc(), encoding='utf-8')
        raise
