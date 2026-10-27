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
    video_codec="h264"
    def case(name,check):
        with tempfile.TemporaryDirectory(prefix='foxicmp-media-') as folder:
            root=Path(folder);data=root/'data';data.mkdir()
            value=dict(schemaVersion=2,sessionId='a'*32,generation='18446744073709551615',revision='9007199254740993',
                writer=dict(instanceId='b'*32,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='200000000',timestampOffset='0'),resources=[],tracks=[])
            paths=[]
            for i,kind in enumerate(('video','audio')):
                path=data/(kind+'.part');paths.append(path);count=fragment_prefix(inputs[i],5);path.write_bytes(inputs[i][:count])
                value['resources'].append(dict(id=kind,name=path.name,length=None,ranges=[dict(offset='0',length=str(count))],state='downloading'))
                value['tracks'].append(dict(id=kind,kind=kind,required=True,codecHint=video_codec if i==0 else 'aac-lc',containerHint='fmp4',initializationReady=True,state='downloading',source=dict(resourceId=kind)))
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
    reject_edit('unsupported-codec-hint',lambda v,p:v['tracks'][0].update(codecHint='vp8'))
    reject_edit('initialization-not-ready',lambda v,p:v['tracks'][0].update(initializationReady=False),'waiting_initialization')
    def short_init(v,paths):
        v['resources'][0]['ranges'][0]['length']='32';paths[0].write_bytes(inputs[0][:32])
    reject_edit('wait-for-complete-head-moov',short_init,'waiting_initialization')
    def mutate(paths,track,kind,relative,value):
        data=bytearray(paths[track].read_bytes());at=data.index(kind,data.index(b'moov'));data[at+relative:at+relative+len(value)]=value;paths[track].write_bytes(data)
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
    # Same native bridge and file-lease checks against actual HEVC headers.
    video_codec="hevc"
    for profile in ("hevc8","hevc10"):
        inputs=[(Path(fixtures)/("progressive-"+profile)/name).read_bytes() for name in ('video.mp4','audio.m4a')]
        def hevc_exact(v,paths,rpc,open_,read,readers,next_):
            open_();state=rpc('status');assert state['videoCodec']=='hevc' and state['mediaMode']=='separate-fmp4-hevc-aac-lc',state
            assert read()['hex']==inputs[0][:8].hex();assert len(readers())==2
            rpc('release');assert readers()==[]
        case(profile+'-actual-initialization-and-resource-leases',hevc_exact)
        def no_hint(v,paths,rpc,open_,read,readers,next_):
            v['tracks'][0]['codecHint']='';open_();assert rpc('status')['videoCodec']=='hevc'
        case(profile+'-discover-without-codec-hint',no_hint)
        reject_edit(profile+'-false-h264-hint',lambda v,p:v['tracks'][0].update(codecHint='h264'))
    # Following malformed configurations use the real Main/hvc1 fixture.
    inputs=[(Path(fixtures)/'progressive-hevc8'/name).read_bytes() for name in ('video.mp4','audio.m4a')]
    reject_edit('hevc-config-version',lambda v,p:mutate(p,0,b'hvcC',4,b'\x02'))
    reject_edit('hevc-config-reserved-bits',lambda v,p:mutate(p,0,b'hvcC',17,b'\x00'))
    reject_edit('hevc-unsupported-profile',lambda v,p:mutate(p,0,b'hvcC',5,b'\x04'))
    reject_edit('hevc-unsupported-chroma',lambda v,p:mutate(p,0,b'hvcC',20,b'\xff'))
    reject_edit('hevc-invalid-nal-length-size',lambda v,p:mutate(p,0,b'hvcC',25,b'\x0c'))
    reject_edit('hevc-missing-parameter-arrays',lambda v,p:mutate(p,0,b'hvcC',26,b'\x00'))
    reject_edit('hevc-excessive-parameter-arrays',lambda v,p:mutate(p,0,b'hvcC',26,b'\xff'))
    reject_edit('hevc-incomplete-hvc1-parameter-array',lambda v,p:mutate(p,0,b'hvcC',27,b'\x20'))
    reject_edit('hevc-parameter-nal-out-of-bounds',lambda v,p:mutate(p,0,b'hvcC',30,b'\xff\xff'))
    reject_edit('hevc-parameter-nal-header-mismatch',lambda v,p:mutate(p,0,b'hvcC',32,b'\x42'))
    reject_edit('hevc-parameter-nal-zero-temporal-id',lambda v,p:mutate(p,0,b'hvcC',33,b'\x00'))
    reject_edit('hevc-encrypted-sample-entry',lambda v,p:mutate(p,0,b'hvc1',0,b'encv'))
    reject_edit('hevc-invalid-data-reference',lambda v,p:mutate(p,0,b'hvc1',10,b'\x00\x02'))
    reject_edit('hevc-external-data-reference',lambda v,p:mutate(p,0,b'url ',7,b'\x00'))
    case('hevc-confirmed-range-is-waiting',waiting)
    case('hevc-init-rewrite-is-persistent-error',rewrite)
    case('hevc-cancel-closes-resource-leases',cancelled)
    video_codec="av1"
    for profile in ('av1-8','av1-10'):
        inputs=[(Path(fixtures)/('progressive-'+profile)/name).read_bytes() for name in ('video.mp4','audio.m4a')]
        def av1_exact(v,paths,rpc,open_,read,readers,next_):
            open_();state=rpc('status');assert state['videoCodec']=='av1' and state['mediaMode']=='separate-fmp4-av1-aac-lc',state
            assert read()['hex']==inputs[0][:8].hex();assert len(readers())==2
            rpc('release');assert readers()==[]
        case(profile+'-actual-initialization-and-resource-leases',av1_exact)
        def av1_no_hint(v,paths,rpc,open_,read,readers,next_):
            v['tracks'][0]['codecHint']='';open_();assert rpc('status')['videoCodec']=='av1'
        case(profile+'-discover-without-codec-hint',av1_no_hint)
        reject_edit(profile+'-false-hevc-hint',lambda v,p:v['tracks'][0].update(codecHint='hevc'))
    inputs=[(Path(fixtures)/'progressive-av1-8'/name).read_bytes() for name in ('video.mp4','audio.m4a')]
    def without_av1_sequence(v,paths):
        data=bytearray(paths[0].read_bytes());at=data.index(b'av1C');size=int.from_bytes(data[at-4:at],'big')
        assert size>=20
        replacement=(12).to_bytes(4,'big')+b'av1C'+data[at+4:at+8]
        replacement+=(size-12).to_bytes(4,'big')+b'free'+bytes(size-20)
        data[at-4:at-4+size]=replacement;paths[0].write_bytes(data)
    reject_edit('av1-sequence-header-in-configuration-required',without_av1_sequence)
    reject_edit('av1-configuration-marker',lambda v,p:mutate(p,0,b'av1C',4,b'\x01'))
    reject_edit('av1-configuration-version',lambda v,p:mutate(p,0,b'av1C',4,b'\x82'))
    reject_edit('av1-high-profile',lambda v,p:mutate(p,0,b'av1C',5,b'\x20'))
    reject_edit('av1-invalid-level',lambda v,p:mutate(p,0,b'av1C',5,b'\x1f'))
    reject_edit('av1-twelve-bit',lambda v,p:mutate(p,0,b'av1C',6,b'\x6c'))
    reject_edit('av1-monochrome',lambda v,p:mutate(p,0,b'av1C',6,b'\x1c'))
    reject_edit('av1-unsupported-chroma',lambda v,p:mutate(p,0,b'av1C',6,b'\x08'))
    reject_edit('av1-reserved-configuration-bits',lambda v,p:mutate(p,0,b'av1C',7,b'\x80'))
    reject_edit('av1-presentation-delay',lambda v,p:mutate(p,0,b'av1C',7,b'\x10'))
    reject_edit('av1-obu-forbidden-bit',lambda v,p:mutate(p,0,b'av1C',8,b'\x8a'))
    reject_edit('av1-obu-missing-size',lambda v,p:mutate(p,0,b'av1C',8,b'\x08'))
    reject_edit('av1-obu-reserved-bit',lambda v,p:mutate(p,0,b'av1C',8,b'\x0b'))
    reject_edit('av1-obu-unsupported-type',lambda v,p:mutate(p,0,b'av1C',8,b'\x32'))
    reject_edit('av1-metadata-before-sequence',lambda v,p:mutate(p,0,b'av1C',8,b'\x2a'))
    reject_edit('av1-obu-zero-payload',lambda v,p:mutate(p,0,b'av1C',9,b'\x00'))
    reject_edit('av1-obu-payload-overrun',lambda v,p:mutate(p,0,b'av1C',9,b'\x7f'))
    reject_edit('av1-obu-truncated-size',lambda v,p:mutate(p,0,b'av1C',9,b'\x80'*8))
    reject_edit('av1-sequence-config-profile-mismatch',lambda v,p:mutate(p,0,b'av1C',10,b'\x20'))
    reject_edit('av1-encrypted-sample-entry',lambda v,p:mutate(p,0,b'av01',0,b'encv'))
    reject_edit('av1-invalid-data-reference',lambda v,p:mutate(p,0,b'av01',10,b'\x00\x02'))
    case('av1-confirmed-range-is-waiting',waiting)
    case('av1-init-rewrite-is-persistent-error',rewrite)
    case('av1-cancel-closes-resource-leases',cancelled)
    video_codec="vp9"
    for profile in ('vp9-8','vp9-10'):
        inputs=[(Path(fixtures)/('progressive-'+profile)/name).read_bytes() for name in ('video.mp4','audio.m4a')]
        def vp9_exact(v,paths,rpc,open_,read,readers,next_):
            open_();state=rpc('status');assert state['videoCodec']=='vp9' and state['mediaMode']=='separate-fmp4-vp9-aac-lc',state
            assert read()['hex']==inputs[0][:8].hex();assert len(readers())==2
            rpc('release');assert readers()==[]
        case(profile+'-actual-initialization-and-resource-leases',vp9_exact)
        def vp9_no_hint(v,paths,rpc,open_,read,readers,next_):
            v['tracks'][0]['codecHint']='';open_();assert rpc('status')['videoCodec']=='vp9'
        case(profile+'-discover-without-codec-hint',vp9_no_hint)
        reject_edit(profile+'-false-av1-hint',lambda v,p:v['tracks'][0].update(codecHint='av1'))
    inputs=[(Path(fixtures)/'progressive-vp9-8'/name).read_bytes() for name in ('video.mp4','audio.m4a')]
    reject_edit('vp9-missing-configuration',lambda v,p:mutate(p,0,b'vpcC',0,b'free'))
    def short_vp9_config(v,paths):
        data=bytearray(paths[0].read_bytes());at=data.index(b'vpcC',data.index(b'moov'))
        assert int.from_bytes(data[at-4:at],'big')==20
        data[at-4:at+16]=(12).to_bytes(4,'big')+b'vpcC'+data[at+4:at+8]+(8).to_bytes(4,'big')+b'free'
        paths[0].write_bytes(data)
    reject_edit('vp9-truncated-configuration',short_vp9_config)
    for name,relative,byte in (
        ('version',4,0),('flags',7,1),('profile-1',8,1),('profile-3',8,3),
        ('invalid-level',9,12),('profile-depth-mismatch',10,0xa0),('twelve-bit',10,0xc0),
        ('chroma-422',10,0x84),('chroma-444',10,0x86),('chroma-reserved',10,0x8e),
        ('primaries-reserved',11,3),('transfer-reserved',12,3),('matrix-reserved',13,3),
        ('rgb-matrix-with-420',13,0),('nonzero-init-size',15,1)):
        reject_edit('vp9-'+name,lambda v,p,r=relative,b=byte:mutate(p,0,b'vpcC',r,bytes([b])))
    reject_edit('vp9-encrypted-sample-entry',lambda v,p:mutate(p,0,b'vp09',0,b'encv'))
    reject_edit('vp9-invalid-data-reference',lambda v,p:mutate(p,0,b'vp09',10,b'\x00\x02'))
    reject_edit('vp9-external-data-reference',lambda v,p:mutate(p,0,b'url ',7,b'\x00'))
    def vp9_colocated_fullrange(v,paths,rpc,open_,read,readers,next_):
        mutate(paths,0,b'vpcC',10,b'\x83');open_();assert rpc('status')['videoCodec']=='vp9'
    case('vp9-colocated-chroma-and-fullrange',vp9_colocated_fullrange)
    case('vp9-confirmed-range-is-waiting',waiting)
    case('vp9-init-rewrite-is-persistent-error',rewrite)
    case('vp9-cancel-closes-resource-leases',cancelled)
    return dict(result='PASS',passed=len(names),cases=names)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True);parser.add_argument('--fixtures',required=True);args=parser.parse_args()
    print(json.dumps(run(args.runner,args.fixtures)))
