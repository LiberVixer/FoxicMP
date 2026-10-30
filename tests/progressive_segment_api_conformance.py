"""Player-owned single-segment diagnostic; never a playback start command."""
import argparse,copy,hashlib,json,os,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_client import ProgressiveClient,ApiError
from progressive_resources import ResourceProbe
from progressive_integrity import protect_snapshot
from progressive_segment_conformance import snapshot,boxes


def exercise(client,work,fixtures):
    root=Path(work).resolve();root.mkdir(parents=True)
    for name,data in [('init',b'INIT'),('seg1',b'FIRST-SEGMENT'),('seg2',b'SECOND-SEGMENT')]: (root/(name+'.part')).write_bytes(data)
    value=snapshot();probe=ResourceProbe(client);checks=[];reports=[]
    assert client.request(dict(version=1,action='capabilities'))['segmentProbeVersion']==1
    def reject(packet,code):
        try:client.request(packet)
        except ApiError as error:assert error.code==code,error.reply
        else:raise AssertionError(('Accepted invalid segment command',packet))
    def finish():
        reply=probe.wait();assert reply['released'] and reply['playbackSupported'] is False and reply['mode']=='resource-io-only',reply
        for reader in reply['readerIds']:
            info=client.reader_status(reader);assert info['released'] and info['reason']=='files-closed',info
        assert probe.forget()['state']=='forgotten';reports.append(reply);return reply
    try:
        pending=copy.deepcopy(value);pending['resources'][1].update(state='downloading',ranges=[])
        started=probe.start_segment(pending,str(root),'video',0,demux=False,wait_milliseconds=5000)
        assert started['playbackSupported'] is False;checks.append('asynchronous-staging-without-playback')
        replay=dict(version=1,action='resourceProbe',session=pending,sessionDirectory=str(root),reads=[],segment=dict(trackId='video',index='0',demux=False),waitMilliseconds=5000,requestId=probe.request_id)
        assert client.request(replay)['probeId']==probe.probe_id
        conflict=copy.deepcopy(replay);conflict['segment']['index']='1';reject(conflict,'probe_conflict');checks.append('idempotence-includes-segment-selector')
        reject(dict(version=1,action='resourceProbe',session=value,sessionDirectory=str(root),reads=[],segment=dict(trackId='video',index='1',demux=False)),'probe_busy')
        reject(dict(version=1,action='resourceProbeForget',probeId=probe.probe_id),'reader_in_use');checks.append('active-probe-cannot-be-replaced-or-forgotten')
        for _ in range(20):assert client.request(dict(version=1,action='capabilities'))['segmentProbeVersion']==1
        checks.append('control-api-responsive-during-segment-wait')
        bad=copy.deepcopy(pending);bad.update(generation='2',revision='2')
        reject(dict(version=1,action='resourceProbeUpdate',probeId=probe.probe_id,session=bad),'generation_changed')
        pending['revision']='2';pending['resources'][1]=copy.deepcopy(value['resources'][1]);probe.publish(pending)
        reply=finish();assert reply['state']=='finished' and reply['allReadable'] and reply['appliedRevision']=='2',reply
        assert reply['segment']['sha256']==hashlib.sha256(b'INITFIRST-SEGMENT').hexdigest() and reply['segment']['packetCount']==0,reply
        assert 'bytes' not in reply['segment'] and 'hex' not in reply['segment'];checks.append('publication-stages-verified-bytes-and-closes-leases')
        probe.start_segment(value,str(root),'video',1,demux=False);reply=finish()
        assert reply['segment']['initializationResourceId']=='init' and reply['segment']['sha256']==hashlib.sha256(b'INITSECOND-SEGMENT').hexdigest();checks.append('next-segment-inherits-initialization')
        probe.start_segment(value,str(root),'video',2,demux=False,wait_milliseconds=5000)
        reject(dict(version=1,action='resourceProbeCancel',probeId='f'*32),'unknown_probe')
        before=time.monotonic();probe.cancel();reply=finish();assert reply['state']=='cancelled' and not reply['allReadable'] and time.monotonic()-before<2,reply;checks.append('cancel-closes-a-pending-segment-without-false-ready')
        probe.start_segment(value,str(root),'video',2,demux=False,wait_milliseconds=40);reply=finish()
        assert reply['state']=='finished' and reply['segment']['code']=='waiting-segment' and not reply['allReadable'];checks.append('missing-future-segment-timeout-is-not-eof')
        large=copy.deepcopy(value);large['resources'][1].update(length='16777217',ranges=[dict(offset='0',length='16777217')])
        probe.start_segment(large,str(root),'video',0,demux=False);reply=finish()
        assert reply['state']=='error' and reply['errorCode']=='segment-too-large' and reply['segment']['bytesStaged']==0 and not reply['allReadable'],reply;checks.append('oversize-is-an-error-before-io')
        protected=protect_snapshot(value,root);(root/'seg1.part').write_bytes(b'XIRST-SEGMENT')
        probe.start_segment(protected,str(root),'video',0,demux=False);reply=finish()
        assert reply['state']=='error' and reply['errorCode']=='integrity-mismatch' and reply['segment']['bytesStaged']==0 and not reply['allReadable'],reply;checks.append('integrity-failure-releases-files-and-discards-output')
        for kind,name in [('video','video.mp4'),('audio','audio.m4a')]:
            content=(Path(fixtures)/'progressive'/name).read_bytes();top=list(boxes(content));moof=next(i for i,(_,_,t) in enumerate(top) if t==b'moof')
            start=top[moof][0];end=top[moof+1][0]+top[moof+1][1];init,part=content[:start],content[start:end]
            (root/'init.part').write_bytes(init);(root/'seg1.part').write_bytes(part)
            actual=copy.deepcopy(value);actual['resources'][0].update(length=str(len(init)),ranges=[dict(offset='0',length=str(len(init)))])
            actual['resources'][1].update(length=str(len(part)),ranges=[dict(offset='0',length=str(len(part)))])
            actual['tracks'][0].update(kind=kind,codecHint='h264' if kind=='video' else 'aac-lc');actual=protect_snapshot(actual,root)
            probe.start_segment(actual,str(root),'video',0,demux=True);reply=finish()
            assert reply['state']=='finished' and reply['allReadable'] and reply['segment']['packetCount']>0 and reply['segment']['sha256']==hashlib.sha256(init+part).hexdigest(),reply
            checks.append('real-'+kind+'-fmp4-index-through-player-ipc')
        return dict(passed=len(checks),checks=checks,playbackSupported=False,reports=reports)
    finally:
        if probe.identity is not None and not probe.acknowledged:
            try:probe.cancel();probe.wait();probe.forget()
            except (OSError,RuntimeError):pass


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile');p.add_argument('--work',required=True);p.add_argument('--fixtures',required=True);a=p.parse_args()
    print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.work,a.fixtures)))
