"""Schema-2 SDK safety: counters, incarnation guards and release receipts."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt'/'tools'))
from progressive_client import ApiError
from progressive_v2 import V2PreviewSession,counter

class Client:
    def __init__(self):self.calls=[];self.released=True
    def request(self,message):return dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,schema2Playback=dict(mode='separate-fmp4-h264-aac-lc'))
    def start(self,value,**options):
        self.calls.append(('start',copy.deepcopy(value),options))
        return dict(ok=True,readerId='c'*32,generation=value['generation'],revision=value['revision'],writerInstanceId=value['writer']['instanceId'])
    def update(self,value,**options):self.calls.append(('update',copy.deepcopy(value),options));return dict(ok=True)
    def command(self,action,session,**fields):self.calls.append((action,session,fields));return dict(ok=True)
    def release_reader(self,reader,timeout):self.calls.append(('release-reader',reader));return dict(released=self.released,readerId=reader)
    def forget_reader(self,reader):self.calls.append(('forget-reader',reader));return dict(forgotten=True)

def snapshot(ready=True):
    return dict(schemaVersion=2,sessionId='a'*32,generation='18446744073709551615',revision='9007199254740993',writer=dict(instanceId='b'*32,state='downloading'),
        tracks=[dict(kind=kind,required=True,initializationReady=ready) for kind in ('video','audio')])

class V2SdkTest(unittest.TestCase):
    def test_growing_ts_requires_its_own_capability_and_shared_scalar_views(self):
        client=Client();value=snapshot();value['resources']=[dict(id='mux',integrity=dict(algorithm='sha256'))]
        for t in value['tracks']:t.update(containerHint='mpegts-growing',source=dict(resourceId='mux'),codecHint='h264' if t['kind']=='video' else 'aac-lc')
        growing={}
        client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',integrity=dict(algorithm='sha256'),segmentedPlayback=dict(mpegts=dict(mode='segmented-mpegts-h264-aac-lc',muxed=dict(mode='segmented-muxed-mpegts-h264-aac-lc'),growing=growing))))
        preview=V2PreviewSession(client,'/unused')
        with self.assertRaisesRegex(RuntimeError,'growing MPEG-TS'):preview.publish(value)
        self.assertFalse(client.calls);self.assertIsNone(preview.last);self.assertFalse(preview.uncertain)
        growing['mode']='growing-muxed-mpegts-h264-aac-lc';V2PreviewSession(client,'/unused').publish(value);client.calls.clear()
        for change in [lambda v:v.update(segmentCatalog=[]),lambda v:v['resources'][0].pop('integrity'),lambda v:v['tracks'][0].update(initializationResourceId='mux'),lambda v:v['tracks'][1].update(containerHint='mpegts-muxed'),lambda v:v['tracks'][0].update(source=dict(segments=[])),lambda v:v['tracks'][1].update(source=dict(resourceId='different')),lambda v:v['tracks'][1].update(codecHint='opus')]:
            bad=copy.deepcopy(value);change(bad);preview=V2PreviewSession(client,'/unused')
            with self.assertRaises(RuntimeError):preview.publish(bad)
            self.assertFalse(client.calls);self.assertIsNone(preview.last);self.assertFalse(preview.uncertain)
    def test_muxed_ts_does_not_reuse_separate_ts_or_fmp4_negotiation(self):
        client=Client();value=snapshot();value['resources']=[dict(id='mux',integrity={})]
        for t in value['tracks']:t.update(containerHint='mpegts-muxed',source=dict(segments=[]),codecHint='h264' if t['kind']=='video' else 'aac-lc')
        mux={}
        client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',integrity=dict(algorithm='sha256'),segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc',mpegts=dict(mode='segmented-mpegts-h264-aac-lc',muxed=mux))))
        preview=V2PreviewSession(client,'/unused')
        with self.assertRaisesRegex(RuntimeError,'muxed MPEG-TS'):preview.publish(value)
        self.assertFalse(client.calls);self.assertIsNone(preview.last);self.assertFalse(preview.uncertain)
        mux.update(mode='segmented-muxed-mpegts-h264-aac-lc');V2PreviewSession(client,'/unused').publish(value);client.calls.clear()
        value['segmentCatalog']=[]
        with self.assertRaisesRegex(RuntimeError,'paged muxed'):V2PreviewSession(client,'/unused').publish(value)
        self.assertFalse(client.calls);mux['catalog']=dict(mode='paged-muxed-mpegts-v1');V2PreviewSession(client,'/unused').publish(value);client.calls.clear()
        for change in [lambda v:v['resources'][0].pop('integrity'),lambda v:v['tracks'][0].update(containerHint='mpegts'),lambda v:v['tracks'][1].update(codecHint='opus')]:
            bad=copy.deepcopy(value);change(bad);preview=V2PreviewSession(client,'/unused')
            with self.assertRaises(RuntimeError):preview.publish(bad)
            self.assertIsNone(preview.last);self.assertFalse(preview.uncertain);self.assertFalse(client.calls)
    def test_mpegts_negotiation_is_separate_and_requires_integrity(self):
        client=Client();value=snapshot();value['resources']=[dict(id='v',integrity={}),dict(id='a',integrity={})]
        for t in value['tracks']:t.update(containerHint='mpegts',source=dict(segments=[]),codecHint='h264' if t['kind']=='video' else 'aac-lc')
        preview=V2PreviewSession(client,'/unused')
        with self.assertRaisesRegex(RuntimeError,'MPEG-TS'):preview.publish(value)
        self.assertFalse(client.calls);self.assertIsNone(preview.last)
        ts=dict(mode='segmented-mpegts-h264-aac-lc')
        client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',integrity=dict(algorithm='sha256'),segmentedPlayback=dict(mpegts=ts)))
        V2PreviewSession(client,'/unused').publish(value);client.calls.clear()
        for change in [lambda v:v['resources'][0].pop('integrity'),lambda v:v['tracks'][0].update(containerHint='webm'),lambda v:v['tracks'][1].update(codecHint='opus')]:
            bad=copy.deepcopy(value);change(bad);preview=V2PreviewSession(client,'/unused')
            with self.assertRaises(RuntimeError):preview.publish(bad)
            self.assertIsNone(preview.last);self.assertFalse(preview.uncertain);self.assertFalse(client.calls)
        value['segmentCatalog']=[]
        with self.assertRaisesRegex(RuntimeError,'paged MPEG-TS'):V2PreviewSession(client,'/unused').publish(value)
        self.assertFalse(client.calls);ts['catalog']=dict(mode='paged-mpegts-v1');V2PreviewSession(client,'/unused').publish(value)
    def test_webm_requires_its_own_codec_and_catalog_capabilities(self):
        client=Client();value=snapshot()
        for t in value['tracks']:t.update(containerHint='webm',source=dict(segments=[]),codecHint='vp9' if t['kind']=='video' else 'opus')
        preview=V2PreviewSession(client,'/unused')
        with self.assertRaisesRegex(RuntimeError,'WebM'):preview.publish(value)
        self.assertFalse(client.calls);self.assertIsNone(preview.last);self.assertFalse(preview.uncertain)
        webm=dict(mode='segmented-webm-vp9-opus',profiles=['segmented-webm-vp9-opus'])
        client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(webm=webm)))
        preview=V2PreviewSession(client,'/unused');preview.publish(value);self.assertTrue(preview.started);client.calls.clear()
        value['segmentCatalog']=[];preview=V2PreviewSession(client,'/unused')
        with self.assertRaisesRegex(RuntimeError,'catalog'):preview.publish(value)
        self.assertFalse(client.calls);self.assertIsNone(preview.last)
        webm['catalog']=dict(mode='paged-webm-v1');V2PreviewSession(client,'/unused').publish(value);client.calls.clear()
        for field,setting in [('codecHint','av1'),('containerHint','fmp4')]:
            bad=copy.deepcopy(value);bad['tracks'][0][field]=setting;preview=V2PreviewSession(client,'/unused')
            with self.assertRaises(RuntimeError):preview.publish(bad)
            self.assertIsNone(preview.last);self.assertFalse(preview.uncertain);self.assertFalse(client.calls)
        bad=copy.deepcopy(value);bad['tracks'][1]['codecHint']='vorbis'
        with self.assertRaisesRegex(RuntimeError,'WebM'):V2PreviewSession(client,'/unused').publish(bad)
        self.assertFalse(client.calls)
    def test_catalog_requires_separate_capability_before_mutating_state(self):
        client=Client();preview=V2PreviewSession(client,'/unused');value=snapshot();value['segmentCatalog']=[]
        with self.assertRaisesRegex(RuntimeError,'catalog'):preview.publish(value)
        self.assertFalse(client.calls);self.assertIsNone(preview.last);self.assertFalse(preview.uncertain)
    def test_catalog_capability_forwards_root_without_reading_pages(self):
        client=Client();client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc',catalog=dict(mode='paged-fmp4-v1'))))
        value=snapshot();value['segmentCatalog']=[dict(id='p',name='does-not-exist.json',length='1',sha256='0'*64)]
        preview=V2PreviewSession(client,'/unused');preview.publish(value)
        self.assertEqual(client.calls[0][1]['segmentCatalog'],value['segmentCatalog']);self.assertTrue(preview.started)
    def test_missing_catalog_page_start_is_known_waiting_not_uncertain_start(self):
        client=Client();client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc',catalog=dict(mode='paged-fmp4-v1'))))
        client.start=lambda *args,**kw:(_ for _ in ()).throw(ApiError(dict(code='waiting-index',error='not published')))
        value=snapshot();value['segmentCatalog']=[];preview=V2PreviewSession(client,'/unused')
        self.assertEqual(preview.publish(value)['state'],'waiting-index');self.assertFalse(preview.started);self.assertFalse(preview.uncertain)
        self.assertTrue(preview.close()['released'])
    def test_failed_catalog_update_retains_last_accepted_revision(self):
        client=Client();client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc',catalog=dict(mode='paged-fmp4-v1'))))
        value=snapshot();value['segmentCatalog']=[];preview=V2PreviewSession(client,'/unused');preview.publish(value)
        following=copy.deepcopy(value);following['revision']=str(int(value['revision'])+1)
        client.update=lambda *args,**kw:(_ for _ in ()).throw(ApiError(dict(code='waiting-index',error='not published')))
        with self.assertRaises(ApiError):preview.publish(following)
        self.assertEqual(preview.last,value);self.assertTrue(preview.started);self.assertFalse(preview.uncertain)
    def test_segments_require_playback_capability_not_diagnostic_probe(self):
        client=Client();preview=V2PreviewSession(client,'/unused');value=snapshot()
        value['tracks'][0]['source']=dict(segments=[])
        with self.assertRaisesRegex(RuntimeError,'segment'):preview.publish(value)
        self.assertFalse(client.calls);self.assertIsNone(preview.last);self.assertFalse(preview.uncertain)
        client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,segmentProbeVersion=1,
                                    schema2Playback=dict(mode='separate-fmp4-h264-aac-lc'))
        with self.assertRaisesRegex(RuntimeError,'segment'):V2PreviewSession(client,'/unused').publish(value)
        self.assertFalse(client.calls)
    def test_segment_playback_capability_admits_segment_session(self):
        client=Client();client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc')))
        value=snapshot();value['tracks'][0]['source']=dict(segments=[])
        preview=V2PreviewSession(client,'/unused');preview.publish(value);self.assertEqual(client.calls[0][0],'start')
    def test_segmented_hevc_requires_explicit_profile(self):
        client=Client();client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc')))
        value=snapshot();value['tracks'][0].update(codecHint='hevc',source=dict(segments=[]))
        preview=V2PreviewSession(client,'/unused')
        with self.assertRaisesRegex(RuntimeError,'HEVC'):preview.publish(value)
        self.assertFalse(client.calls);self.assertIsNone(preview.last);self.assertFalse(preview.uncertain)
        client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc',profiles=['segmented-fmp4-hevc-aac-lc'])))
        V2PreviewSession(client,'/unused').publish(value);self.assertEqual(client.calls[0][0],'start')
    def test_segmented_av1_requires_explicit_profile(self):
        client=Client();client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc',profiles=['segmented-fmp4-hevc-aac-lc'])))
        value=snapshot();value['tracks'][0].update(codecHint='av1',source=dict(segments=[]))
        preview=V2PreviewSession(client,'/unused')
        with self.assertRaisesRegex(RuntimeError,'AV1'):preview.publish(value)
        self.assertFalse(client.calls);self.assertIsNone(preview.last);self.assertFalse(preview.uncertain)
        client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc',profiles=['segmented-fmp4-av1-aac-lc'])))
        V2PreviewSession(client,'/unused').publish(value);self.assertEqual(client.calls[0][0],'start')
    def test_segmented_vp9_requires_exact_audio_pair(self):
        client=Client();client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc',profiles=['segmented-fmp4-av1-aac-lc'])))
        value=snapshot();value['tracks'][0].update(codecHint='vp9',source=dict(segments=[]))
        with self.assertRaisesRegex(RuntimeError,'VP9'):V2PreviewSession(client,'/unused').publish(value)
        self.assertFalse(client.calls)
        client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc',profiles=['segmented-fmp4-vp9-aac-lc'])))
        V2PreviewSession(client,'/unused').publish(value);self.assertEqual(client.calls[-1][0],'start');client.calls.clear()
        value['tracks'][1].update(codecHint='opus',source=dict(segments=[]))
        with self.assertRaisesRegex(RuntimeError,'VP9/opus'):V2PreviewSession(client,'/unused').publish(value)
        self.assertFalse(client.calls)
        client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc',profiles=['segmented-fmp4-vp9-opus'])))
        V2PreviewSession(client,'/unused').publish(value);self.assertEqual(client.calls[-1][0],'start')
    def test_segmented_opus_is_not_implied_by_h264_support(self):
        client=Client();client.request=lambda _:dict(playbackSchemaVersions=[1,2],readerLeaseProtocol=1,
            schema2Playback=dict(mode='separate-fmp4-h264-aac-lc',segmentedPlayback=dict(mode='segmented-fmp4-h264-aac-lc')))
        value=snapshot();value['tracks'][0].update(codecHint='h264',source=dict(segments=[]));value['tracks'][1].update(codecHint='opus',source=dict(segments=[]))
        preview=V2PreviewSession(client,'/unused')
        with self.assertRaisesRegex(RuntimeError,'Opus'):preview.publish(value)
        self.assertFalse(client.calls);self.assertIsNone(preview.last);self.assertFalse(preview.uncertain)
    def test_integrity_requires_player_capability_before_start(self):
        client=Client();preview=V2PreviewSession(client,'/unused');value=snapshot()
        value['resources']=[dict(integrity=dict(algorithm='sha256',blocks=[]))]
        with self.assertRaisesRegex(RuntimeError,'SHA-256'):preview.publish(value)
        self.assertFalse(client.calls);self.assertIsNone(preview.last);self.assertFalse(preview.uncertain)
    def test_canonical_exact_counters(self):
        self.assertEqual(counter('18446744073709551615'),2**64-1)
        for value in (True,1,1.0,'01','0','+1','1.0','１２','18446744073709551616',''):
            with self.subTest(value=value),self.assertRaises(ValueError):counter(value)
    def test_old_capabilities_rejected(self):
        client=Client();client.request=lambda _:dict(playbackSchemaVersions=[1],readerLeaseProtocol=1)
        with self.assertRaises(RuntimeError):V2PreviewSession(client,'/unused')
    def test_initialization_wait_does_not_open_or_probe(self):
        client=Client();preview=V2PreviewSession(client,'/unused')
        self.assertEqual(preview.publish(snapshot(False))['state'],'waiting-initialization');self.assertEqual(client.calls,[])
        self.assertTrue(preview.close()['released']);self.assertEqual(client.calls,[])
    def test_native_initialization_wait_can_retry_same_revision(self):
        client=Client();start=client.start
        client.start=lambda *a,**kw:(_ for _ in ()).throw(ApiError(dict(code='waiting_initialization')))
        preview=V2PreviewSession(client,'/unused');self.assertEqual(preview.publish(snapshot())['state'],'waiting-initialization');self.assertTrue(preview.status()['released'])
        client.start=start;preview.publish(snapshot());self.assertTrue(preview.started)
    def test_lost_start_never_proves_release(self):
        client=Client();client.start=lambda *a,**kw:(_ for _ in ()).throw(TimeoutError('Lost reply'))
        preview=V2PreviewSession(client,'/unused')
        with self.assertRaises(TimeoutError):preview.publish(snapshot())
        self.assertFalse(preview.status()['released']);self.assertFalse(preview.close()['released'])
    def test_readers_guard_every_mutation_and_release(self):
        client=Client();preview=V2PreviewSession(client,'/unused');value=snapshot();preview.publish(value);preview.heartbeat();preview.status()
        value['revision']=str(int(value['revision'])+1);preview.publish(value);self.assertTrue(preview.close()['released']);self.assertTrue(preview.close()['released'])
        self.assertEqual([x[0] for x in client.calls],['start','heartbeat','status','update','release-reader','forget-reader'])
        self.assertEqual(client.calls[0][1]['generation'],'18446744073709551615');self.assertTrue(client.calls[0][2]['track_reader'])
        self.assertEqual(client.calls[1][2]['readerId'],'c'*32);self.assertEqual(client.calls[3][2]['reader_id'],'c'*32)
    def test_false_release_never_forgets_and_can_retry(self):
        client=Client();client.released=False;preview=V2PreviewSession(client,'/unused');preview.publish(snapshot())
        self.assertFalse(preview.close()['released']);self.assertNotIn('forget-reader',[x[0] for x in client.calls])
        client.released=True;self.assertTrue(preview.close()['released'])
    def test_writer_incarnation_and_revision_conflicts(self):
        client=Client();preview=V2PreviewSession(client,'/unused');value=snapshot();preview.publish(value)
        for field in ('generation','revision','writer'):
            other=copy.deepcopy(value)
            if field=='writer':other[field]['instanceId']='d'*32
            else:other[field]='1'
            with self.subTest(field=field),self.assertRaises(ValueError):preview.publish(other)
        other=copy.deepcopy(value);other['tracks'][0]['initializationReady']=False
        with self.assertRaises(ValueError):preview.publish(other)
        self.assertEqual(len(client.calls),1)
    def test_repeated_publication_is_heartbeat_and_caller_copy_isolated(self):
        client=Client();preview=V2PreviewSession(client,'/unused');value=snapshot();preview.publish(value);preview.publish(value)
        value['revision']='9007199254740994';self.assertEqual(preview.last['revision'],'9007199254740993');self.assertEqual(client.calls[-1][0],'heartbeat')
    def test_idempotent_recovered_start_advances_older_revision(self):
        client=Client();start=client.start
        client.start=lambda value,**options:dict(start(value,**options),revision=str(int(value['revision'])-1))
        preview=V2PreviewSession(client,'/unused');preview.publish(snapshot());self.assertEqual(client.calls[-1][0],'update')
    def test_complete_update_uses_complete_action(self):
        client=Client();preview=V2PreviewSession(client,'/unused');value=snapshot();preview.publish(value)
        value['revision']=str(int(value['revision'])+1);value['writer']['state']='complete';preview.publish(value)
        self.assertTrue(client.calls[-1][2]['complete']);preview.heartbeat();self.assertEqual(client.calls[-1][0],'status')
    def test_cancel_detaches_reader_and_never_changes_downloader(self):
        client=Client();preview=V2PreviewSession(client,'/unused');value=snapshot();preview.publish(value)
        value['revision']=str(int(value['revision'])+1);value['writer']['state']='cancelled';self.assertTrue(preview.publish(value)['released'])
        self.assertEqual([x[0] for x in client.calls],['start','release-reader','forget-reader'])
        with self.assertRaises(RuntimeError):preview.publish(value)

if __name__=='__main__':unittest.main()
