"""Classic playback under child-only TEMP with explicit opt-in reader receipts.

Run on an interactive Windows desktop with a private Classic build and fixtures.
Default bounded slice: TEMP120/240. Use --temp-lengths 320 1100 for open
frontier diagnostics. Requested TEMP and actual GetTempPathW are reported separately; an OS fallback
is never accepted as evidence that the requested long directory was used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def wait(check, label, timeout=15):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = check()
        if last:
            return last
        time.sleep(.05)
    raise AssertionError(('Deadline', label, last))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('player', 'reader-runner', 'fixtures', 'report-directory'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--temp-mode', choices=('os', 'environment'), default='os')
    parser.add_argument('--require-exact-temp', action='store_true')
    parser.add_argument('--temp-lengths', type=int, nargs='+', default=(120, 240))
    args = parser.parse_args()
    if any(n < 1 or n > 2000 for n in args.temp_lengths):
        parser.error('TEMP lengths must be in 1..2000')
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'qt/tools'))
    from progressive_client import ProgressiveClient
    from progressive_v1_conformance import fragment_prefix
    from progressive_family_test import post_command, registration_snapshot
    from owned_process_job import OwnedProcessJob

    args.report_directory.mkdir(parents=True, exist_ok=False)
    job = OwnedProcessJob()
    environment_before = dict(os.environ)
    shell_before = registration_snapshot()
    rows, launches, cleanup = [], [], []

    def save(name, value):
        (args.report_directory / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    try:
        with tempfile.TemporaryDirectory(prefix='foxicmp-classic-runtime-') as temporary:
            root = Path(temporary)
            for length in args.temp_lengths:
                stage = root / ('player-' + str(length))
                shutil.copytree(args.player.resolve().parent, stage)
                player = stage / args.player.name
                # Set before discovery, which also executes this private binary.
                player.with_suffix('.ini').write_text(
                    '[Settings]\nContextMenuPrompted=1\nContextMenuEnabled=0\n'
                    'PauseMinimizedVideo=0\nCheckForUpdates=0\nKeepHistory=0\n'
                    '[Audio]\nAudioRenderer=Null Audio Renderer (Uncompressed)\n', encoding='utf-16')
                requested = root / ('temp-' + str(length))
                while len(str(requested)) < length:
                    component = ('Временные файлы ' + 'x' * 25)[:max(1, length-len(str(requested))-1)]
                    requested /= component[:-1] + 'x' if component.endswith(' ') else component
                requested.mkdir(parents=True)
                env = dict(os.environ)
                for key in ('TMP', 'TEMP', 'TMPDIR'):
                    env[key] = str(requested)
                probe = subprocess.run([str(args.reader_runner.resolve()), '--temp-info-safe' if args.temp_mode=='environment' else '--temp-info'],
                                       env=env, capture_output=True, timeout=10)
                assert probe.returncode == 0, (probe.returncode, probe.stderr)
                temp_info = json.loads(probe.stdout)
                actual = Path(temp_info['progressiveTempDirectory' if args.temp_mode=='environment' else 'GetTempPathW']['path'])
                honored = actual.resolve() == requested.resolve()
                # These two points are intentionally different expectations.
                if length <= 240 or args.require_exact_temp:
                    assert honored, (requested, actual, temp_info)
                client = ProgressiveClient(str(player))
                registry = actual / (client.endpoint.rsplit('\\', 1)[-1] + '.readers')
                assert not registry.exists(), 'Private endpoint registry already exists'
                log = (args.report_directory / (str(length) + '.log')).open('wb')
                proc = subprocess.Popen([str(player), '/new', '/nofocus', '/minimized'], env=env,
                                        stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                launch = dict(pid=proc.pid, requestedLength=length, requestedDirectory=str(requested),
                              actualTemp=temp_info, requestedTempHonored=honored, registry=str(registry),
                              nativeTempProbeExit=probe.returncode, nativeExit=None, phase='launching')
                launches.append(launch)
                active = None

                def native(action, **fields):
                    reply = subprocess.run([str(args.reader_runner.resolve()), '--request', str(registry)],
                                           input=json.dumps(dict(version=1, action=action, **fields))+'\n',
                                           capture_output=True, text=True, timeout=5)
                    assert reply.returncode == 0, (reply.returncode, reply.stderr)
                    value = json.loads(reply.stdout)
                    assert value['ok'], value
                    return value

                def status_playing(session):
                    began = time.monotonic()
                    status = client.command('heartbeat', session)
                    launch['statusChecks'] = launch.get('statusChecks', 0) + 1
                    launch['lastStatus'] = status
                    launch['longestStatusReplySeconds'] = max(launch.get('longestStatusReplySeconds', 0), time.monotonic()-began)
                    assert status['state'] not in ('error', 'finished'), status
                    return status if status['state'] == 'playing' and status['positionSeconds'] > .2 else None

                try:
                    def ready():
                        assert proc.poll() is None, proc.returncode
                        try:
                            caps = client.request(dict(version=1, action='capabilities'), timeout=.5)
                        except (OSError, RuntimeError):
                            return None
                        assert caps['pid'] == proc.pid, caps
                        return caps
                    wait(ready, 'Owned Classic endpoint')
                    launch['phase'] = 'endpoint-ready'
                    save('progress.json', dict(rows=rows, launches=launches))
                    for fragmented in (False, True):
                        folder = root / ('media-%s-%s' % (length, fragmented))
                        folder.mkdir()
                        fixture = args.fixtures.resolve() / ('progressive' if fragmented else 'progressive-indexed')
                        data = [(fixture / name).read_bytes() for name in ('video.mp4', 'audio.m4a')]
                        paths = [folder / 'видео.part', folder / 'звук.part']
                        model = dict(schemaVersion=1, sessionId=uuid.uuid4().hex, generation=1,
                                     revision=1, state='downloading', duration100ns=200000000, tracks={})
                        for i, kind in enumerate(('video', 'audio')):
                            prefix = fragment_prefix(data[i], 16) if fragmented else len(data[i])*4//5
                            paths[i].write_bytes(data[i][:prefix])
                            model['tracks'][kind] = dict(path=paths[i].name, container='m4a' if i else 'mp4',
                                fragmented=fragmented, initializationReady=True, availablePrefixBytes=prefix,
                                expectedTotalBytes=len(data[i]), state='downloading')
                        launch.update(phase='start-request', fragmented=fragmented)
                        save('progress.json', dict(rows=rows, launches=launches))
                        began = time.monotonic()
                        try:
                            started = client.start(model, session_directory=str(folder), track_reader=True, activate=False)
                        finally:
                            launch['apiStartWallSeconds'] = time.monotonic()-began
                        launch['phase'] = 'start-accepted'
                        launch['acceptedStart'] = started
                        active = model['sessionId']
                        assert started.get('readerId'), started
                        playing = wait(lambda: status_playing(active), 'Actual growing Classic playback')
                        assert not playing['complete'], playing
                        assert all(p.stat().st_size < len(raw) for p, raw in zip(paths, data))
                        assert registry.is_dir(), registry
                        assert (registry / (started['readerId'] + '.lease')).is_file()
                        live = native('readerStatus', readerId=started['readerId'])['reader']
                        assert not live['released'] and not live['filesClosed'], live
                        for i, kind in enumerate(('video', 'audio')):
                            with paths[i].open('ab') as stream:
                                stream.write(data[i][stream.tell():])
                            model['tracks'][kind].update(availablePrefixBytes=len(data[i]), state='complete')
                        model.update(revision=2, state='complete')
                        completed = client.update(model, complete=True)
                        assert completed['complete'], completed
                        cancelled = client.command('cancel', active)
                        active = None
                        def closed():
                            receipt = client.reader_status(started['readerId'])
                            return receipt if receipt['released'] and receipt['filesClosed'] else None
                        released = wait(closed, 'Explicit filesClosed receipt', 5)
                        direct = native('readerStatus', readerId=started['readerId'])['reader']
                        assert direct['released'] and direct['filesClosed'], direct
                        assert client.forget_reader(started['readerId'])['forgotten']
                        assert all(p.read_bytes() == raw for p, raw in zip(paths, data))
                        rows.append(dict(result='PASS', requestedLength=length, requestedDirectory=str(requested),
                            actualTemp=temp_info, requestedTempHonored=honored, registry=str(registry),
                            registryLength=len(str(registry)), fragmented=fragmented, started=started,
                            playing=playing, completed=completed, cancelled=cancelled, liveReceipt=live,
                            releasedReceipt=released, nativeReleasedReceipt=direct, sourceSha256=[sha(p) for p in paths]))
                        save('progress.json', dict(rows=rows, launches=launches))
                        # Removal is gated by positive receipt, not an empty registry.
                        for path in paths:
                            path.unlink()
                    post_command(proc.pid, 0)
                    proc.wait(timeout=15)
                    assert proc.returncode == 0, proc.returncode
                    launch['phase'] = 'normal-exit'
                finally:
                    try:
                        if active is not None and proc.poll() is None:
                            client.command('cancel', active)
                    finally:
                        launch['forcedCleanup'] = proc.poll() is None
                        if proc.poll() is None:
                            proc.kill()
                            proc.wait(timeout=5)
                        launch['nativeExit'] = proc.returncode
                        log.close()
                    if registry.exists():
                        # Failed-case cleanup also requires an explicit OS/release proof.
                        for path in registry.glob('*.lease'):
                            proof = native('readerStatus', readerId=path.stem)['reader']
                            launch.setdefault('failureCleanupReceipts', []).append(proof)
                            assert proof['released'] and proof['reason'] in ('files-closed', 'process-exited', 'pid-reused'), proof
                            assert native('readerForget', readerId=path.stem)['forgotten']
                        assert not list(registry.glob('*.lease'))
                        shutil.rmtree(registry)
                        cleanup.append(str(registry))
            private_removed = not root.exists()
        assert dict(os.environ) == environment_before
        assert registration_snapshot() == shell_before, 'Classic shell registration changed'
        result = dict(result='PASS', count=len(rows), rows=rows, launches=launches,
                      privateCopiesRemoved=private_removed, runtimeRegistriesRemoved=cleanup,
                      parentEnvironmentUnchanged=True, shellRegistrationUnchanged=True, tempMode=args.temp_mode, exactTempRequired=args.require_exact_temp)
        save('result.json', result)
        print(json.dumps(dict(result='PASS', count=len(rows))))
        job.finish()
    except BaseException:
        save('failure.json', dict(rows=rows, launches=launches, runtimeRegistriesRemoved=cleanup,
             shellRegistrationUnchanged=registration_snapshot() == shell_before,
             parentEnvironmentUnchanged=dict(os.environ) == environment_before))
        raise


if __name__ == '__main__':
    main()
