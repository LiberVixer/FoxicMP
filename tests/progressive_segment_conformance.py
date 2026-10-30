"""Bounded segment staging/indexing through actual pinned ResourceSource files."""
import argparse,copy,hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_integrity import protect_snapshot


def snapshot():
    resources=[dict(id=rid,name=rid+'.part',length=str(len(data)),ranges=[dict(offset='0',length=str(len(data)))],state='complete') for rid,data in [('init',b'INIT'),('seg1',b'FIRST-SEGMENT'),('seg2',b'SECOND-SEGMENT')]]
    segments=[dict(resourceId='seg'+str(i+1),sequence=str(i+1),start=str(i*100000000),duration='100000000',timestampOffset='0',discontinuity=False,configurationId='c1') for i in range(2)]
    segments[0]['initializationResourceId']='init'
    return dict(schemaVersion=2,sessionId='a'*32,generation='1',revision='1',writer=dict(instanceId='b'*32,state='downloading'),
        timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='200000000',timestampOffset='0'),resources=resources,
        tracks=[dict(id='video',kind='video',required=True,codecHint='h264',containerHint='fmp4',state='downloading',initializationReady=True,source=dict(segments=segments))])


class Fixture:
    def __init__(self,runner,root):
        self.root=root;self.data=root/'data';self.data.mkdir();self.value=snapshot()
        for name,data in [('init',b'INIT'),('seg1',b'FIRST-SEGMENT'),('seg2',b'SECOND-SEGMENT')]:self.path(name).write_bytes(data)
        self.process=subprocess.Popen([runner,'--registry',str(root/'registry')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
    def path(self,name):return self.data/(name+'.part')
    def rpc(self,action,**fields):
        self.process.stdin.write(json.dumps(dict(action=action,**fields))+'\n');self.process.stdin.flush();line=self.process.stdout.readline()
        assert line,self.process.stderr.read();return json.loads(line)
    def open(self):
        answer=self.rpc('open',directory=str(self.data),session=self.value);assert answer['ok'],answer
    def stage(self,index=0,action='stage',track='video'):return self.rpc(action,trackId=track,segmentIndex=str(index))
    def publish(self):
        self.value['revision']=str(int(self.value['revision'])+1)
        return self.rpc('publish',session=self.value)
    def readers(self):return self.rpc('readers',version=1,sessionId=self.value['sessionId'],generation=self.value['generation'])['readers']
    def close(self):
        self.process.stdin.close()
        try:self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:self.process.kill();self.process.wait();raise
        error=self.process.stderr.read();self.process.stdout.close();self.process.stderr.close();assert self.process.returncode==0,error


def boxes(content):
    at=0
    while at<len(content):
        n=int.from_bytes(content[at:at+4],'big');header=8
        if n==1:n=int.from_bytes(content[at+8:at+16],'big');header=16
        assert n>=header and at+n<=len(content),(at,n)
        yield at,n,content[at+4:at+8];at+=n


def run(runner,fixtures):
    checks=[]
    def case(name,action):
        with tempfile.TemporaryDirectory(prefix='foxicmp-segment-') as directory:
            f=Fixture(runner,Path(directory))
            try:action(f);checks.append(name)
            finally:f.close()
    def exact(f):
        f.open();first=f.stage();second=f.stage(1)
        assert first['code']=='data' and bytes.fromhex(first['hex'])==b'INITFIRST-SEGMENT',first
        assert second['code']=='data' and bytes.fromhex(second['hex'])==b'INITSECOND-SEGMENT',second
        assert second['initializationResourceId']=='init';assert len(f.readers())==3
    case('complete-init-and-inherited-init-on-next-segment',exact)
    def late(f):
        f.value['resources'][2].update(length=None,ranges=[],state='downloading');f.path('seg2').write_bytes(b'SECON');f.open()
        answer=f.stage(1);assert answer['code']=='waiting-range' and answer['bytesRead']==0 and not answer['hex'],answer
        assert not f.readers(),f.readers()
        f.path('seg2').write_bytes(b'SECOND-SEGMENT');f.value['resources'][2].update(length='14',ranges=[dict(offset='0',length='14')],state='complete')
        assert f.publish()['ok'];assert f.stage(1)['hex']==b'INITSECOND-SEGMENT'.hex()
    case('unknown-length-waits-without-partial-result-or-file-lease',late)
    def hole(f):
        f.value['resources'][1].update(state='downloading',ranges=[dict(offset='0',length='2'),dict(offset='4',length='9')]);f.open()
        assert f.stage()['code']=='waiting-range';assert not f.readers()
    case('confirmed-hole-is-waiting-not-eof',hole)
    def unpublished(f):
        f.value['tracks'][0]['source']['segments']=[];f.open();assert f.stage()['code']=='waiting-segment'
        f.value['tracks'][0]['source']['segments']=snapshot()['tracks'][0]['source']['segments'];assert f.publish()['ok'];assert f.stage()['code']=='data'
    case('unpublished-segment-waits-until-next-revision',unpublished)
    def wrong(f):
        f.open();assert f.stage()['code']=='data';old=copy.deepcopy(f.value)
        f.value['tracks'][0]['source']['segments'][1]['timestampOffset']='-1'
        assert not f.publish()['ok'];assert f.stage(1)['hex']==b'INITSECOND-SEGMENT'.hex()
        f.value=old;f.value['generation']='2';assert not f.publish()['ok'];assert f.stage()['code']=='data'
    case('invalid-update-and-generation-preserve-the-current-source',wrong)
    def limit(f,which,size):
        r=f.value['resources'][which];r.update(length=str(size),ranges=[dict(offset='0',length=str(size))]);f.open()
        answer=f.stage();assert answer['code']=='segment-too-large' and answer['bytesRead']==0,answer
        assert not f.readers()
    case('media-size-limit-is-error-before-reading',lambda f:limit(f,1,16*1024*1024+1))
    case('initialization-size-limit-is-error-before-reading',lambda f:limit(f,0,1024*1024+1))
    def zero(f):
        f.value['resources'][1].update(length='0',ranges=[]);f.path('seg1').write_bytes(b'');f.open()
        assert f.stage()['code']=='unsupported_media'
    case('empty-resource-does-not-report-a-staged-segment',zero)
    def cancel(f):
        f.open();assert f.stage()['code']=='data';f.rpc('cancel');answer=f.stage();assert answer['code']=='cancelled' and answer['bytesRead']==0
    case('cancel-discards-output',cancel)
    def writer(f):
        f.open();f.value['writer']['state']='failed';assert f.publish()['ok'];assert f.stage()['code']=='writer-failed'
    case('writer-failure-is-not-eof',writer)
    def missing(f):
        f.path('seg1').unlink();f.open();answer=f.stage();assert answer['code']=='missing-resource' and not answer['bytesRead'],answer
    case('missing-completed-resource-is-an-error',missing)
    def truncated(f):
        f.open();f.path('seg1').write_bytes(b'F');answer=f.stage();assert answer['code']=='source-truncated' and not answer['bytesRead'],answer
    case('truncation-never-returns-partial-initialization',truncated)
    def integrity(f):
        f.value=protect_snapshot(f.value,f.data,block_bytes=4);f.path('seg1').write_bytes(b'XIRST-SEGMENT');f.open()
        answer=f.stage();assert answer['code']=='integrity-mismatch' and answer['bytesRead']==0,answer
    case('sha256-failure-discards-whole-staged-output',integrity)
    def held(f):
        f.open();assert f.stage()['code']=='data';f.rpc('retain');f.rpc('release')
        assert not f.rpc('released')['released'] and len(f.readers())==2
        f.rpc('dropRetained');assert f.rpc('released')['released']
        assert all(r['released'] and r['reason']=='files-closed' for r in f.readers())
    case('leases-close-after-the-last-stager-owner',held)
    def renamed(f):
        f.open();first=f.stage();f.path('seg1').rename(f.path('renamed'));f.path('seg1').write_bytes(b'X'*13)
        assert f.stage()['sha256']==first['sha256']
    case('rename-keeps-the-original-pinned-resource',renamed)
    def external(f):
        f.value['tracks'][0]['initializationResourceId']='init'
        del f.value['tracks'][0]['source']['segments'][0]['initializationResourceId']
        f.open();assert f.stage(1)['hex']==b'INITSECOND-SEGMENT'.hex()
    case('track-level-initialization-is-inherited',external)
    # Parse genuine moof/mdat resources. Expected packet bytes/timestamps come
    # from ffprobe's independent demux of the original generated fixture.
    for kind,name in [('video','video.mp4'),('audio','audio.m4a')]:
        def packets(f,kind=kind,name=name):
            path=Path(fixtures)/'progressive'/name;content=path.read_bytes();top=list(boxes(content))
            fragments=[(at,n) for i,(at,n,t) in enumerate(top) if t==b'moof' and i+1<len(top) and top[i+1][2]==b'mdat'][:2]
            init=content[:fragments[0][0]];f.path('init').write_bytes(init)
            raw=subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_entries','packet=pos,size,pts_time,dts_time,duration_time,flags','-of','json',str(path)],text=True)
            expected=json.loads(raw)['packets'];f.value['resources'][0].update(length=str(len(init)),ranges=[dict(offset='0',length=str(len(init)))])
            for i,(at,n) in enumerate(fragments):
                following=next(b for b in top if b[0]==at+n);part=content[at:following[0]+following[1]];f.path('seg'+str(i+1)).write_bytes(part)
                f.value['resources'][i+1].update(length=str(len(part)),ranges=[dict(offset='0',length=str(len(part)))])
            from decimal import Decimal
            for i,(at,n) in enumerate(fragments):
                end=next(b[0]+b[1] for b in top if b[0]==at+n)
                subset=[p for p in expected if at<=int(p['pos'])<end]
                start=min(Decimal(p['pts_time']) for p in subset)
                # Some ffprobe versions omit packet duration for fMP4. The
                # next fragment boundary independently defines this interval.
                stop=min(Decimal(p['pts_time']) for p in expected if int(p['pos'])>=end)
                segment=f.value['tracks'][0]['source']['segments'][i]
                segment.update(start=str(int(start*10000000)),duration=str(int((stop-start)*10000000)))
            track=f.value['tracks'][0];track.update(kind=kind,codecHint='h264' if kind=='video' else 'aac-lc')
            f.value=protect_snapshot(f.value,f.data);f.open()
            for i,(at,n) in enumerate(fragments):
                staged=f.stage(i,action='demux');assert staged['code']=='data' and staged['packets'],staged
                end=next(b[0]+b[1] for b in top if b[0]==at+n)
                subset=[p for p in expected if at<=int(p['pos'])<end];assert len(subset)==len(staged['packets'])
                from decimal import Decimal
                for actual,packet in zip(staged['packets'],subset):
                    position,size=int(packet['pos']),int(packet['size']);assert size==actual['size']
                    assert actual['sha256']==hashlib.sha256(content[position:position+size]).hexdigest()
                    # ffprobe prints six decimal places; allow one microsecond.
                    for target,field in [('start','pts_time'),('decode','dts_time')]:
                        assert abs(int(actual[target])-int(Decimal(packet[field])*10000000))<=10,(actual,packet)
                    assert actual['sync']==('K' in packet['flags'])
            # Malformed fragment tables are rejected without returning bytes.
            f.rpc('release');part=bytearray(f.path('seg1').read_bytes());at=part.index(b'trun')+8;part[at:at+4]=(65537).to_bytes(4,'big');f.path('seg1').write_bytes(part)
            f.value=protect_snapshot(f.value,f.data);f.open();bad=f.stage(action='demux');assert not bad['ok'] and bad['code']=='segment-too-large',bad
        case('real-'+kind+'-fragment-packets-match-ffprobe-and-inherit-init',packets)
    return dict(result='PASS',passed=len(checks),checks=checks,exitCode=0)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True);parser.add_argument('--fixtures',default=os.environ.get('FOXICMP_TEST_MEDIA',str(Path(__file__).resolve().parents[1]/'_bin/qt-test-media')));args=parser.parse_args()
    print(json.dumps(run(args.runner,args.fixtures)))
