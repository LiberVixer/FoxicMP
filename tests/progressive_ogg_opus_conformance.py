"""Ogg CRC/continuations/granules, payload identity, decoded PCM and leases."""
import argparse,copy,hashlib,json,struct,subprocess,tempfile
from fractions import Fraction
from pathlib import Path
from progressive_ogg_opus_fixture import fixture,PROFILES,pages,packets,prefix_at_seconds,crc_page
from progressive_standalone_fixture import publish
from progressive_mux_ts_conformance import Rpc

def page(laces,body,flags,granule,serial,sequence):
 h=bytearray(b'OggS'+bytes((0,flags))+struct.pack('<QIII',granule,serial,sequence,0)+bytes((len(laces),))+bytes(laces)+body)
 return crc_page(h)

def repage(raw,split_audio=False,large_tags=False):
 items=packets(raw);serial=struct.unpack_from('<I',raw,14)[0];out=[];decoded=0;final=pages(raw)[-1]['granule']
 if large_tags:
  tags=b'OpusTags'+struct.pack('<I',70000)+b'V'*70000+struct.pack('<I',0);items[1]=tags
 for index,packet in enumerate(items):
  granule=0
  if index>=2:
   toc=packet[0];assert toc&3==0 and toc>>3>=16;decoded+=120<<(toc>>3&3);granule=final if index+1==len(items) else decoded
  lace=[255]*(len(packet)//255)+[len(packet)%255];at=0
  while lace:
   count=1 if split_audio and index==2 and at==0 else min(255,len(lace));part=lace[:count];lace=lace[count:];n=sum(part)
   flags=(2 if index==0 else 0)|(1 if at else 0)|(4 if index+1==len(items) and not lace else 0)
   out.append(page(part,packet[at:at+n],flags,2**64-1 if lace else granule,serial,len(out)));at+=n
 return b''.join(out)

def run(runner):
 checks=[];rows=[]
 with tempfile.TemporaryDirectory(prefix='foxicmp-ogg-opus-') as tmp:
  root=Path(tmp)
  def positive(name,model,raw,probe,growing=True):
   data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))]);original=data/'original.opus';original.write_bytes(raw)
   try:
    if growing:
     prefix=prefix_at_seconds(raw,probe,2);v=publish(model,raw,data,prefix+min(7,len(raw)-prefix));assert rpc('open',session=v,directory=str(data))['ok'];status=rpc('status');assert status['mediaMode']=='standalone-ogg-opus' and status['decoderContainer']=='webm' and status['audioCodec']=='opus'
     before=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(before),length='1')['bytesRead']==-1
     # An incomplete next page publishes no partial packet into the decoder view.
     if prefix+8<len(raw):
      v=publish(model,raw,data,prefix+8,previous=v);assert rpc('publish',session=v)['ok'];assert int(rpc('layout')['tracks'][0]['available'])==before
     v=publish(model,raw,data,len(raw),previous=v);assert rpc('publish',session=v)['ok'];size=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==-1
     v=publish(model,raw,data,len(raw),previous=v,complete=True);assert rpc('publish',session=v)['ok']
    else:
     v=publish(model,raw,data,len(raw),complete=True);answer=rpc('open',session=v,directory=str(data));assert answer['ok'],(name,answer);size=int(rpc('layout')['tracks'][0]['available'])
    index=rpc('index',sampleTimes=True);assert index['tracks'][0]['samples']==len(packets(raw))-2,index
    out=bytearray()
    for at in range(0,size,65536):
     a=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(size-at,65536)));assert a['bytesRead']>0;out+=bytes.fromhex(a['hex'])
    assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==0
    projected=data/'projected.webm';projected.write_bytes(out)
    other=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(projected)]));native=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-of','json',str(original)]))
    assert len(other['streams'])==1 and other['streams'][0]['codec_name']=='opus'
    assert [p['data_hash'] for p in other['packets']]==['SHA256:'+hashlib.sha256(p).hexdigest() for p in packets(raw)[2:]]
    for a,b in zip(other['packets'],native['packets']):assert abs(int(a['pts'])*Fraction(other['streams'][0]['time_base'])-int(b['pts'])*Fraction(native['streams'][0]['time_base']))<=Fraction(1,1000),(name,a,b)
    pcm=lambda path:subprocess.check_output(['ffmpeg','-v','error','-i',str(path),'-c:a','pcm_s16le','-f','s16le','-'])
    actual=pcm(projected);expected=pcm(original);assert actual==expected,(name,len(actual),len(expected));assert original.read_bytes()==raw
    readers=lambda:rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];assert len(readers())==1;rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
    checks.extend(name+'-'+v for v in ('packet-hashes-and-clocks','native-pcm-identical','last-owner-release','original-unchanged'))
    if growing:checks.extend(name+'-'+v for v in ('partial-page-waits','full-prefix-waits-for-writer'))
    rows.append(dict(profile=name,packets=len(other['packets']),pcmBytes=len(actual),pcmSha256=hashlib.sha256(actual).hexdigest(),growing=growing))
   finally:rpc.close()
  for profile in PROFILES:
   model,raw,_,probe=fixture(root/('source-'+profile),profile,4);positive(profile,model,raw,probe)
  for ms in ('2.5','5','40','60'):
   name='packet-duration-'+ms;model,raw,_,probe=fixture(root/('source-'+name),'audio-ogg-opus-stereo-'+ms,4);positive(name,model,raw,probe,growing=False)
  model,raw,_,probe=fixture(root/'variants',PROFILES[-1],4)
  for name,body in [('continued-audio',repage(raw,split_audio=True)),('continued-tags',repage(raw,large_tags=True))]:
   plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(body));positive(name,plan,body,probe,growing=False)
  def change_page(body,which,change):
   ps=pages(body);p=ps[which];part=bytearray(body[p['at']:p['end']]);change(part);return body[:p['at']]+crc_page(part)+body[p['end']:]
  gain=change_page(raw,0,lambda b:struct.pack_into('<h',b,28+16,256*3));positive('output-gain-preserved',model,gain,probe,growing=False)
  trimmed=change_page(raw,-1,lambda b:struct.pack_into('<Q',b,6,pages(raw)[-1]['granule']-1));plan=copy.deepcopy(model);plan['timeline']['duration']=str(191999*10000000//48000);positive('single-sample-end-trim',plan,trimmed,probe,growing=False)
  def negative(name,body=raw,change=None):
   data=root/name;data.mkdir();plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(body));v=publish(plan,body,data,len(body),complete=True)
   if change:change(v)
   rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
   try:
    a=rpc('open',session=v,directory=str(data));assert not a['ok'],(name,a);assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name+'-rejected-without-leak')
   finally:rpc.close()
  for name,change in [('optional',lambda v:v['tracks'][0].update(required=False)),('video',lambda v:v['tracks'][0].update(kind='video')),('codec',lambda v:v['tracks'][0].update(codecHint='vorbis')),('no-sha',lambda v:v['resources'][0].pop('integrity')),('offset',lambda v:v['timeline'].update(timestampOffset='1')),('duration',lambda v:v['timeline'].update(duration='1'))]:negative(name,change=change)
  broken=bytearray(raw);broken[-1]^=1;negative('page-crc',broken)
  for name,which,change in [('version',0,lambda b:b.__setitem__(4,1)),('no-bos',0,lambda b:b.__setitem__(5,0)),('sequence',2,lambda b:struct.pack_into('<I',b,18,99)),('serial',2,lambda b:struct.pack_into('<I',b,14,99)),('unexpected-continuation',2,lambda b:b.__setitem__(5,b[5]|1)),('no-eos',-1,lambda b:b.__setitem__(5,b[5]&~4)),('granule-regression',-1,lambda b:struct.pack_into('<Q',b,6,1)),('granule-beyond-packets',-1,lambda b:struct.pack_into('<Q',b,6,2**63)),('pre-skip-inexact',0,lambda b:struct.pack_into('<H',b,28+10,313)),('pre-skip-excessive',0,lambda b:struct.pack_into('<H',b,28+10,4800)),('head-version',0,lambda b:b.__setitem__(28+8,2)),('mapping',0,lambda b:b.__setitem__(28+18,1)),('channels',0,lambda b:b.__setitem__(28+9,6)),('tags-signature',1,lambda b:b.__setitem__(27+b[26],0)),('tags-length',1,lambda b:struct.pack_into('<I',b,27+b[26]+8,0xffffffff)),('invalid-opus-frame-count',2,lambda b:b.__setitem__(slice(27+b[26],29+b[26]),b'\x03\x00'))]:negative(name,change_page(raw,which,change))
  negative('truncated-page',raw[:-1]);negative('chained-stream',raw+raw)
  # Updated SHA blocks are valid, but the new Ogg page's own CRC is invalid.
  data=root/'bad-append';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'bad-append-registry')])
  try:
   prefix=prefix_at_seconds(raw,probe,2);v=publish(model,raw,data,prefix);assert rpc('open',session=v,directory=str(data))['ok']
   broken=bytearray(raw);broken[-1]^=1;v=publish(model,broken,data,len(broken),previous=v,complete=True);a=rpc('publish',session=v);assert not a['ok'],a
   assert rpc('read',decoder=True,track=0,offset='0',length='1')['bytesRead']==-1
   rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('bad-append-fails-closed-and-releases')
  finally:rpc.close()
  data=root/'pinned-tamper';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'pinned-tamper-registry')])
  try:
   v=publish(model,raw,data,prefix_at_seconds(raw,probe,2));assert rpc('open',session=v,directory=str(data))['ok']
   with (data/'mux.part').open('r+b') as file:file.seek(40);byte=file.read(1);file.seek(40);file.write(bytes([byte[0]^1]));file.flush()
   a=rpc('verify');assert not a['ok'] and a['code']=='integrity-mismatch',a
   assert rpc('read',decoder=True,track=0,offset='0',length='1')['bytesRead']==-1
   rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('pinned-byte-tamper-fails-closed-and-releases')
  finally:rpc.close()
  continued=repage(raw,split_audio=True);first=pages(continued)[2];data=root/'partial-continuation';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'partial-continuation-registry')])
  try:
   plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(continued));v=publish(plan,continued,data,first['end']);a=rpc('open',session=v,directory=str(data));assert not a['ok'] and a['code']=='waiting_initialization',a
   assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('partial-first-continued-packet-waits-without-leak')
  finally:rpc.close()
  for n in (7,27,pages(raw)[1]['end']+7):
   data=root/('partial-'+str(n));data.mkdir();rpc=Rpc([runner,'--registry',str(root/('partial-registry-'+str(n)))])
   try:
    v=publish(model,raw,data,n);a=rpc('open',session=v,directory=str(data));assert not a['ok'] and a['code']=='waiting_initialization',a;assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('partial-'+str(n)+'-waits-without-leak')
   finally:rpc.close()
 return dict(result='PASS',count=len(checks),checks=checks,profiles=rows)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
