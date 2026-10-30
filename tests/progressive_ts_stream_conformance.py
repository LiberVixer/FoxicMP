"""Internal TS ResourceSource staging, virtual bytes, samples and actual leases."""
import argparse,copy,hashlib,json,os,subprocess,tempfile,uuid
from fractions import Fraction
from pathlib import Path
from progressive_ts_conformance import init,pes,transport
from progressive_ts_codec_conformance import avc,adts
from progressive_catalog_playback import catalog_snapshot
from progressive_segment_playback import publish_resources
from progressive_integrity import protect_snapshot

def fixture(count=12,big=False):
    value=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(count*3200000),timestampOffset='14000000'),resources=[],tracks=[])
    payload={};expected={};samples={}
    for kind in ('video','audio'):
        rid=kind+'-init';raw=init(kind);payload[rid]=raw
        value['resources'].append(dict(id=rid,name=rid+'.part',length=str(len(raw)),state='complete',ranges=[dict(offset='0',length=str(len(raw)))]))
        track=dict(id=kind,kind=kind,required=True,codecHint='h264' if kind=='video' else 'aac-lc',containerHint='mpegts',state='downloading',initializationReady=True,initializationResourceId=rid,source=dict(segments=[]));all_samples=[];parts=[]
        for i in range(count):
            if kind=='video':
                frames=[avc(key=j==0,frame=j,config=j==0) for j in range(8)]
                if big and i==0:frames[0]+=b'Z'*200000
                data=transport([pes(frame,126000+(i*8+j)*3600,bounded=len(frame)<60000) for j,frame in enumerate(frames)])
            else:
                frames=[adts(bytes([i&255,j,3])) for j in range(15)];data=transport([pes(b''.join(frames),126000+i*28800,audio=True)])
            rid=kind+'-'+str(i);payload[rid]=data;parts.append(data);all_samples.extend(frames)
            value['resources'].append(dict(id=rid,name=rid+'.part',length=str(len(data)),state='downloading',ranges=[]))
            track['source']['segments'].append(dict(resourceId=rid,sequence=str(i+1),start=str(i*3200000),duration='3200000',timestampOffset='0',discontinuity=False,configurationId=kind+'-constant'))
        value['tracks'].append(track);expected[kind]=raw+b''.join(parts);samples[kind]=all_samples
    return value,payload,expected,samples

def run(runner):
    checks=[]
    def case(name,check,count=12,big=False):
        with tempfile.TemporaryDirectory(prefix='foxicmp-ts-stream-') as tmp:
            root=Path(tmp);data=root/'data';data.mkdir();value,payload,expected,samples=fixture(count,big)
            publish_resources(value,payload,data,Fraction(3*32,100),hole='video-1');value=protect_snapshot(value,data)
            proc=subprocess.Popen([runner,'--registry',str(root/'registry')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
            def rpc(action,**fields):
                proc.stdin.write(json.dumps(dict(action=action,**fields))+'\n');proc.stdin.flush()
                line=proc.stdout.readline();assert line,proc.stderr.read();return json.loads(line)
            def readers():
                rows=[];after=''
                while True:
                    r=rpc('readers',version=1,sessionId=value['sessionId'],generation='1',**({'afterReaderId':after} if after else {}));assert r['ok'],r
                    rows+=r['readers'];after=r.get('nextReaderId','')
                    if not after:return rows
            def opened():
                answer=rpc('open',directory=str(data),session=value);assert answer['ok'],answer
            def publish(ready,complete=False):
                previous=copy.deepcopy(value);publish_resources(value,payload,data,Fraction(ready*32,100));value['revision']=str(int(value['revision'])+1)
                if complete:
                    value['writer']['state']='complete'
                    for t in value['tracks']:t['state']='complete'
                protected=protect_snapshot(value,data,previous=previous);value.clear();value.update(protected)
                reply=rpc('publish',session=value);assert reply['ok'],reply
            def read(at,n,track=0):return rpc('read',track=track,offset=str(at),length=str(n))
            try:check(value,payload,expected,samples,data,rpc,readers,opened,publish,read);checks.append(name)
            finally:
                proc.stdin.close()
                try:proc.wait(timeout=10)
                except subprocess.TimeoutExpired:proc.kill();proc.wait();raise
                error=proc.stderr.read();proc.stdout.close();proc.stderr.close();assert proc.returncode==0,error
    def gap(v,p,e,s,d,rpc,readers,open_,pub,read):
        open_();status=rpc('status');assert status['revision']=='1' and status['available']=='3200000' and status['tracks'][0]['units']==8,status
        cutoff=len(p['video-init'])+len(p['video-0']);r=read(cutoff,8);assert r['code']=='waiting-range' and r['untouchedOnError'],r
        assert rpc('sample',track=0,index='8')['code']=='waiting-segment'
        boundary=len(p['video-init']);assert read(boundary-4,16)['hex']==e['video'][boundary-4:boundary+12].hex()
        assert read(cutoff-4,16)['hex']==e['video'][cutoff-4:cutoff].hex()
        pub(5);assert rpc('status')['available']=='16000000'
        assert read(cutoff-4,16)['hex']==e['video'][cutoff-4:cutoff+12].hex()
        assert not (d/'video-6.part').exists()
        pub(100);status=rpc('status');assert not status['complete']
        for track,kind in enumerate(('video','audio')):
            assert read(len(e[kind]),8,track)['code']=='waiting-range'
        pub(100,True)
        for track,kind in enumerate(('video','audio')):
            for at in range(0,len(e[kind]),65536):assert read(at,min(65536,len(e[kind])-at),track)['hex']==e[kind][at:at+65536].hex()
            assert read(len(e[kind]),8,track)['code']=='eof'
        assert rpc('status')['complete'] and len(readers())<=8
        rpc('retain');rpc('release');assert not rpc('released')['released'] and readers()
        rpc('dropRetained');assert rpc('released')['released'] and not readers()
    case('gap-boundary-append-tail-eof-bounded-leases-and-last-owner',gap)
    def frames(v,p,e,s,d,rpc,readers,open_,pub,read):
        open_();pub(100,True)
        for track,kind in enumerate(('video','audio')):
            for index in (0,1,len(s[kind])//2,len(s[kind])-1):
                encoded=rpc('sample',track=track,index=str(index),encoded=True);assert encoded['code']=='data' and encoded['sha256']==hashlib.sha256(s[kind][index]).hexdigest(),encoded
                raw=s[kind][index] if track==0 else s[kind][index][7:]
                decoded=rpc('sample',track=track,index=str(index));assert decoded['code']=='data' and decoded['sha256']==hashlib.sha256(raw).hexdigest(),decoded
            assert rpc('sample',track=track,index=str(len(s[kind])))['code']=='eof'
        assert rpc('seek',track=0,position='4000000')['sample']==8
        assert rpc('seek',track=1,position='213333')['sample']==1
        assert len(readers())<=8
    case('sample-gather-encoded-and-elementary-bytes-backward-seek',frames)
    case('multi-window-large-pes-sample-gather',frames,big=True)
    def metadata(v,p,e,s,d,rpc,readers,open_,pub,read,change):
        open_();before=rpc('status');ids=[x['readerId'] for x in readers()];bad=copy.deepcopy(v);bad['revision']='2';change(bad)
        a=rpc('publish',session=bad);assert not a['ok'],a
        assert rpc('status')==before and [x['readerId'] for x in readers()]==ids
        assert read(0,8)['hex']==e['video'][:8].hex()
    for name,change in [
        ('clock',lambda v:v['timeline'].update(timestampOffset='0')),
        ('sequence',lambda v:v['tracks'][0]['source']['segments'][1].update(sequence='3')),
        ('duration',lambda v:v['timeline'].update(duration=str(int(v['timeline']['duration'])+1))),
        ('discontinuity',lambda v:v['tracks'][0]['source']['segments'][1].update(discontinuity=True)),
        ('codec',lambda v:v['tracks'][0].update(codecHint='hevc')),
        ('track-set',lambda v:v['tracks'][1].update(required=False)),
        ('catalog-mode',lambda v:v.update(segmentCatalog=[])),
    ]:case('metadata-rejection-preserves-index-and-readable-source-'+name,lambda *args,change=change:metadata(*args,change=change))
    def reject_open(v,p,e,s,d,rpc,readers,open_,pub,read,change,code=None):
        change(v,p,d);a=rpc('open',directory=str(d),session=v);assert not a['ok'] and (code is None or a['code']==code),a
        assert not readers()
    for name,change,code in [
        ('missing-complete-media',lambda v,p,d:(d/'video-0.part').unlink(),'missing-resource'),
        ('missing-complete-init',lambda v,p,d:(d/'video-init.part').unlink(),'missing-resource'),
        ('integrity-required',lambda v,p,d:v['resources'][0].pop('integrity'),'integrity-required'),
        ('non-grid-origin',lambda v,p,d:v['timeline'].update(timestampOffset='14000001'),'unsupported_media'),
        ('negative-origin',lambda v,p,d:v['timeline'].update(timestampOffset='-1'),'unsupported_media'),
        ('container',lambda v,p,d:v['tracks'][0].update(containerHint='mp4'),'unsupported_media'),
    ]:case('constructor-cleanup-'+name,lambda *args,change=change,code=code:reject_open(*args,change=change,code=code))
    def bad_new(v,p,e,s,d,rpc,readers,open_,pub,read,codec):
        open_();before=rpc('status');previous=copy.deepcopy(v);publish_resources(v,p,d,Fraction(5*32,100));v['revision']='2'
        path=d/'video-1.part'
        if codec:
            data=bytearray(path.read_bytes());data[0]=0;path.write_bytes(data)
        protected=protect_snapshot(v,d,previous=previous)
        if not codec:
            with path.open('r+b') as f:f.write(b'X')
        a=rpc('publish',session=protected);assert not a['ok'] and a['code']==('ts-truncated' if codec else 'integrity-mismatch'),a
        after=rpc('status');assert after['revision']=='1' and after['tracks']==before['tracks'] and after['errorCode']==a['code'],after
        assert read(0,8)['code']==a['code'] and read(0,8)['untouchedOnError']
        assert rpc('sample',track=0,index='0')['size']==0
        assert not rpc('publish',session=protected)['ok'];rpc('release');assert not readers()
    case('new-resource-sha-error-never-exposes-candidate-and-closes-source',lambda *a:bad_new(*a,codec=False))
    case('new-resource-codec-error-never-exposes-candidate-and-closes-source',lambda *a:bad_new(*a,codec=True))
    def mutated(v,p,e,s,d,rpc,readers,open_,pub,read,replace=False,truncated=False):
        open_();pub(100,True);read(len(e['audio'])-4,4,1) # old video files have left the eight-pin cache
        path=d/'video-0.part'
        if replace:
            replacement=path.with_suffix('.replacement');replacement.write_bytes(path.read_bytes());replacement.replace(path)
        elif truncated:
            with path.open('r+b') as f:f.truncate(8)
        else:
            with path.open('r+b') as f:f.write(b'X')
        answer=read(len(p['video-init'])-4,16)
        assert answer['code'] in ('source-replaced','source-truncated','integrity-mismatch','source-rewritten'),answer
        assert answer['bytesRead']==0 and answer['untouchedOnError'],answer
        assert rpc('status')['errorCode'] and read(0,4)['bytesRead']==0
        rpc('release');assert not readers()
    case('evicted-resource-replacement-is-fatal-and-cross-span-buffer-untouched',lambda *a:mutated(*a,replace=True))
    case('evicted-resource-truncation-is-fatal-and-buffer-untouched',lambda *a:mutated(*a,truncated=True))
    case('evicted-resource-overwrite-is-fatal-and-buffer-untouched',mutated)
    def cancel(v,p,e,s,d,rpc,readers,open_,pub,read):
        open_();rpc('cancel');assert rpc('status')['errorCode']=='cancelled' and read(0,0)['code']=='cancelled'
        assert rpc('sample',track=1,index='0')['code']=='cancelled';assert not rpc('publish',session=v)['ok'];rpc('release');assert not readers()
    case('cancel-short-circuits-read-sample-and-publication-and-releases',cancel)
    def initialization(v,p,e,s,d,rpc,readers,open_,pub,read):
        for t in v['tracks']:t['initializationReady']=False
        open_();assert rpc('status')['available']=='0' and not readers();assert rpc('sample',track=0,index='0')['code']=='waiting-initialization'
        for t in v['tracks']:t['initializationReady']=True
        pub(3);assert rpc('status')['tracks'][0]['units']==24
    case('unready-initialization-does-not-open-files-and-later-publishes',initialization)
    def paged(v,p,e,s,d,rpc,readers,open_,pub,read):
        # The producer plan may exceed inline limits; only the catalog root is sent.
        plan=v;plan['resources']=[r for r in plan['resources']]
        value,expanded=catalog_snapshot(plan,p,d,Fraction(16*32,100))
        a=rpc('open',directory=str(d),session=value);assert a['ok'],a;before=rpc('status');assert before['pages']==1
        plan['revision']='2';candidate,full=catalog_snapshot(plan,p,d,Fraction(40*32,100),previous=expanded)
        path=d/candidate['segmentCatalog'][-1]['name'];raw=path.read_bytes();path.unlink()
        a=rpc('publish',session=candidate);assert not a['ok'] and a['code']=='waiting-index',a;assert rpc('status')==before
        assert read(0,8)['hex']==e['video'][:8].hex();path.write_bytes(raw);assert rpc('publish',session=candidate)['ok']
        plan['revision']='3';complete,full=catalog_snapshot(plan,p,d,1000,previous=full,complete=True);a=rpc('publish',session=complete);assert a['ok'],a
        status=rpc('status');assert status['resources']>64 and status['complete'] and status['pages']>=7,status
        for track,kind in enumerate(('video','audio')):
            for at in range(len(e[kind])-1,-1,-65536):
                start=max(0,at-65535);assert read(start,at-start+1,track)['hex']==e[kind][start:at+1].hex()
            a=rpc('sample',track=track,index='0',encoded=True);assert a['sha256']==hashlib.sha256(s[kind][0]).hexdigest(),a
        assert len(readers())<=8;rpc('release');assert not readers()
    case('paged-242-resources-missing-page-retry-backward-bytes-and-samples',paged,count=120)
    real=real_cases(runner,checks)
    return dict(result='PASS',scope='internal-ts-resource-stream',count=len(checks),checks=checks,maxCatalogResources=242,maxOpenResources=8,realMedia=real,decoderTested='external-ffmpeg-only',publicPlaybackCapability=False,networkRequests=0)

def real_cases(runner,checks):
    reports=[]
    for profile,rate,audio_rate in [('main-bframes','25',48000),('high-fractional','30000/1001',44100)]:
        with tempfile.TemporaryDirectory(prefix='foxicmp-ts-stream-real-') as tmp:
            root=Path(tmp);data=root/'data';data.mkdir()
            value,payload,_,_=fixture(1);value['resources']=[];value['tracks']=[]
            models=[]
            for kind in ('video','audio'):
                path=root/(kind+'.ts')
                if kind=='video':
                    avc_profile='main' if profile=='main-bframes' else 'high'
                    options=['-f','lavfi','-i','testsrc2=size=160x90:rate='+rate+':duration=3','-an','-c:v','libx264','-preset','veryfast','-profile:v',avc_profile,'-pix_fmt','yuv420p','-x264-params','force-cfr=1:repeat-headers=1:keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes=2'+(':8x8dct=1' if avc_profile=='high' else '')]
                else:options=['-f','lavfi','-i','sine=frequency=880:sample_rate='+str(audio_rate)+':duration=3','-vn','-c:a','aac','-ac','2','-b:a','96k']
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
            publish_resources(value,payload,data,1);value=protect_snapshot(value,data)
            proc=subprocess.Popen([runner,'--registry',str(root/'registry')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
            def rpc(action,**fields):
                proc.stdin.write(json.dumps(dict(action=action,**fields))+'\n');proc.stdin.flush();line=proc.stdout.readline();assert line,proc.stderr.read();return json.loads(line)
            try:
                a=rpc('open',directory=str(data),session=value);assert a['ok'],(profile,a)
                first_status=rpc('status');assert not first_status['complete'] and int(first_status['available'])>0,first_status
                previous=copy.deepcopy(value);publish_resources(value,payload,data,100);value['revision']='2';value['writer']['state']='complete'
                for t in value['tracks']:t['state']='complete'
                value=protect_snapshot(value,data,previous=previous);a=rpc('publish',session=value);assert a['ok'],(profile,a)
                assert rpc('status')['complete']
                counts=[]
                for track,(kind,path,initial,media,probe,offsets,boundaries) in enumerate(models):
                    for at in range(0,len(expected[kind]),65536):
                        a=rpc('read',track=track,offset=str(at),length=str(min(65536,len(expected[kind])-at)));assert a['hex']==expected[kind][at:at+65536].hex(),(profile,a)
                    assembled=[]
                    for index,p in enumerate(probe):
                        a=rpc('sample',track=track,index=str(index),encoded=True,includeBytes=True)
                        assert a['code']=='data' and a['sha256']==p['data_hash'].split(':')[1],(profile,kind,index,a)
                        assert abs(int(a['start'])-(ticks(int(p['pts']))-ticks(origin)))<=112 and abs(int(a['decode'])-(ticks(int(p['dts']))-ticks(origin)))<=112
                        assembled.append(bytes.fromhex(a['hex']))
                    elementary=root/(kind+('.h264' if kind=='video' else '.aac'));elementary.write_bytes(b''.join(assembled))
                    def decoded_hash(source):
                        opts=['-an','-pix_fmt','yuv420p','-c:v','rawvideo','-threads','1'] if kind=='video' else ['-vn','-c:a','pcm_s16le']
                        return subprocess.check_output(['ffmpeg','-v','error','-i',str(source),*opts,'-f','hash','-hash','sha256','-'],stderr=subprocess.PIPE).strip()
                    assert decoded_hash(path)==decoded_hash(elementary),(profile,kind,'decoded bytes differ')
                    counts.append(len(probe))
                rpc('release');a=rpc('readers',version=1,sessionId=value['sessionId'],generation='1');assert not a['readers'],a
                checks.append('real-ffmpeg-staged-virtual-samples-and-decoded-hashes-'+profile)
                reports.append(dict(profile=profile,audioRate=audio_rate,accessUnits=counts,segments=[len(m[-1])-1 for m in models],encodedPacketHashesMatch=True,decodedVideoAndPcmHashesMatch=True,playerDecoderTested=False))
            finally:
                proc.stdin.close()
                try:proc.wait(timeout=10)
                except subprocess.TimeoutExpired:proc.kill();proc.wait();raise
                error=proc.stderr.read();proc.stdout.close();proc.stderr.close();assert proc.returncode==0,error
    return reports

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True);args=parser.parse_args();print(json.dumps(run(args.runner)))
