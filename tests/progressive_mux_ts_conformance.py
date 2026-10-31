"""Explicit two-PID TS admission, interleaving, integrity and last-owner leases."""
import argparse,copy,hashlib,json,random,subprocess,tempfile,uuid
from pathlib import Path
from fractions import Fraction
from progressive_ts_conformance import init,pmt,pat,psi,pes,packet,transport
from progressive_ts_codec_conformance import avc,adts
from progressive_segment_playback import publish_resources
from progressive_catalog_playback import catalog_snapshot
from progressive_integrity import protect_snapshot

def multiplex(index=0,big=False,audio_first=False):
    video=[avc(key=j==0,frame=j,config=j==0) for j in range(8)]
    if big:video[0]+=b'Z'*200000
    raw=transport([pes(p,126000+(index*8+j)*3600,bounded=len(p)<60000) for j,p in enumerate(video)])
    audio=[adts(bytes([index&255,j,3])) for j in range(15)]
    a=packet(257,pes(b''.join(audio),126000+index*28800,audio=True),start=True)
    at=0 if audio_first else 188
    return raw[:at]+a+raw[at:],video,audio

def fixture(count=12,big=False):
    initialization=init(extra=b'\x0f\xe1\x01\xf0\x00')
    value=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(count*3200000),timestampOffset='14000000'),resources=[dict(id='mux-init',name='mux-init.part',length=str(len(initialization)),state='complete',ranges=[dict(offset='0',length=str(len(initialization)))])],tracks=[])
    payload={'mux-init':initialization};samples=[[],[]]
    for i in range(count):
        raw,v,a=multiplex(i,big and i==0);rid='mux-'+str(i);payload[rid]=raw;samples[0]+=v;samples[1]+=a
        value['resources'].append(dict(id=rid,name=rid+'.part',length=str(len(raw)),state='downloading',ranges=[]))
    for kind in ('video','audio'):
        value['tracks'].append(dict(id=kind,kind=kind,required=True,codecHint='h264' if kind=='video' else 'aac-lc',containerHint='mpegts-muxed',state='downloading',initializationReady=True,initializationResourceId='mux-init',source=dict(segments=[dict(resourceId='mux-'+str(i),sequence=str(i+1),start=str(i*3200000),duration='3200000',timestampOffset='0',discontinuity=False,configurationId=kind+'-constant') for i in range(count)])))
    return value,payload,initialization+b''.join(payload['mux-'+str(i)] for i in range(count)),samples

class Rpc:
    def __init__(self,args):self.p=subprocess.Popen(args,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
    def __call__(self,action,**fields):
        self.p.stdin.write(json.dumps(dict(action=action,**fields))+'\n');self.p.stdin.flush();line=self.p.stdout.readline();assert line,self.p.stderr.read();return json.loads(line)
    def close(self):
        self.p.stdin.close()
        try:self.p.wait(timeout=10)
        except subprocess.TimeoutExpired:self.p.kill();self.p.wait();raise
        error=self.p.stderr.read();self.p.stdout.close();self.p.stderr.close();assert self.p.returncode==0,error

def run(transport_runner,stream_runner):
    checks=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-mux-ts-parser-') as tmp:
        root=Path(tmp);rpc=Rpc([transport_runner]);initial=init(extra=b'\x0f\xe1\x01\xf0\x00');raw,video,audio=multiplex(big=True)
        def write(name,data):p=root/name;p.write_bytes(data);return str(p)
        def initialized(kind='video',value=initial,muxed=True):return rpc('init',kind=kind,path=write('init.ts',value),muxed=muxed,origin90k='126000')
        try:
            for kind,expected,pid,other in [('video',video,256,257),('audio',audio,257,256)]:
                a=initialized(kind);assert a['ok'] and a['streamPid']==pid and a['otherPid']==other and a['pcrPid']==256,a
                a=rpc('access',path=write('media.ts',raw));assert a['ok'],a
                assert [p['encodedSha256'] for p in a['frames']]==[hashlib.sha256(p).hexdigest() for p in expected],a
                checks.append('interleaved-large-pes-exact-'+kind+'-access-units')
            reversed_init=psi(0,pat())+psi(4096,pmt(pid=257,stream_type=0x0f,pcr_pid=256,extra=b'\x1b\xe1\x00\xf0\x00'))
            a=initialized(value=reversed_init);assert a['ok'] and a['streamPid']==256,a;checks.append('reversed-pmt-entry-order-selects-by-codec')
            for name,value,muxed in [
                ('single-profile-still-rejects-shared-program',initial,False),
                ('mux-profile-rejects-single-program-es',init(),True),
                ('duplicate-aac',init('audio',extra=b'\x0f\xe1\x01\xf0\x00'),True),
                ('duplicate-pid',init(extra=b'\x0f\xe1\x00\xf0\x00'),True),
                ('third-stream',init(extra=b'\x0f\xe1\x01\xf0\x00\x1b\xe1\x02\xf0\x00'),True),
                ('pcr-on-audio',init(pcr_pid=257,extra=b'\x0f\xe1\x01\xf0\x00'),True),
                ('hevc',init(stream_type=0x24,extra=b'\x0f\xe1\x01\xf0\x00'),True),
            ]:
                a=initialized(value=value,muxed=muxed);assert not a['ok'],(name,a);checks.append('reject-'+name)
            initialized('audio');early,_,expected=multiplex(audio_first=True);a=rpc('access',path=write('early.ts',early));assert a['ok'] and len(a['frames'])==15,a;checks.append('audio-before-first-video-pcr-is-bounded-and-validated')
            for kind in ('video','audio'):
                initialized(kind)
                foreign=packet(258,pes(b'foreign'),start=True);a=rpc('stage',path=write('foreign.ts',raw+foreign));assert not a['ok'] and a['code']=='unsupported-ts',a
                checks.append('reject-undeclared-third-pid-'+kind)
            rng=random.Random(0x4d5558);accepted=rejected=0;small,_,_=multiplex()
            for _ in range(256):
                data=bytearray(small);at=rng.randrange(len(data));data[at]^=rng.randint(1,255)
                for kind in ('video','audio'):
                    assert initialized(kind)['ok'];a=rpc('access',path=write('mutated.ts',data))
                    if a['ok']:accepted+=1
                    else:rejected+=1
            checks.append('512-deterministic-mutated-two-pid-codec-parses-without-crash')
        finally:rpc.close()
    def case(name,check,count=12,big=False):
        with tempfile.TemporaryDirectory(prefix='foxicmp-mux-ts-source-') as tmp:
            root=Path(tmp);data=root/'data';data.mkdir();v,p,expected,samples=fixture(count,big);publish_resources(v,p,data,Fraction(96,100),hole='mux-1');v=protect_snapshot(v,data);rpc=Rpc([stream_runner,'--registry',str(root/'registry')])
            def readers():
                rows=[];after=''
                while True:
                    a=rpc('readers',version=1,sessionId=v['sessionId'],generation='1',**({'afterReaderId':after} if after else {}));assert a['ok'],a;rows+=a['readers'];after=a.get('nextReaderId','')
                    if not after:return rows
            def open_():a=rpc('open',directory=str(data),session=v);assert a['ok'],a
            def pub(ready,complete=False):
                previous=copy.deepcopy(v);publish_resources(v,p,data,Fraction(ready*32,100));v['revision']=str(int(v['revision'])+1)
                if complete:
                    v['writer']['state']='complete'
                    for t in v['tracks']:t['state']='complete'
                protected=protect_snapshot(v,data,previous=previous);v.clear();v.update(protected);a=rpc('publish',session=v);assert a['ok'],a
            try:check(v,p,expected,samples,data,rpc,readers,open_,pub);checks.append(name)
            finally:rpc.close()
    def reads(v,p,e,s,d,rpc,readers,open_,pub):
        open_();status=rpc('status');assert status['available']=='3200000' and status['tracks'][0]['bytes']==status['tracks'][1]['bytes'],status
        n=len(p['mux-init'])+len(p['mux-0']);assert rpc('read',track=1,offset=str(n),length='1')['code']=='waiting-range'
        assert len(readers())==2 # one init + one shared media, not four files
        pub(100);assert not rpc('status')['complete'];pub(100,True)
        for track in (0,1):
            for at in range(0,len(e),65536):assert rpc('read',track=track,offset=str(at),length=str(min(65536,len(e)-at)))['hex']==e[at:at+65536].hex()
            for i in (0,1,len(s[track])-1):
                a=rpc('sample',track=track,index=str(i),encoded=True);assert a['sha256']==hashlib.sha256(s[track][i]).hexdigest(),a
            assert rpc('read',track=track,offset=str(len(e)),length='1')['code']=='eof'
        assert len(readers())<=8;rpc('retain');rpc('release');assert not rpc('released')['released'] and readers();rpc('dropRetained');assert rpc('released')['released'] and not readers()
    case('shared-raw-prefix-gap-samples-eof-eight-files-and-last-owner',reads)
    case('shared-large-interleaved-pes-spans-multiple-read-windows',reads,big=True)
    def invalid(v,p,e,s,d,rpc,readers,open_,pub,change):
        open_();before=rpc('status');bad=copy.deepcopy(v);bad['revision']='2';change(bad);a=rpc('publish',session=bad);assert not a['ok'],a;assert rpc('status')==before
        assert rpc('read',track=0,offset='0',length='8')['hex']==e[:8].hex()
    for name,change in [
        ('mixed-mode',lambda v:v['tracks'][1].update(containerHint='mpegts')),
        ('different-init',lambda v:v['tracks'][1].update(initializationResourceId='mux-0')),
        ('different-prefix',lambda v:v['tracks'][1]['source']['segments'].pop()),
        ('different-sequence',lambda v:v['tracks'][1]['source']['segments'][0].update(sequence='0')),
        ('readiness',lambda v:v['tracks'][1].update(initializationReady=False)),
    ]:case('metadata-rejection-preserves-shared-stream-'+name,lambda *a,change=change:invalid(*a,change=change))
    def corrupt_other(v,p,e,s,d,rpc,readers,open_,pub):
        # Metadata and SHA are correct; AAC PES framing fails only on audio view.
        raw=bytearray(p['mux-0']);raw[188+4+1+raw[188+4]]=ord('X') # damage the first audio PES prefix
        (d/'mux-0.part').write_bytes(raw);value=protect_snapshot(v,d)
        a=rpc('open',directory=str(d),session=value);assert not a['ok'],a;assert not readers()
    case('bad-audio-never-publishes-video-or-leaks-shared-files',corrupt_other)
    def failed_update(v,p,e,s,d,rpc,readers,open_,pub):
        open_();before=rpc('status');previous=copy.deepcopy(v);publish_resources(v,p,d,Fraction(5*32,100));v['revision']='2'
        path=d/'mux-1.part';raw=bytearray(path.read_bytes());raw[188+5+raw[192]]=ord('X');path.write_bytes(raw)
        value=protect_snapshot(v,d,previous=previous);a=rpc('publish',session=value);assert not a['ok'],a
        status=rpc('status');assert status['revision']==before['revision'] and status['tracks']==before['tracks'] and status['errorCode'],status
        for kind in (0,1):
            a=rpc('read',track=kind,offset='0',length='16');assert a['bytesRead']==0 and a['untouchedOnError'],a
        rpc('release');assert not readers()
    case('audio-failure-during-update-closes-both-views-without-partial-publication',failed_update)
    def paged(v,p,e,s,d,rpc,readers,open_,pub):
        plan=v;value,expanded=catalog_snapshot(plan,p,d,Fraction(16*32,100));a=rpc('open',directory=str(d),session=value);assert a['ok'],a
        plan['revision']='2';value,expanded=catalog_snapshot(plan,p,d,1000,previous=expanded,complete=True);a=rpc('publish',session=value);assert a['ok'],a;status=rpc('status');assert status['complete'] and status['resources']==97 and status['pages']==6,status
        for kind in (0,1):assert rpc('sample',track=kind,index='0',encoded=True)['sha256']==hashlib.sha256(s[kind][0]).hexdigest()
        assert len(readers())<=8;rpc('release');assert not readers()
    case('97-shared-resources-six-pages-backward-samples-and-clean-release',paged,count=96)
    return dict(result='PASS',scope='muxed-ts-transport-resource-conformance',count=len(checks),checks=checks,mutations=dict(count=512,acceptedFramingOnly=accepted,rejected=rejected,seed='0x4d5558'),maxOpenResources=8,catalogResources=97,catalogPages=6,playerDecoderTested=False,networkRequests=0)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--transport-runner',required=True);p.add_argument('--stream-runner',required=True);a=p.parse_args();print(json.dumps(run(a.transport_runner,a.stream_runner)))
