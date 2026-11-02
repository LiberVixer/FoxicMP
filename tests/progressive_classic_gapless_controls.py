"""Normal Classic API, internal MP3 priority disabled in the private test INI."""
import hashlib
import time
from pathlib import Path

from progressive_mp3_gapless_fixture import fixture, PROFILES
from progressive_standalone_fixture import publish
from progressive_v2 import V2PreviewSession
from progressive_v2_playback_conformance import forget_closed_readers


def exercise(client, work, stop):
    rows = []
    for profile in PROFILES:
        for action in ('cancel', 'stop', 'reopen'):
            root = Path(work) / (profile + '-' + action)
            model, raw, _, _ = fixture(root, profile, 20)
            value = publish(model, raw, root, len(raw), complete=True)
            preview = V2PreviewSession(client, str(root))
            try:
                preview.publish(value)
                deadline = time.monotonic() + 15
                while True:
                    status = preview.heartbeat()
                    assert not status.get('error'), status
                    if status['state'] == 'playing' and status.get('positionSeconds', 0) >= .1:
                        index = status['mp3Index']
                        assert index['pcmWindowAdapter'] == 'classic-directshow-mp3-window-v1', status
                        assert index['gaplessTrimApplied'] and index['pcmWindowState'] == 'applied', status
                        break
                    assert time.monotonic() < deadline, status
                    time.sleep(.08)
                reader = preview.reader_id
                if action == 'stop':
                    stop()
                elif action == 'cancel':
                    client.command('cancel', value['sessionId'], readerId=reader)
                receipt = preview.close(timeout=10)
                assert receipt['released'], receipt
                forget_closed_readers(client, value)
                assert hashlib.sha256((root / 'mux.part').read_bytes()).digest() == hashlib.sha256(raw).digest()
                rows.append(dict(profile=profile, action=action, result='PASS',
                                 readerId=reader, released=True, originalUnchanged=True,
                                 ordinaryInternalMp3PriorityDisabled=True,
                                 pcmWindowAdapter=status['mp3Index']['pcmWindowAdapter']))
            finally:
                if not preview.closed:
                    preview.close(timeout=10)
    assert len({row['readerId'] for row in rows}) == len(rows), rows
    return rows
