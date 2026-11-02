"""Audible timeline, explicit backend admission, confirmed end and strict metadata."""
import argparse,copy,hashlib,json,subprocess,tempfile
from pathlib import Path
from progressive_mp3_gapless_fixture import fixture,frames,PROFILES
from progressive_mp3_indexed_fixture import fixture as indexed_fixture,PROFILES as INDEXED
from progressive_mp3_tag_fixture import metadata
from progressive_standalone_fixture import publish
from progressive_mux_ts_conformance import Rpc

def run(runner):
    checks=[];profiles=[]
    for profile in PROFILES:
        with tempfile.TemporaryDirectory(prefix='foxicmp-gapless-native-') as temp:
            root=Path(temp);model,raw,original,probe=fixture(root,profile,4);data=root/'data';data.mkdir()
            rpc=Rpc([runner,'--registry',str(root/'registry'),'--mp3-gapless'])
            default=Rpc([runner,'--registry',str(root/'default-registry')])
            try:
                value=publish(model,raw,data,frames(raw)[1][0])
                denied=default('preflight',session=value);assert not denied['ok'] and denied['code']=='unsupported_media'
                denied=default('open',session=value,directory=str(data));assert not denied['ok'] and denied['code']=='unsupported_media'
                assert not default('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
                checks.append(profile+'-unverified-backend-denied-before-reader-open')
                assert rpc('open',session=value,directory=str(data))['ok']
                layout=rpc('layout');index=rpc('index');assert int(layout['duration100ns'])==40000000 and int(layout['timestampOffset100ns'])==probe['_gapless']['begin']*10000000//int(profile.split('-')[-1]) and index['tracks'][0]['samples']==0
                checks.append(profile+'-header-only-audible-duration-and-offset')
                value=publish(model,raw,data,len(raw),previous=value);assert rpc('publish',session=value)['ok']
                size=int(rpc('layout')['tracks'][0]['available']);assert rpc('read',decoder=True,track=0,offset=str(size),length='1')['bytesRead']==-1
                index=rpc('index',sampleTimes=True);assert int(index['tracks'][0]['ready'])<40000000 and int(index['tracks'][0]['firstSamples'][0]['start'])<0
                reader=rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'][0]['readerId']
                checks.append(profile+'-full-prefix-withholds-audible-tail-until-writer-complete')
                value=publish(model,raw,data,len(raw),previous=value,complete=True);assert rpc('publish',session=value)['ok']
                complete=rpc('layout');total=int(complete['tracks'][0]['available']);assert total>size
                index=rpc('index');assert int(index['tracks'][0]['ready'])==40000000
                assert rpc('read',decoder=True,track=0,offset=str(total),length='1')['bytesRead']==0
                assert rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'][0]['readerId']==reader
                status=rpc('status');assert status['mediaMode']=='standalone-mp3-gapless' and status['mp3Index']['timeline']=='audible-samples' and status['mp3Index']['outputTrimRequired'] and not status['mp3Index']['gaplessTrimApplied']
                checks.append(profile+'-complete-releases-tail-same-reader-confirmed-eof')
                output=bytearray()
                for at in range(0,total,65536):output+=bytes.fromhex(rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,total-at)))['hex'])
                projected=root/'projection.mp4';projected.write_bytes(output)
                pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(projected),'-c:a','pcm_s16le','-f','s16le','-'])
                channels=2 if '-stereo-' in profile else 1;begin,end=probe['_gapless']['begin'],probe['_gapless']['end'];trimmed=pcm[begin*channels*2:end*channels*2]
                assert hashlib.sha256(trimmed).hexdigest()==probe['_pcm_sha256'] and len(trimmed)==probe['_pcm_bytes']
                checks.append(profile+'-projection-unmodified-payload-exact-audible-pcm')
                rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];assert original.read_bytes()==raw
                checks.append(profile+'-reader-release-original-unchanged')
                profiles.append(dict(profile=profile,index=status['mp3Index'],duration100ns=complete['duration100ns'],timestampOffset100ns=complete['timestampOffset100ns'],nativePcmSha256=probe['_pcm_sha256'],fixtureSha256=hashlib.sha256(raw).hexdigest()))
            finally:rpc.close();default.close()
    with tempfile.TemporaryDirectory(prefix='foxicmp-gapless-errors-') as temp:
        root=Path(temp);model,raw,_,_=fixture(root/'source',PROFILES[0],4)
        def rejected(name,value,body):
            data=root/(name+'-published');data.mkdir();v=publish(value,body,data,len(body),complete=True);rpc=Rpc([runner,'--registry',str(root/(name+'-registry')),'--mp3-gapless'])
            try:
                r=rpc('open',session=v,directory=str(data));assert not r['ok'],(name,r)
                assert not rpc('readers',version=1,sessionId=value['sessionId'],generation='1')['readers'];checks.append(name+'-rejected-without-reader-leak')
            finally:rpc.close()
        header,cursor=metadata(raw);b=bytearray(raw);b[cursor+34]^=1;rejected('bad-tag-crc',copy.deepcopy(model),b)
        b=bytearray(raw);b[cursor:cursor+9]=b'Other1.00';rejected('unknown-encoder',copy.deepcopy(model),b)
        v=copy.deepcopy(model);v['timeline']['duration']=str((len(frames(raw))-1)*1152*10000000//44100);rejected('coded-duration-in-audible-profile',v,raw)
        for name,mutate in [('nonzero-offset',lambda v:v['timeline'].update(timestampOffset='1')),('optional-track',lambda v:v['tracks'][0].update(required=False))]:
            v=copy.deepcopy(model);mutate(v);rejected(name,v,raw)
        for profile in (INDEXED[0],INDEXED[8]):
            v,b,_,_=indexed_fixture(root/profile,profile,4);v['tracks'][0]['containerHint']='mp3-gapless';v['timeline']['duration']='40000000';rejected(profile,v,b)
    return dict(result='PASS',count=len(checks),checks=checks,profiles=profiles,decoderProjectionOnly=True,classicBackendDenied=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
