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
        self.segments_supported=caps.get('schema2Playback',{}).get('segmentedPlayback',{}).get('mode')=='segmented-fmp4-h264-aac-lc'
        self.segment_profiles=set(caps.get('schema2Playback',{}).get('segmentedPlayback',{}).get('profiles',[]))
        self.catalog_supported=caps.get('schema2Playback',{}).get('segmentedPlayback',{}).get('catalog',{}).get('mode')=='paged-fmp4-v1'
        webm=caps.get('schema2Playback',{}).get('segmentedPlayback',{}).get('webm',{})
        self.webm_segments_supported=webm.get('mode')=='segmented-webm-vp9-opus'
        self.webm_segment_profiles=set(webm.get('profiles',[]))
        self.webm_catalog_supported=webm.get('catalog',{}).get('mode')=='paged-webm-v1'
        ts=caps.get('schema2Playback',{}).get('segmentedPlayback',{}).get('mpegts',{})
        self.ts_segments_supported=ts.get('mode')=='segmented-mpegts-h264-aac-lc'
        self.ts_catalog_supported=ts.get('catalog',{}).get('mode')=='paged-mpegts-v1'
        mux=ts.get('muxed',{})
        self.mux_ts_supported=mux.get('mode')=='segmented-muxed-mpegts-h264-aac-lc'
        self.mux_ts_catalog_supported=mux.get('catalog',{}).get('mode')=='paged-muxed-mpegts-v1'
        self.growing_ts_supported=ts.get('growing',{}).get('mode')=='growing-muxed-mpegts-h264-aac-lc'
        self.muxed_mp4_supported=caps.get('schema2Playback',{}).get('muxedPlayback',{}).get('fmp4',{}).get('mode')=='growing-muxed-fmp4-h264-aac-lc'
        self.muxed_webm_supported=caps.get('schema2Playback',{}).get('muxedPlayback',{}).get('webm',{}).get('mode')=='growing-muxed-webm-vp9-opus'
        self.muxed_webm_av1_supported=caps.get('schema2Playback',{}).get('muxedPlayback',{}).get('webm',{}).get('av1',{}).get('mode')=='growing-muxed-webm-av1-opus'
        self.muxed_webm_vp8_supported=caps.get('schema2Playback',{}).get('muxedPlayback',{}).get('webm',{}).get('vp8',{}).get('mode')=='growing-muxed-webm-vp8-opus'
        vorbis=caps.get('schema2Playback',{}).get('muxedPlayback',{}).get('webm',{}).get('vorbis',{})
        self.muxed_vorbis_modes=set(vorbis.get('profiles',[])) if vorbis.get('mode')=='growing-muxed-webm-vorbis' else set()
        self.integrity_supported=caps.get('schema2Playback',{}).get('integrity',{}).get('algorithm')=='sha256'
        if 2 not in caps.get('playbackSchemaVersions',[]) or caps.get('schema2Playback',{}).get('mode')!='separate-fmp4-h264-aac-lc' or caps.get('readerLeaseProtocol')!=1:
            raise RuntimeError('Player lacks the bounded schema-2 media profile; update the player')

    def _command(self,action,**fields):
        return self.client.command(action,self.last['sessionId'],readerId=self.reader_id,**fields)

    def publish(self,snapshot: dict):
        with self.lock:
            if self.closed:raise RuntimeError('Preview was detached; create a new V2PreviewSession')
            current=copy.deepcopy(snapshot);counter(current['generation']);revision=counter(current['revision'])
            segment_tracks=[t for t in current.get('tracks',[]) if 'segments' in t.get('source',{})]
            webm_segments=any(t.get('containerHint')=='webm' for t in segment_tracks)
            ts_segments=any(t.get('containerHint')=='mpegts' for t in segment_tracks)
            mux_ts_segments=any(t.get('containerHint')=='mpegts-muxed' for t in segment_tracks)
            growing_ts=any(t.get('containerHint')=='mpegts-growing' for t in current.get('tracks',[]))
            muxed_mp4=any(t.get('containerHint')=='fmp4-muxed' for t in current.get('tracks',[]))
            muxed_webm=any(t.get('containerHint')=='webm-muxed' for t in current.get('tracks',[]))
            if muxed_mp4 or muxed_webm:
                tracks=current.get('tracks',[]);resources=current.get('resources',[])
                label='muxed WebM' if muxed_webm else 'muxed fMP4'
                hint='webm-muxed' if muxed_webm else 'fmp4-muxed'
                video=next((t.get('codecHint') for t in tracks if t.get('kind')=='video'),None) or ('vp9' if muxed_webm else 'h264')
                audio=next((t.get('codecHint') for t in tracks if t.get('kind')=='audio'),None) or ('opus' if muxed_webm else 'aac-lc')
                vorbis=muxed_webm and audio=='vorbis'
                if vorbis and (not any(t.get('kind')=='video' and t.get('codecHint')==video for t in tracks) or 'growing-muxed-webm-'+video+'-vorbis' not in self.muxed_vorbis_modes):
                    raise RuntimeError('Player lacks this shared WebM Vorbis pair or its explicit video hint; update the player')
                if not vorbis and audio!=('opus' if muxed_webm else 'aac-lc'):
                    raise RuntimeError('Unsupported muxed audio codec')
                if muxed_webm and not vorbis and video=='vp8' and not self.muxed_webm_vp8_supported:
                    raise RuntimeError('Player lacks muxed WebM VP8/Opus playback; update the player')
                if muxed_webm and not vorbis and video=='av1' and not self.muxed_webm_av1_supported:
                    raise RuntimeError('Player lacks muxed WebM AV1/Opus playback; update the player')
                if muxed_mp4 and video!='h264':
                    raise RuntimeError('Unsupported muxed fMP4 video codec')
                if muxed_webm and video not in ('vp8','vp9','av1'):
                    raise RuntimeError('Unsupported muxed WebM video codec')
                if not vorbis and not (self.muxed_webm_vp8_supported if muxed_webm and not vorbis and video=='vp8' else self.muxed_webm_av1_supported if muxed_webm and not vorbis and video=='av1' else self.muxed_webm_supported if muxed_webm else self.muxed_mp4_supported):
                    raise RuntimeError('Player lacks '+label+' playback; update the player')
                if len(tracks)!=2 or {t.get('kind') for t in tracks}!={'video','audio'} or len(resources)!=1 or 'segmentCatalog' in current:
                    raise RuntimeError(label+' requires two logical views and one scalar resource')
                if any(t.get('containerHint')!=hint or not t.get('required') or 'initializationResourceId' in t or t.get('source')!={'resourceId':resources[0].get('id')} or t.get('codecHint') not in (None,'',video if t.get('kind')=='video' else audio) for t in tracks):
                    raise RuntimeError(label+' requires shared scalar codec views without external initialization')
                if resources[0].get('integrity',{}).get('algorithm')!='sha256':
                    raise RuntimeError(label+' playback requires SHA-256')
            elif growing_ts:
                tracks=current.get('tracks',[]);resources=current.get('resources',[])
                if not self.growing_ts_supported:
                    raise RuntimeError('Player lacks growing MPEG-TS AVC/AAC playback; update the player')
                if len(tracks)!=2 or {t.get('kind') for t in tracks}!={'video','audio'} or len(resources)!=1 or 'segmentCatalog' in current:
                    raise RuntimeError('Growing MPEG-TS requires two logical views and one scalar resource')
                if any(t.get('containerHint')!='mpegts-growing' or not t.get('required') or 'initializationResourceId' in t or t.get('source')!={'resourceId':resources[0].get('id')} or t.get('codecHint') not in (None,'','h264' if t.get('kind')=='video' else 'aac-lc') for t in tracks):
                    raise RuntimeError('Growing MPEG-TS requires shared scalar AVC/AAC views without external initialization')
                if resources[0].get('integrity',{}).get('algorithm')!='sha256':
                    raise RuntimeError('Growing MPEG-TS playback requires SHA-256')
            elif mux_ts_segments:
                if len(segment_tracks)!=2 or any(t.get('containerHint')!='mpegts-muxed' for t in segment_tracks):
                    raise RuntimeError('Muxed MPEG-TS requires two logical TS views over shared resources')
                if not self.mux_ts_supported or any(t.get('codecHint') not in (None,'','h264' if t.get('kind')=='video' else 'aac-lc') for t in segment_tracks):
                    raise RuntimeError('Player lacks the muxed MPEG-TS AVC/AAC playback profile; update the player')
                if 'segmentCatalog' in current and not self.mux_ts_catalog_supported:
                    raise RuntimeError('Player lacks the paged muxed MPEG-TS playback profile; update the player')
                if not all('integrity' in r for r in current.get('resources',[])):
                    raise RuntimeError('Muxed MPEG-TS playback requires SHA-256 for every resource')
            elif ts_segments:
                if len(segment_tracks)!=2 or any(t.get('containerHint')!='mpegts' for t in segment_tracks):
                    raise RuntimeError('Segmented MPEG-TS requires separate TS video and audio tracks')
                if not self.ts_segments_supported or any(t.get('codecHint') not in (None,'','h264' if t.get('kind')=='video' else 'aac-lc') for t in segment_tracks):
                    raise RuntimeError('Player lacks the segmented MPEG-TS AVC/AAC playback profile; update the player')
                if 'segmentCatalog' in current and not self.ts_catalog_supported:
                    raise RuntimeError('Player lacks the paged MPEG-TS playback profile; update the player')
                if not all('integrity' in r for r in current.get('resources',[])):
                    raise RuntimeError('MPEG-TS playback requires SHA-256 for every resource')
            elif webm_segments:
                if len(segment_tracks)!=2 or any(t.get('containerHint')!='webm' for t in segment_tracks):
                    raise RuntimeError('Segmented WebM requires separate WebM video and audio tracks')
                video=next((t.get('codecHint') for t in segment_tracks if t.get('kind')=='video'),None)
                audio=next((t.get('codecHint') for t in segment_tracks if t.get('kind')=='audio'),None)
                if not self.webm_segments_supported or audio not in (None,'','opus') or (video and 'segmented-webm-'+video+'-opus' not in self.webm_segment_profiles):
                    raise RuntimeError('Player lacks the requested segmented WebM/Opus playback profile; update the player')
                if 'segmentCatalog' in current and not self.webm_catalog_supported:
                    raise RuntimeError('Player lacks the paged WebM catalog playback profile; update the player')
            else:
                if 'segmentCatalog' in current and not self.catalog_supported:
                    raise RuntimeError('Player lacks the paged fMP4 catalog playback profile; update the player')
                if any('segments' in t.get('source',{}) for t in current.get('tracks',[])) and not self.segments_supported:
                    raise RuntimeError('Player lacks the segmented fMP4 playback profile; update the player')
                opus_requested=any(t.get('kind')=='audio' and t.get('codecHint')=='opus' for t in segment_tracks)
                audio_codec='opus' if opus_requested else 'aac-lc'
                for codec in ('hevc','av1','vp9'):
                    if any(t.get('kind')=='video' and t.get('codecHint')==codec for t in segment_tracks) and 'segmented-fmp4-'+codec+'-'+audio_codec not in self.segment_profiles:
                        raise RuntimeError('Player lacks the segmented '+codec.upper()+'/'+audio_codec+' playback profile; update the player')
                if opus_requested:
                    video=next((t.get('codecHint') for t in segment_tracks if t.get('kind')=='video'),None)
                    profiles=['segmented-fmp4-'+video+'-opus'] if video in ('h264','hevc','av1','vp9') else [p for p in self.segment_profiles if p.endswith('-opus')]
                    if not profiles or not any(p in self.segment_profiles for p in profiles):
                        raise RuntimeError('Player lacks the requested segmented Opus playback profile; update the player')
            if any('integrity' in r for r in current.get('resources',[])) and not self.integrity_supported:
                raise RuntimeError('Player lacks SHA-256 resource integrity; update the player')
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
                    if error.code in {'waiting_initialization','waiting-index'}:
                        self.uncertain=False
                        return dict(version=1,ok=True,active=False,state='waiting-index' if error.code=='waiting-index' else 'waiting-initialization',reason=str(error))
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
