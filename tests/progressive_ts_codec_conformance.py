"""Internal TS AVC/AAC access-unit framing/index; no player or decoder claim."""
import argparse,hashlib,json,os,random,subprocess,tempfile
from fractions import Fraction
from pathlib import Path
from progressive_ts_conformance import init,transport,pes

class Bits:
    def __init__(self):self.bits=''
    def put(self,value,count):self.bits+=format(value,'0'+str(count)+'b');return self
    def ue(self,value):s=bin(value+1)[2:];self.bits+='0'*(len(s)-1)+s;return self
    def se(self,value):return self.ue(value*2-1 if value>0 else -value*2)
    def end(self):
        s=self.bits+'1';s+='0'*(-len(s)%8);return int(s,2).to_bytes(len(s)//8,'big')

def nal(header,rbsp):
    out=bytearray([header]);zeros=0
    for byte in rbsp:
        if zeros>=2 and byte<=3:out.append(3);zeros=0
        out.append(byte);zeros=zeros+1 if byte==0 else 0
    return bytes(out)

def sps(*,fixed=1,scale=50,units=1,sps_id=0,crop=3,progressive=1,vui=1,profile=66):
    b=Bits().put(profile,8).put(0,8).put(30,8).ue(sps_id)
    if profile==100:b.ue(1).ue(0).ue(0).put(0,1).put(0,1)
    b.ue(0).ue(2).ue(1).put(0,1).ue(9).ue(5).put(progressive,1)
    if not progressive:b.put(0,1)
    b.put(1,1).put(1,1).ue(0).ue(0).ue(0).ue(crop).put(vui,1)
    if vui:b.put(0,1).put(0,1).put(0,1).put(0,1).put(1,1).put(units,32).put(scale,32).put(fixed,1).put(0,1).put(0,1).put(0,1).put(0,1)
    return nal(0x67,b.end())

def pps(*,sps_id=0,pps_id=0,groups=0,extended=False,transform=0,scaling=0):
    b=Bits().ue(pps_id).ue(sps_id).put(0,1).put(0,1).ue(groups).ue(0).ue(0).put(0,1).put(0,2).se(0).se(0).se(0).put(1,1).put(0,1).put(0,1)
    if extended:
        b.put(transform,1).put(scaling,1)
        if scaling:
            for _ in range(8 if transform else 6):b.put(0,1)
        b.se(0)
    return nal(0x68,b.end())

def slice_(*,key=True,first=0,frame=0,pps_id=0,body=True,slice_type=None):
    b=Bits().ue(first).ue((2 if key else 0) if slice_type is None else slice_type).ue(pps_id).put(frame,4)
    if key:b.ue(0)
    if body:b.put(0x55aa80,24)
    return nal(0x65 if key else 0x41,b.end())

def annex(*nals):return b''.join(b'\x00\x00\x00\x01'+n for n in nals)
def avc(*,key=True,frame=0,config=True):return annex(bytes([9,0x10 if key else 0x30]),*([sps(),pps()] if config else []),slice_(key=key,frame=frame))

def adts(payload=b'\x01\x02\x03',*,sampling=3,channels=2):
    length=len(payload)+7
    return bytes([255,0xf1,0x40|(sampling<<2)|(channels>>2),((channels&3)<<6)|(length>>11),(length>>3)&255,((length&7)<<5)|31,0xfc])+payload

def run(runner):
    checks=[];real=[];mutations={}
    with tempfile.TemporaryDirectory(prefix='foxicmp-ts-codec-') as tmp:
        root=Path(tmp);proc=subprocess.Popen([runner],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
        def rpc(action,**fields):
            proc.stdin.write(json.dumps(dict(action=action,**fields))+'\n');proc.stdin.flush()
            line=proc.stdout.readline();assert line,proc.stderr.read();return json.loads(line)
        def write(name,raw):
            p=root/name;p.write_bytes(raw);return str(p)
        def initialized(kind='video',origin=0):
            value=rpc('init',path=write('init.ts',init(kind)),kind=kind,origin90k=str(origin));assert value['ok'],value
        def access(raw,kind='video',origin=0):
            initialized(kind,origin);return rpc('access',path=write('media.ts',raw))
        def case(name,raw,kind='video',error=None,expected=None):
            result=access(raw,kind)
            if error:assert not result['ok'] and result['code']==error,(name,result)
            else:
                assert result['ok'],(name,result)
                if expected:expected(result)
            checks.append(name)
        def video(*payloads,pts=None):return transport([pes(p,126000+i*3600 if pts is None else pts[i]) for i,p in enumerate(payloads)])
        def frames_equal(expected):
            def check(v):assert [f['encodedSha256'] for f in v['frames']]==[hashlib.sha256(p).hexdigest() for p in expected]
            return check
        try:
            v0=avc();v1=avc(key=False,frame=1,config=False)
            case('avc-framing-configuration-crop-and-packet-hashes',video(v0,v1),expected=lambda v:(frames_equal([v0,v1])(v),assert_video(v)))
            case('multiple-slices-one-picture',video(annex(bytes([9,0x10]),sps(),pps(),slice_(),slice_(first=20),slice_(first=40))))
            case('high-profile-header',video(annex(bytes([9,0x10]),sps(profile=100),pps(),slice_())))
            case('high-profile-pps-transform-scaling',video(annex(bytes([9,0x10]),sps(profile=100),pps(extended=True,transform=1,scaling=1),slice_())))
            case('sei-and-filler-framing',video(annex(bytes([9,0x10]),sps(),pps(),nal(6,b'\x05\x10'+b'A'*16+b'\x80'),slice_(),nal(12,b'\xff\xff\x80'))))
            bad=[
                ('missing-aud',annex(sps(),pps(),slice_()),'ts-codec'),
                ('duplicate-aud',annex(bytes([9,0x10]),bytes([9,0x10]),sps(),pps(),slice_()),'ts-codec'),
                ('aud-after-config',annex(sps(),bytes([9,0x10]),pps(),slice_()),'ts-codec'),
                ('missing-sps',annex(bytes([9,0x10]),pps(),slice_()),'ts-codec'),
                ('missing-pps',annex(bytes([9,0x10]),sps(),slice_()),'ts-codec'),
                ('missing-idr',avc(key=False,frame=1),'ts-codec'),
                ('idr-nonzero-frame-num',annex(bytes([9,0x10]),sps(),pps(),slice_(frame=1)),'ts-codec'),
                ('slice-missing-first-mb',annex(bytes([9,0x10]),sps(),pps(),slice_(first=1)),'ts-codec'),
                ('slice-outside-picture',annex(bytes([9,0x10]),sps(),pps(),slice_(),slice_(first=60)),'ts-codec'),
                ('duplicate-picture',annex(bytes([9,0x10]),sps(),pps(),slice_(),slice_()),'ts-codec'),
                ('truncated-slice',annex(bytes([9,0x10]),sps(),pps(),slice_(body=False)),'ts-codec-truncated'),
                ('pps-reference',annex(bytes([9,0x10]),sps(),pps(sps_id=1),slice_()),'ts-codec'),
                ('slice-pps-reference',annex(bytes([9,0x10]),sps(),pps(),slice_(pps_id=1)),'ts-codec'),
                ('main-pps-transform',annex(bytes([9,0x10]),sps(profile=77),pps(extended=True,transform=1),slice_()),'ts-codec'),
                ('main-pps-scaling',annex(bytes([9,0x10]),sps(profile=77),pps(extended=True,scaling=1),slice_()),'ts-codec'),
                ('baseline-b-slice',annex(bytes([9,0x50]),sps(),pps(),slice_(key=False,slice_type=1)),'ts-codec'),
                ('slice-groups',annex(bytes([9,0x10]),sps(),pps(groups=1),slice_()),'ts-codec'),
                ('sps-fixed-rate-absent',annex(bytes([9,0x10]),sps(fixed=0),pps(),slice_()),'ts-codec'),
                ('sps-invalid-timing',annex(bytes([9,0x10]),sps(scale=0),pps(),slice_()),'ts-codec'),
                ('sps-excessive-rate',annex(bytes([9,0x10]),sps(scale=1000),pps(),slice_()),'ts-codec'),
                ('sps-crop',annex(bytes([9,0x10]),sps(crop=100),pps(),slice_()),'ts-codec'),
                ('sps-interlace',annex(bytes([9,0x10]),sps(progressive=0),pps(),slice_()),'ts-codec'),
                ('sps-vui-absent',annex(bytes([9,0x10]),sps(vui=0),pps(),slice_()),'ts-codec'),
                ('sps-unsupported-profile',annex(bytes([9,0x10]),sps(profile=110),pps(),slice_()),'ts-codec'),
                ('sps-truncated',annex(bytes([9,0x10]),sps()[:-1],pps(),slice_()),'ts-codec-truncated'),
                ('emulation-prevention',annex(bytes([9,0x10]),sps()+b'\x00\x00\x03\x04',pps(),slice_()),'ts-codec'),
                ('sps-change',annex(bytes([9,0x10]),sps(),sps(scale=60),pps(),slice_()),'ts-frame-config'),
                ('forbidden-nal-bit',annex(bytes([9,0x10]),bytes([0xe7])+sps()[1:],pps(),slice_()),'ts-codec'),
                ('unsupported-nal-type',annex(bytes([9,0x10]),sps(),pps(),nal(13,b'\x80'),slice_()),'ts-codec'),
                ('truncated-sei',annex(bytes([9,0x10]),sps(),pps(),nal(6,b'\x05\x10'+b'A'*8+b'\x80'),slice_()),'ts-codec-truncated'),
                ('aud-slice-type',annex(bytes([9,0x10]),sps(),pps(),slice_(key=False,frame=1)),'ts-codec'),
            ]
            for name,payload,code in bad:case('reject-'+name,video(payload),error=code)
            case('reject-video-dts-gap',video(v0,v1,pts=[126000,129700]),error='ts-frame-timestamp')
            case('reject-video-presentation-duplicate',video(v0,v1,pts=[126000,126000]),error='ts-frame-timestamp')
            case('reject-video-accumulated-cadence-drift',video(v0,v1,avc(key=False,frame=2,config=False),pts=[126000,129601,133202]),error='ts-frame-timestamp')
            for sampling,rate in [(3,48000),(4,44100),(5,32000)]:
                for channels in (1,2):
                    frames=[adts(bytes([i,2,3]),sampling=sampling,channels=channels) for i in range(4)]
                    raw=transport([pes(b''.join(frames),audio=True)])
                    case('aac-bundled-'+str(rate)+'-'+str(channels),raw,'audio',expected=frames_equal(frames))
            a0=adts();audio=transport([pes(a0,audio=True)])
            def audio_bad(name,data,code='ts-codec'):case('reject-aac-'+name,transport([pes(data,audio=True)]),'audio',error=code)
            for name,at,value in [('mpeg2',1,0xf9),('crc',1,0xf0),('layer',1,0xf3),('profile',2,0x8c),('sample-rate',2,0x7c),('channels',3,0xc0),('raw-blocks',6,0xfd)]:
                raw=bytearray(a0);raw[at]=value;audio_bad(name,raw)
            audio_bad('header-truncated',a0[:6],'ts-codec-truncated');audio_bad('frame-truncated',a0[:-1],'ts-codec-truncated')
            audio_bad('trailing-partial-header',a0+b'\xff','ts-codec-truncated')
            audio_bad('configuration-change',a0+adts(sampling=4),'ts-frame-config')
            case('reject-aac-distinct-dts',transport([pes(a0,126000,125000,audio=True)]),'audio',error='ts-codec')
            case('reject-aac-pes-clock-gap',transport([pes(a0,126000,audio=True),pes(a0,128000,audio=True)]),'audio',error='ts-frame-timestamp')
            case('reject-aac-accumulated-cadence-drift',transport([pes(a0,126000+i*1921,audio=True) for i in range(3)]),'audio',error='ts-frame-timestamp')
            many=[];complete=0
            while complete<65537:
                count=min(1000,65537-complete);many.append(pes(adts(b'X')*count,126000+complete*1920,audio=True));complete+=count
            case('reject-access-unit-count-bound',transport(many),'audio',error='ts-limit')
            # Publication validates the complete candidate before changing clock,
            # configuration, byte offsets or frame count.
            initialized(origin=126000)
            segment1=video(v0,v1);first=rpc('append',path=write('first.ts',segment1),start='0',duration='800000');assert first['ok'],first
            segment2=video(v0,v1,pts=[133200,136800]);before=rpc('status')
            answer=rpc('append',path=write('second.ts',segment2),start='800000',duration='900000');assert not answer['ok'] and answer['code']=='ts-frame-envelope',answer
            assert rpc('status')==before;checks.append('index-envelope-rejection-preserves-entire-status')
            config_change=video(annex(bytes([9,0x10]),sps(scale=60),pps(),slice_()),pts=[133200])
            answer=rpc('append',path=write('changed.ts',config_change),start='800000',duration='333333');assert not answer['ok'] and answer['code']=='ts-frame-config',answer
            assert rpc('status')==before;checks.append('index-configuration-rejection-preserves-entire-status')
            gap=video(v0,v1,pts=[136800,140400]);answer=rpc('append',path=write('gap.ts',gap),start='1200000',duration='800000')
            assert not answer['ok'] and answer['code']=='ts-frame-timestamp',answer;assert rpc('status')==before;checks.append('index-cross-segment-gap-rejection-preserves-status')
            second=rpc('append',path=write('second.ts',segment2),start='800000',duration='800000');assert second['ok'] and second['indexedUnits']==4 and second['indexEnd']=='1600000',second
            assert int(second['indexBytes'])==len(init())+len(segment1)+len(segment2);checks.append('index-two-closed-gops-origin-and-virtual-byte-positions')
            assert rpc('seek',position='600000')['seekSample']==0 and rpc('seek',position='1200000')['seekSample']==2
            assert rpc('ready',prefix=str(len(init())))['availableEnd']=='0'
            assert rpc('ready',prefix=str(len(init())+len(segment1)))['availableEnd']=='800000'
            assert rpc('ready',prefix=second['indexBytes'])['availableEnd']=='1600000'
            checks.append('index-video-seek-chooses-idr-and-prefix-readiness-is-bounded')
            initialized(origin=126000)
            for i in range(2):
                raw=video(v0,pts=[126000+i*3601]);begin=i*3601*10000000//90000
                reply=rpc('append',path=write('drift.ts',raw),start=str(begin),duration='400000');assert reply['ok'],reply
            before=rpc('status');raw=video(v0,pts=[133202])
            reply=rpc('append',path=write('drift.ts',raw),start=str(7202*10000000//90000),duration='400000')
            assert not reply['ok'] and reply['code']=='ts-frame-timestamp',reply
            assert rpc('status')==before;checks.append('index-global-cadence-drift-rejection-preserves-state')
            initialized();rng=random.Random(0xc0dec);accepted=rejected=0
            for _ in range(512):
                data=bytearray(v0)
                for _ in range(rng.randint(1,3)):data[rng.randrange(len(data))]^=rng.randint(1,255)
                reply=rpc('access',path=write('mutated.ts',video(data)))
                if reply['ok']:accepted+=1
                else:
                    assert reply['code'] in ('ts-codec','ts-codec-truncated','ts-frame-config','ts-frame-timestamp'),reply
                    rejected+=1
            mutations=dict(count=512,acceptedHeaderFramingOnly=accepted,rejected=rejected,seed='0xc0dec');checks.append('512-mutated-avc-access-unit-headers-without-crash')
            real=real_cases(rpc,write,root,checks)
        finally:
            proc.stdin.close()
            try:proc.wait(timeout=10)
            except subprocess.TimeoutExpired:proc.kill();proc.wait();raise
            stderr=proc.stderr.read();proc.stdout.close();proc.stderr.close();assert proc.returncode==0,stderr
    return dict(result='PASS',scope='internal-ts-access-index',count=len(checks),checks=checks,mutations=mutations,realMedia=real,decoderTested=False,publicPlaybackCapability=False,networkRequests=0)

def assert_video(v):
    assert (v['width'],v['height'],v['timeScale'],v['timeUnits'])==(160,90,50,1),v
    assert v['frames'][0]['sync'] and not v['frames'][1]['sync']
    assert int(v['frameEnd'])-int(v['frameStart'])==800000

def real_cases(rpc,write,root,checks):
    reports=[]
    profiles=[('baseline','video','25',1),('main-bframes','video','25',1),('high-fractional','video','30000/1001',1),('aac48-mono','audio','48000',1),('aac48-stereo','audio','48000',2),('aac44-stereo','audio','44100',2),('aac32-mono','audio','32000',1)]
    for profile,kind,rate,channels in profiles:
        path=root/(profile+'.ts')
        if kind=='video':
            avc_profile='baseline' if profile=='baseline' else 'main' if profile=='main-bframes' else 'high'
            options=['-f','lavfi','-i','testsrc2=size=160x90:rate='+rate+':duration=3','-an','-c:v','libx264','-preset','veryfast','-profile:v',avc_profile,'-pix_fmt','yuv420p','-x264-params','force-cfr=1:repeat-headers=1:keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes='+('0' if profile=='baseline' else '2')+(':8x8dct=1' if avc_profile=='high' else '')]
        else:options=['-f','lavfi','-i','sine=frequency=880:sample_rate='+rate+':duration=3','-vn','-c:a','aac','-ac',str(channels),'-b:a','96k']
        subprocess.run(['ffmpeg','-v','error','-y',*options,'-avoid_negative_ts','make_zero','-muxdelay','0.3','-f','mpegts',str(path)],check=True,capture_output=True)
        raw=path.read_bytes();packets=[raw[a:a+188] for a in range(0,len(raw),188)];pid=lambda p:((p[1]&31)<<8)|p[2]
        initial=next(p for p in packets if pid(p)==0)+next(p for p in packets if pid(p)==4096);media=b''.join(p for p in packets if pid(p)!=17)
        answer=rpc('init',path=write('real-init.ts',initial),kind=kind);assert answer['ok'],answer
        answer=rpc('access',path=write('real-media.ts',media));assert answer['ok'],(profile,answer)
        probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256',*(['-show_data'] if kind=='audio' else []),'-of','json',str(path)]));packets=probe['packets'];stream=probe['streams'][0]
        assert len(packets)==len(answer['frames']),(profile,len(packets),len(answer['frames']))
        for a,b in zip(answer['frames'],packets):
            pts=int(Fraction(int(b['pts']))*Fraction(stream['time_base'])*10000000);dts=int(Fraction(int(b['dts']))*Fraction(stream['time_base'])*10000000)
            assert abs(int(a['start'])-pts)<=112 and abs(int(a['decode'])-dts)<=112,(profile,a,b)
            assert a['encodedSize']==int(b['size']) and a['encodedSha256']==b['data_hash'].split(':')[1],(profile,a,b)
            if kind=='video':
                assert a['sync']==('K' in b.get('flags',''))
                assert abs(int(a['end'])-int(a['start'])-int(Fraction(int(b['duration']))*Fraction(stream['time_base'])*10000000))<=112
            else:
                assert abs((int(a['end'])-int(a['start']))-int(Fraction(1024*10000000,int(rate))))<=1
                encoded=bytes.fromhex(''.join(line.split(':',1)[1].split('  ',1)[0].replace(' ','') for line in b['data'].splitlines() if ':' in line))
                assert a['payloadSha256']==hashlib.sha256(encoded[7:]).hexdigest(),(profile,a)
        if kind=='video':assert (answer['width'],answer['height'])==(160,90)
        else:assert (answer['rate'],answer['channels'])==(int(rate),channels)
        # Split video at validated IDR boundaries; whole AAC PES boundaries are
        # independently selected through the transport result.
        staged=rpc('stage',path=write('real-media.ts',media));assert staged['ok'],staged
        if kind=='video':boundaries=[p['transportOffset'] for p,f in zip(staged['packets'],answer['frames']) if f['sync']]
        else:boundaries=[p['transportOffset'] for i,p in enumerate(staged['packets']) if i%6==0]
        assert len(boundaries)>=2;boundaries[0]=0;boundaries.append(len(media));total=0
        for i,(a,b) in enumerate(zip(boundaries,boundaries[1:])):
            segment=media[a:b];value=rpc('access',path=write('piece.ts',segment));assert value['ok'],(profile,i,value)
            result=rpc('append',path=write('piece.ts',segment),start=value['frameStart'],duration=str(int(value['frameEnd'])-int(value['frameStart'])));assert result['ok'],(profile,i,result)
            total+=len(value['frames']);assert result['indexedUnits']==total
        assert total==len(packets)
        if kind=='audio':
            first_end=answer['frames'][0]['end'];assert rpc('seek',position=first_end)['seekSample']==1
            assert rpc('seek',position=answer['frameEnd'])['seekSample']==total
        assert rpc('ready',prefix=result['indexBytes'])['availableEnd']==answer['frameEnd']
        checks.append('real-ffmpeg-ffprobe-access-and-index-'+profile)
        reports.append(dict(profile=profile,accessUnits=total,segments=len(boundaries)-1,encodedPacketsMatch=True,timestampTolerance100ns=112,bFrames=kind=='video' and any(int(p['pts'])!=int(p['dts']) for p in packets),width=answer['width'],height=answer['height'],rate=answer['rate'],channels=answer['channels']))
    return reports

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True);args=parser.parse_args();print(json.dumps(run(args.runner)))
