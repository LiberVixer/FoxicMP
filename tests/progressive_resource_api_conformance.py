"""Same asynchronous resource-I/O diagnostic contract against all player editions."""
import copy
import json
from pathlib import Path
import sys
import time
import uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt'/'tools'))
from progressive_client import ApiError
from progressive_resources import ResourceProbe
from progressive_resource_conformance import snapshot


def exercise(client,work):
    work=Path(work).resolve();work.mkdir(parents=True);media=work/'media.part';media.write_bytes(b'0123456789abcdef')
    value=snapshot();value['sessionId']=uuid.uuid4().hex
    probe=ResourceProbe(client);checks=[]
    def reject(message,code):
        try:client.request(message)
        except ApiError as error:assert error.code==code,error.reply
        else:raise AssertionError(('Accepted invalid command',message))
    def finish():
        reply=probe.wait();assert reply['released'] and reply['playbackSupported'] is False
        for reader in reply['readerIds']:
            status=client.reader_status(reader);assert status['released'] and status['reason']=='files-closed'
        ids=reply['readerIds'];assert probe.forget()['state']=='forgotten'
        for reader in ids:reject(dict(version=1,action='readerStatus',readerId=reader),'unknown_reader')
        return reply
    query=lambda offset,count:dict(resourceId='media',offset=str(offset),length=str(count))
    try:
        began=time.monotonic();reply=probe.start(value,str(work),[query(0,4),query(4,4)],wait_milliseconds=5000)
        assert time.monotonic()-began<2 and reply['playbackSupported'] is False;checks.append('asynchronous-start-without-playback')
        repeated=dict(version=1,action='resourceProbe',session=value,sessionDirectory=str(work),reads=[query(0,4),query(4,4)],waitMilliseconds=5000,requestId=probe.request_id)
        assert client.request(repeated)['probeId']==probe.probe_id
        conflict=copy.deepcopy(repeated);conflict['reads']=[query(0,1)];reject(conflict,'probe_conflict');checks.append('idempotent-request-token-prevents-conflicting-replay')
        reject(dict(version=1,action='resourceProbe',session=value,sessionDirectory=str(work),reads=[query(0,4)]),'probe_busy')
        reject(dict(version=1,action='resourceProbeForget',probeId=probe.probe_id),'reader_in_use');checks.append('active-job-cannot-be-replaced-or-forgotten')
        for _ in range(20):assert client.request(dict(version=1,action='capabilities'))['resourceIoProbeVersion']==1
        checks.append('control-api-responsive-during-wait')
        wrong=copy.deepcopy(value);wrong['generation']='2';wrong['revision']='2'
        reject(dict(version=1,action='resourceProbeUpdate',probeId=probe.probe_id,session=wrong),'generation_changed');checks.append('stale-writer-cannot-rebind-source')
        value['revision']='2';value['resources'][0]['ranges']=[dict(offset='0',length='8')];probe.publish(value)
        result=finish();assert result['state']=='finished' and result['allReadable'] and [r['bytesRead'] for r in result['reads']]==[4,4]
        assert result['appliedRevision']=='2';checks.append('publication-wakes-real-range-read-and-releases-leases')
        value['revision']='3';probe.start(value,str(work),[query(8,4)],wait_milliseconds=5000)
        reject(dict(version=1,action='resourceProbeCancel',probeId='f'*32),'unknown_probe')
        began=time.monotonic();probe.cancel();result=finish();assert result['state']=='cancelled' and time.monotonic()-began<2;checks.append('cancellation-closes-waiting-source')
        probe.start(value,str(work),[query(8,4)],wait_milliseconds=40);result=finish()
        assert result['reads'][0]['code']=='waiting-range' and not result['allReadable'];checks.append('timeout-is-not-eof-or-ready')
        probe.start(value,str(work),[query(0,16)]);result=finish();assert result['reads'][0]['bytesRead']==8 and not result['allReadable'];checks.append('short-read-does-not-claim-full-availability')
        value['revision']='4';value['writer']['state']='complete';value['resources'][0].update(state='complete',ranges=[dict(offset='0',length='16')]);value['tracks'][0]['state']='complete'
        probe.start(value,str(work),[query(16,1)]);result=finish();assert result['reads'][0]['code']=='eof';checks.append('explicit-complete-is-the-only-eof')
        assert media.read_bytes()==b'0123456789abcdef';checks.append('media-never-modified')
        return dict(passed=len(checks),checks=checks,playbackSupported=False)
    finally:
        if probe.identity is not None and not probe.acknowledged:
            try:probe.cancel();probe.wait();probe.forget()
            except (OSError,RuntimeError):pass


if __name__=='__main__':
    import argparse
    from progressive_client import ProgressiveClient
    parser=argparse.ArgumentParser();parser.add_argument('--player',required=True);parser.add_argument('--profile');parser.add_argument('--work',required=True);parser.add_argument('--expected-pid',type=int)
    args=parser.parse_args();client=ProgressiveClient(args.player,args.profile)
    if args.expected_pid is not None:assert client.request(dict(version=1,action='capabilities'))['pid']==args.expected_pid
    print(json.dumps(exercise(client,args.work)))
