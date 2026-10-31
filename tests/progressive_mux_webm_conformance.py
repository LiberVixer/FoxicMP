"""Shared WebM projection, real decoded bytes and fail-closed publication."""
import argparse
import copy
import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path
from fractions import Fraction
from progressive_mux_webm_fixture import fixture,publish,top_elements
from progressive_mux_ts_conformance import Rpc


def run(runner):
    checks=[];reports=[];replacement_blocked=False
    for profile,seconds in [('vp9-8',4),('vp9-10',4),('vp9-fractional',4),('vp9-known',4),('vp9-8',80)]:
        with tempfile.TemporaryDirectory(prefix='foxicmp-mux-webm-') as tmp:
            root=Path(tmp);data=root/'data';data.mkdir();model,raw,original,groups,scales=fixture(root,profile,seconds)
            fragments=[a for t,a,b,e in top_elements(raw) if t==0x1f43b675];assert len(fragments)>=3
            value=publish(model,raw,data,fragments[2]-17)
            rpc=Rpc([runner,'--registry',str(root/'registry')])
            def dump(kind,path):
                size=int(rpc('layout')['tracks'][kind]['available']);parts=[]
                for at in range(0,size,65536):
                    r=rpc('read',track=kind,offset=str(at),length=str(min(65536,size-at)));assert r['ok'] and r['bytesRead']>0,r;parts.append(bytes.fromhex(r['hex']))
                result=b''.join(parts);assert len(result)==size;path.write_bytes(result);return result
            try:
                assert rpc('preflight',session=value)['ok']
                answer=rpc('open',directory=str(data),session=value);assert answer['ok'],(profile,answer)
                first=rpc('index');assert all(t['samples']>0 for t in first['tracks']),first
                prefix=[dump(k,root/f'prefix-{k}.webm') for k in (0,1)]
                # Fully available Cluster header/partial body never exposes new samples.
                next_stop=fragments[2]-1;value=publish(model,raw,data,next_stop,previous=value)
                assert rpc('publish',session=value)['ok'];assert rpc('index')==first
                for k in (0,1):assert rpc('read',track=k,offset=str(len(prefix[k])),length='1')['bytesRead']==-1
                bad=copy.deepcopy(value);bad['revision']=str(int(value['revision'])+1);bad['tracks'][1]['containerHint']='fmp4'
                assert not rpc('publish',session=bad)['ok'];assert rpc('index')==first
                # Append by arbitrary physical byte cuts; metadata/index prefix
                # commits atomically only after all blocks of both tracks in the entire Cluster.
                stops=sorted(set(list(range(next_stop+4091,len(raw),4091))+[len(raw)])) if seconds==4 else [len(raw)]
                counts=[t['samples'] for t in first['tracks']]
                for stop in stops:
                    value=publish(model,raw,data,stop,previous=value);answer=rpc('publish',session=value);assert answer['ok'],(profile,stop,answer)
                    index=rpc('index');assert all(t['samples']>=n for t,n in zip(index['tracks'],counts));counts=[t['samples'] for t in index['tracks']]
                for k in (0,1):assert rpc('read',track=k,offset=rpc('layout')['tracks'][k]['available'],length='1')['bytesRead']==-1
                value=publish(model,raw,data,len(raw),previous=value,complete=True);answer=rpc('publish',session=value);assert answer['ok'],answer
                index=rpc('index');times=[]
                for k in (0,1):
                    file=root/f'track-{k}.webm';result=dump(k,file);assert result.startswith(prefix[k]);assert index['tracks'][k]['samples']==len(groups[k]),index
                    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(file)]))
                    assert len(probe['streams'])==1 and len(probe['packets'])==len(groups[k]);clock=Fraction(probe['streams'][0]['time_base']);delta=0
                    for a,b in zip(probe['packets'],groups[k]):
                        assert a['data_hash']==b['data_hash'],(profile,k,a,b)
                        for field in ('pts','dts'):
                            d=abs(int(Fraction(int(a[field]))*clock*10000000)-int(Fraction(int(b[field]))*scales[k]*10000000));delta=max(delta,d);assert d==0,(profile,k,field,a,b,d)
                    times.append(delta)
                    def decoded(path):
                        options=['-an','-pix_fmt','yuv420p10le' if profile=='vp9-10' else 'yuv420p','-fps_mode','passthrough','-c:v','rawvideo','-threads','1'] if k==0 else ['-vn','-c:a','pcm_s16le']
                        return subprocess.check_output(['ffmpeg','-v','error','-i',str(path),*options,'-f','hash','-hash','sha256','-'],stderr=subprocess.PIPE).strip()
                    assert decoded(original)==decoded(file),(profile,k,'decoded bytes differ')
                    for at in range(len(result)-1,-1,-65521):
                        start=max(0,at-1023);assert rpc('read',track=k,offset=str(start),length=str(at-start+1))['hex']==result[start:at+1].hex()
                    assert rpc('read',track=k,offset=str(len(result)),length='1')['bytesRead']==0
                status=rpc('status');assert status['mediaMode']=='growing-muxed-webm-vp9-opus' and status['physicalResources']==1,status
                readers=lambda:rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
                assert len(readers())==1;rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
                assert sorted(p.name for p in data.iterdir())==['mux.part']
                reports.append(dict(profile=profile,seconds=seconds,accessUnits=[len(g) for g in groups],maxTimestampDifference100ns=times,decodedVideoAndPcmMatch=True))
                checks.append('partial-fragment-index-immutable-prefix-and-complete-'+profile+str(seconds))
                checks.append('packet-hashes-pts-dts-decoded-hashes-backward-reads-single-lease-'+profile+str(seconds))
            finally:rpc.close()
    with tempfile.TemporaryDirectory(prefix='foxicmp-mux-webm-errors-') as tmp:
        from progressive_webm_fixtures import element,width
        root=Path(tmp);model,raw,_,_,_=fixture(root,seconds=4);tops=list(top_elements(raw));clusters=[(a,b,e) for t,a,b,e in tops if t==0x1f43b675]
        def child_rows(data,at,end):
            while at<end:
                tag,body,stop=element(data,at);assert stop is not None and stop<=end
                yield tag,at,body,stop
                at=stop
        def case(name,changed,initial=False):
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                case_model=copy.deepcopy(model)
                if initial:case_model['resources'][0]['length']=str(len(changed))
                value=publish(case_model,changed if initial else raw,data,len(changed) if initial else clusters[1][0]+1,complete=initial)
                answer=rpc('open',directory=str(data),session=value)
                if initial:assert not answer['ok'],(name,answer)
                else:
                    assert answer['ok'],answer;before=rpc('status');value=publish(model,changed,data,len(raw),previous=value,complete=True)
                    answer=rpc('publish',session=value);assert not answer['ok'],(name,answer);assert rpc('status')['revision']==before['revision']
                    for k in (0,1):assert rpc('read',track=k,offset='0',length='8')['bytesRead']==-1
                    rpc('release')
                assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name)
            finally:rpc.close()
        # Exercise both muxer conventions on every OS, independent of the
        # locally installed FFmpeg version: full packet vs rounded audible
        # BlockDuration with the same TOC and DiscardPadding.
        groups=[]
        for a,b,e in clusters:
            for t,start,x,y in child_rows(raw,b,e):
                if t==0xa0:
                    fields={f:(u,v) for f,_,u,v in child_rows(raw,x,y)}
                    if 0x75a2 in fields:groups.append((a,b,e,start,x,y,fields))
        assert groups
        ca,cb,ce,ga,gb,ge,fields=groups[-1];px,py=fields[0x75a2];padding=int.from_bytes(raw[px:py],'big',signed=True)
        def encoded(tag,body):
            for n in range(1,9):
                if len(body)<(1<<(7*n))-1:break
            return tag.to_bytes((tag.bit_length()+7)//8,'big')+((1<<(7*n))|len(body)).to_bytes(n,'big')+body
        def duration_variant(tick):
            body=b''.join(raw[a:e] for t,a,b,e in child_rows(raw,gb,ge) if t not in (0x9b,0xbf))+encoded(0x9b,bytes([tick]))
            group=encoded(0xa0,body)
            cluster=encoded(0x1f43b675,raw[cb:ga]+group+raw[ge:ce])
            return raw[:ca]+cluster+raw[ce:]
        baseline=None
        for label,ns in [('packet',20000000),('audible',20000000-padding)]:
            changed=duration_variant((ns+999999)//1000000);variant_model=copy.deepcopy(model);variant_model['resources'][0]['length']=str(len(changed))
            data=root/('duration-'+label);data.mkdir();rpc=Rpc([runner,'--registry',str(root/('duration-'+label+'-registry'))])
            try:
                value=publish(variant_model,changed,data,len(changed),complete=True);answer=rpc('open',directory=str(data),session=value);assert answer['ok'],answer
                index=rpc('index')
                if baseline is None:baseline=index
                else:assert index==baseline,(index,baseline)
                rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
            finally:rpc.close()
        checks.append('packet-and-rounded-audible-opus-duration-have-identical-index')
        case('inconsistent-opus-block-duration-rejected',duration_variant(1),True)
        rows=list(child_rows(raw,clusters[1][1],clusters[1][2]));block=next((b,e) for t,a,b,e in rows if t==0xa3)
        b,e=block;w=width(raw[b]);changed=bytearray(raw);changed[b]=0x83;case('unknown-block-track-closes-both-views',changed)
        changed=bytearray(raw);changed[b+w+2]|=2;case('lacing-closes-both-views',changed)
        b,e=next((b,e) for t,a,b,e in rows if t==0xe7);changed=bytearray(raw);changed[b:e]=bytes(e-b);case('regressed-clock-closes-both-views',changed)
        video=raw.index(b'V_VP9');changed=bytearray(raw);changed[video:video+5]=b'V_VP8';case('unsupported-actual-codec-rejected',changed,True)
        _,tracks_b,tracks_e=next(t for t in tops if t[0]==0x1654ae6b)[1:]
        entry=next((a,b,e) for t,a,b,e in child_rows(raw,tracks_b,tracks_e) if t==0xae)
        _,b,e=entry
        number=next((b,e) for t,a,b,e in child_rows(raw,b,e) if t==0xd7);b,e=number;changed=bytearray(raw);changed[b:e]=bytes(e-b);case('zero-track-number-rejected',changed,True)
        for name,action in [('sha-rewrite-fails-both-views','rewrite'),('truncate-fails-both-views','truncate'),('file-replacement-protected-until-release','replace')]:
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                value=publish(model,raw,data,len(raw));assert rpc('open',directory=str(data),session=value)['ok'];path=data/'mux.part'
                if action=='replace':
                    temp=data/'replacement';temp.write_bytes(raw)
                    try:temp.replace(path)
                    except PermissionError:
                        if os.name!='nt':raise
                        replacement_blocked=True
                else:
                    with path.open('r+b') as f:
                        if action=='truncate':f.truncate(400)
                        else:f.seek(clusters[0][1]+100);f.write(b'changed')
                for k in (0,1):
                    answer=rpc('read',track=k,offset='0',length='65536')
                    if action=='replace' and replacement_blocked:assert answer['bytesRead']>0,answer
                    else:assert answer['bytesRead']==-1 and not answer['hex'],answer
                rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
                if action=='replace' and replacement_blocked:temp.replace(path)
                checks.append(name)
            finally:rpc.close()
        for name,initial,final in [('partial-header-waits',17,False),('partial-cluster-is-fatal-on-complete',clusters[1][2]-1,True)]:
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                unknown=copy.deepcopy(model);unknown['resources'][0]['length']=None;value=publish(unknown,raw,data,initial)
                answer=rpc('open',directory=str(data),session=value)
                if not final:assert not answer['ok'] and answer['code']=='waiting_initialization',answer
                else:
                    assert answer['ok'],answer;bad=copy.deepcopy(value);bad['revision']='2';bad['writer']['state']='complete';bad['resources'][0].update(length=str(initial),state='complete')
                    for t in bad['tracks']:t['state']='complete'
                    assert not rpc('publish',session=bad)['ok']
                    for k in (0,1):assert rpc('read',track=k,offset='0',length='8')['bytesRead']==-1
                    rpc('release')
                assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name)
            finally:rpc.close()
        data=root/'cancel';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'cancel-registry')])
        try:
            value=publish(model,raw,data,len(raw));assert rpc('open',directory=str(data),session=value)['ok'];rpc('cancel')
            for k in (0,1):assert rpc('read',track=k,offset='0',length='8')['bytesRead']==-1
            rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('cancellation-releases-shared-resource')
        finally:rpc.close()
    return dict(result='PASS',scope='muxed-webm-native',count=len(checks),checks=checks,realMedia=reports,replacementProtectedByOs=replacement_blocked,playerDecoderTested=False,physicalAvSyncMeasured=False,networkRequests=0)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
