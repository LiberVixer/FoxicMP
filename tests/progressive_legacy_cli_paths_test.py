"""Qt main --harvester-session: observed provider packets, Stop/release, app exit.

The repository's offline provider is only observed, not changed. Use a private
profile and normal TEMP; this is not long-profile/TEMP or physical A/V testing.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import uuid
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/harvester-preview'))
from scripts.progressive_session import atomic_json
if os.name == 'nt':
    from scripts.progressive_pipe import Provider
else:
    from scripts.progressive_unix import Provider
from progressive_v1_conformance import fragment_prefix


def exercise(player, fixtures, report, length, finish_mode):
    events, packets, errors = [], [], []
    process = None
    mutex = threading.Lock()
    began = time.monotonic()
    provider = None
    thread = None
    result = dict(result='FAIL', targetDirectoryLength=length, finishMode=finish_mode)
    report.mkdir(parents=True, exist_ok=False)

    def save():
        with mutex:
            value = dict(events=copy.deepcopy(events), packets=copy.deepcopy(packets),
                         providerErrors=list(errors), result=copy.deepcopy(result))
        (report / 'observations.json').write_text(json.dumps(value, indent=2)+'\n')

    try:
        with tempfile.TemporaryDirectory(prefix='foxicmp-legacy-cli-') as temporary:
            root = Path(temporary)
            folder = root / 'source'
            while len(str(folder)) < length:
                folder /= 'Видео с пробелами ' + 'x'*25
            folder.mkdir(parents=True, mode=0o700)
            folder.chmod(0o700)
            profile = root / 'profile'
            profile.mkdir()
            (profile / 'FoxicMPQt.ini').write_text(
                '[profile]\nversion=1\n[integration]\nprompted=true\ncheckOnStartup=false\n'
                '[input]\nmpris=false\n[audio]\nmute=true\nvolume=0\n'
                '[playback]\nhardwareDecoding=false\nrememberPosition=false\n'
                '[video]\noutput=software\n[ui]\nosd=false\n', encoding='utf-8')
            snapshot = dict(schemaVersion=1, sessionId=uuid.uuid4().hex, generation=1,
                            revision=1, state='downloading', duration100ns=200000000, tracks={})
            data, paths = [], []
            for i, kind in enumerate(('video', 'audio')):
                payload = (fixtures / 'progressive' / ('audio.m4a' if i else 'video.mp4')).read_bytes()
                data.append(payload)
                path = folder / (kind + '.part')
                paths.append(path)
                prefix = fragment_prefix(payload, 5)
                path.write_bytes(payload[:prefix])
                snapshot['tracks'][kind] = dict(path=path.name, container='m4a' if i else 'mp4',
                    fragmented=True, initializationReady=True, availablePrefixBytes=prefix,
                    expectedTotalBytes=None, state='downloading')
            manifest = folder / 'session.json'
            atomic_json(manifest, snapshot)
            provider = Provider(manifest, owner=os.getpid())
            for method in ('attach', 'release'):
                original = getattr(provider.leases, method)
                def observe(pid, born, method=method, original=original):
                    original(pid, born)
                    with mutex:
                        events.append(dict(kind=method, pid=pid, birth=born,
                            playerAlive=process is not None and process.poll() is None))
                setattr(provider.leases, method, observe)

            def capture(raw, pending):
                pending.extend(raw)
                while b'\n' in pending:
                    at = pending.index(b'\n')
                    packet = json.loads(bytes(pending[:at]))
                    del pending[:at+1]
                    with mutex:
                        packets.append(packet)

            if os.name == 'nt':
                operation = provider.win.operation
                pending = {}
                def observed_operation(handle, action, *args, **kwargs):
                    value = operation(handle, action, *args, **kwargs)
                    if action == 'read' and value:
                        capture(value, pending.setdefault(handle, bytearray()))
                    return value
                provider.win.operation = observed_operation
            else:
                client = provider.client
                class ObservedSocket:
                    def __init__(self, socket):
                        self.socket, self.pending = socket, bytearray()
                    def __getattr__(self, name):
                        return getattr(self.socket, name)
                    def recv(self, *args):
                        raw = self.socket.recv(*args)
                        capture(raw, self.pending)
                        return raw
                provider.client = lambda socket: client(ObservedSocket(socket))

            def serve():
                try:
                    provider.run()
                except BaseException as error:
                    errors.append(repr(error))
            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            assert provider.ready.wait(5) and not provider.error, provider.error

            def wait(predicate, label, seconds=15):
                deadline = time.monotonic()+seconds
                while time.monotonic() < deadline:
                    assert not errors, errors
                    value = predicate()
                    if value:
                        return value
                    save()
                    time.sleep(.05)
                raise AssertionError(('Legacy CLI deadline', label))

            def packet(kind):
                with mutex:
                    found = [p for p in packets if p.get('type') == kind and
                        p.get('pid') == process.pid and p.get('sessionId') == snapshot['sessionId']]
                return found[-1] if found else None

            def cli(*args):
                value = subprocess.run([str(player), '--profile', str(profile), '--no-activate', *args],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
                assert value.returncode == 0, (value.returncode, value.stderr)
                return value.returncode

            log = (report/'player.log').open('wb')
            try:
                process = subprocess.Popen([str(player), '--profile', str(profile), '--no-activate',
                    '--harvester-session', str(manifest)], stdout=log, stderr=subprocess.STDOUT)
                wait(lambda: any(e['kind']=='attach' and e['pid']==process.pid for e in events), 'provider accepted PID')
                initial_buffering = wait(lambda: packet('buffering'), 'initial buffering notification')
                for i, kind in enumerate(('video', 'audio')):
                    prefix = fragment_prefix(data[i], 12)
                    with paths[i].open('ab', buffering=0) as stream:
                        stream.write(data[i][stream.tell():prefix])
                        os.fsync(stream.fileno())
                    snapshot['tracks'][kind]['availablePrefixBytes'] = prefix
                snapshot['revision'] += 1
                atomic_json(manifest, snapshot)
                playing = wait(lambda: packet('playing'), 'playing notification')
                assert process.poll() is None
                assert all(p.stat().st_size < len(raw) for p, raw in zip(paths, data))
                pause_exit = cli('--command', 'pause')
                paused = wait(lambda: packet('paused'), 'paused notification')
                with mutex:
                    paused_index = len(packets)
                play_exit = cli('--command', 'play')
                wait(lambda: any(p.get('type')=='playing' for p in packets[paused_index:]), 'resumed playing notification')
                stop_exit = None
                if finish_mode == 'failed':
                    for track in snapshot['tracks'].values():
                        track['state'] = 'failed'
                    snapshot.update(revision=snapshot['revision']+1, state='failed')
                    atomic_json(manifest, snapshot)
                    wait(lambda: packet('error'), 'writer failure notification')
                else:
                    for i, kind in enumerate(('video', 'audio')):
                        with paths[i].open('ab', buffering=0) as stream:
                            stream.write(data[i][stream.tell():])
                            os.fsync(stream.fileno())
                        snapshot['tracks'][kind].update(availablePrefixBytes=len(data[i]),
                            expectedTotalBytes=len(data[i]), state='complete')
                    snapshot.update(revision=snapshot['revision']+1, state='complete')
                    atomic_json(manifest, snapshot)
                    wait(lambda: packet('playing') and packet('playing')['revision']==snapshot['revision'], 'complete revision observed')
                    if finish_mode == 'stop':
                        stop_exit = cli('--command', 'stop')
                released = wait(lambda: next((e for e in events if e['kind']=='release' and e['pid']==process.pid), None), 'explicit release', 35)
                assert released['playerAlive'] and process.poll() is None, released
                assert not provider.leases.items
                for path, raw in zip(paths, data):
                    expected = raw[:fragment_prefix(raw, 12)] if finish_mode == 'failed' else raw
                    assert path.read_bytes() == expected
                invalid = root/'invalid.json'
                invalid.write_text('{broken')
                invalid_exit = cli('--reuse-instance', '--harvester-session', str(invalid))
                wait(lambda: b'Progressive source:' in (report/'player.log').read_bytes(), 'asynchronous CLI error on stderr')
                assert process.poll() is None, 'Invalid session closed the window'
                # A progressive EOF has its own lifecycle. Test main's normal
                # shutdown separately via an ordinary one-second file/after=exit.
                finish = root/'finish.wav'
                with wave.open(str(finish), 'wb') as stream:
                    stream.setparams((1, 2, 8000, 0, 'NONE', 'not compressed'))
                    stream.writeframes(bytes(16000))
                forwarded_exit = cli('--reuse-instance', '--after', 'exit', str(finish))
                process.wait(timeout=15)
                assert process.returncode==0, process.returncode
                result.update(result='PASS', manifestLength=len(str(manifest)),
                    initialBuffering=initial_buffering, observedPlaying=playing, observedPaused=paused,
                    pauseCommandExit=pause_exit, playCommandExit=play_exit,
                    invalidSessionCommandExit=invalid_exit, asynchronousErrorLogged=True,
                    release=released, nativePlayerExit=process.returncode,
                    stopCommandExit=stop_exit, forwardedFileCommandExit=forwarded_exit,
                    sourceSha256=[hashlib.sha256(raw).hexdigest() for raw in data],
                    sourceBytesUnchanged=True, profileIsShort=True, providerErrors=errors,
                    normalExitViaOrdinaryFile=True, progressiveNaturalEofTested=finish_mode=='natural',
                    writerFailureTested=finish_mode=='failed',
                    decoderPositionMeasured=False, physicalAvSyncTested=False)
            finally:
                if process is not None and process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
                    result['forcedCleanupExit'] = process.returncode
                provider.stop.set()
                thread.join(4)
                assert not thread.is_alive(), 'Provider run still active'
                wait(lambda: provider.pending==0, 'provider connections closed', 5)
                log.close()
        result.update(testRootRemoved=not root.exists(), wallSeconds=time.monotonic()-began)
        (report/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        return result
    finally:
        save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('player', 'fixtures', 'report-directory'):
        parser.add_argument('--'+key, type=Path, required=True)
    parser.add_argument('--source-lengths', type=int, nargs='+', default=(0, 320, 1100))
    parser.add_argument('--finish', choices=('stop', 'natural', 'failed'), default='stop')
    args = parser.parse_args()
    args.report_directory.mkdir(parents=True, exist_ok=False)
    rows=[]
    for length in args.source_lengths:
        rows.append(exercise(args.player.resolve(), args.fixtures.resolve(),
                             args.report_directory/str(length), length, args.finish))
    report=dict(result='PASS',count=len(rows),rows=rows)
    (args.report_directory/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(result='PASS',count=len(rows))))

if __name__=='__main__':
    main()
