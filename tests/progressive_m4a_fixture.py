"""Ordinary single-track M4A fixtures; synthetic audio, no downloader access.

HE-AAC uses tests/m4a_fixture_encoder.cpp linked to an external FDK-AAC encoder.
Set FOXICMP_TEST_AAC_ENCODER to that executable. The player never requires it.
"""
import json, os, struct, subprocess, uuid
from pathlib import Path
from progressive_growing_ts_fixture import publish

def u32(*values): return struct.pack('>'+'I'*len(values),*values)
def box(kind,body): return u32(len(body)+8)+kind.encode()+body
def full(kind,body=b'',flags=0): return box(kind,u32(flags)+body)
def desc(tag,body):
    size=len(body);parts=[size&127];size>>=7
    while size: parts.insert(0,(size&127)|128);size>>=7
    return bytes([tag,*parts])+body

def fixture(root,rate=48000,channels=2,aot=2,samples=None,tail=False):
    root=Path(root);root.mkdir(parents=True,exist_ok=True);samples=samples or rate*16+137
    encoded=root/'encoded.bin';encoder=os.environ['FOXICMP_TEST_AAC_ENCODER']
    subprocess.run([encoder,str(rate),str(channels),str(aot),str(samples),str(encoded)],check=True,capture_output=True)
    data=encoded.read_bytes();at=0
    def number():
        nonlocal at
        v=int.from_bytes(data[at:at+4],'big');at+=4;return v
    n=number();asc=data[at:at+n];at+=n;frame=number();skip=number();delay=number();packets=[]
    if aot==5 and channels==1:
        # Explicit PS=false prevents AAC decoders from auto-enabling stereo PS
        # for an otherwise ambiguous mono SBR configuration. Packets unchanged.
        core_index=7 if rate==44100 else 6;out_index=4 if rate==44100 else 3
        bits=f'{2:05b}{core_index:04b}{1:04b}000{0x2b7:011b}{5:05b}1{out_index:04b}{0x548:011b}0'
        bits+='0'*(-len(bits)%8);asc=int(bits,2).to_bytes(len(bits)//8,'big')
    while at<len(data):
        n=number();assert n and at+n<=len(data);packets.append(data[at:at+n]);at+=n
    assert packets and frame==(1024 if aot==2 else 2048)
    matrix=u32(0x10000,0,0,0,0x10000,0,0,0,0x40000000)
    movie=full('mvhd',u32(0,0,rate,samples,0x10000)+b'\x01\x00'+b'\0'*10+matrix+b'\0'*24+u32(2))
    header=full('tkhd',u32(0,0,1,0,samples)+b'\0'*8+b'\0'*4+b'\x01\x00\0\0'+matrix+u32(0,0),3)
    edit=box('edts',full('elst',u32(1,samples,skip,0x10000)))
    mdhd=full('mdhd',u32(0,0,rate,len(packets)*frame)+b'\x55\xc4\0\0')
    hdlr=full('hdlr',u32(0)+b'soun'+b'\0'*12+b'Fixture audio\0')
    esds=full('esds',desc(3,b'\0\x01\0'+desc(4,b'\x40\x15'+b'\0'*3+u32(96000,96000)+desc(5,asc))+desc(6,b'\x02')))
    entry=box('mp4a',b'\0'*6+b'\0\x01'+b'\0'*8+struct.pack('>HHHHI',channels,16,0,0,rate<<16)+esds)
    stsd=full('stsd',u32(1)+entry);stts=full('stts',u32(1,len(packets),frame));stsc=full('stsc',u32(1,1,len(packets),1))
    stsz=full('stsz',u32(0,len(packets),*[len(p) for p in packets]))
    dinf=box('dinf',full('dref',u32(1)+full('url ',flags=1)))
    def moov(offset):
        table=box('stbl',stsd+stts+stsc+stsz+full('stco',u32(1,offset)))
        media=box('mdia',mdhd+hdlr+box('minf',full('smhd',b'\0'*4)+dinf+table))
        return box('moov',movie+box('trak',header+edit+media))
    ftyp=box('ftyp',b'M4A '+u32(0)+b'M4A isommp42');metadata=moov(0)
    offset=len(ftyp)+8+(0 if tail else len(metadata));metadata=moov(offset)
    raw=ftyp+(b'' if tail else metadata)+box('mdat',b''.join(packets))+(metadata if tail else b'')
    original=root/'original.m4a';original.write_bytes(raw)
    model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),
        timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(samples*10000000//rate),timestampOffset='0'),
        resources=[dict(id='audio',name='audio.part',length=str(len(raw)),state='downloading',ranges=[])],
        tracks=[dict(id='audio',kind='audio',required=True,codecHint='aac-lc' if aot==2 else 'he-aac',containerHint='m4a',state='downloading',source=dict(resourceId='audio'),initializationReady=True)])
    ends=[];p=offset
    for packet in packets:p+=len(packet);ends.append(p)
    return model,raw,original,dict(rate=rate,channels=channels,samples=samples,frame=frame,skip=skip,decoderDelay=delay,packets=len(packets),asc=asc.hex(),ends=ends,tailOffset=len(raw)-len(metadata) if tail else None)

def ffmpeg_fixture(root,rate,channels,tail):
    root=Path(root);root.mkdir(parents=True,exist_ok=True);path=root/'original.m4a'
    subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i',f'sine=frequency=997:sample_rate={rate}:duration=16.137','-ac',str(channels),'-c:a','aac','-movflags','0' if tail else '+faststart',str(path)],check=True,capture_output=True)
    raw=path.read_bytes();probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-of','json',str(path)]));stream=probe['streams'][0]
    samples=int(stream['duration_ts']);packets=probe['packets'];skip=int(packets[0].get('side_data_list',[{}])[0].get('skip_samples',0))
    model=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration=str(samples*10000000//rate),timestampOffset='0'),resources=[dict(id='audio',name='audio.part',length=str(len(raw)),state='downloading',ranges=[])],tracks=[dict(id='audio',kind='audio',required=True,codecHint='aac-lc',containerHint='m4a',state='downloading',source=dict(resourceId='audio'),initializationReady=True)])
    from progressive_tail_mp4_fixture import metadata_start
    return model,raw,path,dict(rate=rate,channels=channels,samples=samples,frame=1024,skip=skip,packets=len(packets),ends=[int(p['pos'])+int(p['size']) for p in packets],tailOffset=metadata_start(raw) if tail else None)
