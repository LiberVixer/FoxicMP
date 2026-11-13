"""Ordinary Qt/Classic HEVC continuous playback through the public SDK/IPC."""
import copy
from fractions import Fraction
import json
from pathlib import Path
import shutil
import sys
import time
import uuid

from progressive_backend_demux_conformance import snapshot
from progressive_continuous import ContinuousRemux, HEVC_MODE, HEVC_AAC_MODE
from progressive_hevc_remux_preflight import PROFILES
from progressive_integrity import protect_snapshot
from progressive_mux_mp4_fixture import boxes
from progressive_player_crash_recovery import ApiError, sha, wait
from progressive_stream_hevc_test import fixture, packets, frames
from progressive_stream_remux_worker_test import probe, fixture as avc_fixture


def exercise(peers, root, report, audio=False, expected_audio="null"):
    mode = HEVC_AAC_MODE if audio else HEVC_MODE
    if audio:
        from progressive_stream_hevc_aac_test import PROFILES as av_profiles, fixture as av_fixture
        from progressive_stream_aac_window_test import packets as av_packets
    report = Path(report)
    client = peers[0].client
    rows = []
    current = None

    def poll(sdk, predicate, label, timeout=25):
        def tick():
            status = sdk.status()
            assert not status['errorCode'], status
            value = predicate(status)
            if value:
                return value
            if not status['outputComplete']:
                try:
                    sdk.heartbeat()
                except ApiError as error:
                    assert error.code == 'continuous-closed', error.reply
            return None
        return wait(tick, label, timeout)

    def playback(sdk, predicate, label, timeout=25):
        def check(_):
            value = sdk.playback_status()
            (report / 'last-playback.json').write_text(json.dumps(value, indent=2), encoding='utf-8')
            assert value['state'] not in ('error', 'closed'), value
            assert value['audio'] is audio and value['mode'] == mode, value
            return value if predicate(value) else None
        return poll(sdk, check, label, timeout)

    def close(sdk, source, expected):
        began = time.monotonic()
        released = sdk.close(timeout=2)
        elapsed = time.monotonic() - began
        assert released['sourceFilesClosed'] and released['released'] and elapsed < 2, released
        forgotten = sdk.forget(timeout=2)
        assert forgotten['forgotten'] and forgotten['released'] and sha(source) == expected
        return dict(released=released, forgotten=forgotten, closeSeconds=elapsed, sourceUnchanged=True)

    try:
        for number, profile in enumerate(av_profiles if audio else PROFILES):
            if audio:
                name, sample_rate, channels, rate, pixel_format, tag, audio_first, _ = profile
                original, presentation_samples = av_fixture(root, profile)
            else:
                name, rate, pixel_format, tag = profile
                original = fixture(root, name, rate, pixel_format, tag)
            raw = original.read_bytes()
            source_sha = sha(original)
            directory = root / (name + '-session')
            directory.mkdir()
            path = directory / 'video.part'
            model = snapshot(path, raw, complete=False, prefix=4)
            model['timeline']['duration'] = '240000000'
            model['tracks'][0]['codecHint'] = 'hevc'
            if audio:
                sound = copy.deepcopy(model['tracks'][0])
                sound.update(id='sound', kind='audio', codecHint='aac-lc')
                model['tracks'].append(sound)
            sdk = current = ContinuousRemux(client, str(directory), mode=mode)
            sdk.prepare(model, 'clip')
            waiting = poll(sdk, lambda s: s if s['producerCode'] == 'waiting' else None, 'partial head')
            assert not waiting['playbackReady'] and not waiting['outputComplete'], waiting

            def publish(count, complete=False):
                nonlocal model
                old = model
                with path.open('ab') as file:
                    file.write(raw[path.stat().st_size:count])
                assert path.stat().st_size == count
                model = copy.deepcopy(model)
                model['revision'] = str(int(model['revision'])+1)
                phase = 'complete' if complete else 'downloading'
                model['writer']['state'] = model['resources'][0]['state'] = phase
                for track in model['tracks']:
                    track['state'] = phase
                model['resources'][0]['ranges'] = [dict(offset='0', length=str(count))]
                model = protect_snapshot(model, directory, previous=old)
                sdk.publish(model)
                return poll(sdk, lambda s: s if s['appliedRevision'] == model['revision'] else None, 'applied publication')

            head_end = next(at+size for at, size, kind, _ in boxes(raw) if kind == b'moov')
            publish(head_end-4)
            partial_index = poll(sdk, lambda s: s if s['producerCode'] == 'waiting' else None, 'partial moov')
            assert not partial_index['playbackReady'], partial_index
            metadata = probe(original)
            clock = Fraction(next(s for s in metadata['streams'] if s['codec_type']=='video')['time_base'])
            cut = next(int(packet['pos'])+13 for packet in metadata['packets']
                       if packet['codec_type']=='video' and int(packet['pts'])*clock >= 20)
            publish(cut)
            ready = poll(sdk, lambda s: s if s['playbackReady'] and s['indexedEndSeconds'] >= 12 else None, 'HEVC ready before complete')
            assert not ready['outputComplete'] and path.stat().st_size < len(raw), ready
            if audio:
                assert int(ready['audioPresentationSamples']) == presentation_samples and ready['audioSampleRate']==sample_rate and ready['audioChannels']==channels, ready
            start_seconds = 1 if number == 1 else 0
            sdk.play(title='HEVC continuous ' + name, start_seconds=start_seconds)
            running = playback(sdk, lambda s: s['state'] == 'playing' and s['decoderReaders'] > 0
                               and s['positionSeconds'] > start_seconds+.15, 'HEVC decoder playing')
            if audio:
                assert running['pcmWindowApplied'], running
                if peers[0].edition=='qt':
                    assert running['audioCodec']=='aac' and (running['audioOutput']==expected_audio or presentation_samples==1 and running['audioOutput']==''), running
            began = time.monotonic()
            viewed = playback(sdk, lambda s: s['state'] == 'playing' and
                              s['positionSeconds'] >= running['positionSeconds']+10.1,
                              'ten seconds of incomplete HEVC playback', 20)
            elapsed = time.monotonic()-began
            assert not viewed['outputComplete'] and elapsed >= 9.9 and path.stat().st_size < len(raw), viewed
            sdk.control('pause')
            paused = playback(sdk, lambda s: s['state'] == 'paused', 'HEVC pause')
            sdk.control('seek', seconds=2, allow_growing=True)
            seeking = playback(sdk, lambda s: s['state'] == 'paused' and abs(s['positionSeconds']-2) < .12,
                               'HEVC confirmed growing seek')
            sdk.control('seek', seconds=22, allow_growing=True, wait_for_range=True)
            queued = playback(sdk, lambda s: s['pendingSeekSeconds'] == 22 and s['seekState'] == 'waiting-range',
                              'HEVC future seek waits')
            publish(len(raw), complete=True)
            complete = poll(sdk, lambda s: s if s['outputComplete'] else None, 'HEVC producer complete')
            applied = playback(sdk, lambda s: s['pendingSeekSeconds'] is None and s['seekState'] == 'applied'
                               and s['state'] == 'paused' and abs(s['positionSeconds']-22) < .12,
                               'HEVC queued seek applied')
            cache_root = Path(str(peers[0].registry)+'.continuous-cache')
            caches = list(cache_root.glob('remux-*/output.fmp4'))
            assert len(caches) == 1, caches
            output = report / (name + '-output.fmp4')
            shutil.copyfile(caches[0], output)
            output_stream = next(s for s in probe(output)['streams'] if s['codec_type']=='video')
            assert output_stream['codec_tag_string'] == tag and output_stream['pix_fmt'] == pixel_format
            if audio:
                left, right = av_packets(original), av_packets(output)
                assert left['video']==right['video'] and [p[:3] for p in left['audio']]==[p[:3] for p in right['audio']]
            else:
                assert packets(output) == packets(original)
            assert frames(output, pixel_format) == frames(original, pixel_format)
            sdk.control('play')
            finished = playback(sdk, lambda s: s['state'] == 'finished', 'HEVC natural EOF', 12)
            final = close(sdk, path, source_sha)
            current = None
            assert not list(cache_root.glob('remux-*'))
            rows.append(dict(profile=name, result='PASS', pixelFormat=pixel_format,
                             sourceSha256=source_sha, outputSha256=sha(output),
                             waiting=waiting, partialIndex=partial_index, ready=ready,
                             running=running, viewedBeforeComplete=viewed, viewedWallSeconds=elapsed,
                             paused=paused, growingSeek=seeking, queued=queued, applied=applied,
                             complete=complete, finished=finished, exactPacketClocks=True,
                             exactDecodedFrames=True, **final))
            (report / 'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in rows])))

        if audio:
            # Cancellation must also close both decoder views while paused with
            # an unavailable future seek. Natural EOF alone cannot prove this.
            directory = root / 'cancel-paused-pending'
            directory.mkdir()
            path = directory / 'input.part'
            pending_model = snapshot(path, raw, complete=False, prefix=cut)
            pending_model['timeline']['duration'] = '240000000'
            pending_model['tracks'][0]['codecHint'] = 'hevc'
            sound = copy.deepcopy(pending_model['tracks'][0])
            sound.update(id='sound', kind='audio', codecHint='aac-lc')
            pending_model['tracks'].append(sound)
            before = sha(path)
            sdk = current = ContinuousRemux(client, str(directory), mode=mode)
            sdk.prepare(pending_model, 'clip')
            poll(sdk, lambda s: s if s['playbackReady'] else None, 'cancel fixture ready')
            sdk.play()
            actual = playback(sdk, lambda s: s['state']=='playing' and s['positionSeconds']>.15,
                              'cancel fixture playing')
            sdk.control('pause')
            playback(sdk, lambda s: s['state']=='paused', 'cancel fixture paused')
            sdk.control('seek', seconds=22, allow_growing=True, wait_for_range=True)
            queued = playback(sdk, lambda s: s['pendingSeekSeconds']==22, 'cancel fixture queued')
            final = close(sdk, path, before)
            closed = sdk.playback_status()
            assert closed['pendingSeekSeconds'] is None and closed['decoderReaders']==0, closed
            for action in (sdk.heartbeat, sdk.play):
                try:
                    action()
                except RuntimeError:
                    pass
                else:
                    raise AssertionError('Cancelled HEVC/AAC restarted')
            rows.append(dict(profile='cancel-paused-with-pending-seek', result='PASS',
                             actual=actual, queued=queued, closed=closed, **final))
            current = None

        # Explicit hints/layouts are rejected before any media or lease is opened.
        for name, change in [('missing-hint', ''), ('avc-hint', 'h264'), ('extra-audio', 'hevc')]:
            bad = copy.deepcopy(model)
            bad['sessionId'] = uuid.uuid4().hex
            bad['tracks'][0]['codecHint'] = change
            if name == 'extra-audio':
                track = copy.deepcopy(bad['tracks'][0])
                track.update(id='audio', kind='audio', codecHint='aac-lc')
                bad['tracks'].append(track)
            try:
                client.request(dict(version=1, action='continuousRemuxStart', mode=mode,
                                    requestId=uuid.uuid4().hex, sessionDirectory=str(directory),
                                    session=bad, resourceId='clip'))
            except ApiError as error:
                assert error.code == 'unsupported-stream-source', error.reply
                rows.append(dict(profile='reject-'+name, result='PASS', error=error.reply))
            else:
                raise AssertionError('Unsupported HEVC hint admitted')
        for kind, expected in [('actual-avc', 'unsupported-stream-codec'),
                               ('malformed-hvcc', 'unsupported-stream-hevc-configuration'),
                               ('truncated-complete', 'incomplete-stream-hevc-samples')]:
            if kind == 'actual-avc':
                content = avc_fixture(root, 'actual-avc', bframes=2, audio=audio).read_bytes()
            elif kind == 'malformed-hvcc':
                content = bytearray(raw)
                at = content.index(b'hvcC')+4+21
                content[at] &= ~3
                content = bytes(content)
            else:
                content = raw[:int(metadata['packets'][-1]['pos'])]
            directory = root / kind
            directory.mkdir()
            path = directory / 'input.part'
            bad = snapshot(path, content, complete=kind=='truncated-complete',
                           prefix=len(content) if kind=='truncated-complete' else len(content)*2//3)
            path.write_bytes(content)
            bad['timeline']['duration'] = '240000000'
            bad['tracks'][0]['codecHint'] = 'hevc'
            if audio:
                sound=copy.deepcopy(bad['tracks'][0])
                sound.update(id='sound', kind='audio', codecHint='aac-lc')
                bad['tracks'].append(sound)
            before = sha(path)
            sdk = current = ContinuousRemux(client, str(directory), mode=mode)
            sdk.prepare(bad, 'clip')
            error = wait(lambda: (lambda s: s if s['state']=='error' and s['released'] else None)(sdk.status()),
                         'HEVC rejected media release')
            assert error['errorCode'] == expected and not error['playbackReady'] and not error['outputComplete'], error
            rows.append(dict(profile='reject-'+kind, result='PASS', error=error, **close(sdk, path, before)))
            current = None
        return dict(result='PASS', count=len(rows), rows=rows, mode=mode,
                    publicCapability=True, ordinaryPlayerIpc=True,
                    realIncompletePlayback=True, physicalAvSyncMeasured=False, requiredAudio=audio)
    finally:
        if current is not None:
            primary = sys.exc_info()[1]
            try:
                current.close(timeout=10)
                current.forget(timeout=10)
            except Exception as error:
                if primary is None:
                    raise
                (report / 'case-cleanup-error.txt').write_text(str(error), encoding='utf-8')
