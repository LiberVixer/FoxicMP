#!/usr/bin/env python3
"""Build a reproducible player-only integration SDK ZIP, without media/binaries."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    'ProgressiveContinuousAacPackages.md': 'docs/ProgressiveContinuousAacPackages.md',
    'ProgressiveContinuousAacPackagesValidation-2026-11-06.json': 'docs/ProgressiveContinuousAacPackagesValidation-2026-11-06.json',
    'ProgressiveContinuousAacApi.md': 'docs/ProgressiveContinuousAacApi.md',
    'ProgressiveContinuousAacApiValidation-2026-11-06.json': 'docs/ProgressiveContinuousAacApiValidation-2026-11-06.json',
    'ProgressiveReorderedSdk.md': 'docs/ProgressiveReorderedSdk.md',
    'ProgressiveReorderedSdkValidation-2026-11-05.json': 'docs/ProgressiveReorderedSdkValidation-2026-11-05.json',
    'progressive_continuous.py': 'qt/tools/progressive_continuous.py',
    'ProgressiveContinuousRemux.md': 'docs/ProgressiveContinuousRemux.md',
    'ProgressiveContinuousSdk.md': 'docs/ProgressiveContinuousSdk.md',
    'ProgressiveContinuousSdkValidation-2026-11-04.json': 'docs/ProgressiveContinuousSdkValidation-2026-11-04.json',
    'verify_progressive_sdk.py': 'tools/verify_progressive_sdk.py',
    **{'progressive-v2-rf64-sparse-pcm'+str(bits)+'.json':'tests/fixtures/progressive-v2/rf64-sparse-pcm'+str(bits)+'.json' for bits in (16,24)},
    **{name:'docs/'+name for name in ('ProgressiveRf64Sparse.md','ProgressiveRf64SparseValidation-2026-11-03.json')},
    **{name:'docs/'+name for name in ('ProgressiveRf64RangeMap.md','ProgressiveRf64RangeMapValidation-2026-11-03.json')},
    **{name:'docs/'+name for name in ('ProgressiveRf64Ranges.md','ProgressiveRf64RangesValidation-2026-11-03.json')},
    **{name:'docs/'+name for name in ('ProgressiveExternalIntegrity.md','ProgressiveLargeRf64.md','ProgressiveLargeRf64Validation-2026-11-03.json')},
    'progressive-v2-rf64-external-index.json':'tests/fixtures/progressive-v2/rf64-external-index.json',
    'integrity-empty.idx':'tests/fixtures/progressive-v2/integrity-empty.idx',
    **{name:'docs/'+name for name in ('ProgressiveRf64.md','ProgressiveRf64Validation-2026-11-02.json')},
    **{'progressive-v2-rf64-pcm'+str(bits)+'.json':'tests/fixtures/progressive-v2/rf64-pcm'+str(bits)+'.json' for bits in (16,24)},
    **{name:'docs/'+name for name in ('ProgressiveWavExtensible24.md','ProgressiveWavExtensible24Validation-2026-11-02.json')},
    'progressive-v2-wav-extensible-pcm24.json':'tests/fixtures/progressive-v2/wav-extensible-pcm24.json',
    **{name:'docs/'+name for name in ('ProgressiveWav24.md','ProgressiveWav24Validation-2026-11-02.json')},
    'progressive-v2-wav-pcm24.json':'tests/fixtures/progressive-v2/wav-pcm24.json',
    **{name:'docs/'+name for name in ('ProgressiveEac3.md','ProgressiveEac3Validation-2026-11-02.json')},
    'progressive-v2-eac3.json':'tests/fixtures/progressive-v2/eac3.json',
    **{name:'docs/'+name for name in ('ProgressiveAc3.md','ProgressiveAc3Validation-2026-11-02.json')},
    'progressive-v2-ac3.json':'tests/fixtures/progressive-v2/ac3.json',
    **{name:'docs/'+name for name in ('ProgressiveOggVorbis.md','ProgressiveOggVorbisValidation-2026-11-02.json')},
    'progressive-v2-ogg-vorbis.json':'tests/fixtures/progressive-v2/ogg-vorbis.json',
    **{name:'docs/'+name for name in ('ProgressiveOggOpus.md','ProgressiveOggOpusValidation-2026-11-02.json')},
    'progressive-v2-ogg-opus.json':'tests/fixtures/progressive-v2/ogg-opus.json',
    **{name:'docs/'+name for name in ('ProgressiveWav.md','ProgressiveWavValidation-2026-11-02.json')},
    'progressive-v2-wav-pcm16.json':'tests/fixtures/progressive-v2/wav-pcm16.json',
    **{name:'docs/'+name for name in ('ProgressiveClassicMp3Gapless.md','ProgressiveClassicMp3GaplessValidation-2026-11-02.json')},
    **{name:'docs/'+name for name in ('ProgressiveClassicPcmGraph.md','ProgressiveClassicPcmGraphValidation-2026-11-02.json')},
    **{name:'docs/'+name for name in ('ProgressiveClassicPcmWindow.md','ProgressiveClassicPcmWindowValidation-2026-11-02.json')},
    **{name:'docs/'+name for name in ('ProgressiveMp3Gapless.md','ProgressiveMp3GaplessValidation-2026-11-02.md','ProgressiveMp3GaplessValidation-2026-11-02.json')},
    'progressive-v2-mp3-gapless.json':'tests/fixtures/progressive-v2/mp3-gapless.json',
    **{name: 'qt/tools/' + name for name in (
        'progressive_client.py', 'progressive_windows.py', 'progressive_sdk.py', 'progressive_writer.py',
        'progressive_schema.py', 'progressive_resources.py', 'progressive_v2.py', 'progressive_integrity.py')},
    **{name:'docs/'+name for name in ('ProgressiveRemuxStage.md','ProgressiveRemuxStageValidation-2026-10-31.md','ProgressiveRemuxStageValidation-2026-10-31.json')},
    **{name:'docs/'+name for name in ('ProgressiveBackendDemux.md','ProgressiveBackendDemuxValidation-2026-10-31.md','ProgressiveBackendDemuxValidation-2026-10-31.json')},
    **{name:'docs/'+name for name in ('ProgressiveMp3Tag.md','ProgressiveMp3TagValidation-2026-11-01.md','ProgressiveMp3TagValidation-2026-11-01.json','ProgressiveMp3GaplessReference.md','ProgressiveMp3GaplessReferenceValidation-2026-11-01.json','ProgressiveQtPcmWindow.md','ProgressiveQtPcmWindowValidation-2026-11-01.json')},
    **{name:'docs/'+name for name in ('ProgressiveMp3Indexed.md','ProgressiveMp3IndexedValidation-2026-11-01.md','ProgressiveMp3IndexedValidation-2026-11-01.json')},
    'progressive-v2-mp3-indexed.json':'tests/fixtures/progressive-v2/mp3-indexed.json',
    **{name:'docs/'+name for name in ('ProgressiveMp3.md','ProgressiveMp3Validation-2026-11-01.md','ProgressiveMp3Validation-2026-11-01.json')},
    'progressive-v2-mp3.json':'tests/fixtures/progressive-v2/mp3.json',
    **{name:'docs/'+name for name in ('ProgressiveAdts.md','ProgressiveAdtsValidation-2026-11-01.md','ProgressiveAdtsValidation-2026-11-01.json')},
    'progressive-v2-adts.json':'tests/fixtures/progressive-v2/adts.json',
    **{name:'docs/'+name for name in ('ProgressiveRawFlac.md','ProgressiveRawFlacValidation-2026-11-01.md','ProgressiveRawFlacValidation-2026-11-01.json')},
    'progressive-v2-raw-flac.json':'tests/fixtures/progressive-v2/raw-flac.json',
    **{name:'docs/'+name for name in ('ProgressiveStandaloneFlac.md','ProgressiveStandaloneFlacValidation-2026-11-01.md','ProgressiveStandaloneFlacValidation-2026-11-01.json')},
    'progressive-v2-standalone-flac.json':'tests/fixtures/progressive-v2/standalone-flac.json',
    **{name:'docs/'+name for name in ('ProgressiveStandaloneFmp4Video.md','ProgressiveStandaloneFmp4VideoValidation-2026-11-01.md','ProgressiveStandaloneFmp4VideoValidation-2026-11-01.json')},
    **{'progressive-v2-standalone-fmp4-'+codec+'.json':'tests/fixtures/progressive-v2/standalone-fmp4-'+codec+'.json' for codec in ('hevc','av1','vp9')},
    **{name:'docs/'+name for name in ('ProgressiveStandaloneWebmVideo.md','ProgressiveStandaloneWebmVideoValidation-2026-11-01.md','ProgressiveStandaloneWebmVideoValidation-2026-11-01.json')},
    **{'progressive-v2-standalone-webm-'+codec+'.json':'tests/fixtures/progressive-v2/standalone-webm-'+codec+'.json' for codec in ('vp8','vp9','av1')},
    **{name:'docs/'+name for name in ('ProgressiveStandaloneWebm.md','ProgressiveStandaloneWebmValidation-2026-11-01.md','ProgressiveStandaloneWebmValidation-2026-11-01.json')},
    **{'progressive-v2-standalone-webm-'+codec+'.json':'tests/fixtures/progressive-v2/standalone-webm-'+codec+'.json' for codec in ('opus','vorbis')},
    **{name:'docs/'+name for name in ('ProgressiveStandaloneOpus.md','ProgressiveStandaloneOpusValidation-2026-11-01.md','ProgressiveStandaloneOpusValidation-2026-11-01.json')},
    'progressive-v2-standalone-opus.json':'tests/fixtures/progressive-v2/standalone-opus.json',
    **{name:'docs/'+name for name in ('ProgressiveStandalone.md','ProgressiveStandaloneValidation-2026-11-01.md','ProgressiveStandaloneValidation-2026-11-01.json')},
    **{'progressive-v2-standalone-'+kind+'.json':'tests/fixtures/progressive-v2/standalone-'+kind+'.json' for kind in ('audio','video')},
    **{name:'docs/'+name for name in ('ProgressiveCompletedSeek.md','ProgressiveCompletedSeekValidation-2026-11-01.md','ProgressiveCompletedSeekValidation-2026-11-01.json')},
    'progressive-v2-tail-mp4-vod.json': 'tests/fixtures/progressive-v2/tail-mp4-vod.json',
    **{name:'docs/'+name for name in ('ProgressiveTailMp4.md','ProgressiveTailMp4Validation-2026-11-01.md','ProgressiveTailMp4Validation-2026-11-01.json')},
    'progressive-v2-indexed-mp4-vod.json': 'tests/fixtures/progressive-v2/indexed-mp4-vod.json',
    **{name:'docs/'+name for name in ('ProgressiveIndexedMp4.md','ProgressiveIndexedMp4Validation-2026-11-01.md','ProgressiveIndexedMp4Validation-2026-11-01.json')},
    'progressive-v2-vod.json': 'tests/fixtures/progressive-v2/vod.json',
    'progressive-v2-growing-ts-vod.json': 'tests/fixtures/progressive-v2/growing-ts-vod.json',
    'progressive-v2-muxed-fmp4-opus-vod.json': 'tests/fixtures/progressive-v2/muxed-fmp4-opus-vod.json',
    **{name:'docs/'+name for name in ('ProgressiveMuxedFmp4Opus.md','ProgressiveMuxedFmp4OpusValidation-2026-10-31.md','ProgressiveMuxedFmp4OpusValidation-2026-10-31.json')},
    'progressive-v2-muxed-fmp4-vp9-vod.json': 'tests/fixtures/progressive-v2/muxed-fmp4-vp9-vod.json',
    **{name:'docs/'+name for name in ('ProgressiveMuxedFmp4Vp9.md','ProgressiveMuxedFmp4Vp9Validation-2026-10-31.md','ProgressiveMuxedFmp4Vp9Validation-2026-10-31.json')},
    'progressive-v2-muxed-fmp4-av1-vod.json': 'tests/fixtures/progressive-v2/muxed-fmp4-av1-vod.json',
    **{name:'docs/'+name for name in ('ProgressiveMuxedFmp4Av1.md','ProgressiveMuxedFmp4Av1Validation-2026-10-31.md','ProgressiveMuxedFmp4Av1Validation-2026-10-31.json')},
    'progressive-v2-muxed-fmp4-hevc-vod.json': 'tests/fixtures/progressive-v2/muxed-fmp4-hevc-vod.json',
    **{name:'docs/'+name for name in ('FoxicMPFamilyPlan.md','FoxicMPFamilyMatrix.md','ProgressiveCompletionChecklist.md','ProgressiveMuxedFmp4Hevc.md','ProgressiveMuxedFmp4HevcValidation-2026-10-31.md','ProgressiveMuxedFmp4HevcValidation-2026-10-31.json')},
    'progressive-v2-muxed-webm-vorbis-vod.json': 'tests/fixtures/progressive-v2/muxed-webm-vorbis-vod.json',
    **{name:'docs/'+name for name in ('ProgressiveMuxedWebmVorbis.md','ProgressiveMuxedWebmVorbisValidation-2026-10-31.md','ProgressiveMuxedWebmVorbisValidation-2026-10-31.json')},
    'progressive-v2-muxed-webm-vp8-vod.json': 'tests/fixtures/progressive-v2/muxed-webm-vp8-vod.json',
    **{name:'docs/'+name for name in ('ProgressiveMuxedWebmVp8.md','ProgressiveMuxedWebmVp8Validation-2026-10-31.md','ProgressiveMuxedWebmVp8Validation-2026-10-31.json')},
    'progressive-v2-muxed-webm-av1-vod.json': 'tests/fixtures/progressive-v2/muxed-webm-av1-vod.json',
    **{name:'docs/'+name for name in ('ProgressiveMuxedWebmAv1.md','ProgressiveMuxedWebmAv1Validation-2026-10-31.md','ProgressiveMuxedWebmAv1Validation-2026-10-31.json')},
    'progressive-v2-muxed-webm-vod.json': 'tests/fixtures/progressive-v2/muxed-webm-vod.json',
    **{name:'docs/'+name for name in ('ProgressiveMuxedWebm.md','ProgressiveMuxedWebmValidation-2026-10-31.md','ProgressiveMuxedWebmValidation-2026-10-31.json')},
    'progressive-v2-muxed-fmp4-vod.json': 'tests/fixtures/progressive-v2/muxed-fmp4-vod.json',
    **{name:'docs/'+name for name in ('ProgressiveMuxedFmp4.md','ProgressiveMuxedFmp4Validation-2026-10-31.md','ProgressiveMuxedFmp4Validation-2026-10-31.json')},
    **{name:'docs/'+name for name in ('ProgressiveGrowingTs.md','ProgressiveGrowingTsValidation-2026-10-31.md','ProgressiveGrowingTsValidation-2026-10-31.json')},
    **{name:'docs/'+name for name in ('ProgressiveWebmSegments.md','ProgressiveWebmSegmentsValidation-2026-10-30.md','ProgressiveWebmSegmentsValidation-2026-10-30.json')},
    **{name:'docs/'+name for name in ('ProgressiveTsPlayback.md','ProgressiveTsPlaybackValidation-2026-10-30.md','ProgressiveTsPlaybackValidation-2026-10-30.json','ProgressiveTsTransport.md','ProgressiveTsAccessIndex.md','ProgressiveTsResourceStream.md','ProgressiveTsTransportValidation-2026-10-30.md','ProgressiveTsTransportValidation-2026-10-30.json','ProgressiveTsAccessIndexValidation-2026-10-30.md','ProgressiveTsAccessIndexValidation-2026-10-30.json','ProgressiveTsResourceStreamValidation-2026-10-30.md','ProgressiveTsResourceStreamValidation-2026-10-30.json')},
    **{name:'docs/'+name for name in ('ProgressiveMuxedTsPlayback.md','ProgressiveMuxedTsValidation-2026-10-31.md','ProgressiveMuxedTsValidation-2026-10-31.json')},
    'progressive_preview.py': 'qt/tools/progressive_preview.py',
    **{name:'docs/'+name for name in ('ProgressivePreparedPreview.md','ProgressivePreparedPreviewValidation-2026-10-31.md','ProgressivePreparedPreviewValidation-2026-10-31.json')},
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
        'ProgressiveOptionalSrt.md','ProgressiveOptionalSrtValidation-2026-10-29.md','ProgressiveOptionalSrtValidation-2026-10-29.json',
        'ProgressiveOptionalAudio.md','ProgressiveOptionalAudioValidation-2026-10-29.md','ProgressiveMetadata.md','ProgressiveMetadataValidation-2026-10-29.md','ProgressiveMetadataUiValidation-2026-10-29.md','ProgressiveMetadataUiValidation-2026-10-29.json','ProgressivePartialAudio.md','ProgressivePartialAudioValidation-2026-10-29.md','ProgressivePartialAudioValidation-2026-10-29.json','ProgressiveIntegrity.md','ProgressiveIntegrityValidation-2026-10-30.md','ProgressiveSegments.md','ProgressiveSegmentsValidation-2026-10-30.md','ProgressiveSegmentStaging.md','ProgressiveSegmentStagingValidation-2026-10-30.md','ProgressiveSegmentStagingValidation-2026-10-30.json','ProgressiveSegmentPlayback.md','ProgressiveSegmentPlaybackValidation-2026-10-30.md','ProgressiveSegmentPlaybackValidation-2026-10-30.json','ProgressiveSegmentHevc.md','ProgressiveSegmentHevcValidation-2026-10-30.md','ProgressiveSegmentHevcValidation-2026-10-30.json','ProgressiveSegmentAv1.md','ProgressiveSegmentAv1Validation-2026-10-30.md','ProgressiveSegmentAv1Validation-2026-10-30.json','ProgressiveSegmentVp9Opus.md','ProgressiveSegmentVp9OpusValidation-2026-10-30.md','ProgressiveSegmentVp9OpusValidation-2026-10-30.json','ProgressiveSegmentCatalog.md','ProgressiveSegmentCatalogValidation-2026-10-30.md','ProgressiveSegmentCatalogValidation-2026-10-30.json','ProgressiveLocalCatalog.md','ProgressiveLocalCatalogValidation-2026-10-30.md','ProgressiveLocalCatalogValidation-2026-10-30.json','ProgressiveCatalogCache.md','ProgressiveCatalogCacheValidation-2026-10-30.md','ProgressiveCatalogCacheValidation-2026-10-30.json','ProgressiveCatalogPlayback.md','ProgressiveCatalogPlaybackValidation-2026-10-30.md','ProgressiveCatalogPlaybackValidation-2026-10-30.json','ProgressiveActiveAudioErrors.md','ProgressiveActiveAudioValidation-2026-10-29.md','ProgressiveActiveAudioValidation-2026-10-29.json')},
}

def schema2_playback():
    # Keep the standalone manifest equal to the capability compiled into all
    # three editions, including nested codec/catalog limits.
    source=(ROOT/'shared/progressivefmp4bridge.h').read_text(encoding='utf-8')
    match=re.search(r'inline const char\* Fmp4V2Capabilities\(\).*?return (.*?);',source,re.S)
    if not match:raise ValueError('Cannot locate the shared playback capability')
    expression=match.group(1)
    parts=re.findall(r'R"\((.*?)\)"',expression,re.S)
    if not parts or re.sub(r'R"\((.*?)\)"','',expression,flags=re.S).strip():
        raise ValueError('Expected only adjacent raw capability literals')
    return json.loads(''.join(parts))

def qt_schema2_playback():
    result=schema2_playback()
    source=(ROOT/'shared/progressiveqtplayback.h').read_text(encoding='utf-8')
    match=re.search(r'QtMp3GaplessCapability\(\).*?return R"\((.*?)\)";',source,re.S)
    if not match:raise ValueError('Cannot locate the Qt gapless capability')
    result['standalone']['mp3Gapless']=json.loads(match.group(1))
    result['profiles'].append('standalone-mp3-gapless')
    result['containers'].append('mp3-gapless')
    return result
def classic_schema2_playback():
    result=schema2_playback()
    source=(ROOT/'shared/progressiveclassicplayback.h').read_text(encoding='utf-8')
    match=re.search(r'ClassicMp3GaplessCapability\(\).*?return R"\((.*?)\)";',source,re.S)
    if not match:raise ValueError('Cannot locate the Classic gapless capability')
    result['standalone']['mp3Gapless']=json.loads(match.group(1))
    result['profiles'].append('standalone-mp3-gapless')
    result['containers'].append('mp3-gapless')
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    content = {name: (ROOT / source).read_bytes() for name, source in FILES.items()}
    version = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT))
    manifest = dict(schemaVersion=1, protocol='progressive-manifest-v1', sdkVersion='1.84', readerLeaseProtocol=1,
        resourceIoProbeVersion=1, segmentProbeVersion=1, resourceIoSchemaVersions=[2], resourceIoPlaybackSupported=False,
        playbackSchemaVersions=[1,2], validationSchemaVersions=[2],
        schema2Playback=schema2_playback(), qtSchema2Playback=qt_schema2_playback(), classicSchema2Playback=classic_schema2_playback(),
        completedSourceSeek=json.loads(re.search(r'CompletedSeekCapabilities\(\).*?return R"\((.*?)\)";', (ROOT/'shared/progressivecompletedseek.h').read_text(),re.S).group(1)),
        preparedRemuxPreview=json.loads(re.search(r'PreparedPreviewCapabilities\(\).*?return R"\((.*?)\)";', (ROOT/'shared/preparedclip.h').read_text(),re.S).group(1)),
        continuousRemux=json.loads(re.search(r'ContinuousRemuxCapabilities\(\).*?return R"\((.*?)\)";', (ROOT/'shared/progressivestreamcapabilities.h').read_text(),re.S).group(1)),
        backendRemuxProbe=json.loads(re.search(r'BackendRemuxCapabilities\(\).*?return R"\((.*?)\)";', (ROOT/'shared/progressivebackendprobe.h').read_text(),re.S).group(1)),
        backendDemuxProbe=json.loads(re.search(r'BackendDemuxCapabilities\(\).*?return R"\((.*?)\)";', (ROOT/'shared/progressivebackendprobe.h').read_text(),re.S).group(1)),
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
