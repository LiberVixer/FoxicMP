"""Standalone Opus must retain the dOps/edit-list validation used by paired tracks."""
import copy,tempfile
from pathlib import Path
from progressive_standalone_fixture import fixture,publish
from progressive_mux_ts_conformance import Rpc

def malformed(runner):
    checks=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-solo-opus-errors-') as temp:
        root=Path(temp);model,raw,_,_=fixture(root,'audio-opus-stereo',4)
        def reject(name,changed=raw,plan=None):
            plan=copy.deepcopy(plan or model);plan['resources'][0]['length']=str(len(changed));data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                answer=rpc('open',directory=str(data),session=publish(plan,changed,data,len(changed),complete=True))
                assert not answer['ok'],(name,answer)
                assert not rpc('readers',version=1,sessionId=plan['sessionId'],generation='1')['readers'],name
                checks.append(name+'-rejected-without-leak')
            finally:rpc.close()
        for codec in ('aac-lc',''):
            plan=copy.deepcopy(model);plan['tracks'][0]['codecHint']=codec;reject('opus-with-'+(codec or 'empty')+'-hint',plan=plan)
        aac,body,_,_=fixture(root/'aac','audio-mono',4);aac['tracks'][0]['codecHint']='opus';reject('aac-with-opus-hint',body,aac)
        h=raw.index(b'dOps')+4
        for name,offset,value in [('version',0,1),('zero-channels',1,0),('multichannel',1,3),('mapping-family',10,1)]:
            changed=bytearray(raw);changed[h+offset]=value;reject(name,changed)
        entry=raw.index(b'Opus')+4;mdhd=raw.rfind(b'mdhd',0,h)+4;elst=raw.rfind(b'elst',0,h)+4
        assert raw[mdhd]==0 and raw[elst]==0 and int.from_bytes(raw[elst+4:elst+8],'big')==1
        for name,at,value in [('entry-channels',entry+16,1<<16|16),('entry-rate',entry+24,44100<<16),('media-clock',mdhd+12,44100),('preskip-edit-mismatch',elst+12,0),('non-unit-edit',elst+16,0x20000)]:
            changed=bytearray(raw);changed[at:at+4]=value.to_bytes(4,'big');reject(name,changed)
    return checks
