"""Raw FLAC progressive view: exact packets/PCM, partial frames and CRC fail-closed."""
import argparse,copy,hashlib,json,subprocess,tempfile
from fractions import Fraction
from pathlib import Path
from progressive_raw_flac_fixture import fixture,prefix_at_seconds,PROFILES
from progressive_standalone_fixture import publish
from progressive_mux_ts_conformance import Rpc

def run(runner):
    checks=[]
    for profile in PROFILES:
        with tempfile.TemporaryDirectory(prefix='foxicmp-native-raw-flac-') as tmp:
            root=Path(tmp);model,raw,original,probe=fixture(root,profile,4);data=root/'data';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'registry')])
            try:
                value=publish(model,raw,data,prefix_at_seconds(raw,probe,2));assert rpc('open',session=value,directory=str(data))['ok'];assert len(rpc('layout')['tracks'])==1;assert rpc('index')['tracks'][0]['samples']>0
                assert rpc('status')['mediaMode']=='standalone-flac';value=publish(model,raw,data,len(raw),previous=value,complete=True);assert rpc('publish',session=value)['ok'];assert rpc('index')['tracks'][0]['samples']==len(probe['packets'])
                layout=rpc('layout')['tracks'][0];size=int(layout['available']);assert int(layout['total'])==size;out=bytearray()
                for at in range(0,size,65536):
                    r=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,size-at)));assert r['bytesRead']>0;out+=bytes.fromhex(r['hex'])
                assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==0
                projected=root/'projected.mp4';projected.write_bytes(out);other=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(projected)]));assert len(other['streams'])==1 and other['streams'][0]['codec_name']=='flac';assert len(other['packets'])==len(probe['packets'])
                frames=json.loads(subprocess.check_output(['ffprobe','-v','error','-select_streams','a:0','-show_frames','-of','json',str(projected)]))['frames'];assert len(frames)==len(other['packets'])
                clock=Fraction(other['streams'][0]['time_base']);original_clock=Fraction(probe['streams'][0]['time_base'])
                for a,b,f in zip(other['packets'],probe['packets'],frames):
                    assert a['pos']==f['pkt_pos'] and a['data_hash']==b['data_hash'] and int(a['pts'])*clock==int(b['pts'])*original_clock and int(f['nb_samples'])/Fraction(other['streams'][0]['sample_rate'])==int(b['duration'])*original_clock,(a,b,f)
                assert sum(int(f['nb_samples']) for f in frames)==probe['_pcm_bytes']//((2 if probe['_pcm_format']=='s16le' else 4)*int(other['streams'][0]['channels']))
                pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(projected),'-map','0:a:0','-c:a','pcm_'+probe['_pcm_format'],'-f',probe['_pcm_format'],'-']);assert len(pcm)==probe['_pcm_bytes'] and hashlib.sha256(pcm).hexdigest()==probe['_pcm_sha256'];assert original.read_bytes()==raw
                readers=lambda:rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];assert len(readers())==1;rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
                checks += [profile+'-single-audio-track',profile+'-packet-hashes-and-clocks',profile+'-pcm-bit-exact',profile+'-confirmed-eof-and-last-owner-release']
            finally:rpc.close()
    with tempfile.TemporaryDirectory(prefix='foxicmp-raw-flac-errors-') as tmp:
        root=Path(tmp);model,raw,_,probe=fixture(root/'source',PROFILES[-1],4)
        def attempt(name,body=raw,change=None,ready=True):
            data=root/name;data.mkdir();plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(body));value=publish(plan,body,data,len(body),complete=True)
            if change:change(value)
            rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                result=rpc('open',session=value,directory=str(data));assert not result['ok'],(name,result);assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name+'-rejected-without-leak')
            finally:rpc.close()
        for name,change in [('optional',lambda v:v['tracks'][0].update(required=False)),('video',lambda v:v['tracks'][0].update(kind='video')),('wrong-codec',lambda v:v['tracks'][0].update(codecHint='aac-lc')),('no-sha',lambda v:v['resources'][0].pop('integrity')),('offset',lambda v:v['timeline'].update(timestampOffset='1')),('wrong-duration',lambda v:v['timeline'].update(duration='1'))]:attempt(name,change=change)
        frame=int(probe['packets'][0]['pos']);size=int(probe['packets'][0]['size'])
        for name,at,xor in [('marker',0,1),('metadata-type',4,1),('variable-blocking',frame+1,1),('header-crc',frame+5,1),('frame-crc',frame+size-1,1)]:
            b=bytearray(raw);b[at]^=xor;attempt(name,b)
        attempt('truncated-last-frame',raw[:-1]);b=bytearray(raw);b[8:10]=(1).to_bytes(2,'big');attempt('invalid-block-size',b)
        for name,rate,channels,bits in [('rate',32000,2,24),('channels',48000,3,24),('depth',48000,2,20)]:
            b=bytearray(raw);fields=int.from_bytes(b[18:26],'big');fields=(rate<<44)|((channels-1)<<41)|((bits-1)<<36)|(fields&((1<<36)-1));b[18:26]=fields.to_bytes(8,'big');attempt('unsupported-'+name,b)
        b=bytearray(raw);b[5:8]=(1048576).to_bytes(3,'big');attempt('metadata-over-limit',b)
        # Small independently constructed frames exercise constant/verbatim/fixed/LPC.
        # The test oracle does not call the production bit parser or CRC implementation.
        def crc(data,width,poly):
            value=0
            for byte in data:
                value^=byte<<(width-8)
                for _ in range(8):value=((value<<1)^ (poly if value&(1<<(width-1)) else 0))&((1<<width)-1)
            return value
        def simple_frame(kind,negative_shift=False):
            header=bytes.fromhex('fff86000000f');header+=bytes([crc(header,8,7)])
            bits='0'+format({'constant':0,'verbatim':1,'fixed':8,'lpc':32}[kind],'06b')+'0'
            if kind=='constant':bits+='0'*16
            elif kind=='verbatim':bits+='0'*(16*16)
            elif kind=='fixed':bits+='00'+'0000'+'0000'+'1'*16
            else:bits+='0'*16+'0000'+('11111' if negative_shift else '00000')+'0'+'00'+'0000'+'0000'+'1'*15
            bits+='0'*((-len(bits))%8);frame=header+int(bits,2).to_bytes(len(bits)//8,'big');frame+=crc(frame,16,0x8005).to_bytes(2,'big')
            info=(16).to_bytes(2,'big')*2+b'\0'*6+((44100<<44)|(15<<36)|16).to_bytes(8,'big')+b'\0'*16
            return b'fLaC'+bytes.fromhex('80000022')+info+frame
        for kind in ('constant','verbatim','fixed','lpc'):
            body=simple_frame(kind);data=root/('subframe-'+kind);data.mkdir();plan=copy.deepcopy(model);plan['timeline']['duration']=str(16*10000000//44100+1);plan['metadata']['chapters']=[];plan['resources'][0]['length']=str(len(body));value=publish(plan,body,data,len(body),complete=True);rpc=Rpc([runner,'--registry',str(root/('subframe-'+kind+'-registry'))])
            try:
                assert rpc('open',session=value,directory=str(data))['ok'];assert rpc('index')['tracks'][0]['samples']==1
                pcm=subprocess.check_output(['ffmpeg','-v','error','-f','flac','-i','pipe:0','-f','s16le','-'],input=body);assert pcm==b'\0'*32;checks.append(kind+'-subframe-framing-and-independent-decode')
            finally:rpc.close()
        attempt('negative-lpc-shift',simple_frame('lpc',True),change=lambda v:(v['timeline'].update(duration=str(16*10000000//44100+1)),v['metadata'].update(chapters=[])))
        # A partial last frame is ignored until confirmed bytes finish it, never EOF.
        data=root/'partial';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'partial-registry')])
        try:
            end=int(probe['packets'][1]['pos'])+int(probe['packets'][1]['size']);value=publish(model,raw,data,end-1);assert rpc('open',session=value,directory=str(data))['ok'];assert rpc('index')['tracks'][0]['samples']==1
            value=publish(model,raw,data,end,previous=value);assert rpc('publish',session=value)['ok'];assert rpc('index')['tracks'][0]['samples']==2;rpc('release');checks.append('partial-frame-waits-and-resumes')
        finally:rpc.close()
    return dict(result='PASS',count=len(checks),checks=checks,projectedPacketsExact=True,pcmBitExact=True,originalsModified=False)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
