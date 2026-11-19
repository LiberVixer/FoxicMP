"""Continuous VP8/AV1 WebM corpus, worker protocol and ordinary player checks."""
import argparse
import copy
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from progressive_backend_demux_conformance import snapshot
from progressive_client import ApiError
from progressive_continuous import ContinuousRemux, EXTENDED_WEBM_PROFILES
from progressive_integrity import protect_snapshot
from progressive_player_crash_recovery import Peer, wait
from progressive_stream_remux_worker_test import probe, serve
from progressive_webm_remux_preflight import scan, unknown_clusters
from progressive_webm_vfr_preflight import ebml, explicit_durations

SECONDS = 18


def prepare(root):
    root.mkdir(parents=True, exist_ok=True)
    for codec in ('vp8', 'av1'):
        video = root / (codec + ('.mp4' if codec == 'av1' else '.webm'))
        encoding = ['-c:v', 'libaom-av1', '-pix_fmt', 'yuv420p10le'] if codec == 'av1' else [
            '-c:v', 'libvpx', '-pix_fmt', 'yuv420p', '-deadline', 'realtime']
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-filter_threads', '1', '-f', 'lavfi', '-i',
                        f'testsrc2=size=144x80:rate=25:duration={SECONDS}', '-vf',
                        "select='not(eq(mod(n,5),2))'", '-fps_mode', 'vfr', *encoding,
                        '-cpu-used', '8', '-threads', '2', '-lag-in-frames', '0', '-auto-alt-ref', '0',
                        '-g', '25', str(video)], check=True, capture_output=True)
        for audio in (False, True):
            name = f'{codec}-{"opus" if audio else "video"}'
            inputs = ['-i', str(video)]
            streams = ['-map', '0:v', '-c:v', 'copy']
            if audio:
                inputs += ['-f', 'lavfi', '-i', f'sine=frequency=997:sample_rate=48000:duration={SECONDS}']
                streams += ['-map', '1:a', '-c:a', 'libopus', '-frame_duration', '10',
                            '-ac', '2' if codec == 'av1' else '1']
            source = root / (name + '-original.webm')
            subprocess.run(['ffmpeg', '-v', 'error', '-y', *inputs, *streams, '-cluster_time_limit',
                            '720', '-live', '1', str(source)], check=True, capture_output=True)
            raw = source.read_bytes()
            (root / (name+'-simple.webm')).write_bytes(unknown_clusters(raw, scan(raw)))
            grouped, _ = explicit_durations(raw, remove_default=True, video_track=1)
            (root / (name+'-groups.webm')).write_bytes(grouped)
            if not audio:
                subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(video), '-map', '0:v',
                                '-c:v', 'copy', '-cluster_time_limit', '720',
                                str(root/(name+'-finite.webm'))], check=True, capture_output=True)


def cases(root, selected):
    for mode, profile in EXTENDED_WEBM_PROFILES.items():
        codec, audio = profile['videoCodec'], profile['audio']
        for layout in ('simple', 'groups') if audio else ('simple', 'groups', 'finite'):
            name = f'{codec}-{"opus" if audio else "video"}-{layout}'
            if selected and name not in selected:
                continue
            path = root/(name+'.webm')
            table = scan(path.read_bytes())
            video = [b for b in table['blocks'] if b['track']==1]
            assert video[0]['ticks']==0 and video[-1]['ticks']+40==SECONDS*1000, (name, video[0]['ticks'], video[-1]['ticks'])
            assert len({b['ticks']-a['ticks'] for a, b in zip(video, video[1:])})>1
            yield name, mode, path, video, audio


def packet_rows(path):
    packets = probe(path)['packets']
    return {
        kind:[(p['data_hash'], p['pts'], p['dts'], p['flags'][0]) for p in packets if p['codec_type']==kind]
        for kind in ('video', 'audio')
    }


def decoded(path, audio):
    commands = ('-map', '0:a', '-c:a', 'pcm_s16le') if audio else (
        '-map', '0:v', '-threads', '1', '-fps_mode', 'passthrough', '-c:v', 'rawvideo')
    return subprocess.check_output(['ffmpeg', '-v', 'error', '-threads', '2', '-i', str(path),
                                    *commands, '-f', 'hash', '-hash', 'sha256', '-'])


def exact(source, output, audio):
    assert packet_rows(source)==packet_rows(output), source.name
    assert decoded(source, False)==decoded(output, False), source.name
    if audio:
        assert decoded(source, True)==decoded(output, True), source.name


def worker_cases(args):
    rows = []
    rejected = {}
    with tempfile.TemporaryDirectory(prefix='foxicmp-webm-worker-') as temp:
        temp = Path(temp)
        for name, mode, path, video, audio in cases(args.corpus, args.case):
            result, data = serve(args.worker, path, mode=mode, format_hint='webm',
                                 video_duration=SECONDS*10000000, short_reads=True, cut_seconds=(4,8,12,16))
            assert result['exitCode']==0 and result['firstCompleteFragmentBeforeSourceComplete'], (name, result)
            assert result['result']['mode']==mode and result['result']['sourceEof'] and not result['result']['transcoded']
            output = temp/(name+'.webm')
            output.write_bytes(data)
            exact(path, output, audio)
            rows.append(dict(case=name, mode=mode, videoPackets=len(video), exactPacketsClockDecode=True, worker=result))
        if not args.case:
            path = args.corpus/'vp8-video-simple.webm'
            raw = path.read_bytes()
            layout = scan(raw)
            laced = bytearray(raw)
            laced[layout['blocks'][0]['payload']-1] |= 2
            laced_path = temp/'laced.webm'
            laced_path.write_bytes(laced)
            head = layout['headBytes']
            oversized_path = temp/'oversized-cluster.webm'
            oversized_path.write_bytes(raw[:head]+ebml(0x1f43b675, bytes(16777217)))
            for name, input_path, mode, duration, fault, expected in (
                ('codec-mismatch', path, 'continuous-webm-av1-video-v1', SECONDS*10000000, None, 'unsupported-stream-webm-configuration'),
                ('missing-opus', path, 'continuous-webm-vp8-opus-v1', SECONDS*10000000, None, 'unsupported-stream-webm-configuration'),
                ('wrong-endpoint', path, 'continuous-webm-vp8-video-v1', SECONDS*10000000-500000, None, 'unsupported-stream-webm-clock'),
                ('cancel-wait', path, 'continuous-webm-vp8-video-v1', SECONDS*10000000, 'cancel-wait', 'cancelled'),
                ('unconfirmed-eof', path, 'continuous-webm-vp8-video-v1', SECONDS*10000000, 'unconfirmed-eof', 'unconfirmed-eof'),
                ('integrity-mismatch', path, 'continuous-webm-vp8-video-v1', SECONDS*10000000, 'integrity', 'integrity-mismatch'),
                ('lacing', laced_path, 'continuous-webm-vp8-video-v1', SECONDS*10000000, None, 'unsupported-stream-webm-video-duration'),
                ('cluster-limit', oversized_path, 'continuous-webm-vp8-video-v1', SECONDS*10000000, None, 'stream-webm-cluster-limit'),
            ):
                result, _ = serve(args.worker, input_path, mode=mode, format_hint='webm', video_duration=duration,
                                  growing=fault is not None, fault=fault, cut_seconds=(4,8,12,16))
                assert result['exitCode']==1 and result['result']['code']==expected, (name, result)
                rejected[name] = result['exitCode']
    return dict(rows=rows, rejected=rejected)


def player_cases(args):
    rows = []
    registration = None
    if args.edition=='classic':
        from windows.progressive_family_test import registration_snapshot
        registration = registration_snapshot()
    with tempfile.TemporaryDirectory(prefix='foxicmp-webm-main-') as work:
        root = Path(work)
        profile = Path(args.player).parent if args.edition=='classic' else root/'profile'
        peer = Peer(args.edition, args.player, profile, root, args.reader_runner)
        peer.launch()
        try:
            for name, mode, source, video, audio in cases(args.corpus, args.case):
                folder = root/name
                folder.mkdir()
                raw = source.read_bytes()
                part = folder/'source.part'
                value = snapshot(part, raw, complete=False, prefix=4)
                value['timeline']['duration'] = str(SECONDS*10000000)
                value['tracks'][0].update(codecHint=EXTENDED_WEBM_PROFILES[mode]['videoCodec'], containerHint='webm')
                if audio:
                    sound = dict(value['tracks'][0], id='sound', kind='audio', codecHint='opus')
                    value['tracks'].append(sound)
                sdk = ContinuousRemux(peer.client, folder, mode=mode)
                sdk.prepare(value, 'clip')

                def poll(predicate, label, playback=False, timeout=25):
                    def check():
                        state = sdk.status()
                        (args.report.parent/'last-job.json').write_text(json.dumps(state, indent=2))
                        assert not state['errorCode'], (name, state)
                        if not state['outputComplete']:
                            try:
                                sdk.heartbeat()
                            except ApiError as error:
                                assert error.code=='continuous-closed', error.reply
                        if playback:
                            state = sdk.playback_status()
                            (args.report.parent/'last-playback.json').write_text(json.dumps(state, indent=2))
                            assert state['state'] not in ('closed','error') and state['audio'] is audio, (name, state)
                        return state if predicate(state) else None
                    return wait(check, name+' '+label, timeout)

                def publish(count, complete=False):
                    nonlocal value
                    previous = value
                    with part.open('ab') as stream:
                        stream.write(raw[part.stat().st_size:count])
                    value = copy.deepcopy(value)
                    value['revision'] = str(int(value['revision'])+1)
                    phase = 'complete' if complete else 'downloading'
                    value['writer']['state'] = value['resources'][0]['state'] = phase
                    for track in value['tracks']:
                        track['state'] = phase
                    value['resources'][0]['ranges'] = [dict(offset='0', length=str(count))]
                    value = protect_snapshot(value, folder, previous=previous)
                    sdk.publish(value)

                try:
                    waiting = poll(lambda s:s['producerCode']=='waiting', 'wait for partial header')
                    assert not waiting['playbackReady']
                    cut = next(b['payload']+5 for b in video if b['ticks']>=14000)
                    publish(cut)
                    ready = poll(lambda s:s['playbackReady'] and s['indexedEndSeconds']>=11, 'growing playback ready')
                    assert not ready['outputComplete'] and not ready['videoSampleCountFinal']
                    sdk.play(title='Private WebM '+name)
                    seen = poll(lambda s:s['state']=='playing' and s['positionSeconds']>=10.1,
                                'ten seconds before complete', True)
                    assert seen['mode']==mode and not seen['outputComplete']
                    if audio and args.edition=='qt':
                        assert seen['audioCodec']=='opus' and not seen['pcmWindowApplied'], seen
                    if args.edition=='classic':
                        assert seen['sourceConnected'], seen
                    sdk.control('pause')
                    poll(lambda s:s['state']=='paused', 'pause', True)
                    sdk.control('seek', seconds=1, allow_growing=True)
                    poll(lambda s:s['state']=='paused' and abs(s['positionSeconds']-1)<.15, 'growing seek', True)
                    sdk.control('seek', seconds=16, allow_growing=True, wait_for_range=True)
                    poll(lambda s:s['pendingSeekSeconds']==16 and s['seekState']=='waiting-range', 'queued seek', True)
                    publish(len(raw))
                    held = poll(lambda s:s['producerCode']=='waiting' and s['appliedRevision']==value['revision'],
                                'hold until complete')
                    assert not held['outputComplete']
                    publish(len(raw), True)
                    complete = poll(lambda s:s['outputComplete'], 'verified EOF')
                    assert complete['videoSampleCountFinal'] and int(complete['indexedVideoSamples'])==len(video)
                    if audio:
                        assert complete['audioSampleCountFinal'] and int(complete['indexedAudioSamples'])>0
                    poll(lambda s:s['state']=='paused' and s['pendingSeekSeconds'] is None
                         and abs(s['positionSeconds']-16)<.15, 'queued seek applied', True)
                    cache = Path(str(peer.registry)+'.continuous-cache')
                    files = list(cache.glob('remux-*/output.fmp4'))
                    assert len(files)==1, files
                    output = folder/'verified.webm'
                    shutil.copyfile(files[0], output)
                    exact(source, output, audio)
                    sdk.control('play')
                    poll(lambda s:s['state']=='finished', 'natural EOF', True)
                    rows.append(dict(case=name, mode=mode, beforeCompleteSeconds=seen['positionSeconds'],
                                     sourceSha256=hashlib.sha256(raw).hexdigest(), videoPackets=len(video),
                                     exactPacketsClockDecode=True, pauseSeekAndQueuedSeek=True, naturalEof=True))
                finally:
                    receipt = sdk.close(timeout=3)
                    assert receipt['released'] and receipt['sourceFilesClosed'], receipt
                    confirmed = int(value['resources'][0]['ranges'][0]['length'])
                    assert sdk.forget(timeout=3)['forgotten'] and part.read_bytes()==raw[:confirmed]
                assert not list(cache.glob('remux-*'))
        finally:
            if peer.process and peer.process.poll() is None:
                peer.kill('cleanup-owned-idle-process')
            peer.cleanup()
            for log in root.glob('launch-*.log'):
                (args.report.parent/log.name).write_bytes(log.read_bytes())
            if registration is not None:
                assert registration_snapshot()==registration
    return dict(rows=rows, classicRegistrationUnchanged=registration is not None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', required=True, type=Path)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--case', action='append')
    parser.add_argument('--worker', type=Path)
    parser.add_argument('--edition', choices=('classic', 'qt'))
    parser.add_argument('--player', type=Path)
    parser.add_argument('--reader-runner', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.prepare:
        prepare(args.corpus)
        return
    args.report.parent.mkdir(parents=True, exist_ok=True)
    if args.worker:
        result = worker_cases(args)
    else:
        result = player_cases(args)
    assert result['rows']
    result['result'] = 'PASS'
    args.report.write_text(json.dumps(result, indent=2)+'\n')


if __name__=='__main__':
    main()
