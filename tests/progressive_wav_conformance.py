"""Real WAV projection: byte-exact PCM, clocks, partial writes and rejection."""
import argparse,copy,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
from fractions import Fraction
from progressive_wav_fixture import fixture,PROFILES,prefix_at_seconds
from progressive_standalone_fixture import publish
from progressive_mux_ts_conformance import Rpc

def run(runner):
 checks=[]
 for profile in PROFILES:
  with tempfile.TemporaryDirectory(prefix='foxicmp-wav-') as tmp:
   root=Path(tmp);model,raw,original,probe=fixture(root,profile,4);data=root/'data';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'registry')]);rate=int(profile.split('-')[-1]);align=probe['_block_align']
   try:
    value=publish(model,raw,data,prefix_at_seconds(raw,probe,2)+1);assert rpc('open',session=value,directory=str(data))['ok'];status=rpc('status');assert status['mediaMode']=='standalone-wav-pcm16' and status['audioCodec']=='pcm-s16le';before=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(before),length='1')['bytesRead']==-1
    value=publish(model,raw,data,len(raw),previous=value);assert rpc('publish',session=value)['ok'];full=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(full),length='1')['bytesRead']==-1
    value=publish(model,raw,data,len(raw),previous=value,complete=True);assert rpc('publish',session=value)['ok'];assert int(rpc('layout')['tracks'][0]['available'])==full
    assert rpc('index')['tracks'][0]['samples']==(rate*4+1023)//1024
    out=bytearray()
    for at in range(0,full,65536):
     r=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,full-at)));assert r['bytesRead']>0;out+=bytes.fromhex(r['hex'])
    assert rpc('read',decoder=True,track=0,offset=str(full),length='1')['bytesRead']==0
    projected=root/'projected.mp4';projected.write_bytes(out);other=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-of','json',str(projected)]));stream=other['streams'][0];assert len(other['streams'])==1 and stream['codec_name']=='pcm_s16le' and int(stream['sample_rate'])==rate
    cursor=0
    for packet in other['packets']:
     n=int(packet['duration']);assert int(packet['pts'])*Fraction(stream['time_base'])==Fraction(cursor,rate) and int(packet['size'])==n*align
     at=int(packet['pos']);assert out[at:at+n*align]==raw[44+cursor*align:44+(cursor+n)*align];cursor+=n
    assert cursor==rate*4
    pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(projected),'-c:a','pcm_s16le','-f','s16le','-']);assert hashlib.sha256(pcm).hexdigest()==probe['_pcm_sha256'] and len(pcm)==probe['_pcm_bytes']
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
  for name,at,fmt,value in [('riff',0,'4s',b'RF64'),('wave',8,'4s',b'AVI '),('unknown-riff-size',4,'I',0xffffffff),('format',20,'H',3),('bits24',34,'H',24),('channels',22,'H',6),('rate',24,'I',96000),('byte-rate',28,'I',1),('align',32,'H',1),('no-fmt',12,'4s',b'JUNK'),('no-data',36,'4s',b'JUNK'),('odd-data',40,'I',len(raw)-45),('unknown-data-size',40,'I',0xffffffff)]:
   body=bytearray(raw);struct.pack_into('<'+fmt,body,at,value);attempt(name,body)
  attempt('truncated',raw[:-1]);attempt('trailing',raw+b'X')
  for count in (7,43,45):
   data=root/('partial-'+str(count));data.mkdir();rpc=Rpc([runner,'--registry',str(root/('partial-registry-'+str(count)))])
   try:
    v=publish(model,raw,data,count);a=rpc('open',session=v,directory=str(data));assert not a['ok'] and a['code']=='waiting_initialization',a;assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('partial-'+str(count)+'-waits-without-leak')
   finally:rpc.close()
  # Odd-sized metadata chunks must consume their RIFF pad, without becoming PCM.
  for tag in (b'JUNK',b'LIST',b'bext'):
   extra=tag+struct.pack('<I',3)+b'abc\0';body=bytearray(raw[:12]+extra+raw[12:]);struct.pack_into('<I',body,4,len(body)-8);data=root/tag.decode();data.mkdir();v=publish(model,body,data,len(body),complete=True);rpc=Rpc([runner,'--registry',str(root/(tag.decode()+'-registry'))])
   try:assert rpc('open',session=v,directory=str(data))['ok'];assert rpc('index')['tracks'][0]['samples']==(48000*4+1023)//1024;checks.append(tag.decode()+'-odd-metadata-pad-accepted')
   finally:rpc.close()
 return dict(result='PASS',count=len(checks),checks=checks)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
