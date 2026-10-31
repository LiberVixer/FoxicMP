"""Shared fMP4 Opus matrix: original bytes/clocks/PCM and strict dOps/edit framing."""
import argparse
import copy
import json
import tempfile
from pathlib import Path
from progressive_mux_mp4_conformance import run as healthy
from progressive_mux_mp4_fixture import fixture,publish,boxes
from progressive_mux_ts_conformance import Rpc

PROFILES=('h264-opus','h264-opus-mono-fractional','hevc8-opus','hevc10-opus-mono-fractional','av1-8-opus','av1-10-opus-mono-fractional','vp9-8-opus','vp9-10-opus-mono-fractional')

def run(runner):
    report=healthy(runner,[(p,4) for p in PROFILES]+[('h264-opus',80)],False)
    checks=report['checks']
    with tempfile.TemporaryDirectory(prefix='foxicmp-mux-mp4-opus-errors-') as tmp:
        root=Path(tmp);model,raw,_,_,_=fixture(root,'h264-opus',4)
        def reject(name,changed=raw,plan=None):
            plan=copy.deepcopy(plan or model);plan['resources'][0]['length']=str(len(changed))
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                value=publish(plan,changed,data,len(changed),complete=True);answer=rpc('open',directory=str(data),session=value)
                assert not answer['ok'] and answer['code']=='unsupported_media',(name,answer)
                assert not rpc('readers',version=1,sessionId=plan['sessionId'],generation='1')['readers'],(name,'leaked lease')
                checks.append(name)
            finally:rpc.close()
        for codec in ('aac-lc',''):
            plan=copy.deepcopy(model);plan['tracks'][1]['codecHint']=codec;reject('reject-opus-actual-with-'+(codec or 'default-aac')+'-hint',plan=plan)
        plan=copy.deepcopy(model);plan['tracks'][0]['codecHint']='';reject('reject-opus-without-explicit-video-hint',plan=plan)
        aac_model,aac_raw,_,_,_=fixture(root/'aac','main-delayed',4);aac_model['tracks'][1]['codecHint']='opus'
        reject('reject-aac-actual-with-opus-hint',aac_raw,aac_model)
        h=raw.index(b'dOps')+4
        for name,offset,value in [('version',0,1),('zero-channels',1,0),('multichannel',1,3),('mapping-family',10,1)]:
            changed=bytearray(raw);changed[h+offset]=value;reject('reject-opus-'+name,changed)
        entry=raw.index(b'Opus')+4
        for name,offset,value in [('sample-entry-channels',16,(1<<16|16)),('sample-entry-size',16,(2<<16|24)),('sample-entry-rate',24,44100<<16)]:
            changed=bytearray(raw);changed[entry+offset:entry+offset+4]=value.to_bytes(4,'big');reject('reject-opus-'+name,changed)
        mdhd=raw.rfind(b'mdhd',0,h)+4
        assert raw[mdhd]==0
        changed=bytearray(raw);changed[mdhd+12:mdhd+16]=(44100).to_bytes(4,'big');reject('reject-opus-non-48khz-clock',changed)
        elst=raw.rfind(b'elst',0,h)+4
        assert raw[elst]==0 and int.from_bytes(raw[elst+4:elst+8],'big')==1
        for name,offset,value in [('skip-edit-mismatch',12,0),('non-unit-edit-rate',16,0x20000)]:
            changed=bytearray(raw);changed[elst+offset:elst+offset+4]=value.to_bytes(4,'big');reject('reject-opus-'+name,changed)
        def rewrite(body,which,payload):
            out=bytearray()
            for at,n,kind,head in boxes(body):
                data=body[at+head:at+n]
                if kind in (b'moov',b'trak',b'mdia',b'minf',b'stbl',b'edts'):data=rewrite(data,which,payload)
                elif kind==b'stsd':data=data[:8]+rewrite(data[8:],which,payload)
                elif kind==b'Opus':data=data[:28]+rewrite(data[28:],which,payload)
                elif kind==which:
                    if payload is None:continue
                    data=payload(data)
                out+=(8+len(data)).to_bytes(4,'big')+kind+data
            return bytes(out)
        reject('reject-opus-truncated-dops',rewrite(raw,b'dOps',lambda b:b[:10]))
        reject('reject-opus-extra-dops-bytes',rewrite(raw,b'dOps',lambda b:b+b'\0'))
        reject('reject-opus-missing-edit',rewrite(raw,b'elst',None))
        # Informational original input rate and output gain remain unmodified.
        # Matching non-default pre-skip/edit offsets must not be normalized.
        for name,skip,gain,rate in [('zero-skip',0,0,48000),('custom-skip-and-gain',480,256,24000)]:
            changed=bytearray(raw);changed[h+2:h+4]=skip.to_bytes(2,'big');changed[h+4:h+8]=rate.to_bytes(4,'big');changed[h+8:h+10]=gain.to_bytes(2,'big');changed[elst+12:elst+16]=skip.to_bytes(4,'big')
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                value=publish(model,changed,data,len(changed),complete=True);answer=rpc('open',directory=str(data),session=value);assert answer['ok'],answer
                header=bytes.fromhex(rpc('read',track=1,offset='0',length='65536')['hex']);offset=header.index(b'dOps')+4
                assert header[offset:offset+11]==changed[h:h+11]
                rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('preserve-opus-'+name)
            finally:rpc.close()
        data=root/'hint-update';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'hint-update-registry')])
        try:
            stops=[a for a,n,k,head in boxes(raw) if k==b'moof'];value=publish(model,raw,data,stops[2]-1);assert rpc('open',directory=str(data),session=value)['ok']
            before=rpc('index');first=rpc('read',track=1,offset='0',length='128')
            bad=copy.deepcopy(value);bad['revision']=str(int(value['revision'])+1);bad['tracks'][1]['codecHint']='aac-lc'
            assert not rpc('publish',session=bad)['ok'];assert rpc('index')==before;assert rpc('read',track=1,offset='0',length='128')==first
            rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('metadata-audio-codec-change-keeps-opus-index-and-bytes')
        finally:rpc.close()
        for name in ('partial-init','cancel','sha-rewrite'):
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                value=publish(model,raw,data,17 if name=='partial-init' else len(raw));answer=rpc('open',directory=str(data),session=value)
                if name=='partial-init':assert not answer['ok'] and answer['code']=='waiting_initialization',answer
                else:
                    assert answer['ok'],answer
                    if name=='cancel':assert rpc('cancel')['ok']
                    else:
                        mdat=next(a+head for a,n,k,head in boxes(raw) if k==b'mdat')
                        with (data/'mux.part').open('r+b') as f:f.seek(mdat+20);f.write(b'changed')
                    for k in (0,1):assert rpc('read',track=k,offset='0',length='65536')['bytesRead']==-1
                    rpc('release')
                assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('opus-'+name+'-no-lease-leak')
            finally:rpc.close()
    report.update(count=len(checks),videoAudioPairs=['h264/opus','hevc/opus','av1/opus','vp9/opus'],channels=[1,2],sampleRate=48000)
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
