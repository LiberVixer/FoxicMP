"""Player-only whole-Cluster WebM producer; no downloader integration."""
import uuid
from pathlib import Path
from progressive_webm_fixtures import element,locate,width

PROFILES=('vp8','vp9-8','vp9-10','av1-8','av1-10')

def opus_duration(packet):
    toc=packet[0];config=toc>>3
    frame=(2500000<<(config&3)) if config>=16 else (10000000<<(config&1)) if config>=12 else (10000000,20000000,40000000,60000000)[config&3]
    count=1 if toc&3==0 else packet[1]&63 if toc&3==3 else 2
    return frame*count

def children(data,start,end):
    while start<end:
        tag,body,stop=element(data,start);assert stop is not None and stop<=end
        yield tag,start,body,stop
        start=stop

def split(content,kind):
    tag,_,at=element(content,0);assert tag==0x1a45dfa3
    tag,begin,limit=element(content,at);assert tag==0x18538067 and limit is None
    _,b,e=locate(content,0x2ad7b1);scale=int.from_bytes(content[b:e],'big');assert scale==1000000
    delay=0;frame=0
    if kind=='video':
        _,b,e=locate(content,0x23e383);frame=int.from_bytes(content[b:e],'big')
    else:
        _,b,e=locate(content,0x56aa);delay=int.from_bytes(content[b:e],'big')
    init=None;segments=[]
    for tag,start,body,end in children(content,begin,len(content)):
        if tag!=0x1f43b675:continue
        if init is None:init=content[:start]
        clock=None;samples=[]
        for tag,_,b,e in children(content,body,end):
            if tag==0xe7:clock=int.from_bytes(content[b:e],'big');continue
            padding=0;duration=None
            if tag==0xa0:
                fields={t:(x,y) for t,_,x,y in children(content,b,e)}
                if 0x75a2 in fields:
                    x,y=fields[0x75a2];padding=int.from_bytes(content[x:y],'big',signed=True)
                if 0x9b in fields:
                    x,y=fields[0x9b];duration=int.from_bytes(content[x:y],'big')*scale
                b,e=fields[0xa1]
            elif tag!=0xa3:continue
            w=width(content[b]);relative=int.from_bytes(content[b+w:b+w+2],'big',signed=True)
            assert clock is not None
            start_ns=(clock+relative)*scale-delay
            length=duration or (frame if kind=='video' else opus_duration(content[b+w+3:e]))
            samples.append((start_ns//100,(start_ns+length-padding)//100))
        assert samples
        low=min(s[0] for s in samples);high=max(s[1] for s in samples)
        segments.append((content[start:end],low,high-low))
    assert init and segments
    return init,segments

def fixture(fixtures,profile='vp9-8'):
    assert profile in PROFILES
    video_codec='vp8' if profile=='vp8' else profile.split('-')[0]
    paths=dict(video=Path(fixtures)/('progressive-webm-'+profile)/'video.webm',audio=Path(fixtures)/'progressive-webm-vp9-8'/'audio.webm')
    value=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),
        timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='200000000',timestampOffset='0'),resources=[],tracks=[],
        metadata=dict(revision='1',title='Segmented WebM playback',chapters=[dict(id='intro',time='0',title='Intro'),dict(id='chapter',time='20000000',title='Two seconds')]))
    data={};expected={}
    for kind in ('video','audio'):
        init,segments=split(paths[kind].read_bytes(),kind);rid=kind+'-init';data[rid]=init
        value['resources'].append(dict(id=rid,name=rid+'.part',length=str(len(init)),ranges=[dict(offset='0',length=str(len(init)))],state='complete'))
        track=dict(id=kind,kind=kind,required=True,codecHint=video_codec if kind=='video' else 'opus',containerHint='webm',state='downloading',initializationReady=True,initializationResourceId=rid,source=dict(segments=[]))
        for i,(raw,start,duration) in enumerate(segments):
            rid=kind+'-'+str(i);data[rid]=raw
            value['resources'].append(dict(id=rid,name=rid+'.part',length=str(len(raw)),ranges=[],state='downloading'))
            track['source']['segments'].append(dict(resourceId=rid,sequence=str(i+1),start=str(start),duration=str(duration),timestampOffset='0',discontinuity=False,configurationId=kind+'-config'))
        value['tracks'].append(track);expected[kind]=init+b''.join(raw for raw,_,_ in segments)
    assert len(value['resources'])<=64
    return value,data,expected


def exercise_catalog(client,fixtures,work,pause,seek,profile='vp9-10'):
    import copy,time
    from progressive_catalog_playback import catalog_snapshot
    from progressive_v2 import V2PreviewSession
    from progressive_client import ApiError
    from progressive_v2_playback_conformance import forget_closed_readers
    root=Path(work).resolve();root.mkdir(parents=True)
    plan,data,_=fixture(fixtures,profile);plan['metadata']['chapters'].append(dict(id='forward',time='140000000',title='Fourteen seconds'));value,previous=catalog_snapshot(plan,data,root,12)
    preview=V2PreviewSession(client,str(root),title='Paged WebM segments')
    report=dict(profile=profile,phases=[],networkRequests=0)
    def wait(predicate,label,timeout=30):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            state=preview.heartbeat();assert state.get('ok') and not state.get('error') and state['state']!='error',state
            assert state['readerId']==preview.reader_id and state['mediaMode']=='segmented-webm-'+plan['tracks'][0]['codecHint']+'-opus',state
            assert state['segmentCatalog']['mode']=='paged-webm-v1',state
            if predicate(state):report['phases'].append(dict(phase=label,status=state));return state
            time.sleep(.1)
        raise AssertionError((label,state))
    try:
        preview.publish(value);wait(lambda s:s['state']=='playing' and s['positionSeconds']>=3,'playing-before-complete')
        pause();state=wait(lambda s:s['state']=='paused','user-pause');position=state['positionSeconds']
        plan['revision']='2';candidate,expanded=catalog_snapshot(plan,data,root,100,previous=previous)
        page=root/candidate['segmentCatalog'][0]['name'];raw=page.read_bytes();page.unlink()
        try:preview.publish(candidate)
        except ApiError as error:assert error.code=='waiting-index',error
        else:raise AssertionError('Missing page was accepted')
        assert preview.heartbeat()['revision']=='1'
        page.write_bytes(raw);preview.publish(candidate);previous=expanded
        time.sleep(.4);assert abs(preview.heartbeat()['positionSeconds']-position)<.3
        seek(14);wait(lambda s:s['state']=='paused' and 13.7<=s['positionSeconds']<15,'forward-seek-after-page-publication')
        pause();wait(lambda s:s['state']=='playing' and s['positionSeconds']>=15,'decode-after-forward-seek')
        pause();wait(lambda s:s['state']=='paused','pause-before-backward-seek')
        seek(2);wait(lambda s:s['state']=='paused' and 1.7<=s['positionSeconds']<3,'backward-seek-reopens-evicted-media')
        pause();wait(lambda s:s['state']=='buffering' and s['positionSeconds']>=18,'unfinished-tail-waits-not-eof')
        plan['revision']='3';candidate,expanded=catalog_snapshot(plan,data,root,100,previous=previous,complete=True);preview.publish(candidate)
        wait(lambda s:s['state']=='finished' and s.get('released'),'explicit-complete-and-natural-eof')
        assert preview.close(timeout=10)['released'];forget_closed_readers(client,plan)
        report.update(result='PASS',released=True,missingPageRetry=True,pausedSeek=True)
        return report
    finally:
        if not preview.closed:preview.close(timeout=10)


if __name__=='__main__':
    import argparse,json,time
    from progressive_segment_playback import exercise
    from progressive_client import ProgressiveClient
    parser=argparse.ArgumentParser();parser.add_argument('--catalog',action='store_true');parser.add_argument('--player',required=True);parser.add_argument('--profile');parser.add_argument('--fixtures',required=True);parser.add_argument('--work',required=True);parser.add_argument('--media-profile',choices=PROFILES,default='vp9-8');args=parser.parse_args()
    sequence=[0]
    def command(action,value=None):
        sequence[0]+=1;path=Path(args.work)/'control.json';temp=path.with_suffix('.tmp')
        temp.write_text(json.dumps(dict(sequence=sequence[0],action=action,value=value)),encoding='utf-8')
        deadline=time.monotonic()+1
        while True:
            try:temp.replace(path);break
            except PermissionError:
                if time.monotonic()>=deadline:raise
                time.sleep(.01)
    print(json.dumps((exercise_catalog if args.catalog else exercise)(ProgressiveClient(args.player,args.profile),args.fixtures,args.work,lambda:command('pause'),lambda t:command('seek',t),args.media_profile,**({} if args.catalog else dict(webm=True)))))
