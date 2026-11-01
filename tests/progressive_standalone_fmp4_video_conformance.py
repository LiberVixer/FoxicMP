"""Standalone fMP4 video retains strict HEVC/AV1/VP9 configuration validation."""
import copy,tempfile
from pathlib import Path
from progressive_standalone_fixture import fixture,publish
from progressive_mux_ts_conformance import Rpc

def malformed(runner):
    checks=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-solo-fmp4-video-errors-') as temp:
        root=Path(temp)
        for codec,profile,tag in [('hevc','video-fmp4-hevc-8-hvc1',b'hvcC'),('av1','video-fmp4-av1-8',b'av1C'),('vp9','video-fmp4-vp9-8',b'vpcC')]:
            model,raw,_,_=fixture(root/codec,profile,4)
            def reject(label,body=raw,plan=None):
                name=codec+'-'+label;plan=copy.deepcopy(plan or model);plan['resources'][0]['length']=str(len(body));data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
                try:
                    answer=rpc('open',directory=str(data),session=publish(plan,body,data,len(body),complete=True))
                    assert not answer['ok'] and answer.get('code')!='waiting_initialization',(name,answer)
                    assert not rpc('readers',version=1,sessionId=plan['sessionId'],generation='1')['readers'],name
                    checks.append(name+'-rejected-without-leak')
                finally:rpc.close()
            for label,change in [('wrong-hint',lambda v:v['tracks'][0].update(codecHint='h264')),('empty-hint',lambda v:v['tracks'][0].update(codecHint='')),('audio-kind',lambda v:v['tracks'][0].update(kind='audio',codecHint='aac-lc')),('optional',lambda v:v['tracks'][0].update(required=False))]:
                plan=copy.deepcopy(model);change(plan);reject(label,plan=plan)
            h=raw.index(tag)+4;changed=bytearray(raw);changed[h-4:h]=b'free';reject('missing-config',changed)
            if codec=='hevc':
                changes=[('version',0,0),('unsupported-profile',1,3),('chroma-422',16,0xfe),('twelve-bit',17,0xfc),('nal-width',21,0),('array-count',22,0),('incomplete-hvc1-array',23,raw[h+23]&0x7f)]
            elif codec=='av1':changes=[('marker',0,1),('profile',1,0xe0),('twelve-bit',2,0x6c),('reserved',3,0xe0)]
            else:changes=[('version',0,0),('flags',3,1),('profile',4,3),('depth',6,0xc2),('chroma',6,0x86),('init-size',11,1)]
            for label,offset,value in changes:
                changed=bytearray(raw);assert changed[h+offset]!=value;changed[h+offset]=value;reject(label,changed)
    return checks
