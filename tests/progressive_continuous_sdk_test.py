"""SDK ownership, lost-reply recovery and explicit playback boundaries."""
import copy
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'qt/tools'))
from progressive_continuous import ACTIONS, ContinuousRemux, MODE, REORDERED_MODE, AAC_MODE, HEVC_MODE, HEVC_AAC_MODE, AV1_MODE
from progressive_client import ApiError

CAP = json.loads(re.search(r'return R"\((.*?)\)";',
    (ROOT/'shared/progressivestreamcapabilities.h').read_text(), re.S).group(1))


class Client:
    def __init__(self):
        self.calls = [];self.job = None;self.play = None;self.drop = set()
        self.ready = True;self.released = True;self.foreign = False
        self.duration=24;self.boundary=3;self.complete=False

    def request(self, message, timeout=5):
        self.calls.append(copy.deepcopy(message));action = message['action']
        if action == 'capabilities':
            return dict(continuousRemux=copy.deepcopy(CAP), actions=sorted(ACTIONS), readerLeaseProtocol=1)
        if action == 'continuousRemuxStart':
            s = message['session']
            self.job = dict(mode=message.get('mode', MODE), requestId=message['requestId'], jobId='a'*32,
                sessionId=s['sessionId'], generation=s['generation'], writerInstanceId=s['writer']['instanceId'])
        if action == 'continuousRemuxPlay':
            self.play = {**self.job, 'requestId': message['requestId'], 'playbackId': 'b'*32}
            if 'startSeconds' in message:self.play['startSeconds']=message['startSeconds']
        playback = action in ('continuousRemuxPlay','continuousPlaybackStatus','continuousPlaybackControl')
        result = dict(self.play if playback else self.job)
        result.update(playbackReady=self.ready, released=self.released, sourceFilesClosed=self.released,
            durationSeconds=self.duration,indexedEndSeconds=self.boundary,outputComplete=self.complete,
            forgotten=action == 'continuousRemuxForget', state='playing' if playback else 'ready')
        if self.foreign:result['generation'] = '2'
        if action in self.drop:
            self.drop.remove(action);raise TimeoutError('applied request, lost reply')
        return result


class Test(unittest.TestCase):
    def test_queued_seek_requires_exact_capability_and_opt_in(self):
        c,p=self.setup_preview();p.play()
        p.control('seek',seconds=5,allow_growing=True,wait_for_range=True)
        self.assertIs(c.calls[-1]['waitForRange'],True)
        p.control('cancelSeek');self.assertEqual(c.calls[-1]['command'],'cancelSeek')
        p._queued_seek_capability=None
        count=len(c.calls)
        self.assertRaises(RuntimeError,p.control,'seek',seconds=5,allow_growing=True,wait_for_range=True)
        self.assertRaises(RuntimeError,p.control,'cancelSeek');self.assertEqual(len(c.calls),count)

    def test_queued_seek_rejects_invalid_options_without_transport(self):
        c,p=self.setup_preview();p.play();count=len(c.calls)
        for value in (1,'yes',None):self.assertRaises(ValueError,p.control,'seek',seconds=5,allow_growing=True,wait_for_range=value)
        self.assertRaises(ValueError,p.control,'seek',seconds=5,wait_for_range=True)
        self.assertRaises(ValueError,p.control,'pause',wait_for_range=True)
        self.assertRaises(ValueError,p.control,'cancelSeek',seconds=5)
        self.assertEqual(len(c.calls),count)

    def test_growing_seek_is_explicit_and_not_sent_to_older_players(self):
        c,p=self.setup_preview();p.play();p.control('seek',seconds=1);self.assertNotIn('allowGrowing',c.calls[-1])
        p.control('seek',seconds=1,allow_growing=True);self.assertIs(c.calls[-1]['allowGrowing'],True)
        p._growing_seek_capability=None
        self.assertRaises(RuntimeError,p.control,'seek',seconds=1,allow_growing=True)

    def test_growing_seek_options_reject_wrong_types_or_command(self):
        c,p=self.setup_preview();p.play()
        for value in (1,'yes',None):self.assertRaises(ValueError,p.control,'seek',seconds=1,allow_growing=value)
        self.assertRaises(ValueError,p.control,'pause',allow_growing=True)

    def test_initial_position_racing_range_rejection_can_retry(self):
        c,p=self.setup_preview();original=c.request;reject=[True]
        def request(message,timeout=5):
            if message['action']=='continuousRemuxPlay' and reject.pop():
                raise ApiError(dict(ok=False,code='waiting-range',error='Wait for output'))
            return original(message,timeout)
        c.request=request
        self.assertRaises(ApiError,p.play,start_seconds=1);self.assertIsNone(p.play_request_id)
        c.request=original;p.play(start_seconds=1);self.assertIsNotNone(p.playback_id)

    def test_initial_position_default_remains_legacy_request(self):
        c,p=self.setup_preview();p.play();self.assertNotIn('startSeconds',c.calls[-1])

    def test_initial_position_invalid_does_not_consume_intent(self):
        for value in (True,-1,float('nan'),float('inf'),3600,'1'):
            c,p=self.setup_preview();self.assertRaises(ValueError,p.play,start_seconds=value);self.assertIsNone(p.play_request_id)

    def test_initial_position_wait_and_duration(self):
        c,p=self.setup_preview();self.assertRaises(RuntimeError,p.play,start_seconds=3);self.assertIsNone(p.play_request_id)
        self.assertRaises(ValueError,p.play,start_seconds=24);self.assertIsNone(p.play_request_id)
        p.play(start_seconds=1);self.assertEqual(c.calls[-1]['startSeconds'],1)

    def test_initial_position_complete_and_lost_reply(self):
        c,p=self.setup_preview();c.complete=True;c.drop.add('continuousRemuxPlay')
        self.assertRaises(TimeoutError,p.play,start_seconds=8);self.assertEqual(p.playback_status()['startSeconds'],8)

    def test_initial_position_capability_is_not_mutable_permission(self):
        c=Client();original=c.request
        def request(message,timeout=5):
            result=original(message,timeout)
            if message['action']=='capabilities':result['continuousRemux'].pop('initialPosition')
            return result
        c.request=request;p=ContinuousRemux(c,str(ROOT));p.prepare(self.model(),'clip')
        p.capability['initialPosition']=CAP['initialPosition']
        self.assertRaises(RuntimeError,p.play,start_seconds=1);self.assertIsNone(p.play_request_id)

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

    def test_legacy_start_payload_omits_mode(self):
        c,p=self.setup_preview();self.assertNotIn('mode',c.calls[-1])

    def test_unknown_mode_rejected_without_ipc(self):
        for mode in (None,True,'',MODE+'x'):
            c=Client();self.assertRaises(ValueError,ContinuousRemux,c,str(ROOT),mode=mode);self.assertEqual(c.calls,[])

    def test_old_player_only_supports_default(self):
        c=Client();old=c.request
        def request(message,timeout=5):
            reply=old(message,timeout)
            if message['action']=='capabilities':reply['continuousRemux'].pop('profiles',None)
            return reply
        c.request=request;ContinuousRemux(c,str(ROOT))
        self.assertRaises(RuntimeError,ContinuousRemux,c,str(ROOT),mode=REORDERED_MODE)
        self.assertRaises(RuntimeError,ContinuousRemux,c,str(ROOT),mode=AAC_MODE)
        self.assertRaises(RuntimeError,ContinuousRemux,c,str(ROOT),mode=HEVC_MODE)
        self.assertRaises(RuntimeError,ContinuousRemux,c,str(ROOT),mode=HEVC_AAC_MODE)
        self.assertRaises(RuntimeError,ContinuousRemux,c,str(ROOT),mode=AV1_MODE)
        self.assertFalse(any(m['action']=='continuousRemuxStart' for m in c.calls))

    def test_av1_requires_exact_video_contract(self):
        for field,value in [('videoCodec','hevc'),('audio',True),('bFrames',True),
                            ('av1Profiles',['high']),('bitDepths',[8,10.0]),('sampleEntries',['av01','avc1']),
                            ('packetReorder',True),('oneDisplayedFramePerSample',False),('hiddenFrames',True),
                            ('showExistingFrames',True),('stillPicture',True),('singleLayer',False),
                            ('lowOverheadObuRequired',False),('obuTypes',[1,2,3,4,6]),
                            ('sequenceHeaderRequiredInFirstPacket',False),('codecHintRequired',False)]:
            with self.subTest(field=field):
                c=Client();old=c.request
                def request(message,timeout=5):
                    reply=old(message,timeout);reply['continuousRemux']['profiles'][AV1_MODE][field]=value
                    return reply
                c.request=request
                self.assertRaises(RuntimeError,ContinuousRemux,c,str(ROOT),mode=AV1_MODE)
                self.assertEqual(len(c.calls),1)
        c=Client();p=ContinuousRemux(c,str(ROOT),mode=AV1_MODE)
        p.prepare(self.model(),'clip');self.assertEqual(c.calls[-1]['mode'],AV1_MODE)
        p.play();self.assertEqual(p.playback_status()['mode'],AV1_MODE)
        p.close();p.forget()

    def test_hevc_aac_requires_both_exact_contracts(self):
        for field,value in [('videoCodec','h264'),('videoOnly',True),('audio',False),
                            ('audioCodec','opus'),('sampleRates',[48000]),('channels',[1,6]),
                            ('pcmWindowRequired',False),('initialSkipSamples',True),
                            ('hevcProfiles',['main']),('bitDepths',[8,10.0]),('firstPicture','any'),
                            ('requiredTrackKinds',['video']),('codecHintRequired',False)]:
            with self.subTest(field=field):
                c=Client();old=c.request
                def request(message,timeout=5):
                    reply=old(message,timeout)
                    reply['continuousRemux']['profiles'][HEVC_AAC_MODE][field]=value
                    return reply
                c.request=request
                self.assertRaises(RuntimeError,ContinuousRemux,c,str(ROOT),mode=HEVC_AAC_MODE)
                self.assertEqual(len(c.calls),1)
        c=Client();p=ContinuousRemux(c,str(ROOT),mode=HEVC_AAC_MODE)
        p.prepare(self.model(),'clip');self.assertEqual(c.calls[-1]['mode'],HEVC_AAC_MODE)
        p.play();self.assertEqual(p.playback_status()['mode'],HEVC_AAC_MODE)
        p.close();p.forget()

    def test_hevc_contract_is_explicit_and_exact(self):
        for field,value in [('videoCodec','h264'),('bFrames',False),('audio',True),
                            ('hevcProfiles',['main']),('bitDepths',[8,10.0]),
                            ('chromaSubsampling','4:4:4'),('sampleEntries',['hev1']),
                            ('nalLengthBytes',4.0),('singleLayer',False),
                            ('requiredTrackKinds',['video','audio']),('codecHintRequired',False),
                            ('maxParameterSetsPerType',True),('firstPicture','any')]:
            with self.subTest(field=field):
                c=Client();old=c.request
                def request(message,timeout=5):
                    reply=old(message,timeout)
                    reply['continuousRemux']['profiles'][HEVC_MODE][field]=value
                    return reply
                c.request=request
                self.assertRaises(RuntimeError,ContinuousRemux,c,str(ROOT),mode=HEVC_MODE)
                self.assertEqual(len(c.calls),1)
        c=Client();p=ContinuousRemux(c,str(ROOT),mode=HEVC_MODE)
        p.prepare(self.model(),'clip')
        self.assertEqual(c.calls[-1]['mode'],HEVC_MODE)
        self.assertEqual(p.capability['videoCodec'],'hevc')
        p.capability['mode']=MODE
        self.assertEqual(p.mode,HEVC_MODE)
        p.play()
        self.assertEqual(p.playback_status()['mode'],HEVC_MODE)
        p.close();p.forget()

    def test_reordered_profile_requires_full_explicit_declaration(self):
        for field,value in [('version',True),('mode',MODE),('bFrames',False),('audio',True),
                            ('inheritsCommonContract',False),('explicitSelectionRequired',False),
                            ('containerHint','webm'),('videoCodec','hevc')]:
            c=Client();old=c.request
            def request(message,timeout=5):
                reply=old(message,timeout);reply['continuousRemux']['profiles'][REORDERED_MODE][field]=value;return reply
            c.request=request;self.assertRaises(RuntimeError,ContinuousRemux,c,str(ROOT),mode=REORDERED_MODE)
            self.assertEqual(len(c.calls),1)

    def test_reordered_mode_is_explicit_fixed_and_does_not_mutate_capability(self):
        c=Client();p=ContinuousRemux(c,str(ROOT),mode=REORDERED_MODE);p.prepare(self.model(),'clip')
        self.assertEqual(c.calls[-1]['mode'],REORDERED_MODE);self.assertEqual(p.mode,REORDERED_MODE)
        self.assertRaises(AttributeError,setattr,p,'mode',MODE)
        p.capability['mode']=MODE;p.play();self.assertEqual(p.mode,REORDERED_MODE)
        self.assertEqual(p.playback_status()['mode'],REORDERED_MODE)
        self.assertFalse(CAP['bFrames']);self.assertTrue(CAP['profiles'][REORDERED_MODE]['bFrames'])

    def test_reordered_lost_reply_recovery_binds_exact_profile(self):
        c=Client();p=ContinuousRemux(c,str(ROOT),mode=REORDERED_MODE);c.drop.add('continuousRemuxStart')
        self.assertRaises(TimeoutError,p.prepare,self.model(),'clip');c.job['mode']=MODE
        self.assertRaises(RuntimeError,p.status);self.assertIsNone(p.job_id)
        c.job['mode']=REORDERED_MODE;p.status();c.drop.add('continuousRemuxPlay')
        self.assertRaises(TimeoutError,p.play);p.playback_status();p.close();p.forget()
        self.assertEqual(sum(m['action']=='continuousRemuxStart' for m in c.calls),1)
        self.assertEqual(sum(m['action']=='continuousRemuxPlay' for m in c.calls),1)

    def test_aac_profile_requires_exact_pcm_window_contract(self):
        changes = [('version', True), ('mode', MODE), ('audio', False), ('videoOnly', True),
            ('audioCodec', 'aac'), ('sampleRates', [48000]), ('channels', [True, 2]),
            ('pcmWindowRequired', False), ('pcmWindowVersion', True), ('initialSkipSamples', 0),
            ('requiredTrackKinds', ['audio']), ('sharedResourceRequired', False),
            ('presentationSamplesRequired', False), ('explicitSelectionRequired', False)]
        for field, value in changes:
            with self.subTest(field=field):
                c=Client();old=c.request
                def request(message,timeout=5):
                    reply=old(message,timeout);reply['continuousRemux']['profiles'][AAC_MODE][field]=value;return reply
                c.request=request
                self.assertRaises(RuntimeError,ContinuousRemux,c,str(ROOT),mode=AAC_MODE)
                self.assertEqual(len(c.calls),1)

    def test_aac_is_explicit_and_preserves_video_defaults(self):
        c=Client();p=ContinuousRemux(c,str(ROOT),mode=AAC_MODE);p.prepare(self.model(),'clip')
        self.assertEqual(c.calls[-1]['mode'],AAC_MODE)
        self.assertTrue(p.capability['audio']);self.assertTrue(p.capability['pcmWindowRequired'])
        self.assertFalse(CAP['audio']);self.assertTrue(CAP['videoOnly'])
        self.assertRaises(AttributeError,setattr,p,'mode',MODE)
        p.heartbeat();self.assertFalse(any(m['action']=='continuousRemuxPlay' for m in c.calls))
        p.play();self.assertEqual(p.playback_status()['mode'],AAC_MODE)
        p.control('stop');p.close();p.forget()

    def test_aac_lost_reply_recovery_rejects_different_mode(self):
        c=Client();p=ContinuousRemux(c,str(ROOT),mode=AAC_MODE);c.drop.add('continuousRemuxStart')
        self.assertRaises(TimeoutError,p.prepare,self.model(),'clip');c.job['mode']=REORDERED_MODE
        self.assertRaises(RuntimeError,p.status);self.assertIsNone(p.job_id)
        c.job['mode']=AAC_MODE;p.status();c.drop.add('continuousRemuxPlay')
        self.assertRaises(TimeoutError,p.play);p.playback_status();p.close();p.forget()
        self.assertEqual(sum(m['action']=='continuousRemuxStart' for m in c.calls),1)
        self.assertEqual(sum(m['action']=='continuousRemuxPlay' for m in c.calls),1)


if __name__=='__main__':unittest.main()
