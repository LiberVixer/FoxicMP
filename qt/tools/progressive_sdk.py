"""Player-side event adapter for downloader-owned confirmed-prefix snapshots.

Standard library only. Calls are serialized; invoke them on a downloader worker,
not its GUI thread. No timers, network downloading, remuxing or source deletion.
"""
from __future__ import annotations
import copy
import threading
from progressive_client import ProgressiveClient


class PreviewSession:
    """One explicitly requested preview; stopping it never stops the writer.

    publish() accepts a complete v1 snapshot AFTER the writer flushes data.
    heartbeat() is called by the live writer even while there are no new bytes.
    The caller retains all originals until close()['released'] is explicitly true.
    A terminal preview is never implicitly restarted by subsequent writer events.
    """
    def __init__(self, client: ProgressiveClient, session_directory: str, *,
                 title: str = '', activate: bool = False, replace: bool = False):
        self.client = client
        self.directory = session_directory
        self.title, self.activate, self.replace = title, activate, replace
        self.started = self.closed = False
        self.last = None
        self.lock = threading.RLock()
        caps = client.request(dict(version=1, action='capabilities'))
        required = {'probe', 'start', 'update', 'heartbeat', 'complete', 'error', 'status', 'release'}
        if caps.get('schemaVersion') != 1 or not required <= set(caps.get('actions', [])) or not caps.get('releaseAcknowledgement'):
            raise RuntimeError('Player lacks the v1 integration/release contract; update it first')

    def publish(self, snapshot: dict) -> dict:
        with self.lock:
            if self.closed:
                raise RuntimeError('Preview was detached; explicitly create another PreviewSession')
            current = copy.deepcopy(snapshot)
            if self.last is not None:
                if any(current[k] != self.last[k] for k in ('sessionId', 'generation')):
                    raise ValueError('Writer identity changed; create a new preview')
                if current['revision'] <= self.last['revision']:
                    if current == self.last:
                        return self.heartbeat()
                    raise ValueError('Stale or conflicting writer revision')
            state = current['state']
            if state in {'failed', 'cancelled'}:
                self.last = current
                self.closed = True
                if self.started:
                    if state == 'failed':
                        return self.client.command('error', current['sessionId'], message='Writer failed')
                    self.closed = True
                    return self.client.release(current['sessionId'])
                self.closed = True
                return dict(version=1, ok=True, active=False, released=True, state=state)
            if not self.started:
                if not all(t.get('initializationReady') for t in current['tracks'].values()):
                    self.last = current
                    return dict(version=1, ok=True, active=False, state='waiting-initialization')
                probe = self.client.probe(current, self.directory)
                if not probe['supported']:
                    self.last = current
                    return dict(version=1, ok=True, active=False, state='source-not-ready', probe=probe)
                reply = self.client.start(current, session_directory=self.directory, title=self.title,
                    activate=self.activate, replace=self.replace)
                self.started = True
            else:
                reply = self.client.update(current, complete=state == 'complete')
            self.last = current
            return reply

    def heartbeat(self) -> dict:
        with self.lock:
            if self.closed or not self.started:
                return dict(version=1, ok=True, active=False, state='detached' if self.closed else 'waiting-initialization')
            if self.last['state'] == 'complete':
                return self.status()
            return self.client.command('heartbeat', self.last['sessionId'])

    def status(self) -> dict:
        with self.lock:
            if not self.started:
                return dict(version=1, ok=True, active=False, released=True, state='not-started')
            return self.client.command('status', self.last['sessionId'])

    def writer_error(self, message: str) -> dict:
        with self.lock:
            self.closed = True
            if not self.started:
                self.closed = True
                return dict(version=1, ok=True, active=False, released=True, state='error')
            return self.client.command('error', self.last['sessionId'], message=message)

    def close(self, timeout: float = 2) -> dict:
        with self.lock:
            self.closed = True
            if not self.started:
                return dict(version=1, ok=True, active=False, released=True, state='detached')
            return self.client.release(self.last['sessionId'], timeout)
