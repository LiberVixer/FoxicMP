"""SDK contract checks independent of a desktop, downloader and media backend."""
import copy
from pathlib import Path
import sys
import unittest
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'qt'/'tools'))
from progressive_client import ProgressiveClient, read_snapshot
from progressive_sdk import PreviewSession
from progressive_schema import unsigned64, signed64

class Client:
    def __init__(self):
        self.calls = []
    def request(self, message):
        return dict(schemaVersion=1, releaseAcknowledgement=True,
            actions=['probe','start','update','heartbeat','complete','error','status','release'])
    def probe(self, value, directory):
        self.calls.append(('probe', copy.deepcopy(value)))
        return dict(supported=True)
    def start(self, value, **kwargs):
        self.calls.append(('start', copy.deepcopy(value)))
        return dict(ok=True)
    def update(self, value, **kwargs):
        self.calls.append(('update', copy.deepcopy(value)))
        return dict(ok=True)
    def command(self, action, session_id, **kwargs):
        self.calls.append((action, session_id))
        return dict(ok=True)
    def release(self, session_id, timeout=2):
        self.calls.append(('release', session_id))
        return dict(ok=True, active=False, released=False)

class SdkTest(unittest.TestCase):
    def tracked_client(self):
        client=Client();original=client.request
        client.request=lambda message:dict(original(message),readerLeaseProtocol=1,
            actions=original(message)['actions']+['readerStatus','readerRelease','readerForget','readers'])
        client.start=lambda value,**options:dict(ok=True,readerId='b'*32) if options['track_reader'] else self.fail('Missing lease opt-in')
        return client
    def test_tracked_close_uses_reader_not_replacement_session_and_caches_ack(self):
        client=self.tracked_client();calls=[]
        client.release_reader=lambda reader,timeout:calls.append(('release-reader',reader)) or dict(released=True,readerId=reader)
        client.forget_reader=lambda reader:calls.append(('forget',reader)) or dict(forgotten=True)
        preview=PreviewSession(client,'/unused');preview.publish(self.snapshot(True))
        self.assertEqual(preview.reader_id,'b'*32);self.assertTrue(preview.close()['released']);self.assertTrue(preview.close()['released'])
        self.assertEqual(calls,[('release-reader','b'*32),('forget','b'*32)])
    def test_unconfirmed_reader_is_never_forgotten(self):
        client=self.tracked_client();client.release_reader=lambda reader,timeout:dict(released=False)
        client.forget_reader=lambda reader:self.fail('Forgot unconfirmed reader')
        preview=PreviewSession(client,'/unused');preview.publish(self.snapshot(True));self.assertFalse(preview.close()['released'])
    def test_reader_identity_guards_writer_events_and_updates(self):
        client=self.tracked_client();calls=[]
        client.command=lambda action,session,**fields:calls.append((action,fields)) or dict(ok=True)
        client.update=lambda value,**options:calls.append(('update',options)) or dict(ok=True)
        preview=PreviewSession(client,'/unused');value=self.snapshot(True);preview.publish(value)
        preview.heartbeat();preview.status();value['revision']=2;preview.publish(value);preview.writer_error('Writer error')
        self.assertEqual([action for action,_ in calls],['heartbeat','status','update','error'])
        for action,fields in calls:self.assertEqual(fields['reader_id' if action=='update' else 'readerId'],'b'*32)
    def test_lost_start_reply_never_claims_files_released(self):
        client=self.tracked_client()
        def lost(*args,**kwargs):raise TimeoutError('Start reply lost')
        client.start=lost;preview=PreviewSession(client,'/unused')
        with self.assertRaises(TimeoutError):preview.publish(self.snapshot(True))
        self.assertFalse(preview.status()['released']);self.assertFalse(preview.close()['released'])
        self.assertEqual(preview.last['sessionId'],'f'*32)
    def test_reader_release_uses_reader_id_and_shared_deadline(self):
        client=ProgressiveClient.__new__(ProgressiveClient);requests=[]
        def request(message,timeout):
            requests.append((message,timeout));return dict(reader=dict(released=False))
        client.request=request;self.assertFalse(client.release_reader('b'*32,timeout=.02)['released'])
        self.assertEqual(requests[0][0],dict(version=1,action='readerRelease',readerId='b'*32));self.assertLessEqual(requests[0][1],.02)
    def snapshot(self, ready=False):
        return dict(sessionId='f'*32, generation=1, revision=1, state='downloading',
                    tracks={k:dict(initializationReady=ready) for k in ('video','audio')})
    def test_waiting_stop_does_not_start_or_cancel_writer(self):
        client = Client(); preview = PreviewSession(client, '/unused')
        self.assertEqual(preview.publish(self.snapshot())['state'], 'waiting-initialization')
        self.assertFalse(client.calls)
        self.assertTrue(preview.close()['released'])
        with self.assertRaises(RuntimeError):
            preview.publish(self.snapshot(True))
        self.assertFalse(client.calls)
    def test_revision_identity_and_caller_snapshot_isolation(self):
        client = Client(); preview = PreviewSession(client, '/unused')
        source = self.snapshot(True); preview.publish(source)
        source['revision'] = 2
        self.assertEqual(preview.last['revision'], 1)
        preview.publish(source)
        source['tracks']['audio']['initializationReady'] = False
        with self.assertRaises(ValueError): preview.publish(source)
        source['revision'] = 3; source['generation'] = 2
        with self.assertRaises(ValueError): preview.publish(source)
        self.assertEqual([call[0] for call in client.calls], ['probe','start','update'])
    def test_uncertain_release_never_restarts(self):
        client = Client(); preview = PreviewSession(client, '/unused')
        source = self.snapshot(True); preview.publish(source)
        self.assertIs(preview.close()['released'], False)
        source['revision'] += 1
        with self.assertRaises(RuntimeError): preview.publish(source)
        preview.heartbeat()
        self.assertEqual([call[0] for call in client.calls], ['probe','start','release'])
    def test_release_uses_shared_deadline_and_does_not_infer_success(self):
        client = ProgressiveClient.__new__(ProgressiveClient)
        requests = []
        def request(value, timeout):
            requests.append((value['action'], timeout))
            return dict(version=1, ok=True, active=False, released=False)
        client.request = request
        result = client.release('f'*32, timeout=.02)
        self.assertFalse(result['released'])
        self.assertEqual(requests[0][0], 'release')
        self.assertLessEqual(requests[0][1], .02)
    def test_old_capabilities_cannot_claim_safe_release(self):
        client = Client()
        client.request = lambda _: dict(schemaVersion=1, actions=[])
        with self.assertRaises(RuntimeError): PreviewSession(client, '/unused')
    def test_exact_schema_two_integer_encoding(self):
        self.assertEqual(unsigned64(2**64-1),'18446744073709551615')
        self.assertEqual(signed64(-2**63),'-9223372036854775808')
        self.assertEqual(unsigned64(2**53+1),'9007199254740993')
        for value in (True,1.5,2**64,-1):
            with self.assertRaises(ValueError): unsigned64(value)
        for value in (False,1.0,-2**63-1,2**63):
            with self.assertRaises(ValueError): signed64(value)
        with self.assertRaises(ValueError): unsigned64(2**63,byte_offset=True)
    def test_snapshot_files_are_bounded_unambiguous_json(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'snapshot.json'
            for raw in ('{"revision":1,"revision":2}','{"value":NaN}','[]',' '*65537):
                path.write_text(raw)
                with self.assertRaises(ValueError): read_snapshot(path)

if __name__ == '__main__': unittest.main()
