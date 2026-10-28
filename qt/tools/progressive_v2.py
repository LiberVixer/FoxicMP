"""Schema-2 separate fMP4/WebM tracks, including multiple audio tracks, complete SRT and mixed pairs; no downloader changes."""
from __future__ import annotations
import copy
import threading
from progressive_client import ProgressiveClient, ApiError
from progressive_schema import unsigned64


def counter(value):
    if not isinstance(value,str) or not value or len(value)>20 or not value.isascii() or not value.isdecimal():
        raise ValueError('Expected a canonical uint64 string')
    number=int(value)
    if unsigned64(number,positive=True)!=value:raise ValueError('Expected a canonical positive uint64 string')
    return number


class V2PreviewSession:
    def __init__(self,client: ProgressiveClient,directory: str,*,title: str='',activate: bool=False,replace: bool=False):
        self.client=client;self.directory=directory;self.title=title;self.activate=activate;self.replace=replace
        self.lock=threading.RLock();self.last=None;self.reader_id=None;self.started=False;self.closed=False;self.uncertain=False;self.receipt=None
        caps=client.request(dict(version=1,action='capabilities'))
        if 2 not in caps.get('playbackSchemaVersions',[]) or caps.get('schema2Playback',{}).get('mode')!='separate-fmp4-h264-aac-lc' or caps.get('readerLeaseProtocol')!=1:
            raise RuntimeError('Player lacks the bounded schema-2 media profile; update the player')

    def _command(self,action,**fields):
        return self.client.command(action,self.last['sessionId'],readerId=self.reader_id,**fields)

    def publish(self,snapshot: dict):
        with self.lock:
            if self.closed:raise RuntimeError('Preview was detached; create a new V2PreviewSession')
            current=copy.deepcopy(snapshot);counter(current['generation']);revision=counter(current['revision'])
            if self.last is not None:
                if any(current[key]!=self.last[key] for key in ('sessionId','generation')) or current['writer']['instanceId']!=self.last['writer']['instanceId']:
                    raise ValueError('Writer generation changed; create a new preview')
                old_revision=counter(self.last['revision'])
                if revision<old_revision or (revision==old_revision and current!=self.last):raise ValueError('Stale or conflicting revision')
                if revision==old_revision and self.started:return self.heartbeat()
            state=current['writer']['state']
            if state in {'failed','cancelled'}:
                self.last=current;self.closed=True
                if self.started:
                    if state=='failed':return self._command('error',message='Writer failed')
                    return self.close()
                return dict(version=1,ok=True,active=False,released=not self.uncertain,state=state)
            if not self.started:
                self.last=current
                if not all(track.get('initializationReady') for track in current['tracks'] if track.get('required')):
                    return dict(version=1,ok=True,active=False,state='waiting-initialization')
                self.uncertain=True
                try:
                    reply=self.client.start(current,title=self.title,activate=self.activate,replace=self.replace,session_directory=self.directory,track_reader=True)
                except ApiError as error:
                    if error.code=='waiting_initialization':
                        self.uncertain=False
                        return dict(version=1,ok=True,active=False,state='waiting-initialization',reason=str(error))
                    raise
                reader=reply.get('readerId')
                if not isinstance(reader,str) or len(reader)!=32 or any(c not in '0123456789abcdef' for c in reader):raise RuntimeError('Missing reader ID; retain files and recover the reader')
                if reply.get('generation')!=current['generation'] or reply.get('writerInstanceId')!=current['writer']['instanceId']:raise RuntimeError('Unexpected writer incarnation; retain files')
                self.reader_id=reader;self.started=True;self.uncertain=False
                actual=counter(reply['revision'])
                if actual>revision:raise ValueError('Player already has a newer revision')
                if actual<revision:reply=self.client.update(current,complete=state=='complete',reader_id=reader)
            else:reply=self.client.update(current,complete=state=='complete',reader_id=self.reader_id)
            self.last=current;return reply

    def heartbeat(self):
        with self.lock:
            if self.closed or not self.started:return dict(version=1,ok=True,active=False,state='detached' if self.closed else 'waiting-initialization')
            return self.status() if self.last['writer']['state']=='complete' else self._command('heartbeat')

    def status(self):
        with self.lock:
            if self.receipt is not None:return copy.deepcopy(self.receipt)
            if not self.started:return dict(version=1,ok=True,active=False,released=not self.uncertain,state='start-uncertain' if self.uncertain else 'not-started')
            return self._command('status')

    def close(self,timeout: float=2):
        with self.lock:
            self.closed=True
            if self.receipt is not None:return copy.deepcopy(self.receipt)
            if not self.started:return dict(version=1,ok=True,active=False,released=not self.uncertain,state='start-uncertain' if self.uncertain else 'detached')
            reply=self.client.release_reader(self.reader_id,timeout)
            if reply.get('released') is True:
                self.receipt=copy.deepcopy(reply)
                try:self.client.forget_reader(self.reader_id)
                except (OSError,RuntimeError):pass
            return reply
