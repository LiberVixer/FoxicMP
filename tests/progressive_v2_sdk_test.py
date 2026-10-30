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
