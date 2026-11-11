"""Teardown and worker death within owned Classic real seek stacks.

Not a shipping nested message loop: FOXICMP_PRIVATE_CLASSIC_SEEK_TEST is required.
"""
import argparse
import configparser
import copy
import ctypes
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import shutil
import sys
import subprocess
import tempfile
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'qt' / 'tools'))
from progressive_player_crash_recovery import Peer, sha, wait
from progressive_continuous import ContinuousRemux, MODE, AAC_MODE
from progressive_backend_demux_conformance import snapshot
from progressive_stream_remux_worker_test import fixture
from progressive_stream_aac_aligned_test import fixture as av_fixture
from progressive_continuous_test_media import source_snapshot, source_cuts
from progressive_continuous_heartbeat_conformance import WorkerExit
from progressive_continuous_worker_crash_conformance import OwnedWorker
from progressive_integrity import protect_snapshot
from progressive_family_test import registration_snapshot, post_command


def run(args):
    report = args.report_directory.resolve()
    report.mkdir(parents=True, exist_ok=False)
    registration = registration_snapshot()
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.EnumWindows.argtypes = [ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM), w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
    user.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, ctypes.POINTER(ctypes.c_size_t)]
    callback = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    rows = []
    with tempfile.TemporaryDirectory(prefix='foxicmp-seek-teardown-') as tmp:
        root = Path(tmp)
        audio = args.audio_profile != 'video'
        selected_mode = AAC_MODE if audio else MODE
        rate, channels, fps, bframes = (48000,1,'25',2) if args.audio_profile == '48000-mono' else (44100,2,'30000/1001',3)
        original = av_fixture(root,'original',rate,channels,fps,bframes,False,False,
            seconds=24.137,samples=rate*24+137)[0] if audio else fixture(root,'original')
        raw = original.read_bytes()
        before_sha = sha(original)
        ordinary = root / 'replacement.mp4'
        encoded = subprocess.run(['ffmpeg','-v','error','-y','-i',str(original),'-c','copy',
            '-movflags','empty_moov+frag_keyframe+default_base_moof',str(ordinary)],capture_output=True,timeout=30)
        assert encoded.returncode == 0, (encoded.returncode, encoded.stderr.decode(errors='replace'))
        ordinary_raw = ordinary.read_bytes()
        ordinary_sha = sha(ordinary)
        cases = [(phase, mode) for phase in ('frame-before', 'graph-before', 'graph-after', 'frame-after')
                 for mode in ((args.mode,) if args.mode else ('stop', 'replace', 'close'))]
        if args.smoke:
            cases = [('graph-before', args.mode or 'stop')]
        cases = [(p,m) for p,m in cases if (not args.phase or p==args.phase) and (not args.mode or m==args.mode)]
        assert cases, 'No selected seek-boundary scenarios'
        for phase, mode in cases:
            label = phase + '-' + mode
            folder = root / label
            folder.mkdir()
            artifacts = report / label
            artifacts.mkdir()
            stage = folder / 'player'
            shutil.copytree(args.player.parent, stage)
            player = stage / args.player.name
            settings = configparser.ConfigParser()
            settings.optionxform = str
            text = player.with_suffix('.ini').read_bytes()
            settings.read_string(text.decode('utf-16') if text.startswith(b'\xff\xfe') else text.decode('utf-8-sig'))
            for section, values in {'Settings': {'ContextMenuPrompted': '1', 'ContextMenuEnabled': '0'},
                                    'Audio': {'AudioRenderer': 'Null Audio Renderer (Uncompressed)'},
                                    'Video': {'VideoRenderer': '11'}}.items():
                if not settings.has_section(section):
                    settings.add_section(section)
                for key, value in values.items():
                    settings.set(section, key, value)
            with player.with_suffix('.ini').open('w', encoding='utf-16') as stream:
                settings.write(stream, space_around_delimiters=False)
            source = folder / 'source'
            source.mkdir()
            path = source / 'input.part'
            prefix = source_cuts(original,AAC_MODE)[12] if audio else len(raw)*2//3
            model = source_snapshot(path,raw,prefix=prefix,complete=False,mode=selected_mode)
            path.write_bytes(raw)
            peer = Peer('classic', player, folder / 'unused-profile', folder, args.reader_runner)
            previous = os.environ.get('FOXICMP_PRIVATE_SEEK_DIRECTORY')
            os.environ['FOXICMP_PRIVATE_SEEK_DIRECTORY'] = str(artifacts)
            sdk = None
            watch = None
            replacement = None
            replacement_close = None
            replacement_forget = None
            scenario_passed = False
            close_proof = None
            forget_proof = None
            killed = None
            release_trace = []
            retry = None
            retry_proof = None
            retry_watch = None
            try:
                peer.launch()
                sdk = ContinuousRemux(peer.client, str(source), mode=selected_mode)
                sdk.prepare(model, 'clip')
                def producer_ready():
                    value = sdk.status()
                    assert value['state'] != 'error', value
                    if not value['outputComplete']:
                        sdk.heartbeat()
                    return value if value['playbackReady'] and value['indexedEndSeconds'] > 2 else None
                ready = wait(producer_ready, 'owned source ready', 20)
                if audio:
                    assert ready['audioWindowReady'] and ready['audioSampleRate']==rate and ready['audioChannels']==channels and ready['audioPresentationSamples']==str(rate*24+137), ready
                watch = OwnedWorker(peer, int(ready['workerId'])) if mode == 'worker-crash' else WorkerExit(int(ready['workerId']))
                sdk.play(title='Main reentry ' + label)
                wait(lambda: (lambda s: s if s['state'] == 'playing' and s['positionSeconds'] > .15 and (not audio or s['audio'] and s['pcmWindowApplied']) else None)(sdk.playback_status()), 'owned playing', 20)
                sdk.control('pause')
                held = wait(lambda: (lambda s: s if s['state'] == 'paused' else None)(sdk.playback_status()), 'owned paused', 10)
                assert held['indexedEndSeconds'] < 21 and held['remainingGrowingSeeks'] == 16, held
                if audio:
                    assert held['audio'] and held['pcmWindowApplied'], held
                if mode == 'replace':
                    second = folder / 'replacement'
                    second.mkdir()
                    second_path = second / 'second.part'
                    second_model = source_snapshot(second_path,ordinary_raw,complete=True,mode=selected_mode)
                    for track in second_model['tracks']:
                        track.update(containerHint='fmp4-muxed' if audio else 'fmp4',initializationReady=True)
                    second_model['tracks'][0]['codecHint']='h264'
                windows = []
                @callback
                def visit(hwnd, _):
                    pid = w.DWORD()
                    user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                    name = ctypes.create_unicode_buffer(256)
                    user.GetClassNameW(hwnd, name, len(name))
                    if pid.value == peer.process.pid and name.value == 'FoxicMP':
                        windows.append(hwnd)
                    return True
                user.EnumWindows(visit, 0)
                assert len(windows) == 1
                queued = sdk.control('seek', seconds=21, allow_growing=True, wait_for_range=True)
                assert queued['pendingSeekSeconds'] == 21 and queued['seekRevision'] == '1', queued
                (artifacts / 'arm').write_text(phase)
                new = copy.deepcopy(model)
                new['revision'] = '2'
                new['resources'][0]['ranges'] = [dict(offset='0', length=str(len(raw)))]
                published = protect_snapshot(new, source, previous=model)
                sdk.publish(published)
                # Read-only producer requests may dispatch inside the barrier;
                # reaching it is established by its file, not IPC timing.
                wait(lambda: (artifacts / 'ready').exists(), 'exact main/graph barrier', 15)
                assert (artifacts / 'ready').read_text() == phase
                replies = []
                sent = []
                def send(message, value):
                    result = ctypes.c_size_t()
                    began = time.monotonic()
                    assert user.SendMessageTimeoutW(windows[0], message, value, 0, 2, 3000, ctypes.byref(result)), ctypes.get_last_error()
                    sent.append(dict(message=message, wparam=value, seconds=time.monotonic()-began, result=result.value))
                if mode == 'worker-crash':
                    killed = watch.terminate()
                    (artifacts / 'worker-kill.json').write_text(json.dumps(killed, indent=2))
                    assert killed['workerExitCode'] == 197, killed
                    began = time.monotonic()
                    def failed_source():
                        value = sdk.status()
                        release_trace.append(dict(seconds=time.monotonic()-began, producer=value))
                        assert peer.process.poll() is None and not value['outputComplete'], value
                        if value['errorCode']:
                            assert value['errorCode'] in ('backend-worker-exited','backend-pipe-error'), value
                        return value if value['errorCode'] and value['sourceFilesClosed'] else None
                    try:
                        nested = wait(failed_source, 'worker death closes source within seek', 2)
                    finally:
                        (artifacts / 'worker-release-trace.json').write_text(json.dumps(release_trace, indent=2))
                    elapsed = time.monotonic()-began
                    assert killed['terminationSeconds']+elapsed < 2, (killed, elapsed)
                    killed['sourceCloseSeconds'] = elapsed
                    if not nested['released']:
                        assert nested['cleanupCode'] == 'cache-in-use', nested
                        nested_receipts = peer.client.readers(model['sessionId'], 1)
                        assert any(not v['released'] and not v['filesClosed'] for v in nested_receipts), nested_receipts
                        killed['cacheHeldWithinSeek'] = nested_receipts
                    replies.append(nested)
                elif mode == 'stop':
                    replies.append(sdk.control('stop'))
                    nested = sdk.playback_status()
                    assert nested['state'] == 'closed' and nested['pendingSeekSeconds'] is None, nested
                elif mode == 'replace':
                    replacement = peer.client.start(second_model, title='Replacement target', replace=True,
                        activate=False, session_directory=second, track_reader=True)
                    replies.append(replacement)
                    assert replacement['sessionId'] == second_model['sessionId'] and replacement['readerId']
                    nested = peer.client.command('status', second_model['sessionId'])
                    assert nested['sessionId'] == second_model['sessionId'] and nested['readerId'] == replacement['readerId'], nested
                else:
                    send(0x10, 0)
                    nested = dict(windowCloseSent=True, ownedPid=peer.process.pid)
                (artifacts / 'nested.json').write_text(json.dumps(dict(replies=replies, nested=nested, sent=sent), indent=2))
                (artifacts / 'release').write_text('continue')
                if mode == 'close':
                    peer.process.wait(timeout=20)
                    assert peer.process.returncode == 0, peer.process.returncode
                    final = dict(processExited=True, exitCode=peer.process.returncode)
                elif mode == 'worker-crash':
                    resumed_at = time.monotonic()
                    def dead_playback():
                        value = sdk.playback_status()
                        return value if value['state'] == 'error' and value['decoderReaders'] == 0 else None
                    final = wait(dead_playback, 'dead worker closes graph after seek unwind', 2)
                    assert final['error'] == nested['errorCode'] and final['pendingSeekSeconds'] is None and final['seekState'] != 'applied', final
                    def dead_released():
                        value = sdk.status()
                        assert value['errorCode']==nested['errorCode'] and not value['outputComplete'], value
                        return value if value['released'] and value['sourceFilesClosed'] else None
                    released_after_unwind = wait(dead_released, 'last decoder close releases cache after seek unwind', 2)
                    killed['releaseAfterBarrierSeconds'] = time.monotonic()-resumed_at
                    killed['releasedAfterUnwind'] = released_after_unwind
                    assert killed['releaseAfterBarrierSeconds'] < 2, killed
                elif mode == 'stop':
                    def stopped():
                        value = sdk.playback_status()
                        assert value['state'] != 'error', value
                        return value if value['state'] == 'closed' and value['pendingSeekSeconds'] is None else None
                    final = wait(stopped, 'stopped seek remains closed', 15)
                    assert final['seekState'] != 'applied', final
                else:
                    def playing_replacement():
                        value = peer.client.command('status', second_model['sessionId'])
                        assert value['state'] != 'error', value
                        return value if value['state'] == 'playing' and .15 < value['positionSeconds'] < 3 and (not audio or value.get('audioCodec')=='aac-lc' and all(int(value['decoderIoBytes'][kind])>0 for kind in ('audio','video'))) else None
                    final = wait(playing_replacement, 'replacement starts at its own position', 20)
                    assert final['sessionId'] == second_model['sessionId'] and final['readerId'] == replacement['readerId'], final
                    # Existing command id pauses the owned replacement graph.
                    post_command(peer.process.pid, 889)
                    final = wait(lambda: (lambda v: v if v['state']=='paused' else None)(peer.client.command('status',second_model['sessionId'])), 'replacement pause', 5)
                    assert final['positionSeconds'] < 3 and final['readerId'] == replacement['readerId'], final
                    stale = sdk.playback_status()
                    assert stale['state'] == 'closed' and stale['seekState'] != 'applied', stale
                # A stale reply is not enough: wait for the outer frame scope to leave.
                def trace_complete():
                    trace = [json.loads(line) for line in (artifacts / 'trace.jsonl').read_text().splitlines()]
                    return trace if any(x['stage'] == 'barrier-leave' for x in trace) and any(x['stage'] == 'frame-leave' for x in trace) else None
                trace = wait(trace_complete, 'outer main frame unwound', 5)
                assert all(x['pid'] == peer.process.pid and x['tid'] == x['ownerTid'] for x in trace), trace
                assert max(x['frameDepth'] for x in trace) == 1 and max(x['graphDepth'] for x in trace) <= 1, trace
                barrier = [x for x in trace if x['stage'] == 'barrier-enter']
                assert len(barrier) == 1 and barrier[0]['graphDepth'] == int(phase.startswith('graph-'))
                if mode != 'close':
                    assert any(x['stage'] == 'nested-ipc-dispatch' for x in trace)
                assert not any(x['stage'] == 'barrier-timeout' for x in trace), trace
                if mode == 'worker-crash':
                    receipts = peer.client.readers(model['sessionId'], 1)
                    assert receipts and all(v['released'] and v['filesClosed'] and v['reason']=='files-closed' for v in receipts), receipts
                    cache_root = Path(str(peer.registry) + '.continuous-cache')
                    assert not list(cache_root.glob('remux-*'))
                    close_proof = sdk.close(timeout=2)
                    assert close_proof['released'] and close_proof['sourceFilesClosed'], close_proof
                    forget_proof = sdk.forget(timeout=2)
                    assert forget_proof['forgotten'], forget_proof
                    retry = ContinuousRemux(peer.client, str(source), mode=selected_mode)
                    retry.prepare(published, 'clip')
                    def retry_ready():
                        value = retry.status()
                        assert not value['errorCode'], value
                        retry.heartbeat()
                        return value if value['playbackReady'] else None
                    restarted_producer = wait(retry_ready, 'explicit retry preparation after worker death', 20)
                    assert restarted_producer['jobId'] != ready['jobId'] and not set(restarted_producer['readerIds']) & set(ready['readerIds']), restarted_producer
                    retry_watch = WorkerExit(int(restarted_producer['workerId']))
                    assert not sdk.playback_status()['active'], 'Retry preparation automatically started playback'
                    retry.play(title='Explicit retry after seek worker death')
                    def retry_playing():
                        value = retry.playback_status()
                        assert value['state'] not in ('error','closed','finished'), value
                        return value if value['state']=='playing' and .15<value['positionSeconds']<3 and (not audio or value['audio'] and value['pcmWindowApplied']) else None
                    restarted = wait(retry_playing, 'explicit retry plays at its own position', 20)
                    assert restarted['playbackId'] != held['playbackId'] and not set(restarted['readerIds']) & set(held['readerIds']), restarted
                    retry.control('pause')
                    restarted_paused = wait(lambda: (lambda v:v if v['state']=='paused' else None)(retry.playback_status()), 'retry pause', 5)
                    assert restarted_paused['positionSeconds'] < 3 and restarted_paused['pendingSeekSeconds'] is None, restarted_paused
                    retry_close = retry.close(timeout=2)
                    assert retry_close['released'] and retry_close['sourceFilesClosed'], retry_close
                    retry_forget = retry.forget(timeout=2)
                    assert retry_forget['forgotten'], retry_forget
                    wait(retry_watch.exited, 'explicit retry worker exits after close', 5)
                    retry_proof = dict(producer=restarted_producer, playback=restarted, paused=restarted_paused,
                        readerReceipts=receipts, close=retry_close, forget=retry_forget, workerExit=retry_watch.proof())
                row = dict(profile=label, audioProfile=args.audio_profile, result='PASS', ready=ready, held=held, queued=queued, replies=replies,
                           nested=nested, sent=sent, final=final, trace=trace, graphScopeForced=phase.startswith('graph-'),
                           killedWorker=killed, releaseTrace=release_trace, explicitRetry=retry_proof)
                (artifacts / 'scenario.json').write_text(json.dumps(row, indent=2))
                rows.append(row)
                scenario_passed = True
            finally:
                (artifacts / 'release').write_text('cleanup')
                if peer.process is not None and peer.process.poll() is None:
                    for current in (retry, sdk):
                        if current is None or current.forgotten:
                            continue
                        try:
                            proof = current.close(timeout=15)
                            if scenario_passed:
                                assert proof['released'] and proof['sourceFilesClosed'], proof
                                forgotten = current.forget(timeout=15)
                                close_proof, forget_proof = proof, forgotten
                        except (OSError, RuntimeError):
                            if scenario_passed:
                                raise
                if peer.process is not None and peer.process.poll() is None and replacement is not None:
                    cancelled = peer.client.command('cancel', second_model['sessionId'])
                    if scenario_passed:
                        def released_replacement():
                            values = peer.client.readers(second_model['sessionId'],1)
                            return values if values and all(v['released'] and v['filesClosed'] for v in values) else None
                        replacement_close = wait(released_replacement,'replacement filesClosed',10)
                        replacement_forget = [peer.client.forget_reader(v['readerId']) for v in replacement_close]
                if peer.process is not None and peer.process.poll() is None:
                    post_command(peer.process.pid, 0)
                    try:
                        peer.process.wait(timeout=10)
                    except Exception:
                        peer.process.kill()
                        peer.process.wait()
                        raise
                assert peer.process is not None and peer.process.returncode == 0
                peer.history[-1].update(reason='clean-exit', exitCode=peer.process.returncode)
                worker_exit = None
                if watch is not None:
                    try:
                        wait(watch.exited,'owned remux worker exit after main teardown',5)
                        worker_exit = watch.proof()
                    finally:
                        watch.close()
                if retry_watch is not None:
                    retry_watch.close()
                receipts_before_cleanup = []
                if peer.registry.exists():
                    for receipt_path in peer.registry.glob('*.lease'):
                        value = peer.native('readerStatus', readerId=receipt_path.stem)['reader']
                        assert value['released'] and value['filesClosed'], value
                        receipts_before_cleanup.append(value)
                cache_root = Path(str(peer.registry) + '.continuous-cache')
                remaining_caches = [str(p.name) for p in cache_root.glob('remux-*')] if cache_root.exists() else []
                assert not remaining_caches, remaining_caches
                removed = peer.cleanup()
                if previous is None:
                    os.environ.pop('FOXICMP_PRIVATE_SEEK_DIRECTORY', None)
                else:
                    os.environ['FOXICMP_PRIVATE_SEEK_DIRECTORY'] = previous
            rows[-1].update(processHistory=peer.history, runtimePathsRemoved=removed, closeProof=close_proof, forgetProof=forget_proof, replacementClose=replacement_close, replacementForget=replacement_forget, receiptsBeforeCleanup=receipts_before_cleanup, remainingCachesAfterExit=remaining_caches, workerExit=worker_exit)
            assert sha(path) == before_sha and sha(ordinary) == ordinary_sha and registration_snapshot() == registration
    value = dict(result='PASS', count=len(rows), rows=rows, playerSha256=sha(args.player),
                 driverSha256=sha(__file__), audioProfile=args.audio_profile, temporaryWorkRemoved=not root.exists(),
                 actualCMainFrame=True, actualPublicIpc=True, actualDirectShowSetPositions=True,
                 compileOnlyNestedPump=True, shippingNestedPump=False, audioVideoHardware=False)
    (report / 'result.json').write_text(json.dumps(value, indent=2) + '\n')
    print(json.dumps(dict(result='PASS', count=len(rows))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--player', type=Path, required=True)
    parser.add_argument('--reader-runner', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    parser.add_argument('--audio-profile', choices=('video','48000-mono','44100-stereo'), default='video')
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--phase', choices=('frame-before','graph-before','graph-after','frame-after'))
    parser.add_argument('--mode', choices=('stop','replace','close','worker-crash'))
    args = parser.parse_args()
    args.player = args.player.resolve()
    run(args)
