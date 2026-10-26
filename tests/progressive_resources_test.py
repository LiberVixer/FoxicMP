"""Public resource-I/O SDK safety: no playback, foreign-job cancellation or false release."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt'/'tools'))
from progressive_resources import ResourceProbe


class Client:
    def __init__(self):self.calls=[];self.phase='running';self.closed=False;self.identity=('a'*32,'1');self.lose_start=False;self.token=None
    def request(self, message, timeout=5):
        self.calls.append((copy.deepcopy(message),timeout));action=message['action']
        if action=='capabilities':return dict(resourceIoProbeVersion=1,resourceIoPlaybackSupported=False,actions=['resourceProbe','resourceProbeStatus','resourceProbeCancel','resourceProbeForget','resourceProbeUpdate'])
        if action=='resourceProbe':
            self.token=message['requestId']
            if self.lose_start:raise TimeoutError('Reply lost')
        if action=='resourceProbeForget':self.phase='forgotten'
        return dict(mode='resource-io-only',playbackSupported=False,probeId='c'*32,requestId=self.token,sessionId=self.identity[0],generation=self.identity[1],state=self.phase,released=self.closed)


class ResourceSdkTest(unittest.TestCase):
    def start(self,client):
        probe=ResourceProbe(client);probe.start(dict(sessionId='a'*32,generation='1'),'/unused',[dict(resourceId='media',offset='0',length='4')]);return probe
    def test_old_player_is_rejected_without_opening_media(self):
        client=Client();client.request=lambda message:dict(actions=[])
        with self.assertRaises(RuntimeError):ResourceProbe(client)
    def test_only_resource_commands_are_used(self):
        client=Client();probe=self.start(client);probe.publish(dict(sessionId='a'*32,generation='1'));probe.cancel();client.phase='cancelled';client.closed=True
        self.assertTrue(probe.wait()['released']);self.assertEqual(probe.forget()['state'],'forgotten')
        self.assertTrue(all(m['action']=='capabilities' or m['action'].startswith('resourceProbe') for m,_ in client.calls))
    def test_lost_start_reply_is_recovered_by_read_only_discovery(self):
        client=Client();client.lose_start=True;probe=ResourceProbe(client)
        with self.assertRaises(TimeoutError):probe.start(dict(sessionId='a'*32,generation='1'),'/unused',[])
        probe.status();self.assertEqual(probe.probe_id,'c'*32);self.assertNotIn('probeId',client.calls[-1][0])
    def test_foreign_discovered_job_cannot_be_cancelled(self):
        client=Client();client.lose_start=True;probe=ResourceProbe(client)
        with self.assertRaises(TimeoutError):probe.start(dict(sessionId='a'*32,generation='1'),'/unused',[])
        client.identity=('b'*32,'1')
        with self.assertRaises(RuntimeError):probe.cancel()
        self.assertEqual(client.calls[-1][0]['action'],'resourceProbeStatus')
    def test_timeout_never_claims_release_or_cancels_playback(self):
        client=Client();probe=self.start(client)
        with self.assertRaises(TimeoutError):probe.wait(0)
        self.assertEqual(client.calls[-1][0]['action'],'resourceProbeStatus');self.assertLessEqual(client.calls[-1][1],.0011)
    def test_other_probe_of_same_generation_cannot_be_adopted(self):
        client=Client();client.lose_start=True;probe=ResourceProbe(client)
        with self.assertRaises(TimeoutError):probe.start(dict(sessionId='a'*32,generation='1'),'/unused',[])
        client.token='d'*32
        with self.assertRaises(RuntimeError):probe.cancel()
        self.assertEqual(client.calls[-1][0]['action'],'resourceProbeStatus')
    def test_generation_change_is_rejected_before_sending_update(self):
        client=Client();probe=self.start(client);before=len(client.calls)
        with self.assertRaises(ValueError):probe.publish(dict(sessionId='a'*32,generation='2'))
        self.assertEqual(len(client.calls),before)
    def test_mutating_callers_snapshot_does_not_mutate_sent_request(self):
        client=Client();value=dict(sessionId='a'*32,generation='1');probe=ResourceProbe(client);probe.start(value,'/unused',[]);value['generation']='2'
        self.assertEqual(client.calls[-1][0]['session']['generation'],'1')


if __name__=='__main__':unittest.main()
