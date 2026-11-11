"""Owned Classic main window + actual graph stack, compile-only nested IPC pump.

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
import tempfile
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'qt' / 'tools'))
from progressive_player_crash_recovery import Peer, sha, wait
from progressive_continuous import ContinuousRemux, MODE
from progressive_backend_demux_conformance import snapshot
from progressive_stream_remux_worker_test import fixture
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
    with tempfile.TemporaryDirectory(prefix='foxicmp-main-reentry-') as tmp:
        root = Path(tmp)
        original = fixture(root, 'original')
        raw = original.read_bytes()
        before_sha = sha(original)
        cases = [(phase, mode) for phase in ('frame-before', 'graph-before', 'graph-after', 'frame-after')
                 for mode in ('cancel', 'supersede', 'same-target', 'nested-poll')]
        if args.smoke:
            cases = [('graph-before', 'cancel')]
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
            model = snapshot(path, raw, prefix=len(raw) * 2 // 3, complete=False)
            model['timeline']['duration'] = '240000000'
            path.write_bytes(raw)
            peer = Peer('classic', player, folder / 'unused-profile', folder, args.reader_runner)
            previous = os.environ.get('FOXICMP_PRIVATE_SEEK_DIRECTORY')
            os.environ['FOXICMP_PRIVATE_SEEK_DIRECTORY'] = str(artifacts)
            sdk = None
            scenario_passed = False
            close_proof = None
            forget_proof = None
            try:
                peer.launch()
                sdk = ContinuousRemux(peer.client, str(source), mode=MODE)
                sdk.prepare(model, 'clip')
                def producer_ready():
                    value = sdk.status()
                    assert value['state'] != 'error', value
                    if not value['outputComplete']:
                        sdk.heartbeat()
                    return value if value['playbackReady'] and value['indexedEndSeconds'] > 2 else None
                wait(producer_ready, 'owned source ready', 20)
                sdk.play(title='Main reentry ' + label)
                wait(lambda: (lambda s: s if s['state'] == 'playing' and s['positionSeconds'] > .15 else None)(sdk.playback_status()), 'owned playing', 20)
                sdk.control('pause')
                held = wait(lambda: (lambda s: s if s['state'] == 'paused' else None)(sdk.playback_status()), 'owned paused', 10)
                assert held['indexedEndSeconds'] < 21 and held['remainingGrowingSeeks'] == 16, held
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
                sdk.publish(protect_snapshot(new, source, previous=model))
                # Read-only producer requests may dispatch inside the barrier;
                # reaching it is established by its file, not IPC timing.
                wait(lambda: (artifacts / 'ready').exists(), 'exact main/graph barrier', 15)
                assert (artifacts / 'ready').read_text() == phase
                replies = []
                if mode in ('cancel', 'same-target'):
                    replies.append(sdk.control('cancelSeek'))
                    assert replies[-1]['pendingSeekSeconds'] is None and replies[-1]['seekState'] == 'cancelled'
                if mode in ('supersede', 'same-target'):
                    replies.append(sdk.control('seek', seconds=1 if mode == 'supersede' else 21,
                                               allow_growing=True, wait_for_range=True))
                    assert replies[-1]['seekRevision'] == '2' and replies[-1]['remainingGrowingSeeks'] == 16
                if mode == 'nested-poll':
                    result = ctypes.c_size_t()
                    assert user.SendMessageTimeoutW(windows[0], 0x113, 0xF188, 0, 2, 3000, ctypes.byref(result)), ctypes.get_last_error()
                nested = sdk.playback_status()
                assert nested['playbackId'] == held['playbackId'] and nested['remainingGrowingSeeks'] == 16, nested
                (artifacts / 'nested.json').write_text(json.dumps(dict(replies=replies, nested=nested), indent=2))
                (artifacts / 'release').write_text('continue')
                def settled():
                    value = sdk.playback_status()
                    assert value['state'] != 'error', value
                    if mode == 'cancel':
                        return value if value['pendingSeekSeconds'] is None and value['seekState'] == 'cancelled' else None
                    target = 1 if mode == 'supersede' else 21
                    return value if value['pendingSeekSeconds'] is None and value['seekState'] == 'applied' and abs(value['positionSeconds'] - target) < .12 else None
                final = wait(settled, 'intent retained after outer seek', 15)
                expected = 16 if phase == 'frame-before' and mode == 'cancel' else 15 if phase == 'frame-before' or mode in ('cancel', 'nested-poll') else 14
                assert final['remainingGrowingSeeks'] == expected, (label, expected, final)
                assert final['seekRevision'] == ('2' if mode in ('supersede', 'same-target') else '1'), final
                # A stale reply is not enough: wait for the outer frame scope to leave.
                def trace_complete():
                    trace = [json.loads(line) for line in (artifacts / 'trace.jsonl').read_text().splitlines()]
                    return trace if any(x['stage'] == 'barrier-leave' for x in trace) and any(x['stage'] == 'frame-leave' for x in trace) else None
                trace = wait(trace_complete, 'outer main frame unwound', 5)
                assert all(x['pid'] == peer.process.pid and x['tid'] == x['ownerTid'] for x in trace), trace
                assert max(x['frameDepth'] for x in trace) == 1 and max(x['graphDepth'] for x in trace) <= 1, trace
                barrier = [x for x in trace if x['stage'] == 'barrier-enter']
                assert len(barrier) == 1 and barrier[0]['graphDepth'] == int(phase.startswith('graph-'))
                assert any(x['stage'] == 'nested-ipc-dispatch' for x in trace)
                if mode == 'nested-poll':
                    assert any(x['stage'] == 'poll-continuous-seek' for x in trace)
                if phase != 'frame-before' or mode not in ('cancel', 'supersede', 'same-target'):
                    assert any(x['stage'] == 'graph-call-succeeded' for x in trace), trace
                row = dict(profile=label, result='PASS', held=held, queued=queued, replies=replies,
                           nested=nested, final=final, trace=trace, graphScopeForced=phase.startswith('graph-'))
                (artifacts / 'scenario.json').write_text(json.dumps(row, indent=2))
                rows.append(row)
                scenario_passed = True
            finally:
                (artifacts / 'release').write_text('cleanup')
                if sdk is not None:
                    try:
                        close_proof = sdk.close(timeout=15)
                        if scenario_passed:
                            assert close_proof['released'] and close_proof['sourceFilesClosed'], close_proof
                            forget_proof = sdk.forget(timeout=15)
                    except (OSError, RuntimeError):
                        if scenario_passed:
                            raise
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
                removed = peer.cleanup()
                if previous is None:
                    os.environ.pop('FOXICMP_PRIVATE_SEEK_DIRECTORY', None)
                else:
                    os.environ['FOXICMP_PRIVATE_SEEK_DIRECTORY'] = previous
            rows[-1].update(processHistory=peer.history, runtimePathsRemoved=removed, closeProof=close_proof, forgetProof=forget_proof)
            assert sha(path) == before_sha and registration_snapshot() == registration
    value = dict(result='PASS', count=len(rows), rows=rows, playerSha256=sha(args.player),
                 driverSha256=sha(__file__), temporaryWorkRemoved=not root.exists(),
                 actualCMainFrame=True, actualPublicIpc=True, actualDirectShowSetPositions=True,
                 compileOnlyNestedPump=True, shippingNestedPump=False, audioVideoHardware=False)
    (report / 'result.json').write_text(json.dumps(value, indent=2) + '\n')
    print(json.dumps(dict(result='PASS', count=len(rows))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--player', type=Path, required=True)
    parser.add_argument('--reader-runner', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    parser.add_argument('--smoke', action='store_true')
    args = parser.parse_args()
    args.player = args.player.resolve()
    run(args)
