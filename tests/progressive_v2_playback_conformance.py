"""Real schema-2 fMP4/WebM playback while BOTH source files are incomplete."""
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
from progressive_webm_fixtures import webm_prefix


MEDIA_PROFILES=('h264','hevc8','hevc10','av1-8','av1-10','vp9-8','vp9-10',
    'h264-opus','h264-opus-mono','hevc8-opus','hevc10-opus','av1-8-opus','av1-10-opus','vp9-8-opus','vp9-10-opus','webm-vp9-8','webm-vp9-10','webm-av1-8','webm-av1-10','mixed-h264-opus','mixed-hevc8-opus','mixed-hevc10-opus','mixed-av1-8-opus','mixed-av1-10-opus','mixed-vp9-8-opus','mixed-vp9-10-opus','mixed-webm-vp9-8-aac','mixed-webm-vp9-8-opus','mixed-webm-vp9-10-aac','mixed-webm-vp9-10-opus','mixed-webm-av1-8-aac','mixed-webm-av1-8-opus','mixed-webm-av1-10-aac','mixed-webm-av1-10-opus','webm-vp8-opus','webm-vp8-vorbis','webm-vp8-vorbis-mono','webm-vp9-8-vorbis','webm-vp9-10-vorbis','webm-av1-8-vorbis','webm-av1-10-vorbis','mixed-h264-vorbis','mixed-hevc8-vorbis','mixed-hevc10-vorbis','mixed-av1-8-vorbis','mixed-av1-10-vorbis','mixed-vp9-8-vorbis','mixed-vp9-10-vorbis','mixed-webm-vp8-aac','mixed-webm-vp8-opus')


def forget_closed_readers(client,snapshot):
    # SDK forget is best effort. A retained receipt must prove actual file close
    # before the stand can remove it; an active reader is always a failure.
    remaining=client.readers(snapshot['sessionId'],int(snapshot['generation']))
    for reader in remaining:
        assert reader.get('released') and reader.get('filesClosed'),remaining
        client.forget_reader(reader['readerId'])
    assert client.readers(snapshot['sessionId'],int(snapshot['generation']))==[]


def exercise(client,fixtures,work,profile="h264",seek=None):
    mixed=profile.startswith("mixed-")
    normalized=profile.removeprefix("mixed-")
    webm=normalized.startswith("webm-")
    video_container="webm" if webm else "fmp4"
    audio_container=("fmp4" if webm else "webm") if mixed else video_container
    base=normalized.removeprefix("webm-").removesuffix("-opus").removesuffix("-opus-mono").removesuffix("-aac").removesuffix("-vorbis").removesuffix("-vorbis-mono")
    assert base in {"h264","hevc8","hevc10","av1-8","av1-10","vp9-8","vp9-10","vp8"},profile
    audio_codec="vorbis" if "-vorbis" in profile else "opus" if audio_container=="webm" or "-opus" in profile else "aac-lc"
    channels=1 if profile.endswith("-mono") else 2
    codec="vp8" if base=="vp8" else "vp9" if base.startswith("vp9-") else "av1" if base.startswith("av1-") else "h264" if base=="h264" else "hevc"
    fixture="progressive-webm-"+base if webm else "progressive" if base=="h264" else "progressive-"+base
    capabilities=client.request(dict(version=1,action="capabilities"))
    if codec!="h264":assert codec in capabilities["schema2Playback"]["videoCodecs"],capabilities
    if mixed:assert capabilities["schema2Playback"]["mixedContainers"],capabilities
    work=Path(work).resolve();work.mkdir(parents=True)
    audio_path=Path(fixtures)/('progressive-opus-'+str(channels))/'audio.mp4' if audio_codec=='opus' else Path(fixtures)/'progressive/audio.m4a'
    if audio_container=="webm":audio_path=Path(fixtures)/("progressive-webm-vorbis-"+str(channels)+"/audio.webm" if audio_codec=="vorbis" else "progressive-webm-vp9-8/audio.webm")
    inputs=[(Path(fixtures)/fixture/('video.webm' if webm else 'video.mp4')).read_bytes(),audio_path.read_bytes()]
    containers=[video_container,audio_container]
    def prefix_of(data,seconds,index):
        return (webm_prefix if containers[index]=="webm" else fragment_prefix)(data,seconds)
    if audio_codec in ('opus','vorbis'):assert audio_codec in capabilities['schema2Playback']['audioCodecs'],capabilities
    paths=[work/'видео.part',work/'звук.part'];names=[path.name for path in paths]
    value=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='18446744073709551615',revision='9007199254740993',
        writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='200000000',timestampOffset='0'),
        resources=[],tracks=[])
    for index,kind in enumerate(('video','audio')):
        prefix=prefix_of(inputs[index],5,index);paths[index].write_bytes(inputs[index][:prefix])
        rid='res-'+kind
        value['resources'].append(dict(id=rid,name=names[index],length=None,ranges=[dict(offset='0',length=str(prefix))],state='downloading'))
        value['tracks'].append(dict(id=kind+'-main',kind=kind,required=True,codecHint=codec if not index else audio_codec,containerHint=containers[index],initializationReady=True,state='downloading',source=dict(resourceId=rid)))
    probe=client.probe(value,work);assert probe['supported'],probe
    assert client.readers(value['sessionId'],int(value['generation']))==[], 'Probe retained its resource leases'
    preview=V2PreviewSession(client,str(work),title='Schema 2: physically incomplete fMP4')
    report=dict(profile=profile,expectedVideoCodec=codec,expectedAudioCodec=audio_codec,schemaVersion=2,generation=value['generation'],initialRevision=value['revision'],networkRequests=0)
    try:
        started=preview.publish(value);assert started['active'] and started['schemaVersion']==2 and started['generation']==value['generation']
        assert started['revision']==value['revision'];reader=preview.reader_id
        again=client.start(value,session_directory=work,activate=False,track_reader=True);assert again['readerId']==reader
        changed=copy.deepcopy(value);changed['writer']['instanceId']=uuid.uuid4().hex
        reject(client,dict(version=1,action='start',session=changed,sessionDirectory=str(work),trackReader=True),'generation_changed')
        detached=client.command('release',value['sessionId'],readerId=reader)
        assert detached['generation']==value['generation'] and detached['revision']==value['revision']
        assert preview.close(timeout=10)['released']
        forget_closed_readers(client,value)
        preview=V2PreviewSession(client,str(work),title='Schema 2: resumed preview')
        preview.publish(value);assert preview.reader_id!=reader
        reject(client,dict(version=1,action='heartbeat',sessionId=value['sessionId'],readerId=reader),'reader_mismatch')
        report['cancelRestartAndReaderGuards']=True
        for index in range(2):
            prefix=prefix_of(inputs[index],12,index)
            with paths[index].open('ab') as stream:stream.write(inputs[index][stream.tell():prefix])
            value['resources'][index]['ranges']=[dict(offset='0',length=str(prefix))]
        value['revision']=str(int(value['revision'])+1);preview.publish(value)
        seek_requested=False;seek_observed=False;observations=[];clock=time.monotonic()
        deadline=time.monotonic()+26
        while time.monotonic()<deadline:
            before=time.monotonic()
            try:state=preview.heartbeat()
            except ApiError as error:
                final=client.command('status',value['sessionId'],readerId=preview.reader_id)
                raise AssertionError(('Playback closed before 10 seconds',profile,error.reply,final)) from error
            observations.append(dict(elapsed=round(time.monotonic()-clock,3),latency=round(time.monotonic()-before,3),state=state.get('state'),position=state.get('positionSeconds')))
            observations=observations[-80:]
            if state.get('state')=='error':raise AssertionError(state)
            if (audio_codec in ('opus','vorbis') or mixed) and seek is not None:
                if seek_requested and state.get('positionSeconds',10)<3:seek_observed=True
                if not seek_requested and state.get('state')=='playing' and state.get('positionSeconds',0)>=4:
                    seek(2);seek_requested=True;continue
            if state.get('state')=='playing' and state['positionSeconds']>=10:
                assert all(paths[i].stat().st_size<len(inputs[i]) for i in range(2))
                assert all(int(state['decoderIoBytes'][kind])>0 for kind in ('video','audio'))
                assert state['videoContainer']==video_container and state['audioContainer']==audio_container,state
                assert state['videoCodec']==codec and state['mediaMode']==('separate-'+video_container+('-'+audio_container if mixed else '')+'-'+codec+'-'+audio_codec) and state['audioCodec']==audio_codec,state
                if (audio_codec in ('opus','vorbis') or mixed) and seek is not None:
                    assert seek_requested and seek_observed,'Backward seek was not observed'
                    report['backwardSeekWhileIncomplete']=True
                report['playingWhilePhysicallyIncomplete']=state
                report['incompleteBytes']=[paths[i].stat().st_size for i in range(2)]
                report['fullBytes']=list(map(len,inputs));break
            time.sleep(.1)
        else:raise AssertionError(('No playback to 10 seconds while incomplete',state,observations))
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
        forget_closed_readers(client,value)
        report['result']='PASS';return report
    finally:
        if not preview.closed:
            try:preview.close(timeout=5)
            except (OSError,RuntimeError):pass


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--media-profile',choices=MEDIA_PROFILES,default='h264');parser.add_argument('--player',required=True);parser.add_argument('--profile');parser.add_argument('--fixtures',required=True);parser.add_argument('--work',required=True);parser.add_argument('--expected-pid',type=int)
    args=parser.parse_args();client=ProgressiveClient(args.player,args.profile)
    if args.expected_pid is not None:assert client.request(dict(version=1,action='capabilities'))['pid']==args.expected_pid
    print(json.dumps(exercise(client,args.fixtures,args.work,args.media_profile)))
