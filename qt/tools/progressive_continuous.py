"""Explicit, bounded continuous MP4/H.264 or HEVC preview through the player.

Standard library only. Call from the downloader's worker, never its GUI thread.
The writer alone publishes bytes/heartbeats. This helper does not download,
remux, delete sources, create timers, or implicitly start/restart playback.
"""
from __future__ import annotations

import copy
import math
from pathlib import Path
import threading
import time
import uuid
from progressive_client import ApiError

MODE = 'continuous-h264-video-v1'
REORDERED_MODE = 'continuous-h264-reordered-video-v1'
AAC_MODE = 'continuous-h264-aac-window-v1'
HEVC_MODE = 'continuous-hevc-reordered-video-v1'
ACTIONS = frozenset(('continuousRemuxStart', 'continuousRemuxStatus',
    'continuousRemuxUpdate', 'continuousRemuxHeartbeat', 'continuousRemuxCancel',
    'continuousRemuxForget', 'continuousRemuxPlay', 'continuousPlaybackStatus',
    'continuousPlaybackControl'))


def _id(value):
    if not isinstance(value, str) or len(value) != 32 or any(c not in '0123456789abcdef' for c in value):
        raise ValueError('Expected a lowercase 32-hex identity')
    return value


def _counter(value):
    if not isinstance(value, str) or not value.isascii() or not value.isdecimal() or value.startswith('0') or len(value) > 20 or int(value) > 2**64-1:
        raise ValueError('Expected a canonical positive uint64 string')
    return value


def _timeout(value):
    if type(value) not in (int, float) or not math.isfinite(value) or not 0 < value <= 60:
        raise ValueError('Timeout must be finite, greater than zero and at most 60 seconds')
    return time.monotonic() + value


class ContinuousRemux:
    """One preparation job and at most one explicitly committed playback.

    Use a new object for another job. Lost replies are recovered by read-only
    status calls; publish/heartbeat never commit playback. close() proves release
    without deleting originals; call forget() afterwards to acknowledge the job.
    """
    def __init__(self, client, session_directory, *, mode=MODE):
        if not isinstance(mode, str) or mode not in (MODE, REORDERED_MODE, AAC_MODE, HEVC_MODE):
            raise ValueError('Unsupported continuous profile')
        self._mode = mode
        self.client = client
        directory = Path(session_directory).expanduser()
        if not directory.is_absolute():
            raise ValueError('An absolute local session directory is required')
        self.directory = str(directory)
        self.lock = threading.RLock()
        self.request_id = self.job_id = self.play_request_id = self.playback_id = None
        self.identity = None
        self.closing = self.forgotten = False
        self.receipt = None
        caps = client.request(dict(version=1, action='capabilities'))
        cap = caps.get('continuousRemux')
        if (not isinstance(cap, dict) or type(cap.get('version')) is not int or cap['version'] != 1
                or cap.get('mode') != MODE or cap.get('playbackSupported') is not True
                or cap.get('explicitPlaybackCommit') is not True or cap.get('videoOnly') is not True
                or cap.get('audio') is not False or cap.get('bFrames') is not False
                or cap.get('workerRequired') is not True or type(cap.get('workerProtocol')) is not int or cap['workerProtocol'] != 1
                or cap.get('runtimeValidation') != 'per-job-before-playback'
                or cap.get('readerRelease') is not True or caps.get('readerLeaseProtocol') != 1
                or not ACTIONS <= set(caps.get('actions', []))):
            raise RuntimeError('Player lacks the explicit continuous MP4/H.264 video contract')
        self.capability = copy.deepcopy(cap)
        self._initial_position_capability=copy.deepcopy(cap.get('initialPosition'))
        self._queued_seek_capability=copy.deepcopy(cap.get('queuedSeek'))
        self._growing_seek_capability=copy.deepcopy(cap.get('growingSeek'))
        self._start_seconds=0
        if mode != MODE:
            profiles = cap.get('profiles')
            selected = profiles.get(mode) if isinstance(profiles, dict) else None
            if (cap.get('modeSelection') != 'continuousRemuxStart.mode'
                    or not isinstance(selected, dict) or type(selected.get('version')) is not int
                    or selected['version'] != 1 or selected.get('mode') != mode
                    or selected.get('containerHint') != 'mp4' or selected.get('videoCodec') != ('hevc' if mode == HEVC_MODE else 'h264')
                    or selected.get('videoOnly') is not (mode != AAC_MODE) or selected.get('audio') is not (mode == AAC_MODE)
                    or selected.get('bFrames') is not True or selected.get('inheritsCommonContract') is not True
                    or selected.get('explicitSelectionRequired') is not True):
                raise RuntimeError('Player lacks the selected explicit continuous profile')
            if mode == AAC_MODE and (selected.get('audioCodec') != 'aac-lc'
                    or selected.get('sampleRates') != [44100, 48000] or any(type(v) is not int for v in selected['sampleRates'])
                    or selected.get('channels') != [1, 2] or any(type(v) is not int for v in selected['channels'])
                    or selected.get('requiredTrackKinds') != ['video', 'audio'] or selected.get('sharedResourceRequired') is not True
                    or selected.get('pcmWindowRequired') is not True or type(selected.get('pcmWindowVersion')) is not int or selected['pcmWindowVersion'] != 1
                    or type(selected.get('initialSkipSamples')) is not int or selected['initialSkipSamples'] != 1024
                    or selected.get('presentationSamplesRequired') is not True):
                raise RuntimeError('Player lacks the exact AAC PCM-window contract')
            if mode == HEVC_MODE and (selected.get('hevcProfiles') != ['main', 'main10']
                    or selected.get('bitDepths') != [8, 10] or any(type(v) is not int for v in selected['bitDepths'])
                    or selected.get('chromaSubsampling') != '4:2:0' or selected.get('sampleEntries') != ['hvc1', 'hev1']
                    or type(selected.get('nalLengthBytes')) is not int or selected['nalLengthBytes'] != 4
                    or selected.get('singleLayer') is not True or selected.get('requiredTrackKinds') != ['video']
                    or selected.get('codecHintRequired') is not True
                    or type(selected.get('maxParameterSetsPerType')) is not int or selected['maxParameterSetsPerType'] != 1
                    or selected.get('firstPicture') != 'irap'):
                raise RuntimeError('Player lacks the exact HEVC video contract')
            self.capability.update(copy.deepcopy(selected))

    @property
    def mode(self):
        """Fixed for this object/job; capability edits cannot change its identity."""
        return self._mode

    def _check(self, reply, playback=False):
        expected = self.identity or {}
        if (reply.get('mode') != self.mode or any(reply.get(k) != v for k, v in expected.items())
                or reply.get('requestId') != (self.play_request_id if playback else self.request_id)):
            raise RuntimeError('Unexpected continuous job/playback identity')
        field = 'playbackId' if playback else 'jobId'
        identifier = _id(reply.get(field))
        known = self.playback_id if playback else self.job_id
        if (known is not None and identifier != known) or (playback and reply.get('jobId') != self.job_id):
            raise RuntimeError('Continuous reply belongs to another owner')
        if playback:
            if self._start_seconds and (type(reply.get('startSeconds')) not in (int,float)
                    or not math.isfinite(reply['startSeconds']) or reply['startSeconds']!=self._start_seconds):
                raise RuntimeError('Unexpected initial playback position')
            self.playback_id = identifier
        else:
            self.job_id = identifier
        return reply

    def _job(self):
        if self.request_id is None:
            raise RuntimeError('No preparation was requested')
        if self.job_id is None:
            self.status()  # Read-only recovery, including a lost start reply.
        return self.job_id

    def _fenced(self, action, timeout=5):
        return self._check(self.client.request(dict(version=1, action=action,
            jobId=self._job(), **self.identity), timeout=timeout))

    def prepare(self, snapshot, resource_id):
        with self.lock:
            if self.request_id is not None or self.closing:
                raise RuntimeError('Use a new object for another preparation attempt')
            current = copy.deepcopy(snapshot)
            identity = dict(sessionId=_id(current['sessionId']),
                generation=_counter(current['generation']),
                writerInstanceId=_id(current['writer']['instanceId']))
            _counter(current['revision'])
            if not isinstance(resource_id, str) or not resource_id or len(resource_id.encode('utf-8')) > 64:
                raise ValueError('Invalid local resource identity')
            self.identity = identity
            self.request_id = uuid.uuid4().hex
            fields = dict(version=1, action='continuousRemuxStart',
                requestId=self.request_id, sessionDirectory=self.directory,
                session=current, resourceId=resource_id)
            if self.mode != MODE:
                fields['mode'] = self.mode
            return self._check(self.client.request(fields))

    def status(self, *, timeout=5):
        with self.lock:
            if self.request_id is None:
                raise RuntimeError('No preparation was requested')
            selector = dict(jobId=self.job_id) if self.job_id else dict(requestId=self.request_id)
            return self._check(self.client.request(dict(version=1, action='continuousRemuxStatus', **selector), timeout=timeout))

    def publish(self, snapshot):
        with self.lock:
            if self.closing:
                raise RuntimeError('This preview is closing; writer events cannot restart it')
            current = copy.deepcopy(snapshot)
            identity = dict(sessionId=_id(current['sessionId']), generation=_counter(current['generation']),
                writerInstanceId=_id(current['writer']['instanceId']))
            _counter(current['revision'])
            if self.identity is None or identity != self.identity:
                raise ValueError('Session/generation/writer changed; use another preview')
            return self._check(self.client.request(dict(version=1, action='continuousRemuxUpdate',
                jobId=self._job(), session=current)))

    def heartbeat(self):
        with self.lock:
            if self.closing:
                raise RuntimeError('This preview is closing')
            return self._fenced('continuousRemuxHeartbeat')

    def play(self, *, title='FoxicMP', replace=False, activate=False, start_seconds=0):
        with self.lock:
            if self.closing or self.play_request_id is not None:
                raise RuntimeError('Playback was already requested; recover with playback_status()')
            if type(replace) is not bool or type(activate) is not bool or not isinstance(title, str) or len(title.encode('utf-8')) > 1024:
                raise ValueError('Invalid playback options')
            if type(start_seconds) not in (int, float) or not math.isfinite(start_seconds) or not 0 <= start_seconds < 3600:
                raise ValueError('Invalid initial position')
            if start_seconds:
                cap=self._initial_position_capability
                if (not isinstance(cap,dict) or type(cap.get('version')) is not int or cap['version']!=1
                        or cap.get('field')!='continuousRemuxPlay.startSeconds'
                        or cap.get('range')!='published-presentation-prefix'
                        or cap.get('completeRange')!='source-duration'
                        or cap.get('pending')!='caller-retry'
                        or cap.get('decoderMayBuffer') is not True):
                    raise RuntimeError('Player lacks explicit initial-position support')
            ready = self.status()
            if ready.get('playbackReady') is not True:
                raise RuntimeError('Preparation is not ready; playback was not requested')
            if start_seconds:
                duration=ready.get('durationSeconds');boundary=ready.get('indexedEndSeconds')
                if (type(duration) not in (int,float) or not math.isfinite(duration)
                        or math.ceil(start_seconds*10000000)>=round(duration*10000000)):
                    raise ValueError('Initial position exceeds source duration')
                if ready.get('outputComplete') is not True and (type(boundary) not in (int,float)
                        or not math.isfinite(boundary) or math.ceil(start_seconds*10000000)>=round(boundary*10000000)):
                    raise RuntimeError('Wait for confirmed initial position; playback was not requested')
            self._start_seconds=start_seconds
            self.play_request_id = uuid.uuid4().hex
            fields=dict(version=1, action='continuousRemuxPlay',
                jobId=self.job_id, requestId=self.play_request_id, **self.identity,
                title=title, replace=replace, activate=activate)
            if start_seconds:fields['startSeconds']=start_seconds
            try:
                reply=self.client.request(fields)
            except ApiError as error:
                # An explicit range rejection occurs before ticket creation.
                # A lost/ambiguous reply retains the identity for read-only recovery.
                if start_seconds and error.code=='waiting-range':
                    self.play_request_id=None
                    self._start_seconds=0
                raise
            return self._check(reply, playback=True)

    def playback_status(self, *, timeout=5):
        with self.lock:
            if self.play_request_id is None:
                raise RuntimeError('Playback was never requested')
            selector = dict(playbackId=self.playback_id) if self.playback_id else dict(requestId=self.play_request_id)
            return self._check(self.client.request(dict(version=1, action='continuousPlaybackStatus', **selector), timeout=timeout), playback=True)

    def control(self, command, *, seconds=None, allow_growing=False, wait_for_range=False):
        with self.lock:
            if command not in ('play', 'pause', 'stop', 'seek', 'cancelSeek'):
                raise ValueError('Unknown playback control')
            if type(allow_growing) is not bool or (allow_growing and command!='seek'):
                raise ValueError('Growing seek option requires seek')
            if type(wait_for_range) is not bool or (wait_for_range and (command!='seek' or not allow_growing)):
                raise ValueError('Range waiting requires growing seek opt-in')
            if wait_for_range or command=='cancelSeek':
                expected=dict(version=1,field='continuousPlaybackControl.waitForRange',requiresGrowingOptIn=True,maxPending=1,
                    superseding='latest-target',cancelCommand='cancelSeek',previousPlaybackUntilCommit=True)
                cap=self._queued_seek_capability
                if (not isinstance(cap,dict) or set(cap)!=set(expected)
                        or any(type(cap[k]) is not type(v) or cap[k]!=v for k,v in expected.items())):
                    raise RuntimeError('Player lacks explicit queued-seek support')
            fields = dict(command=command)
            if wait_for_range:fields['waitForRange']=True
            if command == 'seek':
                if type(seconds) not in (int, float) or not math.isfinite(seconds) or not 0 <= seconds < 3600:
                    raise ValueError('Invalid seek time')
                fields['seconds'] = seconds
                if allow_growing:
                    expected=dict(version=1,field='continuousPlaybackControl.allowGrowing',range='published-presentation-prefix',
                        unavailable='waiting-range',maxSeeksPerPlayback=16,decoderMayBuffer=True)
                    cap=self._growing_seek_capability
                    if (not isinstance(cap,dict) or set(cap)!=set(expected)
                            or any(type(cap[k]) is not type(v) or cap[k]!=v for k,v in expected.items())):
                        raise RuntimeError('Player lacks explicit growing-seek support')
                    fields['allowGrowing']=True
            elif seconds is not None:
                raise ValueError('Unexpected seek time')
            if self.closing and command != 'stop':
                raise RuntimeError('This preview is closing')
            if self.playback_id is None:
                self.playback_status()
            if command == 'stop':
                self.closing = True
            return self._check(self.client.request(dict(version=1, action='continuousPlaybackControl',
                playbackId=self.playback_id, **fields)), playback=True)

    def close(self, *, timeout=2):
        with self.lock:
            deadline = _timeout(timeout)
            self.closing = True
            if self.request_id is None:
                return dict(version=1, ok=True, released=True, sourceFilesClosed=True)
            if self.receipt is not None:
                return copy.deepcopy(self.receipt)
            if self.job_id is None:
                self.status(timeout=max(.001, deadline-time.monotonic()))
            reply = self._fenced('continuousRemuxCancel', timeout=max(.001, deadline-time.monotonic()))
            while True:
                if reply.get('released') is True and reply.get('sourceFilesClosed') is True:
                    self.receipt = copy.deepcopy(reply)
                    return reply
                if time.monotonic() >= deadline:
                    return reply  # Timeout is never a release acknowledgement.
                time.sleep(min(.02, max(0, deadline-time.monotonic())))
                reply = self.status(timeout=max(.001, deadline-time.monotonic()))

    def forget(self, *, timeout=2):
        with self.lock:
            deadline = _timeout(timeout)
            if self.receipt is None:
                raise RuntimeError('close() must first prove filesClosed/released')
            if self.forgotten:
                return self.status(timeout=max(.001, deadline-time.monotonic()))
            reply = self._fenced('continuousRemuxForget', timeout=max(.001, deadline-time.monotonic()))
            while reply.get('forgotten') is not True:
                if time.monotonic() >= deadline:
                    raise TimeoutError('Continuous job acknowledgement is still pending')
                time.sleep(min(.02, max(0, deadline-time.monotonic())))
                reply = self.status(timeout=max(.001, deadline-time.monotonic()))
            self.forgotten = True
            return reply
