"""Native segmented stream: append-only offsets, publication barriers and failures."""
import argparse,copy,json,os,subprocess,tempfile
from pathlib import Path
from progressive_segment_playback import fixture,publish_resources,profile_info,PLAYBACK_PROFILES,NATIVE_PROFILES
from progressive_integrity import protect_snapshot


def run(runner,fixtures,profile="h264"):
    _,video_codec,audio_codec,channels=profile_info(profile)
    expected_mode="segmented-fmp4-"+video_codec+"-"+audio_codec
    checks=[]
    def case(name,check):
        with tempfile.TemporaryDirectory(prefix='foxicmp-segment-stream-') as d:
            root=Path(d);data=root/'data';data.mkdir();value,payload,expected=fixture(fixtures,profile)
            publish_resources(value,payload,data,12,hole='video-5');value=protect_snapshot(value,data)
            process=subprocess.Popen([runner,'--registry',str(root/'registry')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
            def rpc(action,**kw):
                process.stdin.write(json.dumps(dict(action=action,**kw))+'\n');process.stdin.flush()
                line=process.stdout.readline();assert line,process.stderr.read();return json.loads(line)
            def open_():
                result=rpc('open',directory=str(data),session=value);assert result['ok'],result
            def publish(seconds,complete=False):
                previous=copy.deepcopy(value);publish_resources(value,payload,data,seconds)
                if complete:
                    for t in value['tracks']:t['state']='complete'
                    value['writer']['state']='complete'
                value['revision']=str(int(value['revision'])+1)
                protected=protect_snapshot(value,data,previous=previous);value.clear();value.update(protected)
                result=rpc('publish',session=value);assert result['ok'],result
            def read(offset,count,track=0):return rpc('read',track=track,offset=str(offset),length=str(count))
            def readers():
                result=[];after=''
                while True:
                    page=rpc('readers',version=1,sessionId=value['sessionId'],generation=value['generation'],**({'afterReaderId':after} if after else {}))
                    result.extend(page['readers']);after=page.get('nextReaderId','')
                    if not after:return result
            try:check(value,payload,expected,data,rpc,open_,publish,read,readers);checks.append(name)
            finally:
                process.stdin.close()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait();raise
                error=process.stderr.read();process.stdout.close();process.stderr.close();assert process.returncode==0,error
    def prefix(v,p,e,d,rpc,open_,pub,read,readers):
        open_();assert rpc('status')['segments']
        cutoff=len(p['video-init'])+sum(len(p['video-'+str(i)]) for i in range(5))
        assert read(cutoff,8)['bytesRead']==-1
        boundary=len(p['video-init']);assert read(boundary-4,16)['hex']==e['video'][boundary-4:boundary+12].hex()
        assert read(cutoff-4,16)['hex']==e['video'][cutoff-4:cutoff].hex()
        pub(14);assert read(cutoff-4,16)['hex']==e['video'][cutoff-4:cutoff+12].hex()
        assert read(boundary-4,16)['hex']==e['video'][boundary-4:boundary+12].hex()
        pub(100,True)
        for index,kind in enumerate(('video','audio')):
            for at in range(0,len(e[kind]),65536):assert read(at,min(65536,len(e[kind])-at),index)['hex']==e[kind][at:at+65536].hex()
            assert read(len(e[kind]),8,index)['bytesRead']==0
        assert len(readers())==len(v['resources']),(len(readers()),len(v['resources']));rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
    case('gap-cross-boundary-append-offsets-complete-eof-and-last-owner-leases',prefix)
    def reject(v,p,e,d,rpc,open_,pub,read,readers,change):
        change(v);answer=rpc('open',directory=str(d),session=v);assert not answer['ok'],answer;assert not readers()
    for name,change in [
        ('discontinuity',lambda v:v['tracks'][0]['source']['segments'][1].update(discontinuity=True)),
        ('configuration-change',lambda v:v['tracks'][0]['source']['segments'][1].update(configurationId='different',discontinuity=True,initializationResourceId='video-init')),
        ('sequence-gap',lambda v:v['tracks'][0]['source']['segments'][1].update(sequence='10')),
        ('timestamp-offset',lambda v:v['tracks'][0]['source']['segments'][0].update(timestampOffset='1')),
        ('wrong-packet-duration',lambda v:v['tracks'][0]['source']['segments'][-1].update(duration='1')),
        ('unknown-codec',lambda v:v['tracks'][0].update(codecHint='vp8')),
        ('optional-track',lambda v:v['tracks'][1].update(required=False)),
    ]:
        # Validate the last segment's duration only after publishing that segment.
        def check(*args,change=change,name=name):
            if name=='wrong-packet-duration':
                v,p,e,d,rpc,open_,pub,read,readers=args;publish_resources(v,p,d,100)
            reject(*args,change=change)
        case('reject-'+name,check)
    def appended(v,p,e,d,rpc,open_,pub,read,readers):
        full=copy.deepcopy(v);v['resources']=[r for r in v['resources'] if r['id'].endswith('-init')]
        for t in v['tracks']:t['source']['segments']=[]
        open_();assert read(len(p['video-init']),8)['bytesRead']==-1
        previous=copy.deepcopy(v);full['revision']='2';publish_resources(full,p,d,12)
        n=protect_snapshot(full,d,previous=previous);answer=rpc('publish',session=n);assert answer['ok'],answer
        assert read(len(p['video-init']),16)['hex']==p['video-0'][:16].hex()
        rpc('release');assert not readers()
    case('initialization-only-then-append-resources-and-segments',appended)
    def embedded(v,p,e,d,rpc,open_,pub,read,readers,initialization):
        rid='video-init' if initialization else 'video-0'
        path=d/(rid+'.part');path.write_bytes(path.read_bytes()+p['video-0'] if initialization else p['video-init']+path.read_bytes())
        resource=next(r for r in v['resources'] if r['id']==rid)
        resource.update(length=str(path.stat().st_size),ranges=[dict(offset='0',length=str(path.stat().st_size))])
        resource.pop('integrity',None);protected=protect_snapshot(v,d);v.clear();v.update(protected)
        answer=rpc('open',directory=str(d),session=v);assert not answer['ok'],answer;assert not readers()
    case('initialization-cannot-hide-media-fragments',lambda *args:embedded(*args,initialization=True))
    case('media-cannot-introduce-another-initialization',lambda *args:embedded(*args,initialization=False))
    def latebad(v,p,e,d,rpc,open_,pub,read,readers):
        open_();(d/'video-5.part').write_bytes(p['video-5']);(d/'video-5.part').write_bytes(b'X'+p['video-5'][1:])
        previous=copy.deepcopy(v);publish_resources(v,p,d,14);v['revision']='2'
        # Writer publishes proof of the intended bytes, while the disk is corrupt.
        (d/'video-5.part').write_bytes(p['video-5']);n=protect_snapshot(v,d,previous=previous)
        with (d/'video-5.part').open('r+b') as stream:stream.write(b'X')
        result=rpc('publish',session=n);assert not result['ok'] and result['code']=='integrity-mismatch',result
        assert read(0,8)['bytesRead']==-1
        rpc('release');assert not readers()
    case('new-segment-integrity-error-is-fatal-before-decoder',latebad)
    def truncated(v,p,e,d,rpc,open_,pub,read,readers):
        open_()
        with (d/'video-1.part').open('r+b') as f:f.truncate(8)
        assert not rpc('verify')['ok'];assert read(0,8)['bytesRead']==-1;rpc('release');assert not readers()
    case('pinned-segment-truncation-is-persistent-error',truncated)
    if profile.startswith('hevc'):
        def no_hint(v,p,e,d,rpc,open_,pub,read,readers):
            v['tracks'][0]['codecHint']='';open_();assert rpc('status')['mediaMode']==expected_mode
        case('discover-real-hevc-without-codec-hint',no_hint)
        case('reject-false-h264-hint',lambda *args:reject(*args,change=lambda v:v['tracks'][0].update(codecHint='h264')))
        def mutate_init(v,p,e,d,rpc,open_,pub,read,readers,marker,offset,content,accepted=False):
            path=d/'video-init.part';payload=bytearray(path.read_bytes())
            if marker==b'hvcC' and offset==27 and b'hev1' in payload:
                # Incomplete arrays are legal in hev1, but forbidden in hvc1.
                at=payload.index(b'hev1');payload[at:at+4]=b'hvc1'
            if marker==b'hvc1' and marker not in payload:marker=b'hev1'
            at=payload.index(marker)+offset;payload[at:at+len(content)]=content;path.write_bytes(payload)
            resource=next(r for r in v['resources'] if r['id']=='video-init');resource.pop('integrity',None)
            protected=protect_snapshot(v,d);v.clear();v.update(protected)
            answer=rpc('open',directory=str(d),session=v)
            assert answer['ok']==accepted,(profile,marker,offset,content.hex(),answer)
            if accepted:
                assert rpc('status')['videoCodec']=='hevc';rpc('release')
            assert not readers()
        for name,offset,content in [('version',4,b'\x02'),('profile',5,b'\x04'),('chroma',20,b'\xff'),('length-size',25,b'\x0c'),('missing-parameter-arrays',26,b'\x00'),('incomplete-parameter-arrays',27,b'\x20')]:
            case('reject-hevc-'+name,lambda *args,offset=offset,content=content:mutate_init(*args,b'hvcC',offset,content))
        case('hev1-sample-entry',lambda *args:mutate_init(*args,b'hvc1',0,b'hev1',accepted=True))
    if profile.startswith('av1-'):
        def no_hint(v,p,e,d,rpc,open_,pub,read,readers):
            v['tracks'][0]['codecHint']='';open_();assert rpc('status')['mediaMode']==expected_mode
        case('discover-real-av1-without-codec-hint',no_hint)
        for hint in ('h264','hevc'):
            case('reject-false-'+hint+'-hint',lambda *args,hint=hint:reject(*args,change=lambda v:v['tracks'][0].update(codecHint=hint)))
        def mutate_av1(v,p,e,d,rpc,open_,pub,read,readers,offset,content,remove_sequence=False):
            path=d/'video-init.part';payload=bytearray(path.read_bytes());at=payload.index(b'av1C')
            if remove_sequence:
                size=int.from_bytes(payload[at-4:at],'big');assert size>=20
                payload[at-4:at-4+size]=(12).to_bytes(4,'big')+b'av1C'+payload[at+4:at+8]+(size-12).to_bytes(4,'big')+b'free'+bytes(size-20)
            else:payload[at+offset:at+offset+len(content)]=content
            path.write_bytes(payload);resource=next(r for r in v['resources'] if r['id']=='video-init');resource.pop('integrity',None)
            protected=protect_snapshot(v,d);v.clear();v.update(protected)
            answer=rpc('open',directory=str(d),session=v);assert not answer['ok'],(profile,offset,content.hex(),answer);assert not readers()
        for name,offset,content in [('marker',4,b'\x01'),('version',4,b'\x82'),('high-profile',5,b'\x20'),('level',5,b'\x1f'),('twelve-bit',6,b'\x6c'),('monochrome',6,b'\x1c'),('chroma',6,b'\x08'),('reserved-bits',7,b'\x80'),('presentation-delay',7,b'\x10'),('obu-forbidden',8,b'\x8a'),('obu-missing-size',8,b'\x08'),('obu-zero-payload',9,b'\x00'),('obu-overrun',9,b'\x7f'),('obu-truncated-size',9,b'\x80'*8),('sequence-profile-mismatch',10,b'\x20')]:
            case('reject-av1-'+name,lambda *args,offset=offset,content=content:mutate_av1(*args,offset,content))
        case('require-av1-sequence-in-configuration',lambda *args:mutate_av1(*args,0,b'',remove_sequence=True))
    if video_codec=='vp9':
        def no_hint(v,p,e,d,rpc,open_,pub,read,readers):
            v['tracks'][0]['codecHint']='';open_();assert rpc('status')['mediaMode']==expected_mode
        case('discover-real-vp9-without-codec-hint',no_hint)
        case('reject-false-av1-hint',lambda *args:reject(*args,change=lambda v:v['tracks'][0].update(codecHint='av1')))
        def mutate_vp9(v,p,e,d,rpc,open_,pub,read,readers,offset,byte,accepted=False):
            path=d/'video-init.part';payload=bytearray(path.read_bytes());at=payload.index(b'vpcC')+offset;payload[at]=byte;path.write_bytes(payload)
            resource=next(r for r in v['resources'] if r['id']=='video-init');resource.pop('integrity',None)
            protected=protect_snapshot(v,d);v.clear();v.update(protected)
            answer=rpc('open',directory=str(d),session=v);assert answer['ok']==accepted,(profile,offset,byte,answer)
            if accepted:assert rpc('status')['videoCodec']=='vp9';rpc('release')
            assert not readers()
        for name,offset,byte in [('version',4,0),('flags',7,1),('profile-1',8,1),('profile-3',8,3),('level',9,12),('twelve-bit',10,0xc0),('chroma-422',10,0x84),('chroma-444',10,0x86),('chroma-reserved',10,0x8e),('primaries',11,3),('transfer',12,3),('matrix',13,3),('rgb-with-420',13,0),('nonzero-init-size',15,1)]:
            case('reject-vp9-'+name,lambda *args,offset=offset,byte=byte:mutate_vp9(*args,offset,byte))
        def mismatch(v,p,e,d,rpc,open_,pub,read,readers):
            resource=next(r for r in v['resources'] if r['id']=='video-init');path=d/resource['name'];content=bytearray(path.read_bytes());at=content.index(b'vpcC')
            content[at+8]=0;content[at+10]=0xa0;path.write_bytes(content);resource.pop('integrity',None)
            protected=protect_snapshot(v,d);v.clear();v.update(protected);answer=rpc('open',directory=str(d),session=v);assert not answer['ok'],answer;assert not readers()
        case('reject-vp9-profile-depth-mismatch',mismatch)
    if audio_codec=='opus':
        def audio_auto(v,p,e,d,rpc,open_,pub,read,readers):
            v['tracks'][1]['codecHint']='';open_();assert rpc('status')['audioCodec']=='opus'
        case('discover-real-opus-without-codec-hint',audio_auto)
        case('reject-false-aac-hint',lambda *args:reject(*args,change=lambda v:v['tracks'][1].update(codecHint='aac-lc')))
        def mutate_opus(v,p,e,d,rpc,open_,pub,read,readers,marker,offset,raw):
            path=d/'audio-init.part';payload=bytearray(path.read_bytes());at=payload.index(marker)+offset;payload[at:at+len(raw)]=raw;path.write_bytes(payload)
            resource=next(r for r in v['resources'] if r['id']=='audio-init');resource.pop('integrity',None)
            protected=protect_snapshot(v,d);v.clear();v.update(protected);answer=rpc('open',directory=str(d),session=v)
            assert not answer['ok'],(profile,marker,offset,raw.hex(),answer);assert not readers()
        for name,marker,offset,raw in [('version',b'dOps',4,b'\x01'),('zero-channels',b'dOps',5,b'\x00'),('multichannel',b'dOps',5,b'\x06'),('mapping-family',b'dOps',14,b'\x01'),('preskip-edit-mismatch',b'dOps',6,b'\x01\x39'),('sample-size',b'Opus',22,b'\x00\x18'),('sample-rate',b'Opus',28,b'\x00\x00\xbb\x80'),('media-clock',b'mdhd',16,b'\x00\x00\xac\x44'),('edit-flags',b'elst',7,b'\x01'),('multiple-edits',b'elst',8,b'\x00\x00\x00\x02'),('edit-version',b'elst',4,b'\x02'),('edit-rate',b'elst',20,b'\x00\x02\x00\x00'),('missing-edit-list',b'edts',0,b'free'),('encrypted-entry',b'Opus',0,b'enca')]:
            case('reject-opus-'+name,lambda *args,marker=marker,offset=offset,raw=raw:mutate_opus(*args,marker,offset,raw))
    def cancel(v,p,e,d,rpc,open_,pub,read,readers):
        open_();rpc('cancel');assert read(0,8)['bytesRead']==-1;rpc('release');assert not readers()
    case('cancel-and-close-release-all-resources',cancel)
    return dict(result='PASS',profile=profile,count=len(checks),checks=checks)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True);parser.add_argument('--fixtures',default=os.environ.get('FOXICMP_TEST_MEDIA',str(Path(__file__).resolve().parents[1]/'_bin/qt-test-media')));parser.add_argument('--media-profile',choices=PLAYBACK_PROFILES+('all',),default='h264');a=parser.parse_args();print(json.dumps([run(a.runner,a.fixtures,p) for p in NATIVE_PROFILES] if a.media_profile=='all' else run(a.runner,a.fixtures,a.media_profile)))
