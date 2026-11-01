"""FLAC fMP4 rejects incompatible config before decoder creation and leaks no readers."""
import copy,tempfile
from pathlib import Path
from progressive_standalone_fixture import fixture,publish
from progressive_mux_ts_conformance import Rpc
from progressive_mux_mp4_fixture import boxes

def malformed(runner):
    checks=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-solo-flac-errors-') as tmp:
        root=Path(tmp);model,raw,_,_=fixture(root/'source','audio-flac-stereo-24-48000',4)
        def reject(name,body=raw,plan=None):
            plan=copy.deepcopy(plan or model);plan['resources'][0]['length']=str(len(body));data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                result=rpc('open',directory=str(data),session=publish(plan,body,data,len(body),complete=True));assert not result['ok'] and result.get('code')!='waiting_initialization',(name,result)
                assert not rpc('readers',version=1,sessionId=plan['sessionId'],generation='1')['readers'],name;checks.append(name+'-rejected-without-leak')
            finally:rpc.close()
        for codec in ('aac-lc','opus',''):
            plan=copy.deepcopy(model);plan['tracks'][0]['codecHint']=codec;reject('hint-'+(codec or 'empty'),plan=plan)
        aac,body,_,_=fixture(root/'aac','audio-mono',4);aac['tracks'][0]['codecHint']='flac';reject('aac-actual-with-flac-hint',body,aac)
        h=raw.index(b'dfLa')+4;info=h+8
        for name,at,value in [('version',h,1),('flags',h+3,1),('not-last-block',h+4,0),('wrong-block-type',h+4,0x81),('wrong-length',h+7,33),('minimum-too-small',info,0),('maximum-zero',info+2,0),('sample-entry-channels',raw.index(b'fLaC')+4+17,1),('sample-entry-bits',raw.index(b'fLaC')+4+19,16)]:
            changed=bytearray(raw)
            if name=='minimum-too-small':changed[at:at+2]=b'\0\x01'
            elif name=='maximum-zero':changed[at:at+2]=b'\0\0'
            else:assert changed[at]!=value;changed[at]=value
            reject(name,changed)
        for name,offset,width,value in [('bad-rate',info+10,8,(96000<<44)|(1<<41)|(23<<36)),('bad-channels',info+10,8,(48000<<44)|(2<<41)|(23<<36)),('bad-depth',info+10,8,(48000<<44)|(1<<41)|(31<<36)),('frame-bounds',info+4,6,(1000<<24)|1)]:
            changed=bytearray(raw);changed[offset:offset+width]=value.to_bytes(width,'big');reject(name,changed)
        mdhd=raw.index(b'mdhd')+4;assert raw[mdhd]==0
        changed=bytearray(raw);changed[mdhd+12:mdhd+16]=(44100).to_bytes(4,'big');reject('clock-mismatch',changed)
        changed=bytearray(raw);changed[h-4:h]=b'free';reject('missing-dfla',changed)
        def rewrite(body,transform,edit=False,duplicate=False,leading_free=False):
            out=bytearray()
            for at,n,kind,head in boxes(body):
                data=body[at+head:at+n]
                if kind in (b'moov',b'trak',b'mdia',b'minf',b'stbl'):data=rewrite(data,transform,edit,duplicate,leading_free)
                elif kind==b'stsd':data=data[:8]+rewrite(data[8:],transform,edit,duplicate,leading_free)
                elif kind==b'fLaC':data=data[:28]+(b'\0\0\0\x08free' if leading_free else b'')+rewrite(data[28:],transform,edit,duplicate,False)
                elif kind==b'dfLa':data=transform(data)
                if kind==b'trak' and edit:
                    payload=bytes(4)+(1).to_bytes(4,'big')+(4000).to_bytes(4,'big')+bytes(4)+bytes.fromhex('00010000')
                    data+=(16+len(payload)).to_bytes(4,'big')+b'edts'+(8+len(payload)).to_bytes(4,'big')+b'elst'+payload
                out+=(8+len(data)).to_bytes(4,'big')+kind+data
                if kind==b'dfLa' and duplicate:out+=(8+len(data)).to_bytes(4,'big')+kind+data
            return bytes(out)
        reject('truncated-streaminfo',rewrite(raw,lambda b:b[:-1]));reject('extra-metadata-bytes',rewrite(raw,lambda b:b+b'\0'))
        reject('edit-list',rewrite(raw,lambda b:b,edit=True));reject('duplicate-dfla',rewrite(raw,lambda b:b,duplicate=True));reject('dfla-not-first',rewrite(raw,lambda b:b,leading_free=True))
    return checks
