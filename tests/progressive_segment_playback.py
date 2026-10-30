"""Player-only producer for stable-config segmented H.264/HEVC/AV1/VP9 + AAC/Opus fMP4 VOD."""
import argparse,copy,json,os,subprocess,sys,time,uuid
from fractions import Fraction
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_integrity import protect_snapshot
from progressive_v2 import V2PreviewSession
from progressive_client import ProgressiveClient
from progressive_segment_conformance import boxes
from progressive_v2_playback_conformance import forget_closed_readers


VIDEO_PROFILES=('h264','hevc8','hevc10','av1-8','av1-10','vp9-8','vp9-10')
PLAYBACK_PROFILES=VIDEO_PROFILES+tuple(p+'-opus' for p in VIDEO_PROFILES)+('h264-opus-mono',)
NATIVE_PROFILES=VIDEO_PROFILES+('h264-opus','h264-opus-mono','vp9-10-opus')

def profile_info(profile):
    assert profile in PLAYBACK_PROFILES,profile
    base=profile.removesuffix('-opus-mono').removesuffix('-opus')
    video='av1' if base.startswith('av1-') else 'vp9' if base.startswith('vp9-') else 'h264' if base=='h264' else 'hevc'
    return base,video,'opus' if '-opus' in profile else 'aac-lc',1 if '-opus' not in profile or profile.endswith('-mono') else 2


def fragment_durations(fragment):
    # ffprobe may report a full AAC frame for the final shortened trun sample.
    # Use explicit ISO sample durations for the packet envelope, retaining
    # ffprobe's independent packet positions/timestamps.
    durations=[]
    moof=next((at,n) for at,n,typ in boxes(fragment) if typ==b'moof')
    payload=fragment[moof[0]+8:moof[0]+moof[1]]
    for at,n,typ in boxes(payload):
        if typ!=b'traf':continue
        traf=payload[at+8:at+n];default=0
        for x,size,typ in boxes(traf):
            box=traf[x+8:x+size];flags=int.from_bytes(box[1:4],'big')
            if typ==b'tfhd':
                q=8+(8 if flags&1 else 0)+(4 if flags&2 else 0)
                if flags&8:default=int.from_bytes(box[q:q+4],'big')
            if typ==b'trun':
                count=int.from_bytes(box[4:8],'big');q=8+(4 if flags&1 else 0)+(4 if flags&4 else 0)
                for _ in range(count):
                    duration=int.from_bytes(box[q:q+4],'big') if flags&0x100 else default
                    assert duration>0;durations.append(duration)
                    q+=4*sum(bool(flags&flag) for flag in (0x100,0x200,0x400,0x800))
    return durations


def fixture(fixtures,profile="h264",*,paths=None,duration=20,resource_limit=64):
    base_profile,video_codec,audio_codec,channels=profile_info(profile)
    value=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),
        timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(duration*10000000),timestampOffset='0'),resources=[],tracks=[],
        metadata=dict(revision='1',title='Segmented fMP4 playback',chapters=[dict(id='intro',time='0',title='Intro'),dict(id='chapter',time='20000000',title='Two seconds')]))
    data={};expected={}
    for kind,name in [('video','video.mp4'),('audio','audio.m4a')]:
        path=Path(fixtures)/('progressive-opus-'+str(channels))/'audio.mp4' if kind=='audio' and audio_codec=='opus' else Path(fixtures)/('progressive' if kind=='audio' or base_profile=='h264' else 'progressive-'+base_profile)/name
        if paths is not None:path=Path(paths[kind])
        content=path.read_bytes();top=list(boxes(content));first=next(i for i,b in enumerate(top) if b[2]==b'moof')
        raw=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_entries','stream=time_base:packet=pos,size,pts,dts,duration','-of','json',str(path)],text=True))
        base=Fraction(raw['streams'][0]['time_base']);packets=raw['packets'];ticks=lambda v:int(Fraction(int(v))*base*10000000)
        init=kind+'-init';init_bytes=content[:top[first][0]];data[init]=init_bytes
        value['resources'].append(dict(id=init,name=init+'.part',length=str(len(init_bytes)),ranges=[dict(offset='0',length=str(len(init_bytes)))],state='complete'))
        track=dict(id=kind,kind=kind,required=True,codecHint=video_codec if kind=='video' else audio_codec,containerHint='fmp4',state='downloading',initializationReady=True,initializationResourceId=init,source=dict(segments=[]))
        parts=[]
        for i,(start,size,typ) in enumerate(top):
            if typ!=b'moof':continue
            assert top[i+1][2]==b'mdat';end=top[i+1][0]+top[i+1][1];fragment=content[start:end]
            subset=[p for p in packets if start<=int(p['pos'])<end];assert subset
            durations=fragment_durations(fragment);assert len(durations)==len(subset)
            begin=min(ticks(p['pts']) for p in subset);finish=max(ticks(int(p['pts'])+duration) for p,duration in zip(subset,durations))
            rid=kind+'-'+str(len(parts));data[rid]=fragment;parts.append(fragment)
            value['resources'].append(dict(id=rid,name=rid+'.part',length=str(len(fragment)),ranges=[],state='downloading'))
            track['source']['segments'].append(dict(resourceId=rid,sequence=str(len(parts)),start=str(begin),duration=str(finish-begin),timestampOffset='0',discontinuity=False,configurationId=kind+'-config'))
        value['tracks'].append(track);expected[kind]=init_bytes+b''.join(parts)
    assert resource_limit is None or len(value['resources'])<=resource_limit
    return value,data,expected


def publish_resources(value,data,root,seconds,*,hole=None):
    wanted={t['initializationResourceId'] for t in value['tracks']}
    for track in value['tracks']:
        for segment in track['source']['segments']:
            if int(segment['start'])<seconds*10000000 and segment['resourceId']!=hole:wanted.add(segment['resourceId'])
    for resource in value['resources']:
        if resource['id'] in wanted:
            path=Path(root)/resource['name'];payload=data[resource['id']]
            if not path.exists():path.write_bytes(payload)
            resource.update(state='complete',ranges=[dict(offset='0',length=str(len(payload)))])


def exercise(client,fixtures,work,pause,seek,profile="h264"):
    root=Path(work).resolve();root.mkdir(parents=True);value,data,_=fixture(fixtures,profile)
    publish_resources(value,data,root,12,hole='video-5')
    value=protect_snapshot(value,root);previous=copy.deepcopy(value)
    capabilities=client.request(dict(version=1,action='capabilities'))['schema2Playback']
    assert capabilities['segmentedPlayback']['mode']=='segmented-fmp4-h264-aac-lc'
    _,video_codec,audio_codec,channels=profile_info(profile)
    mode='segmented-fmp4-'+video_codec+'-'+audio_codec
    if profile!='h264':assert mode in capabilities['segmentedPlayback']['profiles']
    probe=client.probe(value,root);assert probe['supported'] and probe['readySeconds']<6,probe
    forget_closed_readers(client,value)
    preview=V2PreviewSession(client,str(root),title='Segmented fMP4 playback');report=dict(profile=profile,videoCodec=video_codec,audioCodec=audio_codec,channels=channels,phases=[],networkRequests=0)
    try:
        preview.publish(value);reader=preview.reader_id
        def state_until(predicate,label,timeout=25):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                state=preview.heartbeat();assert not state.get('error') and state['state']!='error',state
                assert state['readerId']==reader and state['mediaMode']==mode,state
                if predicate(state):report['phases'].append(dict(phase=label,status=state));return state
                time.sleep(.1)
            raise AssertionError(('Segment playback timeout',label,state))
        def publish(seconds):
            nonlocal value,previous
            publish_resources(value,data,root,seconds);value['revision']=str(int(value['revision'])+1)
            value=protect_snapshot(value,root,previous=previous);previous=copy.deepcopy(value);return preview.publish(value)
        state_until(lambda s:s['state']=='buffering' and s['availableSeconds']<6,'gap-stops-contiguous-prefix')
        publish(12)
        state_until(lambda s:s['state']=='playing' and s['positionSeconds']>=3,'playing-before-complete')
        pause();state=state_until(lambda s:s['state']=='paused','user-pause')
        position=state['positionSeconds'];publish(14);time.sleep(.4)
        assert abs(preview.heartbeat()['positionSeconds']-position)<.3
        seek(2);state_until(lambda s:s['state']=='paused' and 1.8<=s['positionSeconds']<3,'backward-seek-while-paused')
        pause();state_until(lambda s:s['state']=='playing' and s['positionSeconds']>=4,'resume-after-seek')
        state=state_until(lambda s:s['state']=='buffering' and s['positionSeconds']>=12,'unfinished-tail-waits-not-eof',timeout=25)
        assert not state['complete'] and value['writer']['state']=='downloading'
        assert any(not (root/r['name']).exists() for r in value['resources'])
        publish_resources(value,data,root,100)
        for t in value['tracks']:t['state']='complete'
        value['writer']['state']='complete';value['revision']=str(int(value['revision'])+1);value=protect_snapshot(value,root,previous=previous);preview.publish(value)
        state_until(lambda s:s['state']=='finished' and s.get('released'),'explicit-complete-and-natural-eof',timeout=30)
        assert preview.close(timeout=10)['released'];forget_closed_readers(client,value)
        report.update(result='PASS',readerIdentityPreserved=True,playingBeforeComplete=True,pausedSeek=True,tailWaitsForPublication=True,released=True)
        return report
    finally:
        if not preview.closed:preview.close(timeout=10)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile');p.add_argument('--fixtures',required=True);p.add_argument('--work',required=True);p.add_argument('--media-profile',choices=PLAYBACK_PROFILES,default='h264');a=p.parse_args()
    sequence=[0]
    def command(action,value=None):
        sequence[0]+=1;path=Path(a.work)/'control.json';temp=path.with_suffix('.tmp')
        temp.write_text(json.dumps(dict(sequence=sequence[0],action=action,value=value)),encoding='utf-8')
        deadline=time.monotonic()+1
        while True:
            try:temp.replace(path);break
            except PermissionError:
                if time.monotonic()>=deadline:raise
                time.sleep(.01)
    print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.fixtures,a.work,lambda:command('pause'),lambda t:command('seek',t),a.media_profile)))
