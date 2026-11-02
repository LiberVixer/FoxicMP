"""One real fMP4 stream: no placeholder track or external downloader."""
import json,subprocess,uuid
from fractions import Fraction
from pathlib import Path
from progressive_growing_ts_fixture import publish
from progressive_v1_conformance import fragment_prefix as mp4_prefix
from progressive_webm_fixtures import webm_prefix

BASE_PROFILES=('audio-mono','audio-stereo-44100','video-main','video-vfr')
OPUS_PROFILES=('audio-opus-mono','audio-opus-stereo')
WEBM_PROFILES=tuple('audio-webm-'+codec+'-'+channels for codec in ('opus','vorbis') for channels in ('mono','stereo'))
WEBM_VIDEO_PROFILES=('video-webm-vp8','video-webm-vp8-fractional','video-webm-vp9-8','video-webm-vp9-10-fractional','video-webm-av1-8','video-webm-av1-10-fractional')
FMP4_VIDEO_PROFILES=('video-fmp4-hevc-8-hvc1','video-fmp4-hevc-10-hev1-fractional','video-fmp4-av1-8','video-fmp4-av1-10-fractional','video-fmp4-vp9-8','video-fmp4-vp9-10-fractional')
FLAC_PROFILES=tuple('audio-flac-'+channels+'-'+str(bits)+'-'+str(rate) for rate in (44100,48000) for channels in ('mono','stereo') for bits in (16,24))
RAW_FLAC_PROFILES=tuple('audio-raw-flac-'+c+'-'+str(b)+'-'+str(r) for r in (44100,48000) for c in ('mono','stereo') for b in (16,24))
ADTS_PROFILES=tuple('audio-adts-'+c+'-'+str(r) for r in (44100,48000) for c in ('mono','stereo'))
MP3_PROFILES=tuple('audio-mp3-'+c+'-'+m+'-'+str(r) for r in (44100,48000) for c in ('mono','stereo') for m in ('cbr','vbr'))
MP3_INDEXED_PROFILES=tuple('audio-mp3-indexed-'+c+'-'+k+'-'+str(r) for k in ('xing','info','vbri') for r in (44100,48000) for c in ('mono','stereo'))
GAPLESS_MP3_PROFILES=tuple('audio-mp3-gapless-'+c+'-'+str(r) for r in (44100,48000) for c in ('mono','stereo'))
WAV24_PROFILES=tuple('audio-wav24-'+c+'-'+str(r) for r in (44100,48000) for c in ('mono','stereo'))
WAV_PROFILES=tuple('audio-wav-'+c+'-'+str(r) for r in (44100,48000) for c in ('mono','stereo'))
OGG_OPUS_PROFILES=tuple('audio-ogg-opus-'+c+'-'+str(ms) for ms in (10,20) for c in ('mono','stereo'))
OGG_VORBIS_PROFILES=tuple('audio-ogg-vorbis-'+c+'-'+str(q) for q in (2,5) for c in ('mono','stereo'))
AC3_PROFILES=('audio-ac3-mono-64','audio-ac3-mono-192','audio-ac3-stereo-192','audio-ac3-stereo-384')
EAC3_PROFILES=('audio-eac3-mono-64','audio-eac3-mono-192','audio-eac3-stereo-192','audio-eac3-stereo-384')
PROFILES=WAV24_PROFILES+EAC3_PROFILES+AC3_PROFILES+OGG_VORBIS_PROFILES+OGG_OPUS_PROFILES+WAV_PROFILES+GAPLESS_MP3_PROFILES+MP3_INDEXED_PROFILES+MP3_PROFILES+ADTS_PROFILES+BASE_PROFILES+OPUS_PROFILES+WEBM_PROFILES+WEBM_VIDEO_PROFILES+FMP4_VIDEO_PROFILES+FLAC_PROFILES+RAW_FLAC_PROFILES
def fragment_prefix(raw,count):
    return webm_prefix(raw,count) if raw[:4]==bytes.fromhex('1a45dfa3') else mp4_prefix(raw,count)
def fixture(root,profile='audio-mono',seconds=20):
    if profile in EAC3_PROFILES:
        from progressive_eac3_fixture import fixture as eac3_fixture
        return eac3_fixture(root,profile,seconds)
    if profile in AC3_PROFILES:
        from progressive_ac3_fixture import fixture as ac3_fixture
        return ac3_fixture(root,profile,seconds)
    if profile in OGG_VORBIS_PROFILES:
        from progressive_ogg_vorbis_fixture import fixture as vorbis_fixture
        return vorbis_fixture(root,profile,seconds)
    if profile in OGG_OPUS_PROFILES:
        from progressive_ogg_opus_fixture import fixture as ogg_fixture
        return ogg_fixture(root,profile,seconds)
    if profile in WAV24_PROFILES:
        from progressive_wav24_fixture import fixture as wav24_fixture
        return wav24_fixture(root,profile,seconds)
    if profile in WAV_PROFILES:
        from progressive_wav_fixture import fixture as wav_fixture
        return wav_fixture(root,profile,seconds)
    if profile in GAPLESS_MP3_PROFILES:
        from progressive_mp3_gapless_fixture import fixture as gapless_fixture
        return gapless_fixture(root,profile,seconds)
    if profile in MP3_INDEXED_PROFILES:
        from progressive_mp3_indexed_fixture import fixture as indexed_fixture
        return indexed_fixture(root,profile,seconds)
    if profile in MP3_PROFILES:
        from progressive_mp3_fixture import fixture as mp3_fixture
        return mp3_fixture(root,profile,seconds)
    if profile in ADTS_PROFILES:
        from progressive_adts_fixture import fixture as adts_fixture
        return adts_fixture(root,profile,seconds)
    if profile in RAW_FLAC_PROFILES:
        from progressive_raw_flac_fixture import fixture as raw_fixture
        return raw_fixture(root,profile,seconds)
    if profile in FLAC_PROFILES:
        from progressive_standalone_flac_fixture import fixture as flac_fixture
        return flac_fixture(root,profile,seconds)
    if profile in FMP4_VIDEO_PROFILES:
        from progressive_standalone_fmp4_video_fixture import fixture as video_fixture
        return video_fixture(root,profile,seconds)
    if profile in WEBM_VIDEO_PROFILES:
        from progressive_standalone_webm_video_fixture import fixture as video_fixture
        return video_fixture(root,profile,seconds)
    if profile in WEBM_PROFILES:
        from progressive_standalone_webm_fixture import fixture as webm_fixture
        return webm_fixture(root,profile,seconds)
    root=Path(root);root.mkdir(parents=True,exist_ok=True);audio=profile.startswith('audio');opus='-opus-' in profile;original=root/'original.mp4'
    if audio:
        rate=44100 if '44100' in profile else 48000
        args=['-f','lavfi','-i',f'sine=frequency=880:sample_rate={rate}:duration={seconds}','-vn','-c:a','libopus' if opus else 'aac',*(['-frame_duration','20','-application','audio'] if opus else []),'-ac','2' if 'stereo' in profile else '1','-frag_duration','1000000']
    else:
        args=['-f','lavfi','-i',f'testsrc2=size=160x90:rate=25:duration={seconds}','-an','-c:v','libx264','-threads','2','-preset','veryfast','-profile:v','main','-pix_fmt','yuv420p','-x264-params','keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes=2']
        if profile=='video-vfr':args+=['-vf',"select='not(eq(mod(n,5),1))'",'-fps_mode:v','vfr']
    subprocess.run(['ffmpeg','-v','error','-y',*args,'-movflags',('delay_moov' if opus else 'empty_moov')+'+default_base_moof+frag_keyframe',str(original)],check=True,capture_output=True)
    raw=original.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_format','-show_data_hash','sha256','-of','json',str(original)]));assert len(probe['streams'])==1
    kind='audio' if audio else 'video';duration=max(Fraction(p['pts_time'])+Fraction(p.get('duration_time','0')) for p in probe['packets']);duration=max(duration,Fraction(str(probe['format']['duration'])))
    model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(int(duration*10000000)+1),timestampOffset='0'),resources=[dict(id='media',name='mux.part',length=str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='main',kind=kind,required=True,codecHint=('opus' if opus else 'aac-lc') if audio else 'h264',containerHint='fmp4',initializationReady=True,state='downloading',source=dict(resourceId='media'))])
    if profile in ('audio-mono','audio-opus-mono'):model['resources'][0]['length']=None
    model['metadata']=dict(revision='1',title='Standalone '+profile,chapters=[dict(id='c'+str(t),time=str(t*10000000),title=str(t)) for t in ((0,2,17) if seconds>=18 else (0,2))])
    return model,raw,original,probe
