"""Shared HEVC Main/Main10 fMP4: byte/clock fidelity and strict admission."""
import argparse
import copy
import json
import tempfile
from pathlib import Path
from progressive_mux_mp4_conformance import run as healthy
from progressive_mux_mp4_fixture import fixture, publish, boxes
from progressive_mux_ts_conformance import Rpc


def run(runner):
    report=healthy(runner,[('hevc-main',4),('hevc10-fractional',4),('hevc-delayed',4),('hevc-main',80)],False)
    checks=report['checks']
    with tempfile.TemporaryDirectory(prefix='foxicmp-mux-hevc-errors-') as tmp:
        root=Path(tmp);model,raw,_,_,_=fixture(root,'hevc-main',4)
        def reject(name,changed=raw,plan=None):
            plan=copy.deepcopy(plan or model);data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                value=publish(plan,changed,data,len(changed),complete=True)
                answer=rpc('open',directory=str(data),session=value)
                assert not answer['ok'],(name,answer)
                assert not rpc('readers',version=1,sessionId=plan['sessionId'],generation='1')['readers'],(name,'leaked lease')
                checks.append(name)
            finally:rpc.close()
        for codec in ('h264',''):
            plan=copy.deepcopy(model);plan['tracks'][0]['codecHint']=codec
            reject('reject-hevc-actual-with-'+('avc' if codec else 'default-avc')+'-hint',plan=plan)
        avc_model,avc_raw,_,_,_=fixture(root/'avc','main-bframes',4);avc_model['tracks'][0]['codecHint']='hevc'
        reject('reject-avc-actual-with-hevc-hint',avc_raw,avc_model)
        def dependencies(body, payload):
            out=bytearray()
            for at,n,kind,head in boxes(body):
                data=body[at+head:at+n]
                if kind in (b'moov',b'trak',b'mdia',b'minf'):
                    data=dependencies(data,payload)
                elif kind==b'stbl':
                    data=b''.join(body2[a:a+size] for body2 in [data] for a,size,k,h in boxes(body2) if k!=b'sdtp')
                    data+=(8+len(payload)).to_bytes(4,'big')+b'sdtp'+payload
                out+=(8+len(data)).to_bytes(4,'big')+kind+data
            return bytes(out)
        reject('reject-nonempty-sample-dependency-table',dependencies(raw,b'\0'*5))
        reject('reject-invalid-sample-dependency-flags',dependencies(raw,b'\0\0\0\1'))
        # FFmpeg versions differ: a zero-entry sdtp carries no sample semantics.
        data=root/'empty-sdtp';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'empty-sdtp-registry')])
        try:
            changed=dependencies(raw,b'\0'*4);value=publish(model,changed,data,len(changed),complete=True)
            answer=rpc('open',directory=str(data),session=value);assert answer['ok'],answer
            assert all(t['samples']>0 for t in rpc('index')['tracks'])
            rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
            checks.append('accept-empty-sample-dependency-table')
        finally:rpc.close()
        h=raw.index(b'hvcC')+4
        for name,offset,value in [('configuration-version',0,0),('unsupported-profile',1,3),('non-four-byte-nal-length',21,0),('unsupported-chroma',16,0xfc),('unsupported-bit-depth',17,0xf9),('unsupported-array-type',23,0x80|31),('hvc1-incomplete-parameter-set-array',23,32)]:
            changed=bytearray(raw);changed[h+offset]=value;reject('reject-'+name,changed)
        # A rejected metadata hint must not invalidate the published index/bytes.
        data=root/'hint-update';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'hint-update-registry')])
        try:
            stops=[a for a,n,k,head in boxes(raw) if k==b'moof']
            value=publish(model,raw,data,stops[2]-1);assert rpc('open',directory=str(data),session=value)['ok']
            before=rpc('index');first=rpc('read',track=0,offset='0',length='128')
            bad=copy.deepcopy(value);bad['revision']=str(int(value['revision'])+1);bad['tracks'][0]['codecHint']='h264'
            assert not rpc('publish',session=bad)['ok'];assert rpc('index')==before
            assert rpc('read',track=0,offset='0',length='128')==first
            rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
            checks.append('metadata-codec-change-keeps-published-hevc-index-and-bytes')
        finally:rpc.close()
    report['count']=len(checks)
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
