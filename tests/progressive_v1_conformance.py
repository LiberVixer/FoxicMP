"""Common v1 sessionDirectory regression against an already isolated player.

Uses only the shipped client API; no downloader, source URL or OS media commands.
The caller owns the test process/profile. Fixtures: qt/tests/generate_media.py.
"""
import argparse
import copy
import json
from pathlib import Path
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'qt' / 'tools'))
from progressive_client import ProgressiveClient, ApiError


def wait_status(client, session_id, predicate, timeout=30):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = client.command('status', session_id)
        if last.get('state') == 'error':
            raise AssertionError(last)
        if predicate(last):
            return last
        time.sleep(.1)
    raise AssertionError(('status timeout', last))


def reject(client, message, code):
    try:
        client.request(message)
    except ApiError as error:
        assert error.code == code, error.reply
        return
    raise AssertionError(('accepted invalid request', message['action']))


def fragment_prefix(data, seconds):
    at = fragments = 0
    while at + 8 <= len(data):
        size = int.from_bytes(data[at:at + 4], 'big')
        kind = data[at + 4:at + 8]
        if size == 1:
            size = int.from_bytes(data[at + 8:at + 16], 'big')
        if size < 8 or size > len(data) - at:
            raise ValueError('Invalid fixture box')
        at += size
        if kind == b'mdat':
            fragments += 1
            if fragments == seconds:
                return at
    raise ValueError('Fixture has insufficient fragments')


def exercise_directory_contract(client, fixtures, work):
    """The exact same wire scenario runs in Linux Qt, Windows Qt and Classic."""
    work = Path(work).resolve()
    results = []
    active_id = None
    try:
        for fragmented in (False, True):
            folder = work / ('fragmented' if fragmented else 'indexed')
            folder.mkdir(parents=True)
            fixture = Path(fixtures) / ('progressive' if fragmented else 'progressive-indexed')
            data = [(fixture / name).read_bytes() for name in ('video.mp4', 'audio.m4a')]
            paths = [folder / 'видео.part', folder / 'звук.part']
            snapshot = dict(schemaVersion=1, sessionId=uuid.uuid4().hex, generation=1,
                            revision=1, state='downloading', duration100ns=200000000,
                            tracks={})
            for i, kind in enumerate(('video', 'audio')):
                count = fragment_prefix(data[i], 5) if fragmented else len(data[i]) * 3 // 10
                paths[i].write_bytes(data[i][:count])
                snapshot['tracks'][kind] = dict(path=paths[i].name, container='m4a' if i else 'mp4',
                    fragmented=fragmented, initializationReady=True, availablePrefixBytes=count,
                    expectedTotalBytes=None if fragmented else len(data[i]), state='downloading')
            probe = client.probe(snapshot, str(folder))
            assert probe['supported'], probe
            first = client.start(snapshot, session_directory=str(folder), activate=False)
            active_id = snapshot['sessionId']
            assert first['state'] == 'buffering', first
            # Published paths may not change, even if absolute tracks would hide
            # an ignored sessionDirectory field from the path comparison.
            for absolute in (False, True):
                changed = copy.deepcopy(snapshot)
                changed['revision'] += 1
                if absolute:
                    for i, kind in enumerate(('video', 'audio')):
                        changed['tracks'][kind]['path'] = str(paths[i])
                reject(client, dict(version=1, action='update', sessionId=active_id,
                    session=changed, sessionDirectory=str(work)), 'invalid_update')
            for invalid in (None, False, '', '.', str(folder) + '\x00'):
                reject(client, dict(version=1, action='update', sessionId=active_id,
                    session=snapshot, sessionDirectory=invalid), 'invalid_update')
            # A failed replacement must neither use nor replace the active root.
            replacement = copy.deepcopy(snapshot)
            replacement['sessionId'] = uuid.uuid4().hex
            reject(client, dict(version=1, action='start', session=replacement,
                replace=True, activate=False), 'source_not_ready')
            assert client.command('status', active_id)['active']
            for seconds in (12, 16):
                snapshot['revision'] += 1
                for i, kind in enumerate(('video', 'audio')):
                    count = fragment_prefix(data[i], seconds) if fragmented else len(data[i]) * seconds // 20
                    with paths[i].open('ab') as stream:
                        stream.write(data[i][stream.tell():count])
                    snapshot['tracks'][kind]['availablePrefixBytes'] = count
                # No sessionDirectory here: it belongs to the successful start.
                client.update(snapshot)
            snapshot['revision'] += 1
            normalized = client.request(dict(version=1, action='update', sessionId=active_id,
                session=snapshot, sessionDirectory=str(folder) + '/./'))
            assert normalized['active'], normalized
            playing = wait_status(client, active_id,
                lambda r: r['state'] == 'playing' and r['positionSeconds'] > .2)
            assert not playing['complete'], playing
            # Finish after rename, retaining original wire names. Updates must
            # validate pinned handles instead of reopening obsolete paths.
            for i, kind in enumerate(('video', 'audio')):
                final = paths[i].with_suffix('.done')
                paths[i].rename(final)
                with final.open('ab') as stream:
                    stream.write(data[i][stream.tell():])
                snapshot['tracks'][kind].update(availablePrefixBytes=len(data[i]),
                    expectedTotalBytes=len(data[i]), state='complete')
            snapshot.update(state='complete', revision=snapshot['revision'] + 1)
            completed = client.update(snapshot, complete=True)
            assert completed['complete'], completed
            terminal = wait_status(client, active_id,
                lambda r: r['state'] == 'finished' and not r['active'] and r.get('released') is True)
            assert not client.command('cancel', active_id)['active']
            active_id = None
            # Subsequent sessions cannot inherit the directory. Their files
            # exist: a stale root would incorrectly make this start succeed.
            for path in paths:
                path.with_suffix('.done').rename(path)
            replacement = copy.deepcopy(snapshot)
            replacement['sessionId'] = uuid.uuid4().hex
            reject(client, dict(version=1, action='start', session=replacement,
                activate=False), 'source_not_ready')
            client.start(replacement, session_directory=str(folder), activate=False)
            active_id = replacement['sessionId']
            assert not client.command('cancel', active_id)['active']
            active_id = None
            replacement['sessionId'] = uuid.uuid4().hex
            reject(client, dict(version=1, action='start', session=replacement,
                activate=False), 'source_not_ready')
            results.append(dict(fragmented=fragmented, playing=playing,
                                complete=completed, terminal=terminal))
        return results
    finally:
        if active_id is not None:
            client.command('cancel', active_id)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--player', required=True)
    parser.add_argument('--profile')
    parser.add_argument('--fixtures', required=True)
    parser.add_argument('--work', required=True)
    parser.add_argument('--expected-pid', type=int, required=True)
    args = parser.parse_args()
    client = ProgressiveClient(args.player, args.profile)
    caps = client.request(dict(version=1, action='capabilities'))
    assert caps['pid'] == args.expected_pid, 'Wrong endpoint owner'
    result = exercise_directory_contract(client, args.fixtures, args.work)
    print(json.dumps(result, ensure_ascii=False, indent=2))
