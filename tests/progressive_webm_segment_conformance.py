"""Whole-Cluster native streaming, exact timestamps and rejection before decoding."""
import argparse,copy,json,os,subprocess,tempfile
from pathlib import Path
from progressive_webm_segment_playback import fixture,PROFILES
from progressive_segment_playback import publish_resources
from progressive_catalog_playback import catalog_snapshot
from progressive_integrity import protect_snapshot
from progressive_webm_fixtures import element,locate

def run(runner,fixtures,case_filter=None):
    checks=[]
    def case(profile,name,check):
        if case_filter and case_filter not in name:return
        with tempfile.TemporaryDirectory(prefix='foxicmp-webm-segments-') as tmp:
            root=Path(tmp);v,p,e=fixture(fixtures,profile);publish_resources(v,p,root,12,hole='video-5');v=protect_snapshot(v,root)
            proc=subprocess.Popen([runner,'--registry',str(root/'registry')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
            def rpc(action,**fields):
                proc.stdin.write(json.dumps(dict(action=action,**fields))+'\n');proc.stdin.flush()
                line=proc.stdout.readline();assert line,proc.stderr.read();return json.loads(line)
            def readers():
                rows=[];after=''
                while True:
                    a=rpc('readers',version=1,sessionId=v['sessionId'],generation=v['generation'],**({'afterReaderId':after} if after else {}));assert a['ok'],a
                    rows+=a['readers'];after=a.get('nextReaderId','')
                    if not after:return rows
            def read(at,n,track=0):return rpc('read',offset=str(at),length=str(n),track=track)
            try:check(v,p,e,root,rpc,read,readers);checks.append(profile+':'+name)
            finally:
                proc.stdin.close()
                try:proc.wait(timeout=10)
                except subprocess.TimeoutExpired:proc.kill();proc.wait();raise
                error=proc.stderr.read();proc.stdout.close();proc.stderr.close();assert proc.returncode==0,error
    def opened(v,d,rpc):
        answer=rpc('open',directory=str(d),session=v);assert answer['ok'],answer
    def prefix(v,p,e,d,rpc,read,readers):
        opened(v,d,rpc);s=rpc('status');assert s['mediaMode']=='segmented-webm-'+v['tracks'][0]['codecHint']+'-opus'
        boundary=len(p['video-init']);cutoff=boundary+sum(len(p['video-'+str(i)]) for i in range(5))
        assert read(cutoff,8)['bytesRead']==-1
        assert read(boundary-4,16)['hex']==e['video'][boundary-4:boundary+12].hex()
        before=copy.deepcopy(v);publish_resources(v,p,d,100);v['revision']='2';v['writer']['state']='complete'
        for t in v['tracks']:t['state']='complete'
        v=protect_snapshot(v,d,previous=before);answer=rpc('publish',session=v);assert answer['ok'],answer
        for index,kind in enumerate(('video','audio')):
            for at in range(0,len(e[kind]),65536):assert read(at,min(65536,len(e[kind])-at),index)['hex']==e[kind][at:at+65536].hex()
            assert read(len(e[kind]),8,index)['bytesRead']==0
        rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
    def reject(v,p,e,d,rpc,read,readers,change):
        change(v,p,d)
        for r in v['resources']:r.pop('integrity',None)
        v=protect_snapshot(v,d);a=rpc('open',directory=str(d),session=v);assert not a['ok'] and a['code'] in ('unsupported_media','invalid_snapshot'),a
        assert not readers()
    def mutate_resource(v,p,d,rid,edit):
        path=d/(rid+'.part');raw=edit(path.read_bytes());path.write_bytes(raw)
        r=next(r for r in v['resources'] if r['id']==rid);r.update(length=str(len(raw)),ranges=[dict(offset='0',length=str(len(raw)))])
    def unknown_cluster(raw):
        _,body,_=element(raw,0);ids=4;n=body-ids
        return raw[:ids]+bytes([((1<<(8-n))-1)])+b'\xff'*(n-1)+raw[body:]
    def known_segment(raw):
        _,_,at=element(raw,0);_,body,_=element(raw,at)
        n=body-at-4;return raw[:at+4]+((1<<(7*n))+len(raw)).to_bytes(n,'big')+raw[body:]
    def wrong_clock(raw):
        _,b,e=locate(raw,0x2ad7b1);return raw[:b]+(500000).to_bytes(e-b,'big')+raw[e:]
    def no_keyframe(raw):
        _,body,end=element(raw,0)
        at=body
        while at<end:
            tag,b,stop=element(raw,at)
            if tag==0xa3:
                from progressive_webm_fixtures import width
                x=b+width(raw[b])+2;r=bytearray(raw);r[x]&=127;return bytes(r)
            at=stop
        raise AssertionError('Expected SimpleBlock fixture')
    for profile in PROFILES:
        case(profile,'gap-cross-resource-bytes-append-eof-and-leases',prefix)
        mutations=[
            ('discontinuity',lambda v,p,d:v['tracks'][0]['source']['segments'][1].update(discontinuity=True)),
            ('configuration-change',lambda v,p,d:v['tracks'][0]['source']['segments'][1].update(configurationId='different',discontinuity=True,initializationResourceId='video-init')),
            ('timestamp-rebase',lambda v,p,d:v['tracks'][0]['source']['segments'][0].update(timestampOffset='1')),
            ('wrong-envelope',lambda v,p,d:v['tracks'][0]['source']['segments'][0].update(duration='1')),
            ('wrong-codec-hint',lambda v,p,d:v['tracks'][0].update(codecHint='h264')),
            ('vorbis-not-enabled',lambda v,p,d:v['tracks'][1].update(codecHint='vorbis')),
            ('mixed-container',lambda v,p,d:v['tracks'][1].update(containerHint='fmp4')),
            ('init-hides-cluster',lambda v,p,d:mutate_resource(v,p,d,'video-init',lambda raw:raw+p['video-0'])),
            ('media-hides-init',lambda v,p,d:mutate_resource(v,p,d,'video-0',lambda raw:p['video-init']+raw)),
            ('truncated-init',lambda v,p,d:mutate_resource(v,p,d,'video-init',lambda raw:raw[:-1])),
            ('known-segment-size',lambda v,p,d:mutate_resource(v,p,d,'video-init',known_segment)),
            ('unsupported-clock',lambda v,p,d:mutate_resource(v,p,d,'video-init',wrong_clock)),
            ('truncated-cluster',lambda v,p,d:mutate_resource(v,p,d,'video-0',lambda raw:raw[:-1])),
            ('unknown-cluster-size',lambda v,p,d:mutate_resource(v,p,d,'video-0',unknown_cluster)),
            ('segment-missing-keyframe',lambda v,p,d:mutate_resource(v,p,d,'video-1',no_keyframe)),
            ('unbounded-time-gap',lambda v,p,d:v['tracks'][0]['source']['segments'][1].update(start='10100000')),
        ]
        for name,change in mutations:case(profile,'reject-'+name,lambda *args,change=change:reject(*args,change=change))
        def multiple(v,p,e,d,rpc,read,readers):
            t=v['tracks'][0];first,second=t['source']['segments'][:2]
            payload=p['video-0']+p['video-1'];mutate_resource(v,p,d,'video-0',lambda raw:payload)
            first['duration']=str(int(second['start'])+int(second['duration'])-int(first['start']))
            del t['source']['segments'][1]
            for i,part in enumerate(t['source']['segments']):part['sequence']=str(i+1)
            v['resources']=[r for r in v['resources'] if r['id']!='video-1']
            for r in v['resources']:r.pop('integrity',None)
            v=protect_snapshot(v,d);opened(v,d,rpc)
            boundary=len(p['video-init']);assert read(boundary+len(p['video-0'])-4,16)['hex']==payload[len(p['video-0'])-4:len(p['video-0'])+12].hex()
            rpc('release');assert not readers()
        case(profile,'multiple-whole-clusters-in-one-media-resource',multiple)
        def initial(v,p,e,d,rpc,read,readers):
            full=copy.deepcopy(v);v['resources']=[r for r in v['resources'] if r['id'].endswith('-init')]
            for t in v['tracks']:t['source']['segments']=[]
            opened(v,d,rpc);assert read(len(p['video-init']),8)['bytesRead']==-1
            publish_resources(full,p,d,12);full['revision']='2';full=protect_snapshot(full,d,previous=v)
            a=rpc('publish',session=full);assert a['ok'],a
            assert read(len(p['video-init']),16)['hex']==p['video-0'][:16].hex()
            rpc('release');assert not readers()
        case(profile,'initialization-only-then-append',initial)
        def paged(v,p,e,d,rpc,read,readers):
            # New session declaration has not yet been opened; publish complete pages.
            value,expanded=catalog_snapshot(v,p,d,100,complete=True);opened(value,d,rpc)
            assert rpc('status')['segmentCatalog']['mode']=='paged-webm-v1'
            for index,kind in enumerate(('video','audio')):
                for at in range(len(e[kind])-1,-1,-65536):
                    start=max(0,at-65535);assert read(start,at-start+1,index)['hex']==e[kind][start:at+1].hex()
            assert len(readers())<=8;rpc('release');assert not readers()
        case(profile,'paged-catalog-backward-reads-and-eight-handle-bound',paged)
    return dict(result='PASS',checks=checks,profiles=list(PROFILES),networkRequests=0)

if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('--case');a.add_argument('--runner',required=True);a.add_argument('--fixtures',default=os.environ.get('FOXICMP_TEST_MEDIA','_bin/qt-test-media'));args=a.parse_args()
    print(json.dumps(run(args.runner,args.fixtures,args.case)))
