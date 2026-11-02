"""Real WAV projection: byte-exact PCM, clocks, partial writes and rejection."""
import argparse,copy,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
from fractions import Fraction
from progressive_rf64_fixture import fixture,PROFILES,prefix_at_seconds
from progressive_standalone_fixture import publish
from progressive_mux_ts_conformance import Rpc

def run(runner):
 checks=[];rows=[]
 for profile in PROFILES:
  with tempfile.TemporaryDirectory(prefix='foxicmp-wav-') as tmp:
   root=Path(tmp);model,raw,original,probe=fixture(root,profile,4);data=root/'data';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'registry')]);rate=int(profile.split('-')[-1]);align=probe['_block_align'];offset=probe['_data_offset'];bits=probe['_bits'];codec='pcm_s'+str(bits)+'le';mode='standalone-rf64-pcm'+str(bits)
   try:
    value=publish(model,raw,data,prefix_at_seconds(raw,probe,2)+1);assert rpc('open',session=value,directory=str(data))['ok'];status=rpc('status');assert status['mediaMode']==mode and status['audioCodec']=='pcm-s'+str(bits)+'le';before=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(before),length='1')['bytesRead']==-1
    value=publish(model,raw,data,len(raw),previous=value);assert rpc('publish',session=value)['ok'];full=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(full),length='1')['bytesRead']==-1
    value=publish(model,raw,data,len(raw),previous=value,complete=True);assert rpc('publish',session=value)['ok'];assert int(rpc('layout')['tracks'][0]['available'])==full
    assert rpc('index')['tracks'][0]['samples']==(rate*4+16383)//16384
    out=bytearray()
    for at in range(0,full,65536):
     r=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,full-at)));assert r['bytesRead']>0;out+=bytes.fromhex(r['hex'])
    assert rpc('read',decoder=True,track=0,offset=str(full),length='1')['bytesRead']==0
    projected=root/'projected.mp4';projected.write_bytes(out);other=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-of','json',str(projected)]));stream=other['streams'][0];assert len(other['streams'])==1 and stream['codec_name']==codec and int(stream['sample_rate'])==rate
    cursor=0
    for packet in other['packets']:
     n=int(packet['duration']);assert int(packet['pts'])*Fraction(stream['time_base'])==Fraction(cursor,rate) and int(packet['size'])==n*align
     at=int(packet['pos']);assert out[at:at+n*align]==raw[offset+cursor*align:offset+(cursor+n)*align];cursor+=n
    assert cursor==rate*4
    pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(projected),'-c:a',codec,'-f','s'+str(bits)+'le','-']);assert hashlib.sha256(pcm).hexdigest()==probe['_pcm_sha256'] and len(pcm)==probe['_pcm_bytes']
    rows.append(dict(profile=profile,pcmBytes=len(pcm),pcmSha256=hashlib.sha256(pcm).hexdigest(),growing=True))
    assert original.read_bytes()==raw
    readers=lambda:rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];assert len(readers())==1;rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
    checks.extend(profile+'-'+name for name in ('growing-single-audio','partial-block-waits','full-prefix-waits-for-complete','packet-bytes-and-clocks','decoded-pcm-bit-exact','last-owner-release','original-unchanged'))
   finally:rpc.close()
 with tempfile.TemporaryDirectory(prefix='foxicmp-rf64-errors-') as tmp:
  root=Path(tmp)
  for bits in (16,24):
   work=root/str(bits);work.mkdir();profile='audio-rf64-'+str(bits)+'-stereo-48000';model,raw,_,probe=fixture(work/'source',profile,4,writer='manual');offset=probe['_data_offset']
   def attempt(name,body=raw,change=None):
    data=work/name;data.mkdir();plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(body));v=publish(plan,body,data,len(body),complete=True)
    if change:change(v)
    rpc=Rpc([runner,'--registry',str(work/(name+'-registry'))])
    try:
     a=rpc('open',session=v,directory=str(data));assert not a['ok'],(bits,name,a);assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(str(bits)+'-'+name+'-rejected-without-leak')
    finally:rpc.close()
   for name,change in [('optional',lambda v:v['tracks'][0].update(required=False)),('video',lambda v:v['tracks'][0].update(kind='video')),('codec',lambda v:v['tracks'][0].update(codecHint='aac-lc')),('legacy-hint',lambda v:v['tracks'][0].update(containerHint='wav' if bits==16 else 'wav-extensible-pcm24')),('no-sha',lambda v:v['resources'][0].pop('integrity')),('offset',lambda v:v['timeline'].update(timestampOffset='1')),('duration',lambda v:v['timeline'].update(duration='1'))]:attempt(name,change=change)
   for name,at,fmt,value in [('riff',0,'4s',b'RIFF'),('bw64',0,'4s',b'BW64'),('riff-sentinel',4,'I',len(raw)-8),('wave',8,'4s',b'AVI '),('ds64-missing',12,'4s',b'JUNK'),('ds64-size',16,'I',40),('riff-overflow',20,'Q',0xffffffffffffffff),('riff-oversized',20,'Q',1<<34),('riff-truncated',20,'Q',len(raw)-9),('data-zero',28,'Q',0),('data-high-bits',28,'Q',(1<<32)+len(raw)-offset),('samples-zero',36,'Q',0),('samples-wrong',36,'Q',1),('samples-high-bits',36,'Q',(1<<32)+48000*4),('ds64-table',44,'I',1),('format',56,'H',3),('bits',70,'H',32),('channels',58,'H',6),('rate',60,'I',96000),('byte-rate',64,'I',1),('align',68,'H',1),('no-fmt',48,'4s',b'JUNK'),('data-sentinel',offset-4,'I',len(raw)-offset),('no-data',offset-8,'4s',b'JUNK')]:
    body=bytearray(raw);struct.pack_into('<'+fmt,body,at,value);attempt(name,body)
   if bits==24:
    for name,at,fmt,value in [('cb',72,'H',20),('valid20',74,'H',20),('mask',76,'I',0),('float-guid',80,'I',3),('guid-tail',95,'B',0)]:
     body=bytearray(raw);struct.pack_into('<'+fmt,body,at,value);attempt(name,body)
   attempt('truncated',raw[:-1]);attempt('trailing',raw+b'X')
   for count in (7,11,19,27,35,43,47,offset-1,offset+1):
    data=work/('partial-'+str(count));data.mkdir();rpc=Rpc([runner,'--registry',str(work/('partial-registry-'+str(count)))])
    try:
     v=publish(model,raw,data,count);a=rpc('open',session=v,directory=str(data));assert not a['ok'] and a['code']=='waiting_initialization',a;assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(str(bits)+'-partial-'+str(count)+'-waits-without-leak')
    finally:rpc.close()
   def oracle(rpc,expected):
    size=int(rpc('layout')['tracks'][0]['available']);out=bytearray()
    for at in range(0,size,65536):
     v=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,size-at)));assert v['bytesRead']>0;out+=bytes.fromhex(v['hex'])
    projected=work/'oracle.mp4';projected.write_bytes(out)
    pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(projected),'-c:a','pcm_s'+str(bits)+'le','-f','s'+str(bits)+'le','-']);assert pcm==expected
    return size
   # The mandatory ds64 remains first; metadata may follow it.
   for tag in (b'JUNK',b'LIST',b'bext'):
    extra=tag+struct.pack('<I',3)+b'abc\0';body=bytearray(raw[:48]+extra+raw[48:]);struct.pack_into('<Q',body,20,len(body)-8);data=work/tag.decode();data.mkdir();v=publish(model,body,data,len(body),complete=True);rpc=Rpc([runner,'--registry',str(work/(tag.decode()+'-registry'))])
    try:
     assert rpc('open',session=v,directory=str(data))['ok'];oracle(rpc,raw[offset:]);checks.append(str(bits)+'-'+tag.decode()+'-pad-excluded-from-pcm')
    finally:rpc.close()
   # Independent manual writer: completed exact PCM in both channel layouts.
   for channels in ('mono','stereo'):
    p='audio-rf64-'+str(bits)+'-'+channels+'-48000';m,body,_,pr=fixture(work/('manual-'+channels),p,4,writer='manual');data=work/('manual-data-'+channels);data.mkdir();rpc=Rpc([runner,'--registry',str(work/('manual-registry-'+channels))])
    try:
     v=publish(m,body,data,len(body),complete=True);assert rpc('open',session=v,directory=str(data))['ok'];oracle(rpc,body[pr['_data_offset']:]);checks.append(p+'-manual-writer-pcm-bit-exact')
    finally:rpc.close()
   mono,body,_,pr=fixture(work/'append-source','audio-rf64-'+str(bits)+'-mono-44100',4,writer='manual');data=work/'bad-append';data.mkdir();rpc=Rpc([runner,'--registry',str(work/'bad-append-registry')])
   try:
    v=publish(mono,body,data,prefix_at_seconds(body,pr,2));assert rpc('open',session=v,directory=str(data))['ok'];v=publish(mono,body[:-1],data,len(body)-1,previous=v,complete=True);assert not rpc('publish',session=v)['ok'];assert rpc('read',decoder=True,track=0,offset='0',length='1')['bytesRead']==-1;rpc('release');assert not rpc('readers',version=1,sessionId=mono['sessionId'],generation='1')['readers'];checks.append(str(bits)+'-bad-append-fails-closed-and-releases')
   finally:rpc.close()
   data=work/'tamper';data.mkdir();rpc=Rpc([runner,'--registry',str(work/'tamper-registry')])
   try:
    v=publish(model,raw,data,len(raw),complete=True);assert rpc('open',session=v,directory=str(data))['ok'];oracle(rpc,raw[offset:])
    with (data/model['resources'][0]['name']).open('r+b') as f:f.seek(offset+3);f.write(bytes([raw[offset+3]^1]))
    packet=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-of','json',str(work/'oracle.mp4')]))['packets'][0]
    assert rpc('read',decoder=True,track=0,offset=str(int(packet['pos'])+3),length='1')['bytesRead']==-1;rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(str(bits)+'-pinned-byte-tamper-fails-closed-and-releases')
   finally:rpc.close()
  # An odd mono PCM24 data chunk carries one RIFF pad beyond the PCM payload.
  from progressive_rf64_fixture import manual_rf64
  from progressive_wav_ext24_fixture import manual_wave
  mono,body,_,pr=fixture(root/'odd-source','audio-rf64-24-mono-44100',4,writer='manual');expected=body[pr['_data_offset']:]+b'\x12\x34\x80';body=manual_rf64(manual_wave(expected,44100,1),44100*4+1);mono['timeline']['duration']=str((44100*4+1)*10000000//44100);data=root/'odd-data';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'odd-registry')])
  try:
   v=publish(mono,body,data,len(body)-1);assert rpc('open',session=v,directory=str(data))['ok'];size=oracle(rpc,expected);assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==-1
   v=publish(mono,body,data,len(body),previous=v,complete=True);assert rpc('publish',session=v)['ok'];assert oracle(rpc,expected)==size;assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==0;checks.append('24-odd-data-pad-excluded-from-pcm')
  finally:rpc.close()
 geometry=json.loads(subprocess.check_output([runner,'--rf64-geometry'],text=True));assert geometry['result']=='PASS' and geometry['cases']==8 and not geometry['physicalLargeFileCreated'];checks.append('64-bit-layout-index-and-seek-eight-synthetic-cases')
 return dict(result='PASS',count=len(checks),checks=checks,profiles=rows,largeGeometry=geometry)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
