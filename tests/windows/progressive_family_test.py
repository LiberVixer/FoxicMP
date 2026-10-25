"""Exercise the version-1 downloader contract against an isolated Classic copy.
No downloader repository, installed profile or shell registration is modified.
"""
import argparse
import configparser
import ctypes
from ctypes import wintypes as w
import json
from pathlib import Path
import subprocess
import sys
import time
import traceback
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'qt' / 'tools'))
from progressive_client import ProgressiveClient, ApiError
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from progressive_v1_conformance import exercise_directory_contract


def registration_snapshot():
    """Read installed Classic shell commands/icons without changing HKCU."""
    import winreg
    result = {}
    def visit(path):
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
                count, values, _ = winreg.QueryInfoKey(key)
                result[path] = [winreg.EnumValue(key, i) for i in range(values)]
                for i in range(count):
                    visit(path + '\\' + winreg.EnumKey(key, i))
        except FileNotFoundError:
            pass
    base = r'Software\Classes'
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, base) as key:
        for i in range(winreg.QueryInfoKey(key)[0]):
            name = winreg.EnumKey(key, i)
            if name.lower().startswith('foxicmp'):
                visit(base + '\\' + name)
    associations = base + r'\SystemFileAssociations'
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, associations) as key:
        for i in range(winreg.QueryInfoKey(key)[0]):
            visit(associations + '\\' + winreg.EnumKey(key, i) + r'\shell\FoxicMP')
    visit(base + r'\Directory\shell\FoxicMP64.play')
    visit(base + r'\Directory\shell\FoxicMP64.enqueue')
    return result


def prefix(data, seconds):
    at = fragments = 0
    while at + 8 <= len(data):
        size = int.from_bytes(data[at:at+4], 'big')
        kind = data[at+4:at+8]
        if size == 1: size = int.from_bytes(data[at+8:at+16], 'big')
        if size < 8 or at + size > len(data): raise ValueError('Bad fixture MP4')
        at += size
        if kind == b'mdat':
            fragments += 1
            if fragments == seconds: return at
    return len(data)


def post_command(pid, command):
    user = ctypes.WinDLL('user32', use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback_type, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    found = []
    @callback_type
    def visit(hwnd, _):
        owner = w.DWORD();user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(256);user.GetClassNameW(hwnd, name, 256)
        if owner.value == pid and name.value == 'FoxicMP': found.append(hwnd)
        return True
    user.EnumWindows(visit, 0)
    if not found: raise RuntimeError('Owned test Classic window missing')
    user.PostMessageW(found[0], 0x111 if command else 0x10, command, 0)


def wait_status(client, session, predicate, seconds=15):
    deadline = time.monotonic()+seconds
    last = {}
    while time.monotonic() < deadline:
        last = client.command('status', session)
        if predicate(last): return last
        if last.get('state') == 'error': raise AssertionError(last)
        time.sleep(.1)
    raise AssertionError(('Timed out', last))


def run(args):
    player = Path(args.player).resolve()
    ini = player.with_suffix('.ini')
    before = ini.read_bytes()
    settings = configparser.ConfigParser()
    settings.read_string(before.decode('utf-16') if before.startswith((b'\xff\xfe', b'\xfe\xff')) else before.decode('utf-8-sig'))
    assert settings.get('Settings', 'ContextMenuPrompted') == '1'
    assert settings.get('Settings', 'ContextMenuEnabled') == '0'
    registration = registration_snapshot()
    client = ProgressiveClient(str(player))
    assert ini.read_bytes() == before, 'Read-only discovery modified the profile'
    process = subprocess.Popen([str(player), '/new', '/nofocus'], cwd=player.parent)
    results = []
    try:
        deadline = time.monotonic()+20
        while True:
            try:
                caps = client.request({'version': 1, 'action': 'capabilities'})
                assert caps['pid'] == process.pid, 'Unexpected test endpoint owner'
                break
            except (OSError, RuntimeError):
                if process.poll() is not None: raise RuntimeError(('Player exited', process.returncode))
                if time.monotonic() >= deadline: raise
                time.sleep(.1)
        common = exercise_directory_contract(client, args.fixtures, Path(args.report)/'relative-contract')
        results.append(dict(contract='sessionDirectory', scenarios=common))
        for fragmented in (False, True):
            folder = Path(args.report)/('fragmented' if fragmented else 'indexed')
            folder.mkdir(parents=True)
            fixture = Path(args.fixtures)/('progressive' if fragmented else 'progressive-indexed')
            data = [(fixture / name).read_bytes() for name in ('video.mp4','audio.m4a')]
            paths = [folder/'видео.part', folder/'звук.part']
            snapshot = dict(schemaVersion=1, sessionId=uuid.uuid4().hex, generation=1, revision=1,
                            state='downloading', duration100ns=200000000, tracks={})
            for i, kind in enumerate(('video','audio')):
                count = prefix(data[i], 5) if fragmented else len(data[i])*3//10
                paths[i].write_bytes(data[i][:count])
                snapshot['tracks'][kind] = dict(path=str(paths[i]), container='m4a' if i else 'mp4',
                    fragmented=fragmented, initializationReady=True, availablePrefixBytes=count,
                    expectedTotalBytes=None if fragmented else len(data[i]), state='downloading')
            first = client.start(snapshot, title="Проверка недокачанного ролика", activate=False)
            assert first['state'] == 'buffering', first
            assert client.start(snapshot, title="Проверка недокачанного ролика", activate=False)['sessionId'] == snapshot['sessionId']
            try:
                client.command('cancel', 'f'*32)
                raise AssertionError('Stale cancel was accepted')
            except ApiError as error: assert error.code == 'unknown_session'
            time.sleep(.3)
            initial = client.command('status', snapshot['sessionId'])
            assert initial['positionSeconds'] < .2, initial
            snapshot['revision'] += 1
            for i, kind in enumerate(('video','audio')):
                count = prefix(data[i], 12) if fragmented else len(data[i])*7//10
                with paths[i].open('ab') as stream: stream.write(data[i][stream.tell():count])
                snapshot['tracks'][kind]['availablePrefixBytes'] = count
            client.update(snapshot)
            playing = wait_status(client, snapshot['sessionId'], lambda r:r['state']=='playing' and r['positionSeconds']>.2)
            try:
                client.update(snapshot)
                raise AssertionError('Stale update was accepted')
            except ApiError as error: assert error.code == 'invalid_update'
            post_command(process.pid, 888)
            wait_status(client, snapshot['sessionId'], lambda r:r['state']=='paused')
            for i, kind in enumerate(('video','audio')):
                final = paths[i].with_suffix('.done'); paths[i].rename(final)
                with final.open('ab') as stream: stream.write(data[i][stream.tell():])
                snapshot['tracks'][kind].update(availablePrefixBytes=len(data[i]), expectedTotalBytes=len(data[i]), state='complete')
            snapshot.update(state='complete', revision=snapshot['revision']+1)
            completed = client.update(snapshot, complete=True)
            assert completed['complete'] and completed['state']=='paused', completed
            post_command(process.pid, 887)
            terminal = wait_status(client, snapshot['sessionId'], lambda r:r['state']=='finished', 28)
            cancelled = client.command('cancel', snapshot['sessionId'])
            assert not cancelled['active'], cancelled
            results.append(dict(fragmented=fragmented, playing=playing, complete=completed, terminal=terminal))
        return results
    finally:
        if process.poll() is None:
            post_command(process.pid, 0)
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill();process.wait();raise
        assert process.returncode == 0, ('Player exit', process.returncode)
        assert registration_snapshot() == registration, 'Installed Classic shell registration changed'


if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--player',required=True);parser.add_argument('--fixtures',required=True);parser.add_argument('--report',required=True)
    arguments=parser.parse_args();report=Path(arguments.report);report.mkdir(parents=True,exist_ok=True)
    try:
        result=run(arguments);(report/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    except BaseException:
        (report/'exception.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
