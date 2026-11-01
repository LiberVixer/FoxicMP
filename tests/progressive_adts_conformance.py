"""ADTS framing, exact AAC payload/clocks/PCM, confirmed EOF and fail-closed cases."""
import argparse,copy,hashlib,json,subprocess,tempfile
from fractions import Fraction
from pathlib import Path
from progressive_adts_fixture import fixture,frames,PROFILES,prefix_at_seconds
from progressive_standalone_fixture import publish
from progressive_mux_ts_conformance import Rpc

def run(runner):
    checks=[]
    for profile in PROFILES:
        with tempfile.TemporaryDirectory(prefix='foxicmp-adts-') as tmp:
            root=Path(tmp);model,raw,original,probe=fixture(root,profile,4);data=root/'data';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'registry')])
            try:
                value=publish(model,raw,data,prefix_at_seconds(raw,probe,2));assert rpc('open',session=value,directory=str(data))['ok'];assert len(rpc('layout')['tracks'])==1
                status=rpc('status');assert status['mediaMode']=='standalone-adts-aac-lc' and status['audioContainer']=='adts' and status['decoderContainer']=='fmp4' and status['videoCodec']==''
                before=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(before),length='1')['bytesRead']==-1
                value=publish(model,raw,data,len(raw),previous=value,complete=True);assert rpc('publish',session=value)['ok'];assert rpc('index')['tracks'][0]['samples']==len(probe['packets'])
                size=int(rpc('layout')['tracks'][0]['available']);out=bytearray()
                for at in range(0,size,65536):
                    r=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,size-at)));assert r['bytesRead']>0;out+=bytes.fromhex(r['hex'])
                assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==0
                projected=root/'projected.mp4';projected.write_bytes(out);other=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(projected)]));assert len(other['streams'])==1 and other['streams'][0]['codec_name']=='aac' and len(other['packets'])==len(probe['packets'])
                decoded=json.loads(subprocess.check_output(['ffprobe','-v','error','-select_streams','a:0','-show_frames','-of','json',str(projected)]))['frames'];assert len(decoded)==len(other['packets'])
                for a,b,f in zip(other['packets'],probe['packets'],decoded):
                    assert a['data_hash']=='SHA256:'+b['_payload_sha256'] and int(a['pts'])*Fraction(other['streams'][0]['time_base'])==int(b['pts'])*Fraction(probe['streams'][0]['time_base']) and int(f['nb_samples'])/Fraction(other['streams'][0]['sample_rate'])==int(b['duration'])*Fraction(probe['streams'][0]['time_base']),(a,b)
                pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(projected),'-map','0:a:0','-c:a','pcm_s16le','-f','s16le','-']);assert len(pcm)==probe['_pcm_bytes'] and hashlib.sha256(pcm).hexdigest()==probe['_pcm_sha256'];assert original.read_bytes()==raw
                readers=lambda:rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];assert len(readers())==1;rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
                checks += [profile+'-one-audio-track',profile+'-aac-payload-hashes-and-packet-clocks',profile+'-decoded-pcm-equal-to-native-adts',profile+'-waiting-eof-and-last-owner-release']
            finally:rpc.close()
    with tempfile.TemporaryDirectory(prefix='foxicmp-adts-errors-') as tmp:
        root=Path(tmp);model,raw,_,probe=fixture(root/'source',PROFILES[-1],4)
        def attempt(name,body=raw,change=None):
            data=root/name;data.mkdir();plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(body));v=publish(plan,body,data,len(body),complete=True)
            if change:change(v)
            rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                answer=rpc('open',session=v,directory=str(data));assert not answer['ok'],(name,answer);assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name+'-rejected-without-leak')
            finally:rpc.close()
        for name,change in [('optional',lambda v:v['tracks'][0].update(required=False)),('video',lambda v:v['tracks'][0].update(kind='video')),('codec',lambda v:v['tracks'][0].update(codecHint='opus')),('no-sha',lambda v:v['resources'][0].pop('integrity')),('offset',lambda v:v['timeline'].update(timestampOffset='1')),('duration',lambda v:v['timeline'].update(duration='1'))]:attempt(name,change=change)
        for name,at,mask in [('sync',0,1),('mpeg2',1,8),('layer',1,2),('crc-protected',1,1),('non-lc',2,64),('sample-rate',2,4),('channel-config',3,64),('multiple-blocks',6,1)]:
            b=bytearray(raw);b[at]^=mask;attempt(name,b)
        second=frames(raw)[1][0];b=bytearray(raw);b[second+2]=(b[second+2]&0xc3)|(4<<2);attempt('changed-configuration',b)
        b=bytearray(raw);b[3]&=252;b[4]=0;b[5]&=31;attempt('zero-frame-length',b)
        attempt('truncated',raw[:-1]);attempt('trailing-data',raw+b'X');attempt('id3-prefix',b'ID3'+raw)
        data=root/'partial';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'partial-registry')])
        try:
            end=frames(raw)[0][1];v=publish(model,raw,data,7);answer=rpc('open',session=v,directory=str(data));assert not answer['ok'] and answer['code']=='waiting_initialization',answer;assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('partial-initial-frame-waits-without-leak')
            v=publish(model,raw,data,end+6,previous=v);assert rpc('open',session=v,directory=str(data))['ok'];assert rpc('index')['tracks'][0]['samples']==1
            at,n=frames(raw)[1];v=publish(model,raw,data,at+n-1,previous=v);assert rpc('publish',session=v)['ok'] and rpc('index')['tracks'][0]['samples']==1
            v=publish(model,raw,data,at+n,previous=v);assert rpc('publish',session=v)['ok'] and rpc('index')['tracks'][0]['samples']==2;rpc('release');checks.append('partial-header-and-payload-wait-until-frame-complete')
        finally:rpc.close()
    return dict(result='PASS',count=len(checks),checks=checks,projectedPacketsExact=True,decodedPcmEqualToNative=True,originalsModified=False)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
