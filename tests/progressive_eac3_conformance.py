"""E-AC-3 header/CRC validation, exact packets/clocks/PCM and reader lifetime."""
import argparse,copy,hashlib,json,subprocess,tempfile
from fractions import Fraction
from pathlib import Path
from progressive_eac3_fixture import fixture,PROFILES,BITRATES,frames,prefix_at_seconds
from progressive_standalone_fixture import publish
from progressive_mux_ts_conformance import Rpc

def run(runner):
 checks=[];rows=[]
 with tempfile.TemporaryDirectory(prefix='foxicmp-eac3-') as tmp:
  root=Path(tmp)
  def positive(name,model,raw,probe,growing=True):
   data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
   try:
    if growing:
     prefix=prefix_at_seconds(raw,probe,2);v=publish(model,raw,data,prefix+6);a=rpc('open',session=v,directory=str(data));assert a['ok'],a;status=rpc('status');assert status['mediaMode']=='standalone-eac3' and status['audioCodec']=='eac3' and status['decoderContainer']=='fmp4'
     before=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(before),length='1')['bytesRead']==-1
     v=publish(model,raw,data,prefix+7,previous=v);assert rpc('publish',session=v)['ok'];assert int(rpc('layout')['tracks'][0]['available'])==before
     v=publish(model,raw,data,len(raw),previous=v);assert rpc('publish',session=v)['ok'];size=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==-1
     v=publish(model,raw,data,len(raw),previous=v,complete=True);assert rpc('publish',session=v)['ok']
    else:
     v=publish(model,raw,data,len(raw),complete=True);a=rpc('open',session=v,directory=str(data));assert a['ok'],(name,a);size=int(rpc('layout')['tracks'][0]['available'])
    assert rpc('index')['tracks'][0]['samples']==len(frames(raw))
    out=bytearray()
    for at in range(0,size,65536):
     a=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(size-at,65536)));assert a['bytesRead']>0;out+=bytes.fromhex(a['hex'])
    assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==0
    projected=data/'projected.mp4';projected.write_bytes(out);other=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(projected)]));assert len(other['streams'])==1 and other['streams'][0]['codec_name']=='eac3'
    assert [p['data_hash'] for p in other['packets']]==[p['data_hash'] for p in probe['packets']]
    for k,p in enumerate(other['packets']):assert int(p['pts'])*Fraction(other['streams'][0]['time_base'])==Fraction(k*1536,48000) and int(p['duration'])*Fraction(other['streams'][0]['time_base'])==Fraction(1536,48000)
    pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(projected),'-c:a','pcm_s16le','-f','s16le','-']);assert len(pcm)==probe['_pcm_bytes'] and hashlib.sha256(pcm).hexdigest()==probe['_pcm_sha256']
    readers=lambda:rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];assert len(readers())==1;rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
    checks.extend(name+'-'+v for v in ('packet-hashes-and-clocks','pcm-bit-exact','last-owner-release'))
    if growing:checks.extend(name+'-'+v for v in ('partial-frame-waits','full-prefix-waits-for-writer'))
    rows.append(dict(profile=name,frames=len(other['packets']),pcmBytes=len(pcm),pcmSha256=hashlib.sha256(pcm).hexdigest(),growing=growing))
   finally:rpc.close()
  for profile in PROFILES:
   model,raw,original,probe=fixture(root/('source-'+profile),profile,4);positive(profile,model,raw,probe);assert original.read_bytes()==raw;checks.append(profile+'-original-unchanged')
  # Each advertised channel/bitrate pair has an independent PCM oracle.
  for channels in ('mono','stereo'):
   for rate in BITRATES:
    profile='audio-eac3-'+channels+'-'+str(rate)
    if profile in PROFILES:continue
    model,raw,_,probe=fixture(root/('source-'+profile),profile,4);positive(profile,model,raw,probe,growing=False)
  for service in ('vi','hi'):
   name='metadata-'+service;model,raw,_,probe=fixture(root/('source-'+name),'audio-eac3-stereo-192',4,service=service);positive(name,model,raw,probe,growing=False)
  model,raw,_,probe=fixture(root/'negative-source',PROFILES[-1],4)
  def negative(name,body=raw,change=None):
   data=root/name;data.mkdir();plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(body));v=publish(plan,body,data,len(body),complete=True)
   if change:change(v)
   rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
   try:
    a=rpc('open',session=v,directory=str(data));assert not a['ok'],(name,a);assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name+'-rejected-without-leak')
   finally:rpc.close()
  for name,change in [('optional',lambda v:v['tracks'][0].update(required=False)),('video',lambda v:v['tracks'][0].update(kind='video')),('codec',lambda v:v['tracks'][0].update(codecHint='ac3')),('no-sha',lambda v:v['resources'][0].pop('integrity')),('offset',lambda v:v['timeline'].update(timestampOffset='1')),('duration',lambda v:v['timeline'].update(duration='1'))]:negative(name,change=change)
  for name,at,mask in [('sync',0,1),('dependent-stream',2,64),('substream-id',2,8),('rate',4,64),('block-count',4,16),('bsid',5,128),('channel-mode',4,8),('lfe',4,1),('crc',len(raw)-1,1),('payload',100,1)]:
   b=bytearray(raw);b[at]^=mask;negative(name,b)
  # Negative metadata flags carry a valid CRC, independently regenerated.
  def checksum(frame):
   c=0
   for value in frame[2:-2]:
    c^=value<<8
    for _ in range(8):c=((c<<1)^0x8005 if c&0x8000 else c<<1)&65535
   frame[-2:]=c.to_bytes(2,'big');return frame
  def flag(bit):
   n=frames(raw)[0][1];b=bytearray(raw[:n]);b[bit//8]^=1<<(7-bit%8);return bytes(checksum(b))+raw[n:]
  negative('mixing-metadata',flag(51));negative('additional-bsi',flag(53))
  other,body,_,_=fixture(root/'other-bitrate','audio-eac3-stereo-192',4);first=frames(raw)[0][1];negative('configuration-change',raw[:first]+body)
  negative('truncated',raw[:-1]);negative('trailing',raw+b'X');negative('id3-prefix',b'ID3'+raw)
  for n in (6,7,frames(raw)[0][1]-1):
   data=root/('partial-'+str(n));data.mkdir();rpc=Rpc([runner,'--registry',str(root/('partial-registry-'+str(n)))])
   try:
    v=publish(model,raw,data,n);a=rpc('open',session=v,directory=str(data));assert not a['ok'] and a['code']=='waiting_initialization',a;assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('partial-'+str(n)+'-waits-without-leak')
   finally:rpc.close()
  data=root/'bad-append';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'bad-append-registry')])
  try:
   v=publish(model,raw,data,prefix_at_seconds(raw,probe,2));assert rpc('open',session=v,directory=str(data))['ok'];b=bytearray(raw);b[-1]^=1;v=publish(model,b,data,len(b),previous=v,complete=True);a=rpc('publish',session=v);assert not a['ok'],a;assert rpc('read',decoder=True,track=0,offset='0',length='1')['bytesRead']==-1
   rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('bad-append-fails-closed-and-releases')
  finally:rpc.close()
 return dict(result='PASS',count=len(checks),checks=checks,profiles=rows)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
