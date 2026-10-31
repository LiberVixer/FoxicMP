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
    def demux_client(self):
        client=Client();old=client.request
        def request(message,timeout=5):
            out=old(message,timeout)
            if message['action']=='capabilities':out['backendDemuxProbe']=dict(version=1,mode='libavformat-avio-probe-v1',analysisOnly=True,playbackSupported=False)
            return out
        client.request=request;return client
    def test_malformed_optional_demux_capability_preserves_raw_resource_sdk(self):
        for value in (None,False,[],dict(version=True)):
            client=Client();old=client.request
            def request(message,timeout=5):
                out=old(message,timeout)
                if message['action']=='capabilities':out['backendDemuxProbe']=value
                return out
            client.request=request;probe=ResourceProbe(client);self.assertFalse(probe.demux_supported)
            probe.start(dict(sessionId='a'*32,generation='1'),'/unused',[])
    def test_demux_requires_capability_before_starting_job(self):
        client=Client();probe=ResourceProbe(client);before=len(client.calls)
        with self.assertRaises(RuntimeError):probe.start_media({},'/unused','clip','mp4')
        self.assertEqual(len(client.calls),before);self.assertIsNone(probe.identity)
    def test_demux_rejects_protocols_and_non_integer_packet_limits(self):
        probe=ResourceProbe(self.demux_client())
        for hint in ('http','concat','hls','lavfi'):
            with self.assertRaises(ValueError):probe.start_media({},'/unused','clip',hint)
        for count in (True,0,129,0.5,'32'):
            with self.assertRaises(ValueError):probe.start_media({},'/unused','clip','mp4',packet_limit=count)
        self.assertIsNone(probe.identity)
    def test_demux_cannot_mix_segment_or_raw_reads(self):
        probe=ResourceProbe(self.demux_client());media=dict(resourceId='clip',formatHint='mp4')
        with self.assertRaises(ValueError):probe.start({},'/unused',[{}],media=media)
        with self.assertRaises(ValueError):probe.start({},'/unused',[],segment={},media=media)
        self.assertIsNone(probe.identity)
    def test_demux_request_is_copied_and_lost_reply_can_be_recovered(self):
        client=self.demux_client();client.lose_start=True;probe=ResourceProbe(client)
        with self.assertRaises(TimeoutError):probe.start_media(dict(sessionId='a'*32,generation='1'),'/unused','clip','mp4',packet_limit=12)
        self.assertEqual(client.calls[-1][0]['media'],dict(resourceId='clip',formatHint='mp4',packetLimit=12))
        probe.status();self.assertEqual(probe.probe_id,'c'*32)
    def test_demux_never_adopts_playback_success(self):
        probe=ResourceProbe(self.demux_client());out=probe.start_media(dict(sessionId='a'*32,generation='1'),'/unused','clip','mp4')
        for wrong in (dict(code='data',analysisOnly=False,playbackSupported=False),dict(code='data',analysisOnly=True,playbackSupported=True)):
            out['media']=wrong
            with self.assertRaises(RuntimeError):probe._check(out)
    def test_segment_start_requires_capability_before_any_job_is_sent(self):
        client=Client();probe=ResourceProbe(client);before=len(client.calls)
        with self.assertRaises(RuntimeError):probe.start_segment(dict(sessionId='a'*32,generation='1'),'/unused','video',0)
        self.assertEqual(len(client.calls),before);self.assertIsNone(probe.identity)
    def test_segment_index_and_demux_are_exact_types(self):
        probe=ResourceProbe(Client())
        for index in (True,-1,128,0.5):
            with self.assertRaises(ValueError):probe.start_segment({},'/unused','video',index)
        with self.assertRaises(ValueError):probe.start_segment({},'/unused','video',0,demux=1)
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
