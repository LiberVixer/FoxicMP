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
        self.reader_id = None
        self.closed_receipt = None
        self.start_uncertain = False
        self.lock = threading.RLock()
        caps = client.request(dict(version=1, action='capabilities'))
        required = {'probe', 'start', 'update', 'heartbeat', 'complete', 'error', 'status', 'release'}
        if caps.get('schemaVersion') != 1 or not required <= set(caps.get('actions', [])) or not caps.get('releaseAcknowledgement'):
            raise RuntimeError('Player lacks the v1 integration/release contract; update it first')
        self.track_reader = caps.get('readerLeaseProtocol') == 1 and {'readerStatus','readerRelease','readerForget','readers'} <= set(caps.get('actions', []))

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
                        return self._command('error', message='Writer failed')
                    self.closed = True
                    return self.close()
                self.closed = True
                return dict(version=1, ok=True, active=False, released=not self.start_uncertain, state=state)
            if not self.started:
                if not all(t.get('initializationReady') for t in current['tracks'].values()):
                    self.last = current
                    return dict(version=1, ok=True, active=False, state='waiting-initialization')
                probe = self.client.probe(current, self.directory)
                if not probe['supported']:
                    self.last = current
                    return dict(version=1, ok=True, active=False, state='source-not-ready', probe=probe)
                options = dict(session_directory=self.directory, title=self.title,
                    activate=self.activate, replace=self.replace)
                if self.track_reader: options['track_reader'] = True
                # A lost start reply may still have opened files. Preserve the
                # identity so the caller can enumerate leases after reconnect.
                self.start_uncertain = True
                self.last = current
                reply = self.client.start(current, **options)
                self.started = True
                if self.track_reader:
                    self.reader_id = reply.get('readerId')
                    if not isinstance(self.reader_id, str) or len(self.reader_id) != 32 or any(c not in '0123456789abcdef' for c in self.reader_id):
                        raise RuntimeError('Tracked start returned no valid reader ID; retain files and enumerate readers')
                self.start_uncertain = False
            else:
                options=dict(complete=state == 'complete')
                if self.reader_id is not None: options['reader_id']=self.reader_id
                reply = self.client.update(current, **options)
            self.last = current
            return reply

    def _command(self, action, **fields):
        if self.reader_id is not None: fields['readerId']=self.reader_id
        return self.client.command(action, self.last['sessionId'], **fields)

    def heartbeat(self) -> dict:
        with self.lock:
            if self.closed or not self.started:
                return dict(version=1, ok=True, active=False, state='detached' if self.closed else 'waiting-initialization')
            if self.last['state'] == 'complete':
                return self.status()
            return self._command('heartbeat')

    def status(self) -> dict:
        with self.lock:
            if not self.started:
                return dict(version=1, ok=True, active=False, released=not self.start_uncertain, state='start-uncertain' if self.start_uncertain else 'not-started')
            return self._command('status')

    def writer_error(self, message: str) -> dict:
        with self.lock:
            self.closed = True
            if not self.started:
                self.closed = True
                return dict(version=1, ok=True, active=False, released=not self.start_uncertain, state='error')
            return self._command('error', message=message)

    def close(self, timeout: float = 2) -> dict:
        with self.lock:
            self.closed = True
            if not self.started:
                return dict(version=1, ok=True, active=False, released=not self.start_uncertain, state='start-uncertain' if self.start_uncertain else 'detached')
            if self.closed_receipt is not None:
                return copy.deepcopy(self.closed_receipt)
            if self.track_reader:
                if self.reader_id is None:
                    raise RuntimeError('Missing reader receipt; retain files and enumerate readers')
                reply = self.client.release_reader(self.reader_id, timeout)
                if reply.get('released') is True:
                    self.closed_receipt = copy.deepcopy(reply)
                    # A successful close is the SDK's acknowledgement. A failure
                    # to forget consumes a slot but never invalidates this proof.
                    try: self.client.forget_reader(self.reader_id)
                    except (OSError, RuntimeError): pass
                return reply
            return self.client.release(self.last['sessionId'], timeout)
