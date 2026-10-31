"""Shared VP9 Profile 0/2 8/10-bit fMP4: original bytes/clock and fail-closed framing."""
import argparse
import copy
import json
import tempfile
from pathlib import Path
from progressive_mux_mp4_conformance import run as healthy
from progressive_mux_mp4_fixture import fixture, publish, boxes
from progressive_mux_ts_conformance import Rpc


def run(runner):
    report=healthy(runner,[('vp9-8',4),('vp9-10',4),('vp9-10-fractional',4),('vp9-8',80)],False)
    checks=report['checks']
    with tempfile.TemporaryDirectory(prefix='foxicmp-mux-vp9-errors-') as tmp:
        root=Path(tmp);model,raw,_,_,_=fixture(root,'vp9-8',4)
        def reject(name,changed=raw,plan=None):
            plan=copy.deepcopy(plan or model);plan['resources'][0]['length']=str(len(changed))
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                value=publish(plan,changed,data,len(changed),complete=True)
                answer=rpc('open',directory=str(data),session=value)
                assert not answer['ok'] and answer['code']=='unsupported_media',(name,answer)
                assert not rpc('readers',version=1,sessionId=plan['sessionId'],generation='1')['readers'],(name,'leaked lease')
                checks.append(name)
            finally:rpc.close()
        for codec in ('h264','hevc','av1',''):
            plan=copy.deepcopy(model);plan['tracks'][0]['codecHint']=codec
            reject('reject-vp9-actual-with-'+(codec or 'default-avc')+'-hint',plan=plan)
        avc_model,avc_raw,_,_,_=fixture(root/'avc','main-bframes',4);avc_model['tracks'][0]['codecHint']='vp9'
        reject('reject-avc-actual-with-vp9-hint',avc_raw,avc_model)
        h=raw.index(b'vpcC')+4
        for name,offset,value in [('version',0,0),('flags',3,1),('profile-1',4,1),('profile-3',4,3),('invalid-level',5,255),('profile-depth-mismatch',6,0xa2),('twelve-bit',6,0xc2),('chroma-422',6,0x84),('chroma-444',6,0x86),('reserved-chroma',6,0x8e),('reserved-primaries',7,3),('reserved-transfer',8,3),('reserved-matrix',9,3),('rgb-matrix-with-420',9,0),('nonzero-initialization-size',11,1)]:
            changed=bytearray(raw);changed[h+offset]=value;reject('reject-vp9-'+name,changed)
        changed=bytearray(raw);changed[h-4:h]=b'free';reject('reject-vp9-missing-configuration',changed)
        def truncated_configuration(body):
            out=bytearray()
            for at,n,kind,head in boxes(body):
                data=body[at+head:at+n]
                if kind in (b'moov',b'trak',b'mdia',b'minf',b'stbl'):data=truncated_configuration(data)
                elif kind==b'stsd':data=data[:8]+truncated_configuration(data[8:])
                elif kind==b'vp09':data=data[:78]+truncated_configuration(data[78:])
                elif kind==b'vpcC':data=data[:11]
                out+=(8+len(data)).to_bytes(4,'big')+kind+data
            return bytes(out)
        reject('reject-vp9-truncated-configuration',truncated_configuration(raw))
        # Metadata rejection preserves published media; accepted-source errors do not.
        data=root/'hint-update';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'hint-update-registry')])
        try:
            stops=[a for a,n,k,head in boxes(raw) if k==b'moof']
            value=publish(model,raw,data,stops[2]-1);assert rpc('open',directory=str(data),session=value)['ok']
            before=rpc('index');first=rpc('read',track=0,offset='0',length='128')
            bad=copy.deepcopy(value);bad['revision']=str(int(value['revision'])+1);bad['tracks'][0]['codecHint']='h264'
            assert not rpc('publish',session=bad)['ok'];assert rpc('index')==before
            assert rpc('read',track=0,offset='0',length='128')==first
            rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
            checks.append('metadata-codec-change-keeps-published-vp9-index-and-bytes')
        finally:rpc.close()
        for name in ('partial-init','cancel','sha-rewrite'):
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                value=publish(model,raw,data,17 if name=='partial-init' else len(raw))
                answer=rpc('open',directory=str(data),session=value)
                if name=='partial-init':assert not answer['ok'] and answer['code']=='waiting_initialization',answer
                else:
                    assert answer['ok'],answer
                    if name=='cancel':assert rpc('cancel')['ok']
                    else:
                        mdat=next(a+head for a,n,k,head in boxes(raw) if k==b'mdat')
                        with (data/'mux.part').open('r+b') as f:f.seek(mdat+20);f.write(b'changed')
                    for k in (0,1):assert rpc('read',track=k,offset='0',length='65536')['bytesRead']==-1
                    rpc('release')
                assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
                checks.append('vp9-'+name+'-no-lease-leak')
            finally:rpc.close()
    report['count']=len(checks)
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
