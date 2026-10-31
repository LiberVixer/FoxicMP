"""Shared AV1 Main 8/10-bit fMP4: original bytes/clock and fail-closed framing."""
import argparse
import copy
import json
import tempfile
from pathlib import Path
from progressive_mux_mp4_conformance import run as healthy
from progressive_mux_mp4_fixture import fixture, publish, boxes
from progressive_mux_ts_conformance import Rpc


def run(runner):
    report=healthy(runner,[('av1-8',4),('av1-10',4),('av1-10-fractional',4),('av1-8',80)],False)
    checks=report['checks']
    with tempfile.TemporaryDirectory(prefix='foxicmp-mux-av1-errors-') as tmp:
        root=Path(tmp);model,raw,_,_,_=fixture(root,'av1-8',4)
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
        for codec in ('h264','hevc',''):
            plan=copy.deepcopy(model);plan['tracks'][0]['codecHint']=codec
            reject('reject-av1-actual-with-'+(codec or 'default-avc')+'-hint',plan=plan)
        avc_model,avc_raw,_,_,_=fixture(root/'avc','main-bframes',4);avc_model['tracks'][0]['codecHint']='av1'
        reject('reject-avc-actual-with-av1-hint',avc_raw,avc_model)
        h=raw.index(b'av1C')+4
        for name,offset,value in [('marker',0,b'\x01'),('version',0,b'\x82'),('high-profile',1,b'\x20'),('invalid-level',1,b'\x1f'),('twelve-bit',2,b'\x6c'),('monochrome',2,b'\x1c'),('unsupported-chroma',2,b'\x08'),('reserved-bits',3,b'\x80'),('presentation-delay',3,b'\x10'),('obu-forbidden-bit',4,b'\x8a'),('obu-missing-size',4,b'\x08'),('obu-reserved-bit',4,b'\x0b'),('obu-unsupported-type',4,b'\x32'),('metadata-before-sequence',4,b'\x2a'),('obu-zero-payload',5,b'\x00'),('obu-payload-overrun',5,b'\x7f'),('obu-truncated-size',5,b'\x80'*8),('sequence-profile-mismatch',6,b'\x20')]:
            changed=bytearray(raw);changed[h+offset:h+offset+len(value)]=value;reject('reject-av1-'+name,changed)
        def without_sequence(body):
            out=bytearray()
            for at,n,kind,head in boxes(body):
                data=body[at+head:at+n]
                if kind in (b'moov',b'trak',b'mdia',b'minf',b'stbl'):data=without_sequence(data)
                elif kind==b'stsd':data=data[:8]+without_sequence(data[8:])
                elif kind==b'av01':data=data[:78]+without_sequence(data[78:])
                elif kind==b'av1C':data=data[:4]
                out+=(8+len(data)).to_bytes(4,'big')+kind+data
            return bytes(out)
        reject('reject-av1-missing-sequence-header',without_sequence(raw))
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
            checks.append('metadata-codec-change-keeps-published-av1-index-and-bytes')
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
                checks.append('av1-'+name+'-no-lease-leak')
            finally:rpc.close()
    report['count']=len(checks)
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
