"""SDK ownership, lost-reply recovery and explicit playback boundaries."""
import copy
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'qt/tools'))
from progressive_continuous import ACTIONS, ContinuousRemux, MODE

CAP = json.loads(re.search(r'return R"\((.*?)\)";',
    (ROOT/'shared/progressivestreamcapabilities.h').read_text(), re.S).group(1))


class Client:
    def __init__(self):
        self.calls = [];self.job = None;self.play = None;self.drop = set()
        self.ready = True;self.released = True;self.foreign = False

    def request(self, message, timeout=5):
        self.calls.append(copy.deepcopy(message));action = message['action']
        if action == 'capabilities':
            return dict(continuousRemux=copy.deepcopy(CAP), actions=sorted(ACTIONS), readerLeaseProtocol=1)
        if action == 'continuousRemuxStart':
            s = message['session']
            self.job = dict(mode=MODE, requestId=message['requestId'], jobId='a'*32,
                sessionId=s['sessionId'], generation=s['generation'], writerInstanceId=s['writer']['instanceId'])
        if action == 'continuousRemuxPlay':
            self.play = {**self.job, 'requestId': message['requestId'], 'playbackId': 'b'*32}
        playback = action in ('continuousRemuxPlay','continuousPlaybackStatus','continuousPlaybackControl')
        result = dict(self.play if playback else self.job)
        result.update(playbackReady=self.ready, released=self.released, sourceFilesClosed=self.released,
            forgotten=action == 'continuousRemuxForget', state='playing' if playback else 'ready')
        if self.foreign:result['generation'] = '2'
        if action in self.drop:
            self.drop.remove(action);raise TimeoutError('applied request, lost reply')
        return result


class Test(unittest.TestCase):
    def model(self):
        return dict(sessionId='c'*32, generation='1', revision='1', writer=dict(instanceId='d'*32))

    def setup_preview(self):
        c=Client();p=ContinuousRemux(c, str(ROOT));p.prepare(self.model(), 'clip');return c,p

    def test_capability_is_required(self):
        c=Client();c.request=lambda _: {};self.assertRaises(RuntimeError, ContinuousRemux, c, str(ROOT))

    def test_all_actions_and_exact_mode_are_required(self):
        for change in ('actions','mode','worker','audio'):
            c=Client();old=c.request
            def request(message, change=change):
                cap=old(message)
                if change=='actions':cap['actions'].remove('continuousRemuxPlay')
                elif change=='mode':cap['continuousRemux']['mode']='other'
                elif change=='worker':cap['continuousRemux']['workerProtocol']=2
                else:cap['continuousRemux']['audio']=True
                return cap
            c.request=request;self.assertRaises(RuntimeError,ContinuousRemux,c,str(ROOT))

    def test_relative_directory_is_rejected_without_ipc(self):
        c=Client();self.assertRaises(ValueError,ContinuousRemux,c,'relative');self.assertEqual(c.calls,[])

    def test_writer_events_never_start_playback(self):
        c,p=self.setup_preview();s=self.model();s['revision']='2';p.publish(s);p.heartbeat()
        self.assertNotIn('continuousRemuxPlay',[x['action'] for x in c.calls])

    def test_no_implicit_replacement_or_activation(self):
        c,p=self.setup_preview();p.play();self.assertFalse(c.calls[-1]['replace']);self.assertFalse(c.calls[-1]['activate'])

    def test_lost_preparation_reply_uses_readonly_recovery(self):
        c=Client();p=ContinuousRemux(c,str(ROOT));c.drop.add('continuousRemuxStart')
        self.assertRaises(TimeoutError,p.prepare,self.model(),'clip');p.status()
        self.assertEqual(c.calls[-1]['action'],'continuousRemuxStatus');self.assertIn('requestId',c.calls[-1])
        self.assertEqual(sum(x['action']=='continuousRemuxStart' for x in c.calls),1)

    def test_lost_play_reply_does_not_replay_or_start_another_attempt(self):
        c,p=self.setup_preview();c.drop.add('continuousRemuxPlay');self.assertRaises(TimeoutError,p.play)
        p.playback_status();self.assertEqual(c.calls[-1]['action'],'continuousPlaybackStatus');self.assertIn('requestId',c.calls[-1])
        n=len(c.calls);self.assertRaises(RuntimeError,p.play);self.assertEqual(n,len(c.calls))

    def test_not_ready_does_not_register_a_later_play(self):
        c,p=self.setup_preview();c.ready=False;self.assertRaises(RuntimeError,p.play);self.assertIsNone(p.play_request_id)
        c.ready=True;p.heartbeat();self.assertNotIn('continuousRemuxPlay',[x['action'] for x in c.calls]);p.play()

    def test_foreign_identity_is_not_adopted_or_cancelled(self):
        c,p=self.setup_preview();c.foreign=True;self.assertRaises(RuntimeError,p.status)
        self.assertEqual(c.calls[-1]['action'],'continuousRemuxStatus');self.assertEqual(p.job_id,'a'*32)

    def test_writer_replacement_rejected_before_request(self):
        c,p=self.setup_preview();s=self.model();s['writer']['instanceId']='e'*32;n=len(c.calls)
        self.assertRaises(ValueError,p.publish,s);self.assertEqual(n,len(c.calls))

    def test_control_never_falls_back_to_global_stop(self):
        c,p=self.setup_preview();n=len(c.calls);self.assertRaises(RuntimeError,p.control,'stop');self.assertEqual(n,len(c.calls))

    def test_seek_validation_precedes_ipc(self):
        c,p=self.setup_preview();n=len(c.calls)
        for value in (-1,3600,True,float('nan'),float('inf')):self.assertRaises(ValueError,p.control,'seek',seconds=value)
        self.assertEqual(n,len(c.calls))

    def test_stop_is_terminal_for_writer_and_play_commands(self):
        c,p=self.setup_preview();p.play();p.control('stop');n=len(c.calls)
        self.assertRaises(RuntimeError,p.publish,self.model());self.assertRaises(RuntimeError,p.heartbeat);self.assertRaises(RuntimeError,p.play)
        self.assertEqual(n,len(c.calls))

    def test_close_proof_is_retained_and_forget_is_explicit(self):
        c,p=self.setup_preview();self.assertRaises(RuntimeError,p.forget);reply=p.close();self.assertTrue(reply['released'])
        self.assertEqual(c.calls[-1]['action'],'continuousRemuxCancel');n=len(c.calls);self.assertEqual(reply,p.close());self.assertEqual(n,len(c.calls))
        self.assertTrue(p.forget()['forgotten']);self.assertEqual(c.calls[-1]['action'],'continuousRemuxForget')

    def test_timeout_is_not_release_and_does_not_allow_forget(self):
        c,p=self.setup_preview();c.released=False;self.assertFalse(p.close(timeout=.001)['released']);self.assertRaises(RuntimeError,p.forget)

    def test_invalid_timeout_and_options_do_not_close_or_transmit(self):
        c,p=self.setup_preview();n=len(c.calls)
        for value in (0,-1,True,float('nan'),61):self.assertRaises(ValueError,p.close,timeout=value)
        self.assertFalse(p.closing);self.assertEqual(n,len(c.calls))
        self.assertRaises(ValueError,p.play,activate=1);self.assertEqual(n,len(c.calls))

    def test_snapshot_and_capability_are_not_mutated(self):
        c=Client();p=ContinuousRemux(c,str(ROOT));s=self.model();before=copy.deepcopy(s);p.prepare(s,'clip');self.assertEqual(s,before)
        self.assertEqual(p.capability,CAP)


if __name__=='__main__':unittest.main()
