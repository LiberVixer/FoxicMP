"""MP3 framing, exact frames/clocks/PCM, confirmed EOF and fail-closed cases."""
import argparse,copy,hashlib,json,subprocess,tempfile
from fractions import Fraction
from pathlib import Path
from progressive_mp3_indexed_fixture import fixture,frames,PROFILES,prefix_at_seconds
from progressive_standalone_fixture import publish
from progressive_mux_ts_conformance import Rpc

def run(runner):
    checks=[]
    for profile in PROFILES:
        with tempfile.TemporaryDirectory(prefix='foxicmp-mp3-') as tmp:
            root=Path(tmp);model,raw,original,probe=fixture(root,profile,4);data=root/'data';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'registry')])
            try:
                value=publish(model,raw,data,prefix_at_seconds(raw,probe,2));assert rpc('open',session=value,directory=str(data))['ok'];assert len(rpc('layout')['tracks'])==1
                status=rpc('status');assert status['mediaMode']=='standalone-mp3-indexed' and status['audioContainer']=='mp3-indexed' and status['decoderContainer']=='fmp4' and status['videoCodec']==''
                index=status['mp3Index'];expected=probe['_index'];assert index['type']==expected['type'] and index['declaredFrames']==expected['frames'] and index['declaredBytes']==expected['bytes'] and index['encoderDelay']==expected['delay'] and index['encoderPadding']==expected['padding'] and not index['gaplessTrimApplied'] and index['timeline']=='encoded-samples'
                before=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(before),length='1')['bytesRead']==-1
                value=publish(model,raw,data,len(raw),previous=value,complete=True);assert rpc('publish',session=value)['ok'];assert rpc('index')['tracks'][0]['samples']==len(probe['packets'])
                size=int(rpc('layout')['tracks'][0]['available']);out=bytearray()
                for at in range(0,size,65536):
                    r=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,size-at)));assert r['bytesRead']>0;out+=bytes.fromhex(r['hex'])
                assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==0
                projected=root/'projected.mp4';projected.write_bytes(out);other=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(projected)]));assert len(other['streams'])==1 and other['streams'][0]['codec_name']=='mp3' and len(other['packets'])==len(probe['packets'])
                decoded=json.loads(subprocess.check_output(['ffprobe','-v','error','-select_streams','a:0','-show_frames','-of','json',str(projected)]))['frames'];assert len(decoded)==len(other['packets'])
                for a,b,f in zip(other['packets'],probe['packets'],decoded):
                    assert a['data_hash']=='SHA256:'+b['_payload_sha256'] and int(a['pts'])*Fraction(other['streams'][0]['time_base'])==int(b['pts'])*Fraction(probe['streams'][0]['time_base']) and int(f['nb_samples'])/Fraction(other['streams'][0]['sample_rate'])==int(b['duration'])*Fraction(probe['streams'][0]['time_base']),(a,b)
                pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(projected),'-map','0:a:0','-c:a','pcm_s16le','-f','s16le','-']);assert len(pcm)==probe['_pcm_bytes'] and hashlib.sha256(pcm).hexdigest()==probe['_pcm_sha256'];assert original.read_bytes()==raw
                packets=frames(raw)[1:];rate=int(probe['streams'][0]['sample_rate']);channels=int(probe['streams'][0]['channels']);first=max(0,int((2-.25)*rate)//1152);end=packets[-1][0]+packets[-1][1]
                seekpcm=subprocess.check_output(['ffmpeg','-v','quiet','-f','mp3','-i','pipe:0','-c:a','pcm_s16le','-f','s16le','-'],input=raw[packets[first][0]:end]);tail=rate*channels  # last 0.5 s, after reservoir warmup
                assert len(seekpcm)>=tail and seekpcm[-tail:]==pcm[-tail:];checks.append(profile+'-preroll-decoder-tail-equal')
                readers=lambda:rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];assert len(readers())==1;rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
                checks.append(profile+'-index-counters-and-unapplied-delay-metadata')
                checks += [profile+'-one-audio-track',profile+'-mp3-frame-hashes-and-packet-clocks',profile+'-decoded-pcm-equal-to-native-mp3',profile+'-waiting-eof-and-last-owner-release']
            finally:rpc.close()
    with tempfile.TemporaryDirectory(prefix='foxicmp-indexed-mp3-errors-') as tmp:
        root=Path(tmp);model,raw,_,probe=fixture(root/'source',PROFILES[0],4);meta=frames(raw)[0][0];side=17;at=meta+4+side
        def attempt(name,body=raw,change=None):
            data=root/name;data.mkdir();plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(body));value=publish(plan,body,data,len(body),complete=True)
            if change:change(value)
            rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                answer=rpc('open',session=value,directory=str(data));assert not answer['ok'],(name,answer);assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name+'-rejected-without-leak')
            finally:rpc.close()
        for name,change in [('no-sha',lambda v:v['resources'][0].pop('integrity')),('optional',lambda v:v['tracks'][0].update(required=False)),('wrong-codec',lambda v:v['tracks'][0].update(codecHint='aac-lc')),('video',lambda v:v['tracks'][0].update(kind='video')),('wrong-duration',lambda v:(v['timeline'].update(duration='1'),v['metadata'].update(chapters=[])))]:attempt(name,change=change)
        for name,off,n in [('unknown-flags',at+4,31),('missing-frame-counter',at+4,14),('zero-frames',at+8,0),('zero-bytes',at+12,0),('complete-byte-counter-mismatch',at+12,probe['_index']['bytes']+1)]:
            b=bytearray(raw);b[off:off+4]=n.to_bytes(4,'big');attempt(name,b)
        b=bytearray(raw);b[at+16]=255;b[at+17]=0;attempt('decreasing-xing-toc',b)
        b=bytearray(raw);b[meta+4]=1;attempt('index-side-information-not-empty',b)
        b=bytearray(raw);b[at:at+4]=b'Nope';attempt('missing-index-signature',b)
        count=probe['_index']['frames']+1;b=bytearray(raw);b[at+8:at+12]=count.to_bytes(4,'big');attempt('complete-frame-counter-mismatch',b,lambda v:(v['timeline'].update(duration=str(count*1152*10000000//44100+1)),v['metadata'].update(chapters=[])))
        vm,vr,_,vp=fixture(root/'vbri-source',PROFILES[-4],4);old=model;model=vm;vp_at=frames(vr)[0][0]
        for name,off,n in [('vbri-version',40,2),('vbri-zero-entries',54,0),('vbri-zero-scale',56,0),('vbri-wide-entry',58,5),('vbri-zero-step',60,0)]:
            b=bytearray(vr);b[vp_at+off:vp_at+off+2]=n.to_bytes(2,'big');attempt(name,b)
        b=bytearray(vr);b[vp_at+62:vp_at+66]=(vp['_index']['bytes']-1).to_bytes(4,'big');attempt('vbri-toc-byte-mismatch',b)
        model=old;data=root/'partial';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'partial-registry')])
        try:
            meta_at,meta_n=frames(raw)[0];value=publish(model,raw,data,meta_at+meta_n-1);answer=rpc('open',session=value,directory=str(data));assert not answer['ok'] and answer['code']=='waiting_initialization'
            assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
            value=publish(model,raw,data,meta_at+meta_n,previous=value);assert rpc('open',session=value,directory=str(data))['ok'];assert rpc('index')['tracks'][0]['samples']==0
            first_at,first_n=frames(raw)[1];value=publish(model,raw,data,first_at+first_n-1,previous=value);assert rpc('publish',session=value)['ok'] and rpc('index')['tracks'][0]['samples']==0
            value=publish(model,raw,data,first_at+first_n,previous=value);assert rpc('publish',session=value)['ok'] and rpc('index')['tracks'][0]['samples']==1;rpc('release');checks.append('partial-index-and-first-audio-frame-wait-and-resume')
        finally:rpc.close()
    with tempfile.TemporaryDirectory(prefix='foxicmp-index-counter-overrun-') as tmp:
        root=Path(tmp);model,raw,_,probe=fixture(root/'source',PROFILES[0],4);offset=frames(raw)[0][0]+4+17+8;count=probe['_index']['frames']-10;raw=bytearray(raw);raw[offset:offset+4]=count.to_bytes(4,'big');model['timeline']['duration']=str(count*1152*10000000//44100+1);data=root/'data';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'registry')])
        try:
            value=publish(model,raw,data,prefix_at_seconds(raw,probe,2));assert rpc('open',session=value,directory=str(data))['ok']
            value=publish(model,raw,data,len(raw),previous=value,complete=True);assert not rpc('publish',session=value)['ok'];assert rpc('read',decoder=True,track=0,offset='0',length='1')['bytesRead']==-1;rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('growing-index-overrun-fails-closed-and-releases')
        finally:rpc.close()
    return dict(result='PASS',count=len(checks),checks=checks,projectedPacketsExact=True,decodedPcmEqualToEncodedSource=True,gaplessTrimApplied=False,originalsModified=False)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
