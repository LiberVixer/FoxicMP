"""Player-only TS AVC/AAC fixture producer. Does not access Harvester."""
import argparse,copy,json,subprocess,sys,time,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt'/'tools'))

def fixture(root,profile='main-bframes',seconds=20):
    root=Path(root);root.mkdir(parents=True,exist_ok=True)
    rate='30000/1001' if profile=='high-fractional' else '25'
    audio_rate=44100 if profile=='high-fractional' else 48000
    value=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='1',timestampOffset='0'),resources=[],tracks=[])
    models=[]
    for kind in ('video','audio'):
        path=root/(kind+'.ts')
        if kind=='video':
            avc_profile='main' if profile.startswith('main') else 'high'
            options=['-f','lavfi','-i','testsrc2=size=160x90:rate='+rate+':duration='+str(seconds),'-an','-c:v','libx264','-preset','veryfast','-profile:v',avc_profile,'-pix_fmt','yuv420p','-x264-params','force-cfr=1:repeat-headers=1:keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes=2'+(':8x8dct=1' if avc_profile=='high' else '')]
        else:options=['-f','lavfi','-i','sine=frequency=880:sample_rate='+str(audio_rate)+':duration='+str(seconds),'-vn','-c:a','aac','-ac','2','-b:a','96k']
        subprocess.run(['ffmpeg','-v','error','-y',*options,'-avoid_negative_ts','make_zero','-muxdelay','0.3','-f','mpegts',str(path)],check=True,capture_output=True)
        raw=path.read_bytes();packets=[raw[a:a+188] for a in range(0,len(raw),188)];pid=lambda p:((p[1]&31)<<8)|p[2]
        initial=next(p for p in packets if pid(p)==0)+next(p for p in packets if pid(p)==4096)
        offsets={};kept=[]
        for at,packet in enumerate(packets):
            offsets[at*188]=len(kept)*188
            if pid(packet)!=17:kept.append(packet)
        media=b''.join(kept)
        probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_data_hash','sha256','-of','json',str(path)]))['packets']
        # ffprobe only reports pos on the first AAC frame of a PES.
        last_pos=None
        for p in probe:
            last_pos=p.get('pos',last_pos);assert last_pos is not None;p['pos']=last_pos
        if kind=='video':boundaries=[i for i,p in enumerate(probe) if 'K' in p.get('flags','')]
        else:
            pes=[i for i,p in enumerate(probe) if i==0 or p['pos']!=probe[i-1]['pos']];boundaries=pes[::6]
        models.append((kind,path,initial,media,probe,offsets,boundaries))
    origin=min(int(p[0]['dts']) for _,_,_,_,p,_,_ in models);ticks=lambda v:v*10000000//90000
    value['timeline']['timestampOffset']=str(ticks(origin));duration=0;expected={};payload={}
    for kind,path,initial,media,probe,offsets,boundaries in models:
        rid=kind+'-init';payload[rid]=initial;expected[kind]=initial+media
        value['resources'].append(dict(id=rid,name=rid+'.part',length=str(len(initial)),state='complete',ranges=[dict(offset='0',length=str(len(initial)))]))
        track=dict(id=kind,kind=kind,required=True,codecHint='h264' if kind=='video' else 'aac-lc',containerHint='mpegts',state='downloading',initializationReady=True,initializationResourceId=rid,source=dict(segments=[]))
        boundaries.append(len(probe))
        for seq,(a,b) in enumerate(zip(boundaries,boundaries[1:])):
            start=0 if a==0 else offsets[int(probe[a]['pos'])];end=len(media) if b==len(probe) else offsets[int(probe[b]['pos'])]
            part=media[start:end];rid=kind+'-'+str(seq);payload[rid]=part
            first=min(ticks(int(p['pts']))-ticks(origin) for p in probe[a:b])
            # The metadata envelope follows the rational audio/video cadence;
            # access validation independently allows one 90 kHz rounding tick.
            if b<len(probe):last=min(ticks(int(p['pts']))-ticks(origin) for p in probe[b:boundaries[seq+2]])
            elif kind=='audio':last=ticks(int(probe[-1]['pts']))-ticks(origin)+1024*10000000//audio_rate
            else:last=max(ticks(int(p['pts'])+int(p['duration']))-ticks(origin) for p in probe[a:b])
            duration=max(duration,last)
            value['resources'].append(dict(id=rid,name=rid+'.part',length=str(len(part)),state='downloading',ranges=[]))
            track['source']['segments'].append(dict(resourceId=rid,sequence=str(seq+1),start=str(first),duration=str(last-first),timestampOffset='0',discontinuity=False,configurationId=kind+'-constant'))
        value['tracks'].append(track)
    value['timeline']['duration']=str(duration)
    if profile=='main-preroll':
        delta=800000
        value['timeline']['timestampOffset']=str(int(value['timeline']['timestampOffset'])+delta)
        value['timeline']['duration']=str(int(value['timeline']['duration'])-delta)
        for t in value['tracks']:
            for segment in t['source']['segments']:segment['start']=str(int(segment['start'])-delta)
    value['metadata']=dict(revision='1',title='Segmented MPEG-TS '+profile,chapters=[dict(id='intro',time='0',title='Intro'),dict(id='two',time='20000000',title='Two seconds')])
    return value,payload,expected,models


def exercise(client,fixtures,work,pause,seek,profile='main-bframes',catalog=False,muxed=False):
    from progressive_segment_playback import publish_resources
    from progressive_catalog_playback import catalog_snapshot
    from progressive_integrity import protect_snapshot
    from progressive_v2 import V2PreviewSession
    from progressive_v2_playback_conformance import forget_closed_readers
    root=Path(work).resolve();root.mkdir(parents=True,exist_ok=True)
    if muxed:
        from progressive_mux_ts_playback import fixture as producer
    else:producer=fixture
    plan,data,_,_=producer(root,profile,seconds=(80 if muxed else 40) if catalog else 20)
    tail=(75 if muxed else 35) if catalog else 17
    plan['metadata']['chapters'].append(dict(id='tail',time=str(tail*10000000),title='Tail'))
    if catalog:value,previous=catalog_snapshot(plan,data,root,12)
    else:
        value=plan;publish_resources(value,data,root,12,hole='mux-5' if muxed else 'video-5');value=protect_snapshot(value,root);previous=copy.deepcopy(value)
    caps=client.request(dict(version=1,action='capabilities'))['schema2Playback']['segmentedPlayback']['mpegts']
    if muxed:caps=caps['muxed']
    mode='segmented-muxed-mpegts-h264-aac-lc' if muxed else 'segmented-mpegts-h264-aac-lc'
    assert caps['mode']==mode and caps['integrityRequired'] and caps['decoderContainer']=='fmp4'
    probe=client.probe(value,root);assert probe['supported'] and not probe['complete'],probe
    forget_closed_readers(client,value)
    preview=V2PreviewSession(client,str(root),title='MPEG-TS '+profile);report=dict(profile=profile,catalog=catalog,muxed=muxed,phases=[],networkRequests=0)
    try:
        preview.publish(value);reader=preview.reader_id
        def wait(predicate,label,timeout=30):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                state=preview.heartbeat();assert state.get('ok') and not state.get('error') and state['state']!='error',state
                assert state['readerId']==reader and state['mediaMode']==mode,state
                if predicate(state):report['phases'].append(dict(phase=label,status=state));return state
                time.sleep(.1)
            raise AssertionError(('TS playback timeout',label,state))
        def publish(seconds,complete=False):
            nonlocal value,previous
            if catalog:
                plan['revision']=str(int(value['revision'])+1);value,previous=catalog_snapshot(plan,data,root,seconds,previous=previous,complete=complete)
            else:
                publish_resources(value,data,root,seconds);value['revision']=str(int(value['revision'])+1)
                if complete:
                    value['writer']['state']='complete'
                    for t in value['tracks']:t['state']='complete'
                value=protect_snapshot(value,root,previous=previous);previous=copy.deepcopy(value)
            return preview.publish(value)
        if not catalog:
            wait(lambda s:s['state']=='buffering' and s['availableSeconds']<6,'gap-blocks-prefix')
            publish(12)
        wait(lambda s:s['state']=='playing' and s['positionSeconds']>=3,'decoded-before-complete')
        pause();state=wait(lambda s:s['state']=='paused','pause');position=state['positionSeconds']
        publish(14);time.sleep(.4);assert abs(preview.heartbeat()['positionSeconds']-position)<.3
        seek(2);wait(lambda s:s['state']=='paused' and 1.8<=s['positionSeconds']<3,'backward-seek-while-paused')
        pause();wait(lambda s:s['state']=='playing' and s['positionSeconds']>=4,'resume-after-seek')
        wait(lambda s:s['state']=='buffering' and s['positionSeconds']>=12 and not s['complete'],'tail-waits-not-eof')
        assert any(not (root/r['name']).exists() for r in plan['resources'])
        publish(100,True)
        seek(tail);wait(lambda s:s['state'] in ('playing','finished') and s['positionSeconds']>=tail-.3,'forward-seek-to-published-tail')
        wait(lambda s:s['state']=='finished' and s.get('released'),'complete-and-natural-eof',timeout=30)
        assert preview.close(timeout=10)['released'];forget_closed_readers(client,value)
        report.update(result='PASS',readerIdentityPreserved=True,playingBeforeComplete=True,pausedSeek=True,tailWaitsForPublication=True,forwardSeek=True,released=True)
        return report
    finally:
        if not preview.closed:preview.close(timeout=10)

if __name__=='__main__':
    from progressive_client import ProgressiveClient
    p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile');p.add_argument('--fixtures');p.add_argument('--work',required=True);p.add_argument('--media-profile',choices=['main-bframes','high-fractional','main-preroll'],default='main-bframes');p.add_argument('--catalog',action='store_true');a=p.parse_args();sequence=[0]
    def command(action,value=None):
        sequence[0]+=1;path=Path(a.work)/'control.json';temp=path.with_suffix('.tmp');temp.write_text(json.dumps(dict(sequence=sequence[0],action=action,value=value)),encoding='utf-8');deadline=time.monotonic()+1
        while True:
            try:temp.replace(path);break
            except PermissionError:
                if time.monotonic()>=deadline:raise
                time.sleep(.01)
    print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.fixtures,a.work,lambda:command('pause'),lambda t:command('seek',t),a.media_profile,a.catalog)))
