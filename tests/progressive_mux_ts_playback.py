"""Player-owned muxed TS segment fixture producer; never accesses Harvester."""
import argparse,json,subprocess,sys,time,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt'/'tools'))

def fixture(root,profile='main-bframes',seconds=20):
    root=Path(root);root.mkdir(parents=True,exist_ok=True)
    rate='30000/1001' if profile=='high-fractional' else '25'
    audio_rate=44100 if profile=='high-fractional' else 48000
    avc_profile='high' if profile=='high-fractional' else 'main'
    options=['-f','lavfi','-i','testsrc2=size=160x90:rate='+rate+':duration='+str(seconds),'-f','lavfi','-i','sine=frequency=880:sample_rate='+str(audio_rate)+':duration='+str(seconds),'-map','0:v','-map','1:a','-c:v','libx264','-preset','veryfast','-profile:v',avc_profile,'-pix_fmt','yuv420p','-x264-params','force-cfr=1:repeat-headers=1:keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes=2'+(':8x8dct=1' if avc_profile=='high' else ''),'-c:a','aac','-ac','2','-b:a','96k','-avoid_negative_ts','make_zero','-f','segment','-segment_time','1','-segment_format','mpegts','-reset_timestamps','0',str(root/'mux-piece-%04d.ts')]
    subprocess.run(['ffmpeg','-v','error','-y',*options],check=True,capture_output=True)
    pieces=[];initial=None;all_packets=[[],[]];raw_original=[]
    for path in sorted(root.glob('mux-piece-*.ts')):
        raw=path.read_bytes();packets=[raw[a:a+188] for a in range(0,len(raw),188)];pid=lambda p:((p[1]&31)<<8)|p[2]
        if initial is None:initial=next(p for p in packets if pid(p)==0)+next(p for p in packets if pid(p)==4096)
        # The bounded profile carries PAT/PMT, the two declared ES and null PID.
        # SDT is not decoder data and is removed only by this test producer.
        media=b''.join(p for p in packets if pid(p)!=17);raw_original.append(raw)
        probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_data_hash','sha256','-of','json',str(path)]))['packets']
        groups=[[p for p in probe if p['codec_type']==kind] for kind in ('video','audio')];assert all(groups)
        for i,g in enumerate(groups):all_packets[i].extend(g)
        pieces.append((media,groups))
    original=root/'mux-original.ts';original.write_bytes(b''.join(raw_original))
    origin=int(all_packets[1][0]['pts']);ticks=lambda n:n*10000000//90000
    value=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='1',timestampOffset=str(ticks(origin))),resources=[],tracks=[])
    payload={'mux-init':initial};value['resources'].append(dict(id='mux-init',name='mux-init.part',length=str(len(initial)),state='complete',ranges=[dict(offset='0',length=str(len(initial)))]))
    for seq,(raw,_) in enumerate(pieces):
        rid='mux-'+str(seq);payload[rid]=raw;value['resources'].append(dict(id=rid,name=rid+'.part',length=str(len(raw)),state='downloading',ranges=[]))
    duration=0;models=[];common=initial+b''.join(p[0] for p in pieces)
    for kind,name in enumerate(('video','audio')):
        track=dict(id=name,kind=name,required=True,codecHint='h264' if kind==0 else 'aac-lc',containerHint='mpegts-muxed',state='downloading',initializationReady=True,initializationResourceId='mux-init',source=dict(segments=[]))
        count=0
        for seq,(_,groups) in enumerate(pieces):
            packets=groups[kind]
            if kind==0:
                first=min(ticks(int(p['pts']))-ticks(origin) for p in packets)
                end=min(ticks(int(p['pts']))-ticks(origin) for p in pieces[seq+1][1][0]) if seq+1<len(pieces) else max(ticks(int(p['pts'])+int(p['duration']))-ticks(origin) for p in packets)
            else:first=count*1024*10000000//audio_rate;end=(count+len(packets))*1024*10000000//audio_rate
            count+=len(packets);duration=max(duration,end)
            track['source']['segments'].append(dict(resourceId='mux-'+str(seq),sequence=str(seq+1),start=str(first),duration=str(end-first),timestampOffset='0',discontinuity=False,configurationId=name+'-constant'))
        value['tracks'].append(track);models.append((name,original,initial,common[len(initial):],all_packets[kind],{},[]))
    value['timeline']['duration']=str(duration)
    if profile=='main-preroll':
        # A common 10 ms origin shift gives audio preroll while keeping the
        # first IDR presentation nonnegative; video DTS is already negative.
        value['timeline']['timestampOffset']=str(ticks(origin)+100000);value['timeline']['duration']=str(duration-100000)
        for t in value['tracks']:
            for p in t['source']['segments']:p['start']=str(int(p['start'])-100000)
    value['metadata']=dict(revision='1',title='Muxed MPEG-TS '+profile,chapters=[dict(id='intro',time='0',title='Intro'),dict(id='two',time='20000000',title='Two seconds')])
    return value,payload,dict(video=common,audio=common),models

if __name__=='__main__':
    from progressive_client import ProgressiveClient
    from progressive_ts_playback import exercise
    p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--profile');p.add_argument('--fixtures');p.add_argument('--work',required=True);p.add_argument('--media-profile',default='main-bframes');p.add_argument('--catalog',action='store_true');a=p.parse_args();sequence=[0]
    def command(action,value=None):
        sequence[0]+=1;path=Path(a.work)/'control.json';temp=path.with_suffix('.tmp');temp.write_text(json.dumps(dict(sequence=sequence[0],action=action,value=value)),encoding='utf-8');deadline=time.monotonic()+1
        while True:
            try:temp.replace(path);break
            except PermissionError:
                if time.monotonic()>=deadline:raise
                time.sleep(.01)
    print(json.dumps(exercise(ProgressiveClient(a.player,a.profile),a.fixtures,a.work,lambda:command('pause'),lambda t:command('seek',t),a.media_profile,a.catalog,muxed=True)))
