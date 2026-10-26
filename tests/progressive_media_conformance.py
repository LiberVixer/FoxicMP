"""Native schema-2 media bridge: actual codec headers, ranges and lifetime."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import tempfile
from progressive_v1_conformance import fragment_prefix


def run(runner,fixtures):
    inputs=[(Path(fixtures)/'progressive'/name).read_bytes() for name in ('video.mp4','audio.m4a')]
    names=[]
    def case(name,check):
        with tempfile.TemporaryDirectory(prefix='foxicmp-media-') as folder:
            root=Path(folder);data=root/'data';data.mkdir()
            value=dict(schemaVersion=2,sessionId='a'*32,generation='18446744073709551615',revision='9007199254740993',
                writer=dict(instanceId='b'*32,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='200000000',timestampOffset='0'),resources=[],tracks=[])
            paths=[]
            for i,kind in enumerate(('video','audio')):
                path=data/(kind+'.part');paths.append(path);count=fragment_prefix(inputs[i],5);path.write_bytes(inputs[i][:count])
                value['resources'].append(dict(id=kind,name=path.name,length=None,ranges=[dict(offset='0',length=str(count))],state='downloading'))
                value['tracks'].append(dict(id=kind,kind=kind,required=True,codecHint='h264' if i==0 else 'aac-lc',containerHint='fmp4',initializationReady=True,state='downloading',source=dict(resourceId=kind)))
            process=subprocess.Popen([runner,'--registry',str(root/'registry')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
            def rpc(action,**fields):
                process.stdin.write(json.dumps(dict(action=action,**fields))+'\n');process.stdin.flush()
                line=process.stdout.readline();assert line,process.stderr.read();return json.loads(line)
            def opened():
                reply=rpc('open',directory=str(data),session=value);assert reply['ok'],reply
            def read(at=0,size=8,track=0):return rpc('read',track=track,offset=str(at),length=str(size))
            def readers():return rpc('readers',version=1,sessionId=value['sessionId'],generation=value['generation'])['readers']
            def next_value():
                result=copy.deepcopy(value);result['revision']=str(int(value['revision'])+1);return result
            try:
                check(value,paths,rpc,opened,read,readers,next_value);names.append(name)
            finally:
                process.stdin.close()
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait();raise
                errors=process.stderr.read();process.stdout.close();process.stderr.close();assert process.returncode==0,errors
    def exact(v,paths,rpc,open_,read,readers,next_):
        open_();state=rpc('status');assert state['generation']==v['generation'] and state['revision']==v['revision']
        for i,k in enumerate(('video','audio')):
            assert read(track=i)['hex']==inputs[i][:8].hex()
            assert rpc('status')['decoderIoBytes'][k]=='8'
        assert len(readers())==2;rpc('release');assert readers()==[]
    case('full-uint64-counters-and-real-source-bytes',exact)
    def held(v,paths,rpc,open_,read,readers,next_):
        open_();rpc('retain');rpc('release');assert len(readers())==2;rpc('dropRetained');assert readers()==[]
    case('internal-leases-close-after-last-media-owner',held)
    def waiting(v,paths,rpc,open_,read,readers,next_):
        open_();end=paths[0].stat().st_size;assert read(end)['bytesRead']==-1
        assert read(end-4)['bytesRead']==4
    case('unconfirmed-tail-is-waiting-and-short-read',waiting)
    def grow(v,paths,rpc,open_,read,readers,next_):
        open_();n=next_()
        for i in range(2):
            before=paths[i].stat().st_size;paths[i].rename(paths[i].with_suffix('.renamed'));paths[i]=paths[i].with_suffix('.renamed')
            paths[i].with_suffix('.part').write_bytes(b'X'*before)
            with paths[i].open('ab') as stream:stream.write(inputs[i][before:])
            n['resources'][i].update(length=str(len(inputs[i])),ranges=[dict(offset='0',length=str(len(inputs[i])))],state='complete');n['tracks'][i]['state']='complete'
        n['writer']['state']='complete';assert rpc('publish',session=n)['ok'];assert read()['hex']==inputs[0][:8].hex()
        assert read(len(inputs[0]))['bytesRead']==0;assert rpc('status')['revision']==n['revision']
    case('rename-pins-original-identity-and-complete-allows-eof',grow)
    def bad_generation(v,paths,rpc,open_,read,readers,next_):
        open_();n=next_();n['generation']='1';assert not rpc('publish',session=n)['ok'];assert read()['bytesRead']==8
    case('invalid-generation-update-keeps-current-reader',bad_generation)
    def rewrite(v,paths,rpc,open_,read,readers,next_):
        open_();original=paths[0].read_bytes();changed=bytearray(original);changed[original.index(b'moov')+30]^=1;paths[0].write_bytes(changed)
        assert not rpc('publish',session=next_())['ok'];paths[0].write_bytes(original);assert read()['bytesRead']==-1
    case('changed-init-fails-persistently-even-after-restoration',rewrite)
    def reject_edit(name,edit,code='unsupported_media'):
        def check(v,paths,rpc,open_,read,readers,next_):
            edit(v,paths);reply=rpc('open',directory=str(paths[0].parent),session=v)
            assert not reply['ok'] and reply['code']==code,reply;assert readers()==[]
        case(name,check)
    reject_edit('unsupported-clock',lambda v,p:v['timeline']['timebase'].update(numerator=2))
    reject_edit('negative-timestamp-offset',lambda v,p:v['timeline'].update(timestampOffset='-1'))
    reject_edit('indexed-container-not-fmp4',lambda v,p:v['tracks'][0].update(containerHint='mp4'))
    reject_edit('unsupported-codec-hint',lambda v,p:v['tracks'][0].update(codecHint='av1'))
    reject_edit('initialization-not-ready',lambda v,p:v['tracks'][0].update(initializationReady=False),'waiting_initialization')
    def short_init(v,paths):
        v['resources'][0]['ranges'][0]['length']='32';paths[0].write_bytes(inputs[0][:32])
    reject_edit('wait-for-complete-head-moov',short_init,'waiting_initialization')
    def mutate(paths,track,kind,relative,value):
        data=bytearray(paths[track].read_bytes());at=data.index(kind);data[at+relative:at+relative+len(value)]=value;paths[track].write_bytes(data)
    reject_edit('external-data-reference',lambda v,p:mutate(p,0,b'url ',7,b'\x00'))
    reject_edit('encrypted-avc-rejected',lambda v,p:mutate(p,0,b'avcC',0,b'sinf'))
    reject_edit('actual-codec-overrides-false-h264-hint',lambda v,p:mutate(p,0,b'avc1',0,b'hvc1'))
    def not_lc(v,paths):
        data=bytearray(paths[1].read_bytes());at=data.index(b'esds')+8
        def desc(at):
            tag=data[at];at+=1
            for _ in range(4):
                part=data[at];at+=1
                if part<128:return tag,at
            raise AssertionError('Bad fixture descriptor')
        tag,at=desc(at);assert tag==3;tag,at=desc(at+3);assert tag==4;tag,at=desc(at+13);assert tag==5
        data[at]=(data[at]&7)|(5<<3);paths[1].write_bytes(data)
    reject_edit('actual-aac-object-type-must-be-lc',not_lc)
    def cancelled(v,paths,rpc,open_,read,readers,next_):
        open_();rpc('cancel');assert read()['bytesRead']==-1;rpc('release');assert readers()==[]
    case('cancel-unblocks-media-and-closes-leases',cancelled)
    return dict(result='PASS',passed=len(names),cases=names)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True);parser.add_argument('--fixtures',required=True);args=parser.parse_args()
    print(json.dumps(run(args.runner,args.fixtures)))
