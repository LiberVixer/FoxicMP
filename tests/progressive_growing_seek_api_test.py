"""Bounded explicit growing seek, production Window/main and Classic graph."""
from progressive_continuous_aac_api_test import exercise as aac_exercise
from progressive_continuous import ContinuousRemux
from progressive_backend_demux_conformance import snapshot
from progressive_player_crash_recovery import wait,sha
from progressive_stream_remux_worker_test import fixture

def exercise(peers,root,audio_output='null'):
    result=aac_exercise(peers,root,audio_output,growing_seek=True)
    original=fixture(root,'seek-abort-reference');raw=original.read_bytes()
    for mode in ('stop','cancel'):
        folder=root/('growing-seek-'+mode);folder.mkdir();path=folder/'input.part'
        model=snapshot(path,raw,prefix=len(raw)*2//3,complete=False);model['timeline']['duration']='240000000';path.write_bytes(raw)
        sdk=ContinuousRemux(peers[0].client,str(folder));sdk.prepare(model,'clip')
        try:
            wait(lambda:sdk.status()['playbackReady'],'seek-abort ready',20);sdk.play()
            wait(lambda:(lambda s:s if s['state']=='playing' and s['positionSeconds']>.15 else None)(sdk.playback_status()),'seek-abort playing',20)
            sdk.control('pause');wait(lambda:sdk.playback_status()['state']=='paused','seek-abort paused',5)
            accepted=sdk.control('seek',seconds=2,allow_growing=True)
            if mode=='stop':sdk.control('stop')
            receipt=sdk.close(timeout=2);assert receipt['released'] and receipt['sourceFilesClosed'],receipt
            forgotten=sdk.forget(timeout=2);assert forgotten['forgotten'],forgotten
            assert sha(path)==sha(original)
            result['rows'].append(dict(profile='growing-seek-immediate-'+mode,result='PASS',seekReply=accepted,release=receipt,forgotten=forgotten))
        finally:
            if not sdk.forgotten:
                sdk.close(timeout=5);sdk.forget(timeout=5)
    result['count']=len(result['rows']);return result
