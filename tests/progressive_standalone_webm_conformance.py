"""One-stream WebM admission keeps real Opus/Vorbis headers strict."""
import copy,tempfile
from pathlib import Path
from progressive_standalone_fixture import fixture,publish
from progressive_webm_fixtures import locate
from progressive_mux_ts_conformance import Rpc

def malformed(runner):
    checks=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-solo-webm-errors-') as temp:
        root=Path(temp)
        for codec in ('opus','vorbis'):
            model,raw,_,_=fixture(root/codec,'audio-webm-'+codec+'-stereo',4)
            def reject(label,body=raw,plan=None):
                name=codec+'-'+label;plan=copy.deepcopy(plan or model);plan['resources'][0]['length']=str(len(body));data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
                try:
                    answer=rpc('open',directory=str(data),session=publish(plan,body,data,len(body),complete=True))
                    assert not answer['ok'] and answer.get('code')!='waiting_initialization',(name,answer)
                    assert not rpc('readers',version=1,sessionId=plan['sessionId'],generation='1')['readers'],name
                    checks.append(name+'-rejected-without-leak')
                finally:rpc.close()
            for label,change in [('wrong-hint',lambda v:v['tracks'][0].update(codecHint='vorbis' if codec=='opus' else 'opus')),('empty-hint',lambda v:v['tracks'][0].update(codecHint='')),('video-kind',lambda v:v['tracks'][0].update(kind='video',codecHint='vp9')),('optional',lambda v:v['tracks'][0].update(required=False))]:
                plan=copy.deepcopy(model);change(plan);reject(label,plan=plan)
            _,b,e=locate(raw,0x63a2)
            if codec=='opus':
                changes=[('signature',b,0),('version',b+8,2),('channels',b+9,3),('mapping-family',b+18,1),('preskip-mismatch',b+10,0)]
                for tag,label in [(0x56aa,'delay'),(0x56bb,'preroll')]:
                    _,start,end=locate(raw,tag);changed=bytearray(raw);changed[start:end]=bytes(end-start);reject(label,changed)
            else:
                ident=raw.index(b'\x01vorbis',b,e);setup=raw.index(b'\x05vorbis',b,e)
                changes=[('count',b,1),('signature',ident+1,0),('version',ident+7,1),('channels',ident+11,6),('rate',ident+12,0),('block-size',ident+28,0),('framing',ident+29,0),('comment-framing',setup-1,0),('setup-sync',setup+8,0)]
            for name,at,value in changes:
                changed=bytearray(raw);assert changed[at]!=value;changed[at]=value;reject(name,changed)
    return checks
