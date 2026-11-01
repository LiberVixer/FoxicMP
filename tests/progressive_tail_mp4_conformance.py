"""Tail-index playback projection and metadata-only range recommendations."""
import argparse,copy,json,tempfile,time
from pathlib import Path
from progressive_indexed_mp4_conformance import run as indexed_run
from progressive_tail_mp4_fixture import fixture,publish,prefix_at,metadata_start,boxes
from progressive_mux_ts_conformance import Rpc
from progressive_integrity import protect_snapshot
from progressive_backend_demux_conformance import wait,forget


def probe_contract(runner):
    checks=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-tail-probe-') as temp:
        root=Path(temp);data=root/'data';data.mkdir();model,raw,_,groups,scales=fixture(root,seconds=4)
        rpc=Rpc([runner,'--registry',str(root/'readers')])
        def start(value,wait_ms=0):
            r=rpc('resourceProbe',version=1,session=value,sessionDirectory=str(data),reads=[],index=dict(resourceId='mux'),waitMilliseconds=wait_ms)
            assert r['ok'],r
            return r['probeId']
        def result(value):
            id=start(value);answer=wait(rpc,id);forget(rpc,id);return answer
        try:
            value=publish(model,raw,data,0,tail_bytes=0)
            answer=result(value);hint=answer['index']['priorityRanges'][0]
            assert hint==dict(resourceId='mux',offset='0',length='16',reason='mp4-atom-header',advisory=True),answer
            checks.append('missing-head-recommends-bounded-header')
            prefix=prefix_at(groups,scales,1)
            value=publish(model,raw,data,prefix,tail_bytes=0,previous=value)
            answer=result(value);hint=answer['index']['priorityRanges'][0];tail=metadata_start(raw)
            assert hint['offset']==str(tail) and hint['length']=='16',answer
            checks.append('mdat-payload-skipped-without-reading-hole')
            value=publish(model,raw,data,prefix,tail_bytes=16,previous=value)
            answer=result(value);hint=answer['index']['priorityRanges'][0]
            assert hint['offset']==str(tail) and hint['length']==str(len(raw)-tail) and hint['reason']=='mp4-index',answer
            checks.append('tail-header-recommends-whole-index')
            id=start(value,10000)
            value=publish(model,raw,data,prefix,previous=value)
            assert rpc('resourceProbeUpdate',version=1,probeId=id,session=value)['ok']
            answer=wait(rpc,id);assert answer['allReadable'] and answer['index']['code']=='data' and answer['index']['priorityRanges']==[],answer
            assert answer['index']['tailIndex'] and answer['index']['moovOffset']==str(tail),answer
            forget(rpc,id);checks.append('metadata-publication-wakes-waiter')
            # Cancellation must remove obsolete download recommendations, even
            # when cancelling an already-finished metadata query.
            id=start(value);wait(rpc,id);answer=rpc('resourceProbeCancel',version=1,probeId=id)
            assert answer['index']['code']=='cancelled' and not answer['index']['priorityRanges'] and not answer['allReadable'],answer
            forget(rpc,id);checks.append('cancel-clears-advisory-ranges')
            with (data/'mux.part').open('r+b') as stream:stream.seek(tail+20);stream.write(b'changed')
            answer=result(value);assert answer['state']=='error' and answer['index']['code']=='integrity-mismatch',answer
            checks.append('metadata-integrity-failure-is-terminal')
            assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
            checks.append('all-probe-handles-released-and-acknowledged')
        finally:rpc.close()
    return checks



def boundaries(runner):
    checks=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-tail-boundaries-') as temp:
        root=Path(temp);model,raw,_,groups,scales=fixture(root,seconds=4)
        start=metadata_start(raw);prefix=prefix_at(groups,scales,1)
        cases=[('third-range-rejected',raw,'third','unsupported_media'),
               ('separate-media-island-rejected',raw,'island','unsupported_media'),
               ('metadata-only-cannot-open-decoder',raw,'no-prefix','waiting-index'),
               ('metadata-budget-enforced',raw+(1048576).to_bytes(4,'big')+b'free'+bytes(1048568),None,'unsupported_media'),
               ('atom-count-enforced',raw+(b'\0\0\0\x08free'*(17-len(list(boxes(raw))))),None,'unsupported_media')]
        for name,content,kind,code in cases:
            data=root/name;data.mkdir();plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(content))
            value=publish(plan,content,data,prefix)
            resource=value['resources'][0]
            if kind=='third':resource['ranges'].insert(1,dict(offset=str(start-64),length='16'))
            if kind=='island':resource['ranges'][1]=dict(offset=str(start-32),length=str(len(content)-start+32))
            if kind=='no-prefix':resource['ranges']=resource['ranges'][1:]
            value=protect_snapshot(value,data)
            rpc=Rpc([runner,'--registry',str(root/(name+'-readers'))])
            try:
                answer=rpc('open',directory=str(data),session=value)
                assert not answer['ok'] and answer['code']==code,(name,answer)
                assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
                checks.append(name)
            finally:rpc.close()
    return dict(result='PASS',checks=checks,count=len(checks))



def head_compatibility(runner):
    # The old head-moov profile was bounded by metadata bytes, not atom count.
    # Additional free atoms must not silently acquire the new tail-only limit.
    from progressive_indexed_mp4_fixture import fixture as make,publish as write
    with tempfile.TemporaryDirectory(prefix='foxicmp-head-atom-compat-') as temp:
        root=Path(temp);model,raw,_,groups,scales=make(root,seconds=4)
        changed=bytearray(raw);delta=17*8;at=0
        while True:
            at=raw.find(b'stco',at)
            if at<0:break
            count=int.from_bytes(raw[at+8:at+12],'big')
            for i in range(count):
                pos=at+12+4*i;changed[pos:pos+4]=(int.from_bytes(raw[pos:pos+4],'big')+delta).to_bytes(4,'big')
            at+=4
        stop=next(size for at,size,kind,_ in boxes(raw) if at==0)
        changed=changed[:stop]+b'\0\0\0\x08free'*17+changed[stop:]
        model['resources'][0]['length']=str(len(changed))
        value=write(model,changed,root,len(changed),complete=True)
        rpc=Rpc([runner,'--registry',str(root/'readers')])
        try:
            answer=rpc('open',directory=str(root),session=value);assert answer['ok'],answer
            index=rpc('index');assert [t['samples'] for t in index['tracks']]==[len(g) for g in groups],index
            for k,group in enumerate(groups):
                size=int(rpc('layout')['tracks'][k]['available']);parts=[]
                for at in range(0,size,65536):
                    r=rpc('read',decoder=True,track=k,offset=str(at),length=str(min(65536,size-at)));assert r['bytesRead']>0,r;parts.append(bytes.fromhex(r['hex']))
                path=root/('view-'+str(k)+'.mp4');path.write_bytes(b''.join(parts))
                import subprocess
                packets=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_data_hash','sha256','-of','json',str(path)]))['packets']
                assert [p['data_hash'] for p in packets]==[p['data_hash'] for p in group]
            rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
        finally:rpc.close()
    return dict(result='PASS',checks=['legacy-head-profile-keeps-more-than-16-atoms-with-exact-payloads'],count=1)


def run(runner,resource_runner=None):
    result=indexed_run(runner,fixture_provider=fixture,publisher=publish,tail_mode=True)
    if resource_runner:
        result['checks']+=probe_contract(resource_runner);result['count']=len(result['checks'])
    result['boundaryChecks']=boundaries(runner);result['checks']+=result['boundaryChecks']['checks'];result['count']=len(result['checks'])
    result['tailIndex']=True;result['sparseMediaPlayback']=False;return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);p.add_argument('--resource-runner');p.add_argument('--boundaries-only',action='store_true');p.add_argument('--head-compatibility-only',action='store_true');a=p.parse_args();print(json.dumps(head_compatibility(a.runner) if a.head_compatibility_only else boundaries(a.runner) if a.boundaries_only else run(a.runner,a.resource_runner)))
