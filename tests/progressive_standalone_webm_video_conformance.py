"""Standalone video must keep strict WebM headers, keyframes and block framing."""
import copy,tempfile
from pathlib import Path
from progressive_standalone_fixture import fixture,publish
from progressive_webm_fixtures import locate,width
from progressive_mux_ts_conformance import Rpc

def malformed(runner):
    checks=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-solo-webm-video-errors-') as temp:
        root=Path(temp)
        for codec in ('vp8','vp9','av1'):
            model,raw,_,_=fixture(root/codec,'video-webm-'+codec+('' if codec=='vp8' else '-8'),4)
            def reject(label,body=raw,plan=None,index=False):
                name=codec+'-'+label;plan=copy.deepcopy(plan or model);plan['resources'][0]['length']=str(len(body));data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
                try:
                    answer=rpc('open',directory=str(data),session=publish(plan,body,data,len(body),complete=True))
                    if index:
                        assert answer['ok'],answer;answer=rpc('index');rpc('release')
                    assert not answer.get('ok',True) and answer.get('code')!='waiting_initialization',(name,answer)
                    assert not rpc('readers',version=1,sessionId=plan['sessionId'],generation='1')['readers'],name
                    checks.append(name+'-rejected-and-released')
                finally:rpc.close()
            for label,change in [('wrong-hint',lambda v:v['tracks'][0].update(codecHint='vp9' if codec!='vp9' else 'av1')),('empty-hint',lambda v:v['tracks'][0].update(codecHint='')),('audio-kind',lambda v:v['tracks'][0].update(kind='audio',codecHint='opus')),('optional',lambda v:v['tracks'][0].update(required=False))]:
                plan=copy.deepcopy(model);change(plan);reject(label,plan=plan)
            for tag,label in [(0x23e383,'zero-default-duration'),(0xb0,'zero-width'),(0xba,'zero-height')]:
                _,b,e=locate(raw,tag);changed=bytearray(raw);changed[b:e]=bytes(e-b);reject(label,changed)
            _,b,e=locate(raw,0xa3);flag=b+width(raw[b])+2
            for name,value in [('initial-non-keyframe',raw[flag]&~0x80),('laced-block',raw[flag]|2)]:
                changed=bytearray(raw);assert changed[flag]!=value;changed[flag]=value;reject(name,changed,index=True)
            if codec=='av1':
                _,b,e=locate(raw,0x63a2);changed=bytearray(raw);changed[b]=0;reject('bad-av1-configuration',changed)
    return checks
