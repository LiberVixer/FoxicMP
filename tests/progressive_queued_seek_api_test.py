"""One pending continuous seek: supersede/cancel/publish without playback polling."""
import copy
import json
from fractions import Fraction
import time
import sys
import subprocess
from progressive_continuous import ContinuousRemux, AAC_MODE, MODE, REORDERED_MODE
from progressive_continuous_aac_controller_test import av_snapshot
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_continuous_worker_crash_conformance import OwnedWorker
from progressive_player_crash_recovery import ApiError,sha,wait
from progressive_stream_aac_window_test import PROFILES
from progressive_stream_aac_aligned_test import fixture as av_fixture
from progressive_stream_remux_worker_test import fixture as video_fixture,probe


def exercise(peers,root,audio_output='null',mode=AAC_MODE):
    rows=[]
    profiles=PROFILES if mode==AAC_MODE else [('reordered' if mode==REORDERED_MODE else 'video',None,None,None,None,None,None)]
    def make(label,profile):
        folder=root/label;folder.mkdir()
        if mode==AAC_MODE:
            _,rate,channels,fps,bframes,first,n=profile
            original,_=av_fixture(folder,'original',rate,channels,fps,bframes,first,False,seconds=14.137,samples=n if n is not None else rate*14+137)
            raw=original.read_bytes();prefix=int(next(p['pos'] for p in probe(original)['packets'] if p['codec_type']=='video' and Fraction(p['pts_time'])>=12))+13
            path=folder/'input.part';model=av_snapshot(path,raw,prefix);model['timeline']['duration']='143000000'
        else:
            original=video_fixture(folder,'original',bframes=2 if mode==REORDERED_MODE else 0);raw=original.read_bytes();path=folder/'input.part';model=snapshot(path,raw,prefix=len(raw)*2//3,complete=False);model['timeline']['duration']='240000000'
        path.write_bytes(raw);sdk=ContinuousRemux(peers[0].client,str(folder),mode=mode);sdk.prepare(model,'clip')
        def publish(failed=False):
            old=model;new=copy.deepcopy(old);new['revision']=str(int(old['revision'])+1);new['resources'][0]['ranges']=[dict(offset='0',length=str(len(raw)))]
            if failed:new['tracks'][0]['state']='failed'
            return sdk.publish(protect_snapshot(new,folder,previous=old))
        def poll(fn,label):
            def check():
                s=sdk.status();(folder/'last-producer.json').write_text(json.dumps(s,indent=2));assert s['state']!='error',s
                if not s['outputComplete']:sdk.heartbeat()
                return fn()
            return wait(check,label,20)
        poll(lambda:(lambda s:s if s['playbackReady'] and s['indexedEndSeconds']>2 else None)(sdk.status()),'queued ready')
        sdk.play(title='Queued '+label)
        def initial_playback():
            s=sdk.playback_status();(folder/'last-playback.json').write_text(json.dumps(s,indent=2));return s if s['state']=='playing' and s['positionSeconds']>.15 else None
        poll(initial_playback,'queued playing')
        sdk.control('pause');held=poll(lambda:(lambda s:s if s['state']=='paused' else None)(sdk.playback_status()),'queued pause')
        return sdk,path,held,publish,poll
    for profile in profiles:
        sdk,path,held,publish,poll=make(profile[0],profile);original_sha=sha(path);transitions=[]
        target=12.5 if mode==AAC_MODE else 21.
        try:
            assert held['indexedEndSeconds']<target and held['remainingGrowingSeeks']==16,held
            queued=sdk.control('seek',seconds=target,allow_growing=True,wait_for_range=True)
            assert queued['pendingSeekSeconds']==target and queued['seekState']=='waiting-range' and queued['seekRevision']=='1',queued
            time.sleep(.15);still=sdk.playback_status();assert still['state']=='paused' and abs(still['positionSeconds']-held['positionSeconds'])<.15 and still['remainingGrowingSeeks']==16,still
            superseded=sdk.control('seek',seconds=target+.25,allow_growing=True,wait_for_range=True)
            assert superseded['pendingSeekSeconds']==target+.25 and superseded['seekRevision']=='2',superseded
            try:sdk.control('seek',seconds=3599,allow_growing=True,wait_for_range=True)
            except ApiError as e:assert e.code=='invalid_request',e.reply
            else:raise AssertionError('Invalid queue target accepted')
            preserved=sdk.playback_status();assert preserved['pendingSeekSeconds']==target+.25 and preserved['seekRevision']=='2',preserved
            cancelled=sdk.control('cancelSeek');assert cancelled['pendingSeekSeconds'] is None and cancelled['seekState']=='cancelled' and cancelled['remainingGrowingSeeks']==16,cancelled
            sdk.control('seek',seconds=target,allow_growing=True,wait_for_range=True)
            publish()
            # Only remux status/heartbeats here: an application timer must apply it.
            poll(lambda:sdk.status()['indexedEndSeconds']>target,'queued publication')
            time.sleep(.5)
            applied=poll(lambda:(lambda s:s if s['seekState']=='applied' and s['state']=='paused' and abs(s['positionSeconds']-(target))<.12 else None)(sdk.playback_status()),'queued applied')
            assert applied['pendingSeekSeconds'] is None and applied['seekRevision']=='3' and applied['remainingGrowingSeeks']==15 and applied['playbackId']==held['playbackId'] and not applied['outputComplete'],applied
            transitions.append(applied)
            # Restore headroom before testing a running seek; Classic retains
            # its ordinary five-second resume buffer near an incomplete tail.
            sdk.control('seek',seconds=1,allow_growing=True,wait_for_range=True)
            rewind=poll(lambda:(lambda s:s if s['seekState']=='applied' and s['state']=='paused' and abs(s['positionSeconds']-1)<.12 else None)(sdk.playback_status()),'queued paused rewind')
            assert rewind['remainingGrowingSeeks']==14,rewind
            transitions.append(rewind)
            sdk.control('play');poll(lambda:sdk.playback_status()['state']=='playing','queued resume')
            sdk.control('seek',seconds=2,allow_growing=True,wait_for_range=True)
            moving=poll(lambda:(lambda s:s if s['seekState']=='applied' and s['state']=='playing' and 2.15<s['positionSeconds']<3 else None)(sdk.playback_status()),'queued playing commit')
            assert moving['remainingGrowingSeeks']==13 and moving['pendingSeekSeconds'] is None,moving
            transitions.append(moving)
            receipt=sdk.close(timeout=2);assert receipt['released'] and receipt['sourceFilesClosed'],receipt
            forgotten=sdk.forget(timeout=2);assert forgotten['forgotten'] and sha(path)==original_sha,forgotten
            rows.append(dict(profile=profile[0],result='PASS',queued=queued,superseded=superseded,cancelled=cancelled,transitions=transitions,release=receipt,forgotten=forgotten,sourceSha256=original_sha))
        finally:
            if not sdk.forgotten:sdk.close(timeout=5);sdk.forget(timeout=5)
    # Terminal events must discard the queued target, without consuming a seek.
    for ending in ('stop','cancel','replace','worker-crash','producer-error'):
        sdk,path,held,publish,poll=make('pending-'+ending,profiles[0])
        original_sha=sha(path)
        try:
            target=12.5 if mode==AAC_MODE else 21.
            sdk.control('seek',seconds=target,allow_growing=True,wait_for_range=True)
            if ending=='stop':
                terminal=sdk.control('stop');assert terminal['pendingSeekSeconds'] is None and terminal['remainingGrowingSeeks']==16,terminal
            elif ending=='replace':
                # Continuous controller intentionally retains only one job.
                # Replace with the ordinary local playback API, not a second remux.
                folder=root/'replacement';folder.mkdir();base=video_fixture(folder,'ordinary-source');replacement=folder/'ordinary.mp4'
                subprocess.run(['ffmpeg','-v','error','-y','-i',str(base),'-c','copy','-movflags','+frag_keyframe+empty_moov+default_base_moof',str(replacement)],check=True,capture_output=True)
                raw=replacement.read_bytes()
                model=snapshot(replacement,raw,complete=True);model['timeline']['duration']='240000000'
                model['tracks'][0].update(containerHint='fmp4',codecHint='h264',initializationReady=True)
                client=peers[0].client;client.request(dict(version=1,action='start',session=model,sessionDirectory=str(folder),replace=True))
                def regular():
                    reply=client.request(dict(version=1,action='heartbeat',sessionId=model['sessionId']))
                    return reply if reply.get('active') and reply['state']=='playing' and reply['positionSeconds']>.15 else None
                terminal=wait(regular,'ordinary replacement playing',20)
                closed=wait(lambda:(lambda s:s if s['state']=='closed' and s['pendingSeekSeconds'] is None else None)(sdk.playback_status()),'replacement drops old queue',5)
                assert closed['remainingGrowingSeeks']==16,closed
                observation=folder/'old-stop-observations.json'
                observation.write_text(json.dumps(dict(before=terminal,oldPlayback=closed,samples=[]),indent=2))
                sdk.control('stop')
                samples=[]
                def advances():
                    reply=client.request(dict(version=1,action='heartbeat',sessionId=model['sessionId']))
                    samples.append(dict(at=time.monotonic(),status=reply))
                    observation.write_text(json.dumps(dict(before=terminal,oldPlayback=closed,samples=samples),indent=2))
                    assert reply.get('active') and reply['state']=='playing' and reply['sessionId']==terminal['sessionId'],reply
                    return reply if reply['positionSeconds']>=terminal['positionSeconds']+.25 else None
                continued=wait(advances,'ordinary replacement advances after old stop',2)
                client.request(dict(version=1,action='cancel',sessionId=model['sessionId']))
                receipts=wait(lambda:(lambda rs:rs if rs and all(r['released'] and r['filesClosed'] for r in rs) else None)(client.readers(model['sessionId'],1)),'ordinary files closed',2)
                for receipt in receipts:assert client.forget_reader(receipt['readerId'])['forgotten']
                terminal=dict(terminal,oldPlayback=closed,continued=continued,receipts=receipts,oldStopSamples=samples)
            elif ending=='worker-crash':
                watch=OwnedWorker(peers[0],sdk.status()['workerId'])
                try:killed=watch.terminate()
                finally:watch.close()
                terminal=wait(lambda:(lambda s:s if s['state']=='error' and s['pendingSeekSeconds'] is None else None)(sdk.playback_status()),'crash drops queue',10)
                terminal=dict(terminal,killedWorker=killed)
            elif ending=='producer-error':
                publish(failed=True)
                terminal=wait(lambda:(lambda s:s if s['state']=='error' and s['pendingSeekSeconds'] is None else None)(sdk.playback_status()),'producer error drops queue',10)
            else:terminal={}
            receipt=sdk.close(timeout=2);assert receipt['released'] and receipt['sourceFilesClosed'],receipt
            if ending=='cancel':
                terminal=sdk.playback_status();wait(lambda:sdk.playback_status()['pendingSeekSeconds'] is None,'cancel drops queue',5)
            forgotten=sdk.forget(timeout=2);assert forgotten['forgotten'] and sha(path)==original_sha,forgotten
            rows.append(dict(profile='pending-'+ending,result='PASS',terminal=terminal,release=receipt,forgotten=forgotten))
        finally:
            primary_failure=sys.exc_info()[0] is not None
            if not sdk.forgotten:
                try:sdk.close(timeout=5);sdk.forget(timeout=5)
                except Exception:
                    if not primary_failure:raise
    return dict(result='PASS',count=len(rows),rows=rows,publicMode=mode,privateAudioOutputs=True)
