"""One-track admission, byte identity, role-aware indexes and reader lifecycle."""
import argparse,copy,json,tempfile
from pathlib import Path
from progressive_standalone_fixture import fixture,publish,fragment_prefix,BASE_PROFILES,OPUS_PROFILES,WEBM_PROFILES,WEBM_VIDEO_PROFILES
from progressive_mux_ts_conformance import Rpc

def run(runner,opus_only=False,webm_only=False,webm_video_only=False):
    checks=[]
    for profile in (WEBM_VIDEO_PROFILES if webm_video_only else WEBM_PROFILES if webm_only else OPUS_PROFILES if opus_only else BASE_PROFILES):
        with tempfile.TemporaryDirectory(prefix='foxicmp-standalone-native-') as temp:
            root=Path(temp);model,raw,original,probe=fixture(root,profile,4);rpc=Rpc([runner,'--registry',str(root/'readers')]);data=root/'data';data.mkdir()
            try:
                value=publish(model,raw,data,fragment_prefix(raw,2));r=rpc('open',session=value,directory=str(data));assert r['ok'],r
                assert len(rpc('layout')['tracks'])==1;index=rpc('index');assert index['tracks'][0]['samples']>0,index
                status=rpc('status');kind=model['tracks'][0]['kind'];assert status['mediaMode']=='standalone-'+model['tracks'][0]['containerHint']+'-'+model['tracks'][0]['codecHint'] and len(status['mediaTracks'])==1,status
                assert status['mediaTracks'][0]['kind']==kind and status['physicalResources']==1
                assert rpc('read',decoder=True,track=0,offset=str(int(value['resources'][0]['ranges'][0]['length'])),length='1')['bytesRead']==-1
                value=publish(model,raw,data,len(raw),previous=value,complete=True);assert rpc('publish',session=value)['ok']
                assert rpc('index')['tracks'][0]['samples']==len(probe['packets'])
                if webm_video_only:
                    from fractions import Fraction
                    from progressive_webm_fixtures import locate
                    _,b,e=locate(raw,0x23e383);duration=int.from_bytes(raw[b:e],'big')//100
                    times=rpc('index',sampleTimes=True)['tracks'][0]['firstSamples']
                    for sample,packet in zip(times,probe['packets']):
                        start=int(Fraction(packet['pts_time'])*10000000)
                        assert int(sample['start'])==start and int(sample['end'])==start+duration,(profile,sample,packet)
                    assert len(times)==8;checks.append(profile+'-packet-clock-and-fractional-duration')

                chunks=[]
                for at in range(0,len(raw),65536):
                    answer=rpc('read',decoder=True,track=0,offset=str(at),length=str(min(65536,len(raw)-at)));assert answer['bytesRead']>0,answer;chunks.append(bytes.fromhex(answer['hex']))
                assert b''.join(chunks)==raw
                assert rpc('read',decoder=True,track=0,offset=str(len(raw)),length='1')['bytesRead']==0
                readers=lambda:rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
                assert len(readers())==1;rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
                checks += [profile+'-one-track-one-resource',profile+'-kind-aware-index',profile+'-exact-decoder-bytes-and-confirmed-eof',profile+'-last-owner-release']
            finally:rpc.close()
    with tempfile.TemporaryDirectory(prefix='foxicmp-standalone-errors-') as temp:
        root=Path(temp);model,raw,_,_=fixture(root,seconds=4)
        changes=[('optional',lambda v:v['tracks'][0].update(required=False)),('wrong-codec',lambda v:v['tracks'][0].update(codecHint='flac')),('wrong-kind',lambda v:v['tracks'][0].update(kind='video',codecHint='h264')),('missing-integrity',lambda v:v['resources'][0].pop('integrity')),('noncontiguous',lambda v:v['resources'][0].update(ranges=[dict(offset='1',length=str(len(raw)-1))])),('nonzero-offset',lambda v:v['timeline'].update(timestampOffset='1')),('webm-not-admitted',lambda v:v['tracks'][0].update(containerHint='webm'))]
        for name,change in changes:
            data=root/name;data.mkdir();value=publish(model,raw,data,len(raw),complete=True);change(value);rpc=Rpc([runner,'--registry',str(root/(name+'-readers'))])
            try:
                answer=rpc('open',directory=str(data),session=value);assert not answer['ok'],(name,answer)
                assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name+'-rejected-without-leak')
            finally:rpc.close()
    if webm_video_only:
        from progressive_standalone_webm_video_conformance import malformed
        checks+=malformed(runner)
    if webm_only:
        from progressive_standalone_webm_conformance import malformed
        checks+=malformed(runner)
    if opus_only:
        from progressive_standalone_opus_conformance import malformed
        checks+=malformed(runner)
    return dict(result='PASS',count=len(checks),checks=checks,physicalTrackCount=1,syntheticTracks=False,decoderBytesExact=True)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);p.add_argument('--opus-only',action='store_true');p.add_argument('--webm-only',action='store_true');p.add_argument('--webm-video-only',action='store_true');a=p.parse_args();print(json.dumps(run(a.runner,a.opus_only,a.webm_only,a.webm_video_only)))
