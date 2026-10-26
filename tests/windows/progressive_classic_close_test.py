"""Hold an owned test Classic's real STA, exercise close/start/exit, then resume it.

No decoder hooks and no installed profiles are used. Suspending the idle graph
thread makes completion impossible until the test explicitly resumes its owner.
"""
import argparse
import configparser
import ctypes
from ctypes import wintypes as w
import json
from pathlib import Path
import subprocess
import time
import traceback
import uuid

from progressive_family_test import ProgressiveClient, ApiError, post_command, registration_snapshot, prefix, wait_status


def run(args):
    player = Path(args.player).resolve()
    raw = player.with_suffix('.ini').read_bytes()
    settings = configparser.ConfigParser()
    settings.read_string(raw.decode('utf-16'))
    assert settings.get('Settings', 'ContextMenuPrompted') == '1'
    assert settings.get('Settings', 'ContextMenuEnabled') == '0'
    registration = registration_snapshot()
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    user = ctypes.WinDLL('user32', use_last_error=True)
    class ThreadEntry(ctypes.Structure):
        _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('tid', w.DWORD),
                    ('pid', w.DWORD), ('priority', w.LONG), ('delta', w.LONG), ('flags', w.DWORD)]
    kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
    kernel.Thread32First.argtypes = [w.HANDLE, ctypes.POINTER(ThreadEntry)]
    kernel.Thread32Next.argtypes = [w.HANDLE, ctypes.POINTER(ThreadEntry)]
    kernel.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenThread.restype = w.HANDLE
    kernel.GetThreadDescription.argtypes = [w.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    kernel.SuspendThread.argtypes = [w.HANDLE]
    kernel.ResumeThread.argtypes = [w.HANDLE]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    user.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, ctypes.POINTER(ctypes.c_size_t)]
    callback = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]

    def wait(check, seconds=12):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            value = check()
            if value:
                return value
            time.sleep(.03)
        raise AssertionError('Close state machine condition timed out')

    def graph_thread(pid):
        snapshot = kernel.CreateToolhelp32Snapshot(4, 0)
        assert snapshot != ctypes.c_void_p(-1).value
        try:
            names = []
            entry = ThreadEntry(size=ctypes.sizeof(ThreadEntry))
            valid = kernel.Thread32First(snapshot, ctypes.byref(entry))
            while valid:
                if entry.pid == pid:
                    handle = kernel.OpenThread(0x802, False, entry.tid)
                    if handle:
                        description = ctypes.c_void_p()
                        hr = kernel.GetThreadDescription(handle, ctypes.byref(description))
                        name = ctypes.wstring_at(description) if hr >= 0 and description else ''
                        if description:
                            kernel.LocalFree(description)
                        names.append((entry.tid, hr, name))
                        if name == 'FoxicMP graph STA':
                            return handle
                        kernel.CloseHandle(handle)
                valid = kernel.Thread32Next(snapshot, ctypes.byref(entry))
            raise AssertionError(('Owned graph STA not found', names))
        finally:
            kernel.CloseHandle(snapshot)

    def window(pid):
        found = []
        @callback
        def visit(hwnd, unused):
            owner = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            name = ctypes.create_unicode_buffer(256)
            user.GetClassNameW(hwnd, name, 256)
            if owner.value == pid and name.value == 'FoxicMP':
                found.append(hwnd)
            return True
        user.EnumWindows(visit, 0)
        assert len(found) == 1, found
        return found[0]

    def ping(hwnd):
        result = ctypes.c_size_t()
        started = time.perf_counter()
        assert user.SendMessageTimeoutW(hwnd, 0, 0, 0, 2, 500, ctypes.byref(result)), 'UI blocked by STA close'
        return time.perf_counter() - started

    def fixture(root):
        root.mkdir(parents=True)
        value = dict(schemaVersion=1, sessionId=uuid.uuid4().hex, generation=1, revision=1,
                     state='downloading', duration100ns=200000000, tracks={})
        for kind, name in [('video', 'video.mp4'), ('audio', 'audio.m4a')]:
            data = (Path(args.fixtures) / 'progressive' / name).read_bytes()
            length = prefix(data, 12)
            path = root / (kind + '.part')
            path.write_bytes(data[:length])
            value['tracks'][kind] = dict(path=str(path.resolve()), container='mp4' if kind == 'video' else 'm4a',
                fragmented=True, initializationReady=True, availablePrefixBytes=length,
                expectedTotalBytes=None, state='downloading')
        return value

    cases = []
    for mode in ('stop-loaded', 'stop-loading', 'replace', 'supersede', 'exit'):
        root = Path(args.report) / mode
        source = fixture(root / 'first')
        client = ProgressiveClient(str(player))
        process = subprocess.Popen([str(player), '/new', '/nofocus'], cwd=player.parent)
        thread = None
        suspended = False
        try:
            def connected():
                try:
                    reply = client.request(dict(version=1, action='capabilities'))
                    assert reply['pid'] == process.pid
                    return reply
                except OSError:
                    return None
            caps = wait(connected)
            assert caps['graphShutdown']['progressiveStaCloseAsync']
            hwnd = window(process.pid)
            thread = graph_thread(process.pid)
            if mode == 'stop-loading':
                assert kernel.SuspendThread(thread) == 0
                suspended = True
            first = client.start(source, activate=False, track_reader=True)
            reader = first['readerId']
            if mode != 'stop-loading':
                wait_status(client, source['sessionId'], lambda reply: reply.get('positionSeconds', 0) > .2)
                assert kernel.SuspendThread(thread) == 0
                suspended = True
            started = time.perf_counter()
            stopped = client.command('cancel', source['sessionId'])
            elapsed = time.perf_counter() - started
            assert not stopped['active'] and not stopped['released'], stopped
            assert elapsed < 1, ('Cancel waited for the suspended graph STA', elapsed)
            assert not client.reader_status(reader)['released'], 'Premature reader release'
            pings = [ping(hwnd) for unused in range(8)]
            pending_reader = None
            if mode in {'replace', 'supersede'}:
                second = fixture(root / 'second')
                # A cancelled session is inactive: the normal start must remain compatible
                # even while its old graph is still closing. Active pending start stays busy.
                pending = client.start(second, replace=mode == 'supersede', activate=False, track_reader=True)
                pending_reader = pending['readerId']
                assert pending['active'] and pending['state'] == 'buffering' and pending['positionSeconds'] == 0, pending
                assert not client.reader_status(pending_reader)['released']
                competing = fixture(root / 'competing')
                try:
                    client.start(competing, activate=False)
                    raise AssertionError('An active pending start was replaced without permission')
                except ApiError as error:
                    assert error.code == 'session_busy', error.reply
                if mode == 'supersede':
                    third = fixture(root / 'third')
                    newest = client.start(third, replace=True, activate=False, track_reader=True)
                    assert newest['sessionId'] == third['sessionId']
                    client.command('cancel', third['sessionId'])
                pings.extend(ping(hwnd) for unused in range(8))
            if mode == 'exit':
                post_command(process.pid, 0)
                time.sleep(.2)
                assert process.poll() is None, 'Window owner exited before STA teardown'
                pings.append(ping(hwnd))
            assert kernel.ResumeThread(thread) == 1
            suspended = False
            if mode == 'exit':
                process.wait(timeout=12)
                assert process.returncode == 0, process.returncode
            else:
                wait(lambda: client.reader_status(reader)['released'])
                if mode == 'replace':
                    playing = wait_status(client, second['sessionId'], lambda reply: reply.get('positionSeconds', 0) > .2)
                    assert playing['active']
                    client.command('cancel', second['sessionId'])
                if pending_reader:
                    wait(lambda: client.reader_status(pending_reader)['released'])
                if mode == 'supersede':
                    wait(lambda: client.reader_status(newest['readerId'])['released'])
                    time.sleep(.3)
                    assert not client.command('status', third['sessionId'])['active'], 'Cancelled pending open activated late'
                ping(hwnd)
            cases.append(dict(case=mode, result='PASS', cancelSeconds=elapsed,
                              maxUiPingSeconds=max(pings), ownerHeldUntilResume=True))
        finally:
            if suspended:
                kernel.ResumeThread(thread)
            if thread:
                kernel.CloseHandle(thread)
            if process.poll() is None:
                post_command(process.pid, 0)
                try:
                    process.wait(timeout=12)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    raise
            assert process.returncode == 0, process.returncode
            assert registration_snapshot() == registration, 'Installed shell registration changed'
    return cases


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for option in ('player', 'fixtures', 'report'):
        parser.add_argument('--' + option, required=True)
    args = parser.parse_args()
    report = Path(args.report)
    report.mkdir(parents=True, exist_ok=True)
    try:
        (report / 'result.json').write_text(json.dumps(run(args), indent=2), encoding='utf-8')
    except BaseException:
        (report / 'exception.txt').write_text(traceback.format_exc(), encoding='utf-8')
        raise
