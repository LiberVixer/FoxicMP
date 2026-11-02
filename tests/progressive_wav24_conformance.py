"""Real WAV projection: byte-exact PCM, clocks, partial writes and rejection."""
import argparse,copy,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
from fractions import Fraction
from progressive_wav24_fixture import fixture,PROFILES,prefix_at_seconds
from progressive_standalone_fixture import publish
from progressive_mux_ts_conformance import Rpc

def run(runner):
 checks=[];rows=[]
 for profile in PROFILES:
  with tempfile.TemporaryDirectory(prefix='foxicmp-wav-') as tmp:
   root=Path(tmp);model,raw,original,probe=fixture(root,profile,4);data=root/'data';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'registry')]);rate=int(profile.split('-')[-1]);align=probe['_block_align']
   try:
    value=publish(model,raw,data,prefix_at_seconds(raw,probe,2)+1);assert rpc('open',session=value,directory=str(data))['ok'];status=rpc('status');assert status['mediaMode']=='standalone-wav-pcm24' and status['audioCodec']=='pcm-s24le';before=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(before),length='1')['bytesRead']==-1
    value=publish(model,raw,data,len(raw),previous=value);assert rpc('publish',session=value)['ok'];full=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(full),length='1')['bytesRead']==-1
    value=publish(model,raw,data,len(raw),previous=value,complete=True);assert rpc('publish',session=value)['ok'];assert int(rpc('layout')['tracks'][0]['available'])==full
    assert rpc('index')['tracks'][0]['samples']==(rate*4+1023)//1024
    out=bytearray()
    for at in range(0,full,65536):
     r=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,full-at)));assert r['bytesRead']>0;out+=bytes.fromhex(r['hex'])
    assert rpc('read',decoder=True,track=0,offset=str(full),length='1')['bytesRead']==0
    projected=root/'projected.mp4';projected.write_bytes(out);other=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-of','json',str(projected)]));stream=other['streams'][0];assert len(other['streams'])==1 and stream['codec_name']=='pcm_s24le' and int(stream['sample_rate'])==rate
    cursor=0
    for packet in other['packets']:
     n=int(packet['duration']);assert int(packet['pts'])*Fraction(stream['time_base'])==Fraction(cursor,rate) and int(packet['size'])==n*align
     at=int(packet['pos']);assert out[at:at+n*align]==raw[44+cursor*align:44+(cursor+n)*align];cursor+=n
    assert cursor==rate*4
    pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(projected),'-c:a','pcm_s24le','-f','s24le','-']);assert hashlib.sha256(pcm).hexdigest()==probe['_pcm_sha256'] and len(pcm)==probe['_pcm_bytes']
    rows.append(dict(profile=profile,pcmBytes=len(pcm),pcmSha256=hashlib.sha256(pcm).hexdigest(),growing=True))
    assert original.read_bytes()==raw
    readers=lambda:rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];assert len(readers())==1;rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
    checks.extend(profile+'-'+name for name in ('growing-single-audio','partial-block-waits','full-prefix-waits-for-complete','packet-bytes-and-clocks','decoded-pcm-bit-exact','last-owner-release','original-unchanged'))
   finally:rpc.close()
 with tempfile.TemporaryDirectory(prefix='foxicmp-wav-errors-') as tmp:
  root=Path(tmp);model,raw,_,probe=fixture(root/'source',PROFILES[-1],4)
  def attempt(name,body=raw,change=None):
   data=root/name;data.mkdir();plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(body));v=publish(plan,body,data,len(body),complete=True)
   if change:change(v)
   rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
   try:
    a=rpc('open',session=v,directory=str(data));assert not a['ok'],(name,a);assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name+'-rejected-without-leak')
   finally:rpc.close()
  for name,change in [('optional',lambda v:v['tracks'][0].update(required=False)),('video',lambda v:v['tracks'][0].update(kind='video')),('codec',lambda v:v['tracks'][0].update(codecHint='aac-lc')),('no-sha',lambda v:v['resources'][0].pop('integrity')),('offset',lambda v:v['timeline'].update(timestampOffset='1')),('duration',lambda v:v['timeline'].update(duration='1'))]:attempt(name,change=change)
  for name,at,fmt,value in [('riff',0,'4s',b'RF64'),('wave',8,'4s',b'AVI '),('unknown-riff-size',4,'I',0xffffffff),('format',20,'H',3),('bits16',34,'H',16),('channels',22,'H',6),('rate',24,'I',96000),('byte-rate',28,'I',1),('align',32,'H',1),('no-fmt',12,'4s',b'JUNK'),('no-data',36,'4s',b'JUNK'),('odd-data',40,'I',len(raw)-45),('unknown-data-size',40,'I',0xffffffff)]:
   body=bytearray(raw);struct.pack_into('<'+fmt,body,at,value);attempt(name,body)
  attempt('truncated',raw[:-1]);attempt('trailing',raw+b'X')
  for count in (7,43,45):
   data=root/('partial-'+str(count));data.mkdir();rpc=Rpc([runner,'--registry',str(root/('partial-registry-'+str(count)))])
   try:
    v=publish(model,raw,data,count);a=rpc('open',session=v,directory=str(data));assert not a['ok'] and a['code']=='waiting_initialization',a;assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('partial-'+str(count)+'-waits-without-leak')
   finally:rpc.close()
  def oracle(rpc,expected):
   size=int(rpc('layout')['tracks'][0]['available']);out=bytearray()
   for at in range(0,size,65536):
    v=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,size-at)));assert v['bytesRead']>0;out+=bytes.fromhex(v['hex'])
   projected=root/'oracle.mp4';projected.write_bytes(out)
   pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(projected),'-c:a','pcm_s24le','-f','s24le','-']);assert pcm==expected
   return size
  # Odd-sized metadata chunks must consume their RIFF pad, without becoming PCM.
  for tag in (b'JUNK',b'LIST',b'bext'):
   extra=tag+struct.pack('<I',3)+b'abc\0';body=bytearray(raw[:12]+extra+raw[12:]);struct.pack_into('<I',body,4,len(body)-8);data=root/tag.decode();data.mkdir();v=publish(model,body,data,len(body),complete=True);rpc=Rpc([runner,'--registry',str(root/(tag.decode()+'-registry'))])
   try:
    assert rpc('open',session=v,directory=str(data))['ok'];assert rpc('index')['tracks'][0]['samples']==(48000*4+1023)//1024;checks.append(tag.decode()+'-odd-metadata-pad-accepted')
    oracle(rpc,raw[44:]);checks.append(tag.decode()+'-metadata-excluded-from-pcm')
   finally:rpc.close()
  # cbSize=0 is allowed in an 18-byte PCM fmt; nonzero extensions are not.
  body=bytearray(raw[:36]+b'\0\0'+raw[36:]);struct.pack_into('<I',body,16,18);struct.pack_into('<I',body,4,len(body)-8)
  data=root/'fmt18';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'fmt18-registry')])
  try:
   v=publish(model,body,data,len(body),complete=True);assert rpc('open',session=v,directory=str(data))['ok'];oracle(rpc,raw[44:]);checks.append('fmt18-zero-extension-pcm-bit-exact')
  finally:rpc.close()
  bad=bytearray(body);struct.pack_into('<H',bad,36,1);attempt('fmt18-extension',bad)
  # One extra mono sample makes the data chunk odd. Its RIFF pad is not PCM.
  mono,odd,_,_=fixture(root/'odd-source',PROFILES[0],4);tail=b'\x12\x34\x80';body=bytearray(odd+tail+b'\0');struct.pack_into('<I',body,40,len(odd)-44+3);struct.pack_into('<I',body,4,len(body)-8)
  samples=44100*4+1;mono['timeline']['duration']=str(samples*10000000//44100);data=root/'odd-data-valid';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'odd-data-valid-registry')])
  try:
   v=publish(mono,body,data,len(body)-1);assert rpc('open',session=v,directory=str(data))['ok'];size=oracle(rpc,odd[44:]+tail);assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==-1;checks.append('odd-data-waits-for-writer')
   v=publish(mono,body,data,len(body),previous=v,complete=True);assert rpc('publish',session=v)['ok'];assert oracle(rpc,odd[44:]+tail)==size;assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==0;checks.extend(['odd-data-pad-excluded-from-pcm','odd-data-complete-eof'])
  finally:rpc.close()
  # A late inconsistent complete fails closed and releases its original reader.
  mono,body,_,probe=fixture(root/'append-source',PROFILES[0],4);data=root/'bad-append';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'bad-append-registry')])
  try:
   v=publish(mono,body,data,prefix_at_seconds(body,probe,2));assert rpc('open',session=v,directory=str(data))['ok'];v=publish(mono,body[:-1],data,len(body)-1,previous=v,complete=True);assert not rpc('publish',session=v)['ok'];assert rpc('read',decoder=True,track=0,offset='0',length='1')['bytesRead']==-1;rpc('release');assert not rpc('readers',version=1,sessionId=mono['sessionId'],generation='1')['readers'];checks.append('bad-append-fails-closed-and-releases')
  finally:rpc.close()
  data=root/'tamper';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'tamper-registry')])
  try:
   v=publish(model,raw,data,len(raw),complete=True);assert rpc('open',session=v,directory=str(data))['ok'];oracle(rpc,raw[44:])
   with (data/model['resources'][0]['name']).open('r+b') as f:f.seek(47);f.write(bytes([raw[47]^1]))
   packet=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-of','json',str(root/'oracle.mp4')]))['packets'][0]
   assert rpc('read',decoder=True,track=0,offset=str(int(packet['pos'])+3),length='1')['bytesRead']==-1;assert not rpc('verify')['ok'];rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('pinned-byte-tamper-fails-closed-and-releases')
  finally:rpc.close()
 return dict(result='PASS',count=len(checks),checks=checks,profiles=rows)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
