"""SDK contract checks independent of a desktop, downloader and media backend."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'qt'/'tools'))
from progressive_client import ProgressiveClient
from progressive_sdk import PreviewSession

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

if __name__ == '__main__': unittest.main()
