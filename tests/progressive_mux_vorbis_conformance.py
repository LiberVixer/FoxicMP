"""Shared Vorbis WebM: stateful audio, original bytes, safe admission and release."""
import argparse,copy,json,tempfile
from pathlib import Path
from progressive_mux_webm_conformance import run as media_run
from progressive_mux_webm_fixture import fixture,publish,top_elements
from progressive_webm_fixtures import locate,width
from progressive_webm_segment_playback import children
from progressive_mux_ts_conformance import Rpc

CASES=[('vp8-vorbis',4),('vp8-fractional-vorbis-mono',4),('vp9-8-vorbis',4),('vp9-10-vorbis-mono',4),('av1-8-vorbis',4),('av1-10-vorbis-mono',4),('vp9-known-vorbis',4),('vp9-8-vorbis',80)]
def run(runner):
    result=media_run(runner,CASES,legacy_faults=False);checks=result['checks']
    with tempfile.TemporaryDirectory(prefix='foxicmp-mux-vorbis-errors-') as tmp:
        root=Path(tmp);model,raw,_,_,_=fixture(root,'vp8-vorbis',4)
        def rejected(name,content,plan):
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                value=publish(plan,content,data,len(content),complete=True);answer=rpc('open',directory=str(data),session=value)
                assert not answer['ok'] and answer.get('code')!='waiting_initialization',(name,answer)
                assert not rpc('readers',version=1,sessionId=plan['sessionId'],generation='1')['readers'];checks.append(name)
            finally:rpc.close()
        for field in ('video','audio'):
            bad=copy.deepcopy(model);bad['tracks'][0 if field=='video' else 1].pop('codecHint')
            rejected('vorbis-requires-explicit-'+field+'-hint',raw,bad)
        bad=copy.deepcopy(model);bad['tracks'][1]['codecHint']='opus';rejected('vorbis-actual-codec-rejects-opus-hint',raw,bad)
        opus,opusraw,_,_,_=fixture(root/'opus','vp8',4);opus['tracks'][1]['codecHint']='vorbis';rejected('opus-actual-codec-rejects-vorbis-hint',opusraw,opus)
        _,b,e=locate(raw,0x63a2);ident=raw.index(b'\x01vorbis',b,e);setup=raw.index(b'\x05vorbis',b,e)
        for name,at,value in [('count',b,1),('id-signature',ident+1,0),('id-version',ident+7,1),('channels',ident+11,6),('rate',ident+12,0),('block',ident+28,0),('id-framing',ident+29,0),('comment-framing',setup-1,0),('setup-sync',setup+8,0)]:
            changed=bytearray(raw);assert changed[at]!=value;changed[at]=value;rejected('vorbis-invalid-'+name,changed,model)
        packets=[]
        for tag,ca,cb,ce in top_elements(raw):
            if tag!=0x1f43b675:continue
            for t,ga,gb,ge in children(raw,cb,ce):
                if t==0xa3:bb,be=gb,ge
                elif t==0xa0:bb,be=next((x,y) for f,_,x,y in children(raw,gb,ge) if f==0xa1)
                else:continue
                if raw[bb]==0x82:packets.append((ca,cb,ce,ga,gb,ge,bb,be))
        assert packets
        at=packets[0][6]+width(raw[packets[0][6]])+3;changed=bytearray(raw);changed[at]|=1;rejected('vorbis-header-in-audio-packet',changed,model)
        def encoded(tag,body):
            for n in range(1,9):
                if len(body)<(1<<(7*n))-1:break
            return tag.to_bytes((tag.bit_length()+7)//8,'big')+((1<<(7*n))|len(body)).to_bytes(n,'big')+body
        def delay_variant(delay,preroll=0):
            _,ta,tb,te=next(row for row in top_elements(raw) if row[0]==0x1654ae6b)
            entries=[]
            for tag,a,b,e in children(raw,tb,te):
                if tag==0xbf:continue
                fields=list(children(raw,b,e)) if tag==0xae else []
                audio=any(t==0x83 and int.from_bytes(raw[x:y],'big')==2 for t,_,x,y in fields)
                if not audio:entries.append(raw[a:e]);continue
                body=b''.join(raw[x:y] for t,x,_,y in fields if t not in (0x56aa,0x56bb,0xbf))
                for key,value in [(0x56aa,delay),(0x56bb,preroll)]:body+=encoded(key,value.to_bytes(max(1,(value.bit_length()+7)//8),'big'))
                entries.append(encoded(0xae,body))
            return raw[:ta]+encoded(0x1654ae6b,b''.join(entries))+raw[te:]
        for label,delay in [('zero',0),('floor-128-samples',2666666),('ceil-128-samples',2666667)]:
            content=delay_variant(delay);plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(content));plan['timeline']['duration']=str(int(plan['timeline']['duration'])+1000000)
            data=root/('delay-'+label);data.mkdir();rpc=Rpc([runner,'--registry',str(root/('delay-'+label+'-registry'))])
            try:
                value=publish(plan,content,data,len(content),complete=True);answer=rpc('open',directory=str(data),session=value);assert answer['ok'],answer
                assert rpc('index')['tracks'][1]['samples']>0;rpc('release');assert not rpc('readers',version=1,sessionId=plan['sessionId'],generation='1')['readers'];checks.append('vorbis-codec-delay-'+label)
            finally:rpc.close()
        for label,delay,preroll in [('non-sample-aligned',1,0),('oversized',1000000001,0),('unsupported-preroll',0,80000000)]:
            content=delay_variant(delay,preroll);plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(content));rejected('vorbis-delay-'+label,content,plan)
        ca,cb,ce,ga,gb,ge,bb,be=packets[0]
        def duration_variant(ticks,padding=1000000):
            block=bytearray(raw[bb:be]);block[width(block[0])+2]&=0x7f
            group=encoded(0xa0,encoded(0xa1,bytes(block))+encoded(0x9b,bytes([ticks]))+encoded(0x75a2,padding.to_bytes(4,'big',signed=True)))
            cluster=encoded(0x1f43b675,raw[cb:ga]+group+raw[ge:ce])
            return raw[:ca]+cluster+raw[ce:]
        for label,ticks in [('full-packet',3),('audible-remainder',2)]:
            content=duration_variant(ticks);plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(content));plan['timeline']['duration']=str(int(plan['timeline']['duration'])+1000000)
            data=root/('duration-'+label);data.mkdir();rpc=Rpc([runner,'--registry',str(root/('duration-'+label+'-registry'))])
            try:
                value=publish(plan,content,data,len(content),complete=True);answer=rpc('open',directory=str(data),session=value);assert answer['ok'],answer
                first=rpc('index',sampleTimes=True)['tracks'][1]['firstSamples'][0];assert int(first['end'])-int(first['start'])==16666,first
                rpc('release');checks.append('vorbis-block-duration-'+label+'-padding-once')
            finally:rpc.close()
        for label,ticks,padding in [('inconsistent-duration',9,1000000),('padding-exceeds-packet',3,3000000)]:
            content=duration_variant(ticks,padding);plan=copy.deepcopy(model);plan['resources'][0]['length']=str(len(content));rejected('vorbis-'+label,content,plan)
        # Reusing an accepted snapshot after a rejected codec change must keep
        # its index and read permissions. Media corruption after admission
        # instead closes both views until release.
        data=root/'publish';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'publish-registry')])
        try:
            value=publish(model,raw,data,len(raw));assert rpc('open',directory=str(data),session=value)['ok'];before=rpc('index')
            bad=copy.deepcopy(value);bad['revision']='2';bad['tracks'][1]['codecHint']='opus';assert not rpc('publish',session=bad)['ok'];assert rpc('index')==before
            for k in (0,1):assert rpc('read',track=k,offset='0',length='64')['bytesRead']==64
            with (data/'mux.part').open('r+b') as f:f.seek(200);f.write(b'changed')
            for k in (0,1):assert rpc('read',track=k,offset='0',length='65536')['bytesRead']==-1
            rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
            checks+=['vorbis-audio-hint-update-rejected-with-old-index-intact','vorbis-sha-rewrite-closes-both-views']
        finally:rpc.close()
    result.update(scope='muxed-vorbis-webm-native',count=len(checks),legacyOpusProfilesChanged=False)
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
