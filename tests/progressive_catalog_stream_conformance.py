"""Actual fMP4 pages -> bridge -> bounded resource I/O. No media decoder/UI."""
import argparse,copy,json,os,subprocess,tempfile
from pathlib import Path
from progressive_catalog_playback import catalog_fixture,catalog_snapshot
from progressive_segment_playback import fixture,PLAYBACK_PROFILES

def run(runner,fixtures):
    checks=[]
    def case(name,check,profile='h264',short=False):
        with tempfile.TemporaryDirectory(prefix='foxicmp-catalog-stream-') as directory:
            root=Path(directory);plan,data,expected=fixture(fixtures,profile) if short else catalog_fixture(fixtures,profile)
            value,expanded=catalog_snapshot(plan,data,root,20,complete=short)
            process=subprocess.Popen([runner,'--registry',str(root/'registry')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
            def rpc(action,**fields):
                process.stdin.write(json.dumps(dict(action=action,**fields))+'\n');process.stdin.flush()
                line=process.stdout.readline();assert line,process.stderr.read();return json.loads(line)
            def readers():
                answer=rpc('readers',version=1,sessionId=plan['sessionId'],generation=plan['generation']);assert answer['ok'],answer;return answer['readers']
            def open_():
                answer=rpc('open',directory=str(root),session=value);assert answer['ok'],answer
            def read(track,at,count):return rpc('read',track=track,offset=str(at),length=str(count))
            def publish(seconds,complete=False):
                nonlocal value,expanded
                plan['revision']=str(int(plan['revision'])+1)
                candidate,full=catalog_snapshot(plan,data,root,seconds,previous=expanded,complete=complete)
                answer=rpc('publish',session=candidate);assert answer['ok'],answer
                value,expanded=candidate,full
            try:
                check(plan,data,expected,root,value,expanded,rpc,readers,open_,read,publish)
                checks.append(name)
            finally:
                process.stdin.close()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait();raise
                error=process.stderr.read();process.stdout.close();process.stderr.close();assert process.returncode==0,error
    def stream(plan,data,expected,root,value,expanded,rpc,readers,open_,read,publish):
        open_();before=rpc('status');assert before['segmentCatalog']['mode']=='paged-fmp4-v1'
        boundary=len(data['video-init'])
        assert read(0,boundary-4,16)['hex']==expected['video'][boundary-4:boundary+12].hex()
        assert len(readers())<=8
        publish(80)
        assert rpc('status')['segmentCatalog']['resourceCount']>128
        assert read(0,boundary-4,16)['hex']==expected['video'][boundary-4:boundary+12].hex()
        publish(100,True)
        for index,kind in enumerate(('video','audio')):
            for at in range(0,len(expected[kind]),65536):
                answer=read(index,at,min(65536,len(expected[kind])-at))
                assert answer['hex']==expected[kind][at:at+65536].hex(),answer
            assert read(index,len(expected[kind]),8)['bytesRead']==0
        assert len(readers())<=8
        rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
    for profile in ('h264','hevc10-opus','av1-10'):
        case('paged-182-resources-real-bytes-seek-offsets-eof-last-owner-'+profile,stream,profile)
    def missing(plan,data,expected,root,value,expanded,rpc,readers,open_,read,publish):
        path=root/value['segmentCatalog'][0]['name'];raw=path.read_bytes();path.unlink()
        assert rpc('preflight',session=value)['ok'] # Declaration only; no page I/O on UI.
        answer=rpc('open',directory=str(root),session=value)
        assert not answer['ok'] and answer['code']=='waiting-index',answer
        assert not readers();path.write_bytes(raw);open_()
        before=rpc('status');plan['revision']='2'
        candidate,_=catalog_snapshot(plan,data,root,80,previous=expanded)
        path=root/candidate['segmentCatalog'][-1]['name'];raw=path.read_bytes();path.unlink()
        answer=rpc('publish',session=candidate)
        assert not answer['ok'] and answer['code']=='waiting-index',answer
        assert rpc('status')['revision']==before['revision']
        assert read(0,0,8)['bytesRead']==8
        path.write_bytes(raw);assert rpc('publish',session=candidate)['ok']
    case('missing-initial-or-appended-page-no-io-preflight-retry-preserves-old-stream',missing)
    def malformed(plan,data,expected,root,value,expanded,rpc,readers,open_,read,publish):
        bad=copy.deepcopy(value);bad['segmentCatalog'][-1]['name']='../escape.json'
        answer=rpc('preflight',session=bad);assert not answer['ok'] and answer['code']=='invalid_snapshot',answer
        assert not readers();open_()
        plan['revision']='2';candidate,_=catalog_snapshot(plan,data,root,80,previous=expanded)
        path=root/candidate['segmentCatalog'][-1]['name'];raw=path.read_bytes();path.write_bytes(raw.replace(b'video-config',b'xxxxx-config',1))
        answer=rpc('publish',session=candidate)
        assert not answer['ok'] and answer['code']=='integrity-mismatch',answer
        assert rpc('status')['revision']=='1' and read(0,0,8)['bytesRead']==8
        path.write_bytes(raw);assert rpc('publish',session=candidate)['ok']
    case('bad-envelope-and-corrupt-new-page-never-commit-or-poison-old-stream',malformed)
    def sticky(plan,data,expected,root,value,expanded,rpc,readers,open_,read,publish):
        open_();other=copy.deepcopy(value);other.pop('segmentCatalog');other['revision']='2'
        answer=rpc('publish',session=other);assert not answer['ok'] and answer['code']=='unsupported_media',answer
        assert read(0,0,8)['bytesRead']==8
    case('catalog-mode-cannot-be-downgraded-mid-session',sticky)
    def sha(plan,data,expected,root,value,expanded,rpc,readers,open_,read,publish):
        value['resources'][0].pop('integrity')
        answer=rpc('open',directory=str(root),session=value)
        assert not answer['ok'] and answer['code']=='integrity-required',answer
        assert not readers()
    case('missing-resource-sha-rejected-before-media-leases',sha)
    def codecs(plan,data,expected,root,value,expanded,rpc,readers,open_,read,publish):
        open_();assert rpc('status')['segmentCatalog']['resourceCount']>0
        for index,kind in enumerate(('video','audio')):
            for at in range(0,len(expected[kind]),65536):assert read(index,at,min(65536,len(expected[kind])-at))['hex']==expected[kind][at:at+65536].hex()
        rpc('release');assert not readers()
    for profile in PLAYBACK_PROFILES:case('all-confirmed-bytes-catalog-codec-'+profile,codecs,profile,True)
    return dict(result='PASS',scope='catalog-bridge-native',count=len(checks),checks=checks,
                maxLongFixtureResources=183,maxActiveMediaLeases=8,decoderTested=False)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True);parser.add_argument('--fixtures',default=os.environ.get('FOXICMP_TEST_MEDIA',str(Path(__file__).resolve().parents[1]/'_bin/qt-test-media')))
    args=parser.parse_args();print(json.dumps(run(args.runner,args.fixtures)))
