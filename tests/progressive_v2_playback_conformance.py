"""Real H.264/HEVC schema-2 fMP4 playback while BOTH source files are incomplete."""
import argparse
import copy
import json
from pathlib import Path
import sys
import time
import uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt'/'tools'))
from progressive_client import ProgressiveClient, ApiError
from progressive_v2 import V2PreviewSession
from progressive_v1_conformance import fragment_prefix, reject


def exercise(client,fixtures,work,profile="h264"):
    assert profile in {"h264","hevc8","hevc10"},profile
    codec="h264" if profile=="h264" else "hevc"
    fixture="progressive" if profile=="h264" else "progressive-"+profile
    capabilities=client.request(dict(version=1,action="capabilities"))
    if codec=="hevc":assert "hevc" in capabilities["schema2Playback"]["videoCodecs"],capabilities
    work=Path(work).resolve();work.mkdir(parents=True)
    inputs=[(Path(fixtures)/fixture/name).read_bytes() for name in ('video.mp4','audio.m4a')]
    paths=[work/'видео.part',work/'звук.part'];names=[path.name for path in paths]
    value=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='18446744073709551615',revision='9007199254740993',
        writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='200000000',timestampOffset='0'),
        resources=[],tracks=[])
    for index,kind in enumerate(('video','audio')):
        prefix=fragment_prefix(inputs[index],5);paths[index].write_bytes(inputs[index][:prefix])
        rid='res-'+kind
        value['resources'].append(dict(id=rid,name=names[index],length=None,ranges=[dict(offset='0',length=str(prefix))],state='downloading'))
        value['tracks'].append(dict(id=kind+'-main',kind=kind,required=True,codecHint=codec if not index else 'aac-lc',containerHint='fmp4',initializationReady=True,state='downloading',source=dict(resourceId=rid)))
    probe=client.probe(value,work);assert probe['supported'],probe
    assert client.readers(value['sessionId'],int(value['generation']))==[], 'Probe retained its resource leases'
    preview=V2PreviewSession(client,str(work),title='Schema 2: physically incomplete fMP4')
    report=dict(profile=profile,expectedVideoCodec=codec,schemaVersion=2,generation=value['generation'],initialRevision=value['revision'],networkRequests=0)
    try:
        started=preview.publish(value);assert started['active'] and started['schemaVersion']==2 and started['generation']==value['generation']
        assert started['revision']==value['revision'];reader=preview.reader_id
        again=client.start(value,session_directory=work,activate=False,track_reader=True);assert again['readerId']==reader
        changed=copy.deepcopy(value);changed['writer']['instanceId']=uuid.uuid4().hex
        reject(client,dict(version=1,action='start',session=changed,sessionDirectory=str(work),trackReader=True),'generation_changed')
        detached=client.command('release',value['sessionId'],readerId=reader)
        assert detached['generation']==value['generation'] and detached['revision']==value['revision']
        assert preview.close(timeout=10)['released']
        assert client.readers(value['sessionId'],int(value['generation']))==[]
        preview=V2PreviewSession(client,str(work),title='Schema 2: resumed preview')
        preview.publish(value);assert preview.reader_id!=reader
        reject(client,dict(version=1,action='heartbeat',sessionId=value['sessionId'],readerId=reader),'reader_mismatch')
        report['cancelRestartAndReaderGuards']=True
        for index in range(2):
            prefix=fragment_prefix(inputs[index],12)
            with paths[index].open('ab') as stream:stream.write(inputs[index][stream.tell():prefix])
            value['resources'][index]['ranges']=[dict(offset='0',length=str(prefix))]
        value['revision']=str(int(value['revision'])+1);preview.publish(value)
        deadline=time.monotonic()+22
        while time.monotonic()<deadline:
            try:state=preview.heartbeat()
            except ApiError as error:
                final=client.command('status',value['sessionId'],readerId=preview.reader_id)
                raise AssertionError(('Playback closed before 10 seconds',profile,error.reply,final)) from error
            if state.get('state')=='error':raise AssertionError(state)
            if state.get('state')=='playing' and state['positionSeconds']>=10:
                assert all(paths[i].stat().st_size<len(inputs[i]) for i in range(2))
                assert all(int(state['decoderIoBytes'][kind])>0 for kind in ('video','audio'))
                assert state['videoCodec']==codec and state['mediaMode']==('separate-fmp4-hevc-aac-lc' if codec=='hevc' else 'separate-fmp4-h264-aac-lc'),state
                report['playingWhilePhysicallyIncomplete']=state
                report['incompleteBytes']=[paths[i].stat().st_size for i in range(2)]
                report['fullBytes']=list(map(len,inputs));break
            time.sleep(.1)
        else:raise AssertionError(('No playback to 10 seconds while incomplete',state))
        for index in range(2):
            final=paths[index].with_suffix('.done');paths[index].rename(final);paths[index]=final
            with final.open('ab') as stream:stream.write(inputs[index][stream.tell():])
            value['resources'][index].update(length=str(len(inputs[index])),ranges=[dict(offset='0',length=str(len(inputs[index])))],state='complete')
            value['tracks'][index]['state']='complete'
        value['writer']['state']='complete';value['revision']=str(int(value['revision'])+1)
        preview.publish(value)
        deadline=time.monotonic()+20
        while time.monotonic()<deadline:
            state=preview.status()
            if state.get('state')=='error':raise AssertionError(state)
            if state.get('state')=='finished' and state.get('released'):
                assert state['revision']==value['revision'] and state['generation']==value['generation'];report['terminal']=state;break
            time.sleep(.1)
        else:raise AssertionError(('No natural schema-2 finish',state))
        report['release']=preview.close(timeout=5);assert report['release']['released']
        assert client.readers(value['sessionId'],int(value['generation']))==[]
        report['result']='PASS';return report
    finally:
        if not preview.closed:
            try:preview.close(timeout=5)
            except (OSError,RuntimeError):pass


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--media-profile',choices=['h264','hevc8','hevc10'],default='h264');parser.add_argument('--player',required=True);parser.add_argument('--profile');parser.add_argument('--fixtures',required=True);parser.add_argument('--work',required=True);parser.add_argument('--expected-pid',type=int)
    args=parser.parse_args();client=ProgressiveClient(args.player,args.profile)
    if args.expected_pid is not None:assert client.request(dict(version=1,action='capabilities'))['pid']==args.expected_pid
    print(json.dumps(exercise(client,args.fixtures,args.work,args.media_profile)))
