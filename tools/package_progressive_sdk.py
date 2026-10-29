#!/usr/bin/env python3
"""Build a reproducible player-only integration SDK ZIP, without media/binaries."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    **{name: 'qt/tools/' + name for name in (
        'progressive_client.py', 'progressive_windows.py', 'progressive_sdk.py', 'progressive_writer.py',
        'progressive_schema.py', 'progressive_resources.py', 'progressive_v2.py')},
    'progressive-v2-vod.json': 'tests/fixtures/progressive-v2/vod.json',
    'README.ru.md': 'docs/ProgressiveSdk.md',
    'ProgressiveSdk.md': 'docs/ProgressiveSdk.md',
    'LinuxProgressiveApi.md': 'docs/LinuxProgressiveApi.md',
    'LICENSE.txt': 'LICENSE.txt',
    **{name: 'docs/' + name for name in (
        'ProgressiveIntegrationPlan.md', 'ProgressiveTechnicalSpec-2026-10-25.ru.md',
        'ProgressiveV1Validation-2026-10-25.md', 'ProgressiveV1Validation-2026-10-25.json',
        'ProgressiveSdkValidation-2026-10-25.md', 'ProgressiveSdkValidation-2026-10-25.json',
        'ProgressiveManifestV2.md', 'ProgressiveModelValidation-2026-10-25.md',
        'ProgressiveModelValidation-2026-10-25.json', 'ProgressiveReaderLeases.md',
        'ProgressiveReaderValidation-2026-10-25.md', 'ProgressiveReaderValidation-2026-10-25.json',
        'ProgressiveResourceIo.md', 'ProgressiveResourceValidation-2026-10-26.md',
        'ProgressiveResourceValidation-2026-10-26.json', 'ProgressiveMediaV2.md',
        'ProgressiveMediaValidation-2026-10-26.md', 'ProgressiveMediaValidation-2026-10-26.json',
        'ProgressivePreparation.md', 'ProgressivePreparationValidation-2026-10-26.md',
        'ProgressivePreparationValidation-2026-10-26.json', 'ProgressivePublication.md',
        'ProgressivePublicationValidation-2026-10-26.md', 'ProgressivePublicationValidation-2026-10-26.json',
        'ProgressiveRetirement.md', 'ProgressiveRetirementValidation-2026-10-26.md',
        'ProgressiveRetirementValidation-2026-10-26.json',
        'ProgressiveLegacyOpening.md', 'ProgressiveLegacyOpeningValidation-2026-10-26.md',
        'ProgressiveLegacyOpeningValidation-2026-10-26.json',
        'ProgressiveLegacyMaintenance.md', 'ProgressiveLegacyMaintenanceValidation-2026-10-26.md',
        'ProgressiveLegacyMaintenanceValidation-2026-10-26.json',
        'ProgressiveEngineShutdown.md', 'ProgressiveEngineShutdownValidation-2026-10-26.md',
        'ProgressiveEngineShutdownValidation-2026-10-26.json',
        'ProgressiveClassicClose.md', 'ProgressiveClassicCloseValidation-2026-10-26.md',
        'ProgressiveClassicCloseValidation-2026-10-26.json',
        'ProgressiveHevc.md', 'ProgressiveHevcValidation-2026-10-26.md',
        'ProgressiveHevcValidation-2026-10-26.json',
        'ProgressiveAv1.md', 'ProgressiveAv1Validation-2026-10-26.md',
        'ProgressiveAv1Validation-2026-10-26.json',
        'ProgressiveVp9.md', 'ProgressiveVp9Validation-2026-10-27.md',
        'ProgressiveVp9Validation-2026-10-27.json',
        'ProgressiveOpus.md', 'ProgressiveOpusValidation-2026-10-27.md',
        'ProgressiveOpusValidation-2026-10-27.json',
        'ProgressiveWebM.md', 'ProgressiveWebMValidation-2026-10-28.md',
        'ProgressiveWebMValidation-2026-10-28.json',
        'ProgressiveWebMAv1.md', 'ProgressiveWebMAv1Validation-2026-10-28.md',
        'ProgressiveWebMAv1Validation-2026-10-28.json',
        'ProgressiveMixedContainers.md', 'ProgressiveMixedValidation-2026-10-28.md',
        'ProgressiveMixedValidation-2026-10-28.json',
        'ProgressiveVp8Vorbis.md', 'ProgressiveVp8VorbisValidation-2026-10-28.md',
        'ProgressiveVp8VorbisValidation-2026-10-28.json',
        'ProgressiveMultiTrack.md','ProgressiveMultiTrackValidation-2026-10-28.md','ProgressiveMultiTrackValidation-2026-10-28.json',
        'ProgressiveSeek.md','ProgressiveSeekValidation-2026-10-28.md','ProgressiveSeekValidation-2026-10-28.json',
        'ProgressiveOptionalSrt.md','ProgressiveOptionalSrtValidation-2026-10-29.md','ProgressiveOptionalSrtValidation-2026-10-29.json')},
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    content = {name: (ROOT / source).read_bytes() for name, source in FILES.items()}
    version = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT))
    manifest = dict(schemaVersion=1, protocol='progressive-manifest-v1', sdkVersion='1.24', readerLeaseProtocol=1,
        resourceIoProbeVersion=1, resourceIoSchemaVersions=[2], resourceIoPlaybackSupported=False,
        playbackSchemaVersions=[1,2], validationSchemaVersions=[2],
        schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',profiles=['separate-fmp4-h264-aac-lc','separate-fmp4-hevc-aac-lc','separate-fmp4-av1-aac-lc','separate-fmp4-vp9-aac-lc','separate-fmp4-h264-opus','separate-fmp4-hevc-opus','separate-fmp4-av1-opus','separate-fmp4-vp9-opus','separate-webm-vp9-opus','separate-webm-av1-opus','separate-fmp4-webm-h264-opus','separate-fmp4-webm-hevc-opus','separate-fmp4-webm-av1-opus','separate-fmp4-webm-vp9-opus','separate-webm-fmp4-vp9-aac-lc','separate-webm-fmp4-av1-aac-lc','separate-webm-fmp4-vp9-opus','separate-webm-fmp4-av1-opus','separate-webm-vp8-opus','separate-webm-vp8-vorbis','separate-webm-vp9-vorbis','separate-webm-av1-vorbis','separate-fmp4-webm-h264-vorbis','separate-fmp4-webm-hevc-vorbis','separate-fmp4-webm-av1-vorbis','separate-fmp4-webm-vp9-vorbis','separate-webm-fmp4-vp8-aac-lc','separate-webm-fmp4-vp8-opus'],mixedContainers=True,trackCount=2,trackCountMin=2,trackCountMax=13,mediaTrackCountMax=9,audioTrackLimit=8,multiAudio=True,audioTracksRequired=True,subtitles=dict(containers=['srt'],codecs=['srt'],encoding='utf-8',trackLimit=4,completeFileRequired=True,lateCompletion=True,optionalErrorIsolation=True,retainsValidatedCopyOnSourceError=True,sizeLimitBytes=524288),vorbisChannels=[1,2],vorbisSampleRate=48000,vorbisFloorTypes=[1],vorbisCodebookEntryLimit=65536,vorbisSeekPrerollMilliseconds=200,containers=['fmp4','webm'],webm=dict(videoCodecs=['vp8','vp9','av1'],audioCodecs=['opus','vorbis'],knownClusterSizesRequired=True,lacing=False,defaultVideoDurationRequired=True,clusterLimitBytes=16777216),audioCodecs=['aac-lc','opus','vorbis'],opusChannels=[1,2],opusChannelMappingFamilies=[0],opusSampleRate=48000,opusPrerollMilliseconds=80,opusEditListRequired=True,videoCodecs=['h264','hevc','av1','vp9','vp8'],vp9Profiles=[0,2],vp9BitDepths=[8,10],vp9SampleEntries=['vp09'],av1Profiles=['main'],av1BitDepths=[8,10],av1SampleEntries=['av01'],av1SequenceHeaderInConfigurationRequired=True,hevcProfiles=['main','main10'],hevcBitDepths=[8,10],hevcSampleEntries=['hvc1','hev1'],metadataApplied=False,live=False,muxed=False,segments=False),
        qtMultiAudioSeek=dict(maxTargetSecondsWhileIncomplete=922337203686,completeSourceByteSeek=True,confirmedRangesOnly=True,mode='confirmed-byte-seek'),
        mediaPreparation=dict(offUi=['probe','start','update','complete'],maxConcurrent=1,responseDeadlineMilliseconds=5000,updatesOffUi=True,cancellableDiskIo=False),
        qtEngineRetirement=dict(maxEngines=16,workerLimit=2,nativeDestroyOffUi=True,softwareRendererFreeOffUi=True,openGlRendererFreeOffUi=False,callbackDataRetained=True,processOwned=True),
        classicGraphShutdown=dict(owner="classic-sta",abortOpenWaitPumpsSentMessages=True,closeWaitPumpsSentMessages=True,nativeTeardownDetached=False,fullInputPump=False,progressiveStaCloseAsync=True,deferredStart=True,deferredExit=True,maxPendingStarts=1,uiOwnedGraphCloseAsync=False),
        legacyRefresh=dict(fileIoOutsideMetadataLock=True,snapshotIndexCommitAtomic=True,notificationsMetadataOnly=True),
        legacyPreparation=dict(offUi=True,maxConcurrent=1,responseDeadlineMilliseconds=5000,cancellableDiskIo=False,previousPlaybackUntilCommit=True),
        readerRegistryOffUi=True,
        sourceRetirement=dict(maxSources=32,workerLimit=2,processOwned=True,filesClosedBeforeRelease=True),
        sourceCommit=version, sourceDirty=dirty,
        files={name: hashlib.sha256(data).hexdigest() for name, data in sorted(content.items())})
    content['sdk-manifest.json'] = (json.dumps(manifest, indent=2)+'\n').encode()
    with zipfile.ZipFile(args.output, 'x', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(content.items()):
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    print(json.dumps(dict(path=str(args.output), sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
                         sourceCommit=version, sourceDirty=dirty)))

if __name__ == '__main__':
    main()
