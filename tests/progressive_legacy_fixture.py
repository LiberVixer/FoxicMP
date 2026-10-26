"""Offline writer/provider fixture for the pre-API --harvester-session path.

Only test-owned directories/media. Never imports or modifies Harvester.
"""
import argparse
import json
import os
from pathlib import Path
import sys
import threading
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'/'harvester-preview'))
from scripts.progressive_session import atomic_json, read_json
if os.name == 'nt':
    from scripts.progressive_pipe import Provider
else:
    from scripts.progressive_unix import Provider
sys.path.insert(0, str(Path(__file__).resolve().parent))
from progressive_v1_conformance import fragment_prefix

parser = argparse.ArgumentParser()
parser.add_argument('--manifest', type=Path, required=True)
parser.add_argument('--fixtures', type=Path, required=True)
args = parser.parse_args()
root = args.manifest.parent
if os.name != 'nt':
    root.chmod(0o700)
provider = Provider(args.manifest, owner=os.getpid())
errors = []
def serve():
    try:
        provider.run()
    except BaseException as error:
        errors.append(str(error))
thread = threading.Thread(target=serve, daemon=True)
thread.start()
if not provider.ready.wait(5) or provider.error:
    raise RuntimeError(provider.error or 'Legacy provider startup timeout')
atomic_json(root/'fixture-ready.json', dict(ready=True))
last = ''
padding = {'video': 0, 'audio': 0}
try:
    while not (root/'fixture-stop').exists():
        if errors:
            raise RuntimeError(errors)
        control = root/'fixture-control'
        stage = control.read_text() if control.exists() else ''
        if stage and stage != last:
            if stage in ('disconnect', 'resume'):
                if stage == 'disconnect':
                    provider.stop.set(); thread.join(timeout=3)
                    if thread.is_alive():
                        raise TimeoutError('Legacy provider stop timeout')
                    # Emulate full process exit: run() returns before its daemon
                    # accept/client threads close all named-pipe server handles.
                    # FIRST_PIPE_INSTANCE must not race those remaining handles.
                    for old_thread in threading.enumerate():
                        if old_thread is not threading.current_thread():
                            old_thread.join(timeout=2)
                            if old_thread.is_alive():
                                raise TimeoutError('Legacy provider I/O shutdown timeout')
                else:
                    provider = Provider(args.manifest, owner=os.getpid())
                    thread = threading.Thread(target=serve, daemon=True); thread.start()
                    if not provider.ready.wait(5) or provider.error:
                        raise RuntimeError(provider.error or 'Provider restart timeout')
                atomic_json(root/'fixture-phase.json', dict(stage=stage))
                last = stage
                continue
            snapshot = read_json(args.manifest)
            if stage in ('grow', 'slow-grow', 'complete'):
                for i, kind in enumerate(('video','audio')):
                    data = (args.fixtures/'progressive'/('audio.m4a' if i else 'video.mp4')).read_bytes()
                    count = len(data) if stage == 'complete' else fragment_prefix(data, 12)
                    track = snapshot['tracks'][kind]
                    with (root/track['path']).open('ab', buffering=0) as stream:
                        previous = stream.tell() - padding[kind]
                        if stage == 'slow-grow' and kind == 'video' and not padding[kind]:
                            atoms = b'\x00\x00\x00\x08free' * 100000
                            stream.write(atoms); padding[kind] = len(atoms)
                        stream.write(data[previous:count]); os.fsync(stream.fileno())
                    track['availablePrefixBytes'] = count + padding[kind]
                    if stage == 'complete':
                        track.update(expectedTotalBytes=count + padding[kind], state='complete')
                if stage == 'complete':
                    snapshot['state'] = 'complete'
            elif stage == 'failed':
                snapshot['state'] = 'failed'
            else:
                raise ValueError('Unknown fixture stage')
            snapshot['revision'] += 1
            atomic_json(args.manifest, snapshot)
            atomic_json(root/'fixture-phase.json', dict(stage=stage))
            last = stage
        time.sleep(.03)
finally:
    provider.stop.set()
    thread.join(timeout=3)
