"""Explicit, bounded continuous MP4/H.264, HEVC or AV1 preview through the player.

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
WEBM_VP8_MODE = 'continuous-webm-vp8-video-v1'
WEBM_VP8_OPUS_MODE = 'continuous-webm-vp8-opus-v1'
WEBM_AV1_MODE = 'continuous-webm-av1-video-v1'
WEBM_AV1_OPUS_MODE = 'continuous-webm-av1-opus-v1'
EXTENDED_WEBM_PROFILES = {
    WEBM_VP8_MODE: dict(videoCodec='vp8', audio=False),
    WEBM_VP8_OPUS_MODE: dict(videoCodec='vp8', audio=True),
    WEBM_AV1_MODE: dict(videoCodec='av1', audio=False),
    WEBM_AV1_OPUS_MODE: dict(videoCodec='av1', audio=True),
}
REORDERED_MODE = 'continuous-h264-reordered-video-v1'
AAC_MODE = 'continuous-h264-aac-window-v1'
HEVC_MODE = 'continuous-hevc-reordered-video-v1'
HEVC_AAC_MODE = 'continuous-hevc-aac-window-v1'
AV1_MODE = 'continuous-av1-video-v1'
AV1_AAC_MODE = 'continuous-av1-aac-window-v1'
VP9_MODE = 'continuous-vp9-video-v1'
VP9_AAC_MODE = 'continuous-vp9-aac-window-v1'
VP9_SUPERFRAME_MODE = 'continuous-vp9-superframe-video-v1'
VP9_SUPERFRAME_AAC_MODE = 'continuous-vp9-superframe-aac-window-v1'
WEBM_VP9_MODE = 'continuous-webm-vp9-video-v1'
WEBM_VP9_SUPERFRAME_MODE = 'continuous-webm-vp9-superframe-video-v1'
WEBM_VP9_OPUS_MODE = 'continuous-webm-vp9-opus-v1'
WEBM_VP9_OPUS_NTSC_MODE = 'continuous-webm-vp9-opus-ntsc-v1'
WEBM_VP9_OPUS_PACKET_CLOCK_MODE = 'continuous-webm-vp9-opus-packet-clock-v1'
WEBM_VP9_OPUS_DERIVED_AV_MODE = 'continuous-webm-vp9-opus-simple-vfr-derived-av-v1'
WEBM_VP9_OPUS_NOMINAL_VFR_MODE = 'continuous-webm-vp9-opus-simple-vfr-nominal-v1'
WEBM_VP9_OPUS_AUTO_VFR_MODE = 'continuous-webm-vp9-opus-simple-vfr-auto-count-v1'
WEBM_VP9_OPUS_SIMPLE_VFR_MODE = 'continuous-webm-vp9-opus-simple-vfr-v1'
WEBM_VP9_OPUS_VFR_MODE = 'continuous-webm-vp9-opus-vfr-v1'
WEBM_VP9_OPUS_CFR_MODE = 'continuous-webm-vp9-opus-cfr-v1'
WEBM_CFR_RATES = ((24000,1001),(24,1),(25,1),(30000,1001),(30,1),(50,1),(60000,1001),(60,1))
ACTIONS = frozenset(('continuousRemuxStart', 'continuousRemuxStatus',
    'continuousRemuxUpdate', 'continuousRemuxHeartbeat', 'continuousRemuxCancel',
    'continuousRemuxForget', 'continuousRemuxPlay', 'continuousPlaybackStatus',
    'continuousPlaybackControl'))


def _exact_contract(actual, expected):
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return actual.keys() == expected.keys() and all(_exact_contract(actual[k], v) for k, v in expected.items())
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(_exact_contract(a, b) for a, b in zip(actual, expected))
    return actual == expected


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
    def __init__(self, client, session_directory, *, mode=MODE, audio_samples=None, video_rate=None, video_samples=None):
        if not isinstance(mode, str) or mode not in (MODE, REORDERED_MODE, AAC_MODE, HEVC_MODE, HEVC_AAC_MODE, AV1_MODE, AV1_AAC_MODE, VP9_MODE, VP9_AAC_MODE, VP9_SUPERFRAME_MODE, VP9_SUPERFRAME_AAC_MODE, WEBM_VP9_MODE, WEBM_VP9_SUPERFRAME_MODE, WEBM_VP9_OPUS_MODE, WEBM_VP9_OPUS_NTSC_MODE, WEBM_VP9_OPUS_CFR_MODE, WEBM_VP9_OPUS_VFR_MODE, WEBM_VP9_OPUS_SIMPLE_VFR_MODE, WEBM_VP9_OPUS_AUTO_VFR_MODE, WEBM_VP9_OPUS_NOMINAL_VFR_MODE, WEBM_VP9_OPUS_DERIVED_AV_MODE, WEBM_VP9_OPUS_PACKET_CLOCK_MODE, *EXTENDED_WEBM_PROFILES):
            raise ValueError('Unsupported continuous profile')
        webm_mode = mode in (WEBM_VP9_MODE, WEBM_VP9_SUPERFRAME_MODE, WEBM_VP9_OPUS_MODE, WEBM_VP9_OPUS_NTSC_MODE, WEBM_VP9_OPUS_CFR_MODE, WEBM_VP9_OPUS_VFR_MODE, WEBM_VP9_OPUS_SIMPLE_VFR_MODE, WEBM_VP9_OPUS_AUTO_VFR_MODE, WEBM_VP9_OPUS_NOMINAL_VFR_MODE, WEBM_VP9_OPUS_DERIVED_AV_MODE, WEBM_VP9_OPUS_PACKET_CLOCK_MODE)
        vp9_superframe = mode in (VP9_SUPERFRAME_MODE, VP9_SUPERFRAME_AAC_MODE, WEBM_VP9_SUPERFRAME_MODE, WEBM_VP9_OPUS_MODE, WEBM_VP9_OPUS_NTSC_MODE, WEBM_VP9_OPUS_CFR_MODE, WEBM_VP9_OPUS_VFR_MODE, WEBM_VP9_OPUS_SIMPLE_VFR_MODE, WEBM_VP9_OPUS_AUTO_VFR_MODE, WEBM_VP9_OPUS_NOMINAL_VFR_MODE, WEBM_VP9_OPUS_DERIVED_AV_MODE, WEBM_VP9_OPUS_PACKET_CLOCK_MODE)
        vp9_mode = mode in (VP9_MODE, VP9_AAC_MODE, VP9_SUPERFRAME_MODE, VP9_SUPERFRAME_AAC_MODE, WEBM_VP9_MODE, WEBM_VP9_SUPERFRAME_MODE, WEBM_VP9_OPUS_MODE, WEBM_VP9_OPUS_NTSC_MODE, WEBM_VP9_OPUS_CFR_MODE, WEBM_VP9_OPUS_VFR_MODE, WEBM_VP9_OPUS_SIMPLE_VFR_MODE, WEBM_VP9_OPUS_AUTO_VFR_MODE, WEBM_VP9_OPUS_NOMINAL_VFR_MODE, WEBM_VP9_OPUS_DERIVED_AV_MODE, WEBM_VP9_OPUS_PACKET_CLOCK_MODE)
        av1_mode = mode in (AV1_MODE, AV1_AAC_MODE)
        audio_mode = mode in (AAC_MODE, HEVC_AAC_MODE, AV1_AAC_MODE, VP9_AAC_MODE, VP9_SUPERFRAME_AAC_MODE, WEBM_VP9_OPUS_MODE, WEBM_VP9_OPUS_NTSC_MODE, WEBM_VP9_OPUS_CFR_MODE, WEBM_VP9_OPUS_VFR_MODE, WEBM_VP9_OPUS_SIMPLE_VFR_MODE, WEBM_VP9_OPUS_AUTO_VFR_MODE, WEBM_VP9_OPUS_NOMINAL_VFR_MODE, WEBM_VP9_OPUS_DERIVED_AV_MODE, WEBM_VP9_OPUS_PACKET_CLOCK_MODE)
        hevc_mode = mode in (HEVC_MODE, HEVC_AAC_MODE)
        nominal_mode = mode == WEBM_VP9_OPUS_NOMINAL_VFR_MODE
        auto_count_mode = mode in (WEBM_VP9_OPUS_AUTO_VFR_MODE, WEBM_VP9_OPUS_NOMINAL_VFR_MODE, WEBM_VP9_OPUS_DERIVED_AV_MODE, WEBM_VP9_OPUS_PACKET_CLOCK_MODE)
        simple_vfr_mode = mode in (WEBM_VP9_OPUS_SIMPLE_VFR_MODE, WEBM_VP9_OPUS_AUTO_VFR_MODE, WEBM_VP9_OPUS_NOMINAL_VFR_MODE, WEBM_VP9_OPUS_DERIVED_AV_MODE, WEBM_VP9_OPUS_PACKET_CLOCK_MODE)
        vfr_mode = mode in (WEBM_VP9_OPUS_VFR_MODE, WEBM_VP9_OPUS_SIMPLE_VFR_MODE, WEBM_VP9_OPUS_AUTO_VFR_MODE, WEBM_VP9_OPUS_NOMINAL_VFR_MODE, WEBM_VP9_OPUS_DERIVED_AV_MODE, WEBM_VP9_OPUS_PACKET_CLOCK_MODE)
        cfr_mode = mode == WEBM_VP9_OPUS_CFR_MODE
        ntsc_mode = mode == WEBM_VP9_OPUS_NTSC_MODE
        opus_mode = mode in (WEBM_VP9_OPUS_MODE, WEBM_VP9_OPUS_NTSC_MODE, WEBM_VP9_OPUS_CFR_MODE, WEBM_VP9_OPUS_VFR_MODE, WEBM_VP9_OPUS_SIMPLE_VFR_MODE, WEBM_VP9_OPUS_AUTO_VFR_MODE, WEBM_VP9_OPUS_NOMINAL_VFR_MODE, WEBM_VP9_OPUS_DERIVED_AV_MODE, WEBM_VP9_OPUS_PACKET_CLOCK_MODE)
        packet_audio = mode == WEBM_VP9_OPUS_PACKET_CLOCK_MODE
        derived_audio = mode in (WEBM_VP9_OPUS_DERIVED_AV_MODE, WEBM_VP9_OPUS_PACKET_CLOCK_MODE)
        if opus_mode and not derived_audio:
            if type(audio_samples) is not int or not 648 < audio_samples <= 48000000:
                raise ValueError("Opus requires an exact bounded presentation sample count")
        elif audio_samples is not None:
            raise ValueError("audio_samples is only valid for WebM/Opus")
        if cfr_mode:
            if type(video_rate) not in (tuple,list) or len(video_rate)!=2 or any(type(v) is not int for v in video_rate) or tuple(video_rate) not in WEBM_CFR_RATES:
                raise ValueError('CFR requires one canonical allowed video_rate pair')
            self._video_rate=tuple(video_rate)
        else:
            if video_rate is not None:raise ValueError('video_rate is only valid for CFR mode')
            self._video_rate=None
        if vfr_mode and not auto_count_mode:
            if type(video_samples) is not int or not 0 < video_samples <= 50000:
                raise ValueError('VFR requires an exact bounded video_samples count')
        elif video_samples is not None:
            raise ValueError('video_samples is only valid for VFR mode')
        self._video_samples = video_samples
        self._audio_samples = audio_samples
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
        if mode in EXTENDED_WEBM_PROFILES:
            try:
                extension = client.request(dict(version=1, action='continuousWebmCapabilities'))
            except ApiError as error:
                if error.code not in ('invalid_request', 'unknown_session'):
                    raise
                raise RuntimeError('Player lacks continuous VP8/AV1 WebM playback') from error
            profiles = extension.get('profiles') if isinstance(extension, dict) else None
            required = dict(version=1, action='continuousWebmCapabilities', containerHint='webm',
                            extends='continuousRemux', knownLength=True, sha256Required=True,
                            frameCount='derived', videoDuration='required-exact-millisecond-endpoint',
                            simpleBlocks=True, blockGroups='explicit-duration', finiteClusters=True,
                            unknownSizeClusters=True, audioCodec='opus', opusPacketSamples='from-toc',
                            opusPreSkipSamples=[120,312], discardPaddingPreserved=True,
                            maxInputBytes=134217728, maxOutputBytes=67108864, maxPackets=50000,
                            maxInitializationBytes=1048576, maxClusterBytes=16777216)
            if (not isinstance(profiles, dict)
                    or not _exact_contract(profiles.get(mode), EXTENDED_WEBM_PROFILES[mode])
                    or any(not _exact_contract(extension.get(key), value) for key, value in required.items())):
                raise RuntimeError('Player lacks the selected continuous WebM contract')
            self.capability.update(copy.deepcopy(profiles[mode]), containerHint='webm',
                                   videoOnly=not profiles[mode]['audio'], mode=mode)
        elif mode != MODE:
            profiles = cap.get('profiles')
            selected = profiles.get(mode) if isinstance(profiles, dict) else None
            if (cap.get('modeSelection') != 'continuousRemuxStart.mode'
                    or not isinstance(selected, dict) or type(selected.get('version')) is not int
                    or selected['version'] != 1 or selected.get('mode') != mode
                    or selected.get('containerHint') != ('webm' if webm_mode else 'mp4') or selected.get('videoCodec') != ('vp9' if vp9_mode else 'av1' if av1_mode else 'hevc' if hevc_mode else 'h264')
                    or selected.get('videoOnly') is not (not audio_mode) or selected.get('audio') is not audio_mode
                    or selected.get('bFrames') is not (not (av1_mode or vp9_mode)) or selected.get('inheritsCommonContract') is not True
                    or selected.get('explicitSelectionRequired') is not True):
                raise RuntimeError('Player lacks the selected explicit continuous profile')
            if audio_mode and not opus_mode and (selected.get('audioCodec') != 'aac-lc'
                    or selected.get('sampleRates') != [44100, 48000] or any(type(v) is not int for v in selected['sampleRates'])
                    or selected.get('channels') != [1, 2] or any(type(v) is not int for v in selected['channels'])
                    or selected.get('requiredTrackKinds') != ['video', 'audio'] or selected.get('sharedResourceRequired') is not True
                    or selected.get('pcmWindowRequired') is not True or type(selected.get('pcmWindowVersion')) is not int or selected['pcmWindowVersion'] != 1
                    or type(selected.get('initialSkipSamples')) is not int or selected['initialSkipSamples'] != 1024
                    or selected.get('presentationSamplesRequired') is not True):
                raise RuntimeError('Player lacks the exact AAC PCM-window contract')
            if opus_mode:
                exact = dict(audioCodec='opus', sampleRates=[48000], channels=[1,2], pcmWindowRequired=False,
                    opusPacketSamples=960, opusPreSkipSamples=312, opusGain=0, opusMappingFamily=0,
                    seekPreRollNanoseconds=80000000, minAudioPackets=2,
                    audioSamplesField='continuousRemuxStart.audioSamples', maxAudioPresentationSamples=48000000,
                    sharedResourceRequired=True, discardPaddingPreserved=True, clusterSealing=True, audioBlockGroupsOnly=not vfr_mode)
                if derived_audio:exact.pop('audioSamplesField')
                if packet_audio:
                    exact.pop('opusPacketSamples');exact.update(opusPacketSamples='from-toc',opusPreSkipSamples=[120,312],minOpusPacketSamples=120,maxOpusPacketSamples=5760,opusPacketSampleQuantum=120)
                if any(not _exact_contract(selected.get(k), v) for k,v in exact.items()):
                    raise RuntimeError('Player lacks the exact WebM/Opus contract')
            if hevc_mode and (selected.get('hevcProfiles') != ['main', 'main10']
                    or selected.get('bitDepths') != [8, 10] or any(type(v) is not int for v in selected['bitDepths'])
                    or selected.get('chromaSubsampling') != '4:2:0' or selected.get('sampleEntries') != ['hvc1', 'hev1']
                    or type(selected.get('nalLengthBytes')) is not int or selected['nalLengthBytes'] != 4
                    or selected.get('singleLayer') is not True or selected.get('requiredTrackKinds') != (['video', 'audio'] if audio_mode else ['video'])
                    or selected.get('codecHintRequired') is not True
                    or type(selected.get('maxParameterSetsPerType')) is not int or selected['maxParameterSetsPerType'] != 1
                    or selected.get('firstPicture') != 'irap'):
                raise RuntimeError('Player lacks the exact HEVC video contract')
            if av1_mode and (selected.get('av1Profiles') != ['main'] or selected.get('bitDepths') != [8, 10]
                    or any(type(v) is not int for v in selected['bitDepths'])
                    or selected.get('chromaSubsampling') != '4:2:0' or selected.get('sampleEntries') != ['av01']
                    or selected.get('singleLayer') is not True or selected.get('packetReorder') is not False
                    or selected.get('oneDisplayedFramePerSample') is not True or selected.get('hiddenFrames') is not False
                    or selected.get('showExistingFrames') is not False or selected.get('stillPicture') is not False
                    or selected.get('lowOverheadObuRequired') is not True or selected.get('obuTypes') != [1, 2, 5, 6, 15]
                    or any(type(v) is not int for v in selected['obuTypes'])
                    or selected.get('sequenceHeaderRequiredInFirstPacket') is not True
                    or selected.get('requiredTrackKinds') != (['video', 'audio'] if audio_mode else ['video']) or selected.get('codecHintRequired') is not True):
                raise RuntimeError('Player lacks the exact AV1 video contract')
            if vp9_mode and (selected.get('vp9Profiles') != [0, 2] or any(type(v) is not int for v in selected['vp9Profiles'])
                    or selected.get('bitDepths') != [8, 10] or any(type(v) is not int for v in selected['bitDepths'])
                    or selected.get('chromaSubsampling') != '4:2:0' or selected.get('sampleEntries') != ['vp09']
                    or type(selected.get('configurationVersion')) is not int or selected['configurationVersion'] != 1
                    or (not webm_mode and selected.get('headMoovRequired') is not True) or type(selected.get('maxHeadBytes')) is not int or selected['maxHeadBytes'] != 1048576
                    or selected.get('packetReorder') is not False or selected.get('oneDisplayedFramePerSample') is not True
                    or selected.get('hiddenFrames') is not vp9_superframe or selected.get('showExistingFrames') is not False
                    or selected.get('superframes') is not vp9_superframe or selected.get('fixedCodedAndRenderSize') is not True
                    or selected.get('requiredTrackKinds') != (['video', 'audio'] if audio_mode else ['video']) or selected.get('codecHintRequired') is not True):
                raise RuntimeError('Player lacks the exact VP9 video contract')
            if vp9_superframe and (selected.get('indexedSuperframesRequired') is not True
                    or type(selected.get('maxFramesPerSample')) is not int or selected['maxFramesPerSample'] != 8
                    or type(selected.get('maxHiddenFramesPerSample')) is not int or selected['maxHiddenFramesPerSample'] != 7
                    or selected.get('displayedFrameLast') is not True or selected.get('hiddenInterFramesOnly') is not True
                    or selected.get('firstSampleSingleKeyframe') is not True):
                raise RuntimeError('Player lacks the exact indexed VP9 superframe contract')
            if webm_mode:
                exact={'unknownSegmentSizeRequired': True, 'finiteClusters': True, 'unknownSizeClusters': True, 'simpleBlocksOnly': True, 'lacing': False, 'blockGroups': False, 'timestampScaleNanoseconds': 1000000, 'defaultDurationNanoseconds': 40000000, 'frameRate': 25, 'expectedSamplesFromTimeline': True, 'maxClusterBytes': 16777216, 'maxClusters': 8192, 'maxTopLevelElements': 32768, 'maxClusterChildren': 65536, 'maxMetadataDepth': 8, 'maxHeadElements': 32}
                if opus_mode:
                    exact.update(simpleBlocksOnly=False, blockGroups=True)
                if ntsc_mode or cfr_mode:
                    exact.update(frameRate=dict(numerator=30000,denominator=1001),
                        defaultDurationNanoseconds=[33366666,33366667],fractionalFrameClock=True,
                        timelineDurationRounding='floor-or-ceil-100ns',packetTimestampRounding='nearest-millisecond',
                        videoCoverage='validated-frame-grid-minus-one-millisecond')
                if cfr_mode:
                    exact.update(frameRate=dict(field='continuousRemuxStart.videoRate'),
                        defaultDurationNanoseconds='floor-or-ceil-from-videoRate',defaultDurationToleranceNanoseconds=1,videoRateField='continuousRemuxStart.videoRate',
                        allowedVideoRates=[dict(numerator=n,denominator=d) for n,d in WEBM_CFR_RATES])
                if vfr_mode:
                    exact.update(frameRate='variable',defaultDurationNanoseconds='optional-bounded-when-video-block-duration-present',
                        expectedSamplesFromTimeline=False,variableFrameClock=True,videoSamplesField='continuousRemuxStart.videoSamples',
                        videoBlockDurationRequired=True,videoBlockGroupsRequired=True,videoReferences=True,
                        videoCoverage='validated-contiguous-explicit-sample-intervals',timelineDurationRounding='exact-100ns-from-block-end',
                        packetTimestampRounding='original-millisecond',maxVideoFrameDurationTicks=1000,defaultDurationOptional=True)
                if simple_vfr_mode:
                    exact.update(defaultDurationNanoseconds='absent',defaultDurationOptional=False,
                        videoBlockDurationRequired=False,videoBlockGroupsRequired=False,videoSimpleBlocksRequired=True,
                        videoCoverage='validated-next-pts-and-final-endpoint',timelineDurationRounding='exact-100ns-from-final-endpoint',
                        nextPtsLookahead=True,lastFrameRequiresSourceComplete=True,maxPendingBytes=33554432,maxPendingClusters=32,postMediaTags=False)
                if auto_count_mode:
                    exact.pop('videoSamplesField')
                    exact.update(videoSampleCount='derived-at-confirmed-eof',videoSamplesRequired=False,
                        indexedVideoSamplesField='indexedVideoSamples',videoSampleCountFinalField='videoSampleCountFinal')
                    if 'videoSamplesField' in selected:raise RuntimeError('Auto-count profile must not declare a frame plan')
                if derived_audio:
                    exact.pop('audioSamplesField',None)
                    exact.update(audioSamplesRequired=False,audioSampleCount='derived-from-packets-preskip-end-trim',
                        indexedAudioSamplesField='indexedAudioSamples',audioSampleCountFinalField='audioSampleCountFinal',
                        audioTrackEndObservedField='audioTrackEndObserved',audioPacketLookahead=True,
                        shortAudioWithoutTrim='wait-for-source-complete',maxEndTrimSamples=5759 if packet_audio else 959)
                    if packet_audio:exact.update(endTrimBound='less-than-final-packet-duration',packetClockFromPayload=True,qtAudioTimestampScaleNanoseconds=1000,qtSeekTrim='presentation-samples-before-user-filters')
                    if 'audioSamplesField' in selected:raise RuntimeError('Derived-audio profile must not declare a sample plan')
                if nominal_mode:
                    exact.update(defaultDurationNanoseconds='bounded-nominal-hint',nominalDefaultDurationRequired=True,
                        minNominalDefaultDurationNanoseconds=1000000,maxNominalDefaultDurationNanoseconds=1000000000,
                        nominalDefaultDurationPreserved=True)
                if any(not _exact_contract(selected.get(key), value) for key,value in exact.items()) or 'headMoovRequired' in selected:
                    raise RuntimeError('Player lacks the exact bounded WebM/VP9 contract')
            self.capability.update(copy.deepcopy(selected))

    @property
    def mode(self):
        """Fixed for this object/job; capability edits cannot change its identity."""
        return self._mode

    def _check(self, reply, playback=False):
        expected = self.identity or {}
        extended = EXTENDED_WEBM_PROFILES.get(self.mode)
        if extended and reply.get('audio') is not extended['audio']:
            raise RuntimeError('Unexpected continuous WebM track layout')
        if extended or self.mode in (WEBM_VP9_OPUS_AUTO_VFR_MODE,WEBM_VP9_OPUS_NOMINAL_VFR_MODE, WEBM_VP9_OPUS_DERIVED_AV_MODE, WEBM_VP9_OPUS_PACKET_CLOCK_MODE):
            count=reply.get('indexedVideoSamples')
            if (not isinstance(count,str) or not count.isascii() or not count.isdecimal()
                    or (count!='0' and count.startswith('0')) or len(count)>5 or int(count)>50000
                    or type(reply.get('videoSampleCountFinal')) is not bool
                    or type(reply.get('outputComplete')) is not bool
                    or reply['videoSampleCountFinal'] is not reply['outputComplete']
                    or (reply['videoSampleCountFinal'] and int(count)==0) or 'videoSamples' in reply):
                raise RuntimeError('Invalid derived video sample status')
        if (extended and extended['audio']) or self.mode in (WEBM_VP9_OPUS_DERIVED_AV_MODE, WEBM_VP9_OPUS_PACKET_CLOCK_MODE):
            count=reply.get('indexedAudioSamples')
            if (not isinstance(count,str) or not count.isascii() or not count.isdecimal()
                    or (count!='0' and count.startswith('0')) or len(count)>8 or int(count)>48000000
                    or type(reply.get('audioSampleCountFinal')) is not bool
                    or type(reply.get('audioTrackEndObserved')) is not bool
                    or reply['audioSampleCountFinal'] is not reply['outputComplete']
                    or (reply['audioSampleCountFinal'] and int(count)<649) or 'audioSamples' in reply):
                raise RuntimeError('Invalid derived audio sample status')
        if self._video_samples is not None and not _exact_contract(reply.get('videoSamples'),str(self._video_samples)):
            raise RuntimeError('Unexpected continuous video sample plan')
        if self._video_rate is not None and not _exact_contract(reply.get("videoRate"),dict(zip(("numerator","denominator"),self._video_rate))):
            raise RuntimeError("Unexpected continuous video rate")
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
            if self._audio_samples is not None:
                fields['audioSamples'] = str(self._audio_samples)
            if self._video_samples is not None:fields['videoSamples']=str(self._video_samples)
            if self._video_rate is not None:
                fields['videoRate']=dict(zip(('numerator','denominator'),self._video_rate))
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
