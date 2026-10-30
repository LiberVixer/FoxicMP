"""Internal MPEG-TS transport staging: no player/API activation or decoder claim."""
import argparse,hashlib,json,os,random,subprocess,tempfile
from pathlib import Path

def crc(raw):
    value=0xffffffff
    for byte in raw:
        value^=byte<<24
        for _ in range(8):value=((value<<1)^(0x04c11db7 if value&0x80000000 else 0))&0xffffffff
    return value

def section(tag,body):
    length=len(body)+4
    raw=bytes([tag,0xb0|(length>>8),length&255])+body
    return raw+crc(raw).to_bytes(4,'big')

def pat(*,version=0,programs=((1,4096),)):
    return section(0,b'\x00\x01'+bytes([0xc1|version<<1,0,0])+b''.join(n.to_bytes(2,'big')+(0xe000|pid).to_bytes(2,'big') for n,pid in programs))

def pmt(*,version=0,program=1,pid=256,pcr_pid=None,stream_type=0x1b,descriptors=b'',extra=b''):
    pcr_pid=pid if pcr_pid is None else pcr_pid
    body=program.to_bytes(2,'big')+bytes([0xc1|version<<1,0,0])+(0xe000|pcr_pid).to_bytes(2,'big')+(0xf000|len(descriptors)).to_bytes(2,'big')+descriptors
    return section(2,body+bytes([stream_type])+(0xe000|pid).to_bytes(2,'big')+b'\xf0\x00'+extra)

def clock(value,prefix):
    assert 0<=value<1<<33
    return bytes([prefix<<4|((value>>30)&7)<<1|1,(value>>22)&255,((value>>15)&127)<<1|1,(value>>7)&255,(value&127)<<1|1])

def packet(pid,payload=b'',*,start=False,counter=0,pcr=None):
    assert len(payload)<=184 and (pcr is None or len(payload)<=176)
    adaptation=pcr is not None or len(payload)!=184
    control=3 if adaptation and payload else 2 if adaptation else 1
    head=bytes([0x47,((pid>>8)&31)|(64 if start else 0),pid&255,control<<4|counter])
    if not adaptation:return head+payload
    length=183-len(payload)
    if not length:return head+b'\x00'+payload
    field=bytes([16 if pcr is not None else 0])
    if pcr is not None:
        base,extension=divmod(pcr,300)
        field+=bytes([(base>>25)&255,(base>>17)&255,(base>>9)&255,(base>>1)&255,(base&1)<<7|0x7e|extension>>8,extension&255])
    assert len(field)<=length
    return head+bytes([length])+field+b'\xff'*(length-len(field))+payload

def psi(pid,raw,*,counter=0,first_size=183):
    out=[packet(pid,b'\x00'+raw[:first_size],start=True,counter=counter)];at=first_size
    while at<len(raw):
        counter=(counter+1)&15;out.append(packet(pid,raw[at:at+184],counter=counter));at+=184
    return b''.join(out)

def init(kind='video',**kw):
    kw.setdefault('stream_type',0x1b if kind=='video' else 0x0f)
    return psi(0,pat())+psi(4096,pmt(**kw))

def pes(payload,pts=126000,dts=None,*,audio=False,bounded=True):
    timing=clock(pts,2 if dts is None else 3)+(b'' if dts is None else clock(dts,1))
    length=3+len(timing)+len(payload) if bounded else 0
    assert length<=65535
    return b'\x00\x00\x01'+bytes([0xc0 if audio else 0xe0])+length.to_bytes(2,'big')+bytes([0x80,0x80 if dts is None else 0xc0,len(timing)])+timing+payload

def transport(packets,*,first_size=176,pcrs=None):
    out=[];counter=0
    for i,raw in enumerate(packets):
        pcr=(126000+i*3600)*300 if pcrs is None else pcrs[i]
        chunks=[raw[:first_size]]+[raw[at:at+184] for at in range(first_size,len(raw),184)]
        for j,chunk in enumerate(chunks):
            out.append(packet(256,chunk,start=j==0,counter=counter,pcr=pcr if j==0 else None));counter=(counter+1)&15
    return b''.join(out)

def edited(raw,offset,value):
    out=bytearray(raw);out[offset:offset+len(value)]=value;return bytes(out)

def run(runner,fixtures=None,real_only=False):
    checks=[];evidence=[];mutations={}
    with tempfile.TemporaryDirectory(prefix='foxicmp-ts-transport-') as tmp:
        root=Path(tmp);proc=subprocess.Popen([runner],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
        def rpc(action,**fields):
            proc.stdin.write(json.dumps(dict(action=action,**fields))+'\n');proc.stdin.flush()
            line=proc.stdout.readline();assert line,proc.stderr.read();return json.loads(line)
        def write(name,raw):
            path=root/name;path.write_bytes(raw);return str(path)
        payloads=[b'\x00\x00\x00\x01\x09\xf0'+bytes(range(256))*3,b'\x00\x00\x00\x01\x09\xf0'+b'other-frame'*80]
        baseline=transport([pes(data,126000+i*3600) for i,data in enumerate(payloads)])
        def initialized(raw=None,kind='video'):
            value=rpc('init',path=write('init.ts',init(kind) if raw is None else raw),kind=kind);assert value['ok'],value
        def case(name,raw=baseline,initial=None,kind='video',error=None,payloads=payloads):
            initialized();before=rpc('stage',path=write('baseline.ts',baseline));assert before['ok'],before
            if initial is not None or kind!='video':
                reply=rpc('init',path=write('candidate-init.ts',init(kind) if initial is None else initial),kind=kind)
                if not reply['ok']:
                    assert error and reply['code']==error,(name,reply)
                    assert rpc('status')['packetCount']==2;checks.append(name);return
                assert error is None or raw!=baseline,(name,reply)
            reply=rpc('stage',path=write('media.ts',raw))
            if error:
                assert not reply['ok'] and reply['code']==error,(name,reply)
                assert rpc('status')['packetCount']==2,(name,'failed stage replaced state')
            else:
                assert reply['ok'],(name,reply)
                if payloads is not None:
                    assert [p['sha256'] for p in reply['packets']]==[hashlib.sha256(p).hexdigest() for p in payloads],(name,reply)
                    for p,expected in zip(reply['packets'],payloads):
                        assert b''.join(raw[a:a+n] for a,n in p['ranges'])==expected
                        assert p['transportOffset']%188==0 and p['transportEnd']%188==0
                assert reply['pcrCount']>0
            checks.append(name)
        try:
            if not real_only:
                case('fragmented-pes-payload-bytes-ranges-and-timestamps')
                case('pes-header-across-ts-packets',transport([pes(p) for p in payloads],first_size=3))
                case('unbounded-video-pes-ended-by-next-start-and-segment',transport([pes(p,bounded=False) for p in payloads]))
                case('pts-dts-distinct',transport([pes(p,126000+i*3600,122400+i*3600) for i,p in enumerate(payloads)]))
                case('33-bit-maximum-clock',transport([pes(payloads[0],(1<<33)-1)],pcrs=[((1<<33)-1)*300+299]),payloads=payloads[:1])
                case('long-pmt-crosses-packets',initial=init(descriptors=bytes([0x52,180])+b'\x00'*180))
                case('psi-header-across-ts-packets',initial=psi(0,pat(),first_size=2)+psi(4096,pmt(),first_size=2))
                case('identical-tables-before-media',psi(0,pat())+psi(4096,pmt())+baseline)
                # Pointer tail finishes a section while a repeated section starts.
                tail=pat()[10:];repeated=packet(0,b'\x00'+pat()[:10],start=True)+packet(0,bytes([len(tail)])+tail+pat(),start=True,counter=1)
                case('psi-pointer-finishes-and-repeats-section',repeated+baseline)
                case('null-packet-accepted',packet(8191,b'\xff'*184)+baseline)
                case('adaptation-only-before-pes',packet(256,counter=0,pcr=126000*300)+baseline)
                audio=[b'\xff\xf1'+bytes(range(70)),b'\xff\xf1'+bytes(range(80))]
                case('adts-stream-type-and-audio-pes',transport([pes(p,126000+i*1920,audio=True) for i,p in enumerate(audio)]),kind='audio',payloads=audio)
                bad=[
                    ('partial-ts',baseline[:-1],'ts-truncated'),
                    ('sync-byte',edited(baseline,0,b'X'),'ts-truncated'),
                    ('transport-error',edited(baseline,1,bytes([baseline[1]|128])),'unsupported-ts'),
                    ('scrambled-ts',edited(baseline,3,bytes([baseline[3]|128])),'unsupported-ts'),
                    ('reserved-control',edited(baseline,3,b'\x00'),'unsupported-ts'),
                    ('adaptation-overflow',edited(baseline,4,b'\xb7'),'unsupported-ts'),
                    ('discontinuity',edited(baseline,5,b'\x90'),'unsupported-ts'),
                    ('invalid-adaptation-stuffing',edited(baseline,len(baseline)-188+6,b'\x01'),'unsupported-ts'),
                    ('counter-gap',edited(baseline,188+3,bytes([(baseline[188+3]&240)|7])),'ts-continuity'),
                    ('counter-duplicate',edited(baseline,188+3,bytes([baseline[188+3]&240])),'ts-continuity'),
                    ('undeclared-pid',edited(baseline,1,b'\x41\x01'),'unsupported-ts'),
                    ('pcr-reserved-bits',edited(baseline,10,b'\x00'),'unsupported-ts'),
                    ('pcr-extension',edited(baseline,10,bytes([(baseline[10]&128)|127,44])),'unsupported-ts'),
                    ('no-pcr-before-media',transport([pes(p) for p in payloads],pcrs=[None,None]),'unsupported-ts'),
                    ('pcr-regression',transport([pes(p) for p in payloads],pcrs=[1000,999]),'ts-timestamp'),
                    ('pcr-rollover',transport([pes(p) for p in payloads],pcrs=[((1<<33)-1)*300,0]),'ts-timestamp'),
                    ('pes-tail-without-start',edited(baseline,1,bytes([baseline[1]&~64])),'ts-truncated'),
                    ('dts-regression',transport([pes(payloads[0],126000),pes(payloads[1],125999)]),'ts-timestamp'),
                    ('pts-before-dts-rollover',transport([pes(payloads[0],0,(1<<33)-1)]),'unsupported-ts'),
                    ('pat-version-change',psi(0,pat(version=1))+baseline,'ts-config-change'),
                    ('pmt-version-change',psi(4096,pmt(version=1))+baseline,'ts-config-change'),
                    ('pmt-pid-change',psi(4096,pmt(pid=257))+baseline,'ts-config-change'),
                    ('partial-media-section',packet(0,b'\x00'+pat()[:8],start=True)+baseline,'ts-truncated'),
                    ('psi-without-start',packet(0,pat(),start=False)+baseline,'unsupported-ts'),
                ]
                for name,raw,code in bad:case('reject-'+name,raw,error=code)
                for name,raw in [('bad-start-code',edited(pes(payloads[0]),0,b'X')),('stream-id',edited(pes(payloads[0]),3,b'\xc0')),('scrambled-pes',edited(pes(payloads[0]),6,b'\xb0')),('missing-pts',edited(pes(payloads[0]),7,b'\x00')),('dts-without-pts',edited(pes(payloads[0]),7,b'\x40')),('timestamp-marker',edited(pes(payloads[0]),9,b'\x20'))]:
                    case('reject-pes-'+name,transport([raw]),error='unsupported-ts')
                case('reject-truncated-pes',transport([pes(payloads[0])[:-1]]),error='ts-truncated')
                case('reject-pes-overrun',transport([pes(payloads[0])+b'X']),error='unsupported-ts')
                for name,raw,code in [
                    ('pat-crc',edited(init(),187,bytes([init()[187]^1])),'ts-crc'),
                    ('missing-pmt',psi(0,pat()),'ts-truncated'),
                    ('missing-pat',psi(4096,pmt()),'unsupported-ts'),
                    ('repeated-init-pat',psi(0,pat())+psi(0,pat(),counter=1)+psi(4096,pmt()),'unsupported-ts'),
                    ('multi-program',psi(0,pat(programs=((1,4096),(2,4097))))+psi(4096,pmt()),'unsupported-ts'),
                    ('network-pid',psi(0,pat(programs=((0,16),(1,4096))))+psi(4096,pmt()),'unsupported-ts'),
                    ('multi-elementary-stream',init(extra=b'\x0f\xe1\x01\xf0\x00'),'unsupported-ts'),
                    ('wrong-kind',init('audio'),'unsupported-ts'),
                    ('hevc-not-enabled',init(stream_type=0x24),'unsupported-ts'),
                    ('crypto-descriptor',init(descriptors=b'\x09\x04\x00\x01\xe1\x00'),'unsupported-ts'),
                    ('different-pcr-pid',init(pcr_pid=257),'unsupported-ts'),
                    ('program-mismatch',init(program=2),'unsupported-ts'),
                    ('partial-init-ts',init()[:-1],'ts-truncated'),
                    ('init-bound',b'\x00'*(1024*1024+1),'ts-limit'),
                ]:case('reject-init-'+name,initial=raw,error=code)
                case('reject-media-bound',b'\x00'*(16*1024*1024+1),error='ts-limit')
                case('reject-pes-bound',transport([pes(b'X'*(4*1024*1024),bounded=False)]),error='ts-limit')
                many=b''.join(packet(256,pes(b'X',126000+i),start=True,counter=i&15,pcr=126000*300 if i==0 else None) for i in range(65537))
                case('reject-pes-count-bound',many,error='ts-limit')
                initialized();rng=random.Random(0xf0c1);accepted=rejected=0
                for _ in range(512):
                    raw=bytearray(baseline)
                    for _ in range(rng.randint(1,4)):
                        index=rng.randrange(len(raw));raw[index]^=rng.randint(1,255)
                    reply=rpc('stage',path=write('mutated.ts',raw))
                    if reply['ok']:accepted+=1
                    else:
                        assert reply['code'] in ('unsupported-ts','ts-truncated','ts-continuity','ts-timestamp','ts-config-change','ts-crc','ts-limit'),reply
                        rejected+=1
                mutations=dict(count=512,acceptedTransportOnly=accepted,rejected=rejected,seed='0xf0c1')
                checks.append('deterministic-512-mutated-media-without-crash')
            if fixtures:
                evidence=real_cases(rpc,write,Path(fixtures),checks,root)
        finally:
            proc.stdin.close()
            try:proc.wait(timeout=10)
            except subprocess.TimeoutExpired:proc.kill();proc.wait();raise
            stderr=proc.stderr.read();proc.stdout.close();proc.stderr.close();assert proc.returncode==0,stderr
    return dict(result='PASS',scope='internal-ts-transport',count=len(checks),checks=checks,realMedia=evidence,mutations=mutations,decoderTested=False,publicPlaybackCapability=False,networkRequests=0)


def real_cases(rpc,write,fixtures,checks,root):
    evidence=[]
    for profile,kind,name,delay in [('video','video','video.mp4','0'),('audio','audio','audio.m4a','0'),('audio-bundled','audio','audio.m4a','0.3')]:
        source=fixtures/'progressive'/name;out=root/(profile+'.ts')
        subprocess.run(['ffmpeg','-v','error','-y','-i',str(source),'-map','0:0','-t','3','-c','copy','-avoid_negative_ts','make_zero','-muxdelay',delay,'-f','mpegts',str(out)],check=True,capture_output=True)
        raw=out.read_bytes();assert len(raw)%188==0
        packets=[raw[at:at+188] for at in range(0,len(raw),188)]
        pid=lambda p:((p[1]&31)<<8)|p[2]
        initial=next(p for p in packets if pid(p)==0)+next(p for p in packets if pid(p)==4096)
        media=b''.join(p for p in packets if pid(p)!=17)
        value=rpc('init',path=write('real-init.ts',initial),kind=kind);assert value['ok'],value
        value=rpc('stage',path=write('real-media.ts',media));assert value['ok'],(kind,value)
        # ffprobe independently verifies timestamps and elementary bytes.
        probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_data_hash','sha256','-of','json',str(out)]))['packets']
        if kind=='video':
            assert len(probe)==len(value['packets'])
            for a,b in zip(value['packets'],probe):
                assert (a['pts'],a['dts'],a['size'],a['sha256'])==(str(b['pts']),str(b['dts']),int(b['size']),b['data_hash'].split(':')[1]),(a,b)
        else:
            # AAC PES may bundle several ADTS access units. PES timestamps belong
            # to the first frame, not to every packet produced by the AAC parser.
            indexed=[]
            for p in value['packets']:
                data=b''.join(media[at:at+n] for at,n in p['ranges']);at=0;frames=[]
                while at<len(data):
                    assert data[at]==255 and data[at+1]&0xf6==0xf0
                    length=((data[at+3]&3)<<11)|(data[at+4]<<3)|(data[at+5]>>5);assert length>=7 and at+length<=len(data)
                    frames.append(data[at:at+length]);at+=length
                assert frames
                indexed.extend(frames)
                first=probe[len(indexed)-len(frames)];assert p['pts']==str(first['pts']) and p['dts']==str(first['dts'])
            assert len(indexed)==len(probe)
            for a,b in zip(indexed,probe):assert hashlib.sha256(a).hexdigest()==b['data_hash'].split(':')[1],(len(a),b)
        checks.append('real-ffmpeg-ffprobe-'+profile)
        evidence.append(dict(profile=profile,kind=kind,source=source.name,transportBytes=len(media),pesPackets=value['packetCount'],ffprobePackets=len(probe),sha256=hashlib.sha256(media).hexdigest(),payloadsAndTimestampsMatch=True))
    return evidence

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True);parser.add_argument('--fixtures',default=os.environ.get('FOXICMP_TEST_MEDIA'));parser.add_argument('--real-only',action='store_true');args=parser.parse_args()
    print(json.dumps(run(args.runner,args.fixtures,args.real_only)))
