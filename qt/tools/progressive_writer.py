#!/usr/bin/env python3
"""Offline writer integration stand: copy local fMP4 fixtures at a controlled rate.

Never downloads a URL, modifies inputs or removes output. The target is a new
empty private directory. Invoke on an explicitly isolated player/profile.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import time
import uuid
from progressive_client import ProgressiveClient
from progressive_sdk import PreviewSession


def boxes(data):
    at = 0
    while at < len(data):
        if len(data) - at < 8:
            raise ValueError('Incomplete fixture header')
        size = int.from_bytes(data[at:at+4], 'big')
        kind = data[at+4:at+8]
        if size == 1:
            if len(data)-at < 16:
                raise ValueError('Incomplete extended fixture header')
            size = int.from_bytes(data[at+8:at+16], 'big')
        if size < 8 or size > len(data)-at:
            raise ValueError('Invalid fixture box')
        at += size
        if kind == b'mdat':
            yield at
    yield len(data)


def run(client, video, audio, directory, *, duration=20, hold_complete=10.5, step=.2):
    directory = Path(directory).absolute()
    directory.mkdir(mode=0o700)  # Fail instead of touching an existing download.
    inputs = [Path(video), Path(audio)]
    if any(p.stat().st_size > 32*1024*1024 for p in inputs):
        raise ValueError('Fixture stand accepts files up to 32 MiB each')
    data = [p.read_bytes() for p in inputs]
    ends = [list(dict.fromkeys(boxes(d))) for d in data]
    snapshot = dict(schemaVersion=1, sessionId=uuid.uuid4().hex, generation=1,
                    revision=1, state='downloading', duration100ns=int(duration*10_000_000), tracks={})
    paths = [directory/'video.part', directory/'audio.part']
    for i, kind in enumerate(('video', 'audio')):
        paths[i].write_bytes(b'')
        snapshot['tracks'][kind] = dict(path=paths[i].name, container='m4a' if i else 'mp4',
            fragmented=True, initializationReady=False, availablePrefixBytes=0,
            expectedTotalBytes=None, state='downloading')
    preview = PreviewSession(client, str(directory), title='Offline writer stand')
    report = dict(schemaVersion=1, sessionId=snapshot['sessionId'], sourceOpens=2,
                  networkRequests=0, maxPositionBeforeComplete=0,
                  playingBeforeCompleteSeconds=0)
    playing_since = None
    try:
        for n in range(max(map(len, ends))):
            for i, kind in enumerate(('video', 'audio')):
                end = ends[i][min(n, len(ends[i])-1)]
                with paths[i].open('ab', buffering=0) as output:
                    before = output.tell()
                    pending = memoryview(data[i])[before:end]
                    while pending:
                        written = output.write(pending)
                        if not written:
                            raise OSError('Short fixture write')
                        pending = pending[written:]
                    os.fsync(output.fileno())
                snapshot['tracks'][kind].update(availablePrefixBytes=end, initializationReady=True)
            snapshot['revision'] += 1
            reply = preview.publish(snapshot)
            report['lastPublishState'] = reply.get('state')
            time.sleep(step)
        deadline = time.monotonic()+max(hold_complete, 10.5)
        while time.monotonic() < deadline:
            if preview:
                preview.heartbeat()
                status = preview.status()
                if status.get('state') == 'error':
                    raise RuntimeError(status.get('error'))
                if status.get('state') == 'playing':
                    if playing_since is None:
                        playing_since = time.monotonic()
                    report['maxPositionBeforeComplete'] = max(report['maxPositionBeforeComplete'], status['positionSeconds'])
                    report['playingBeforeCompleteSeconds'] = time.monotonic()-playing_since
            time.sleep(.1)
        if preview and report['maxPositionBeforeComplete'] < 10:
            raise AssertionError(('Did not play 10 seconds before complete', report))
        for i, kind in enumerate(('video', 'audio')):
            snapshot['tracks'][kind].update(expectedTotalBytes=len(data[i]), state='complete')
        snapshot.update(state='complete', revision=snapshot['revision']+1)
        preview.publish(snapshot)
        deadline = time.monotonic()+duration+10
        while time.monotonic() < deadline:
            status = preview.status()
            if status.get('state') == 'error':
                raise RuntimeError(status.get('error'))
            if status.get('state') == 'finished' and status.get('released'):
                report['terminal'] = status
                break
            time.sleep(.1)
        else:
            raise TimeoutError('Player did not release the completed preview')
        return report
    finally:
        try:
            report['release'] = preview.close()
        except (OSError, RuntimeError) as error:
            report['releaseError'] = str(error)  # Retain all files on uncertainty.
        (directory/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--player', required=True)
    parser.add_argument('--profile')
    parser.add_argument('--video', type=Path, required=True)
    parser.add_argument('--audio', type=Path, required=True)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--ensure', action='store_true')
    args = parser.parse_args()
    client = ProgressiveClient(args.player, args.profile)
    if args.ensure:
        client.ensure_running()
    report = run(client, args.video, args.audio, args.directory)
    print(json.dumps(report, ensure_ascii=False))
