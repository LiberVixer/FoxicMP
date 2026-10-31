"""Shared fMP4 projection, real decoded bytes and fail-closed publication."""
import argparse
import copy
import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path
from fractions import Fraction
from progressive_mux_mp4_fixture import fixture,publish,boxes
from progressive_mux_ts_conformance import Rpc


def run(runner, media_cases=None, legacy_faults=True):
    checks=[];reports=[];replacement_blocked=False
    for profile,seconds in (media_cases or [('main-bframes',4),('high-fractional',4),('main-delayed',4),('main-bframes',80)]):
        with tempfile.TemporaryDirectory(prefix='foxicmp-mux-mp4-') as tmp:
            root=Path(tmp);data=root/'data';data.mkdir();model,raw,original,groups,scales=fixture(root,profile,seconds)
            atoms=list(boxes(raw));fragments=[a for a,n,k,h in atoms if k==b'moof'];assert len(fragments)>=3
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
                prefix=[dump(k,root/f'prefix-{k}.mp4') for k in (0,1)]
                # Fully available moof/partial mdat never exposes new samples.
                next_stop=fragments[2]-1;value=publish(model,raw,data,next_stop,previous=value)
                assert rpc('publish',session=value)['ok'];assert rpc('index')==first
                for k in (0,1):assert rpc('read',track=k,offset=str(len(prefix[k])),length='1')['bytesRead']==-1
                bad=copy.deepcopy(value);bad['revision']=str(int(value['revision'])+1);bad['tracks'][1]['containerHint']='fmp4'
                assert not rpc('publish',session=bad)['ok'];assert rpc('index')==first
                # Append by arbitrary physical byte cuts; metadata/index prefix
                # commits atomically only after both traf and their entire mdat.
                stops=sorted(set(list(range(next_stop+4091,len(raw),4091))+[len(raw)])) if seconds==4 else [len(raw)]
                counts=[t['samples'] for t in first['tracks']]
                for stop in stops:
                    value=publish(model,raw,data,stop,previous=value);answer=rpc('publish',session=value);assert answer['ok'],(profile,stop,answer)
                    index=rpc('index');assert all(t['samples']>=n for t,n in zip(index['tracks'],counts));counts=[t['samples'] for t in index['tracks']]
                for k in (0,1):assert rpc('read',track=k,offset=rpc('layout')['tracks'][k]['available'],length='1')['bytesRead']==-1
                value=publish(model,raw,data,len(raw),previous=value,complete=True);answer=rpc('publish',session=value);assert answer['ok'],answer
                index=rpc('index');times=[]
                for k in (0,1):
                    file=root/f'track-{k}.mp4';result=dump(k,file);assert result.startswith(prefix[k]);assert index['tracks'][k]['samples']==len(groups[k]),index
                    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(file)]))
                    assert len(probe['streams'])==1 and len(probe['packets'])==len(groups[k]);clock=Fraction(probe['streams'][0]['time_base']);delta=0
                    for a,b in zip(probe['packets'],groups[k]):
                        assert a['data_hash']==b['data_hash'],(profile,k,a,b)
                        for field in ('pts','dts'):
                            d=abs(int(Fraction(int(a[field]))*clock*10000000)-int(Fraction(int(b[field]))*scales[k]*10000000));delta=max(delta,d);assert d==0,(profile,k,field,a,b,d)
                    times.append(delta)
                    def decoded(path):
                        options=['-an','-pix_fmt','yuv420p10le' if profile.startswith(('hevc10','av1-10','vp9-10')) else 'yuv420p','-fps_mode','passthrough','-c:v','rawvideo','-threads','1'] if k==0 else ['-vn','-c:a','pcm_s16le']
                        return subprocess.check_output(['ffmpeg','-v','error','-i',str(path),*options,'-f','hash','-hash','sha256','-'],stderr=subprocess.PIPE).strip()
                    assert decoded(original)==decoded(file),(profile,k,'decoded bytes differ')
                    for at in range(len(result)-1,-1,-65521):
                        start=max(0,at-1023);assert rpc('read',track=k,offset=str(start),length=str(at-start+1))['hex']==result[start:at+1].hex()
                    assert rpc('read',track=k,offset=str(len(result)),length='1')['bytesRead']==0
                status=rpc('status');assert status['mediaMode']=='growing-muxed-fmp4-'+model['tracks'][0]['codecHint']+'-'+model['tracks'][1]['codecHint'] and status['physicalResources']==1,status
                readers=lambda:rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
                assert len(readers())==1;rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
                assert sorted(p.name for p in data.iterdir())==['mux.part']
                reports.append(dict(profile=profile,seconds=seconds,accessUnits=[len(g) for g in groups],maxTimestampDifference100ns=times,decodedVideoAndPcmMatch=True))
                checks.append('partial-fragment-index-immutable-prefix-and-complete-'+profile+str(seconds))
                checks.append('packet-hashes-pts-dts-decoded-hashes-backward-reads-single-lease-'+profile+str(seconds))
            finally:rpc.close()
    if not legacy_faults:
        return dict(result='PASS',scope='muxed-fmp4-opus-native' if model['tracks'][1]['codecHint']=='opus' else 'muxed-'+model['tracks'][0]['codecHint']+'-fmp4-native',count=len(checks),checks=checks,realMedia=reports,playerDecoderTested=False,networkRequests=0,physicalAvSyncMeasured=False)
    with tempfile.TemporaryDirectory(prefix='foxicmp-mux-mp4-errors-') as tmp:
        root=Path(tmp);model,raw,_,_,_=fixture(root,seconds=4);atoms=list(boxes(raw));fragments=[a for a,n,k,h in atoms if k==b'moof']
        def case(name,changed,initial=False):
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                value=publish(model,changed if initial else raw,data,len(raw) if initial else fragments[1]+1,complete=initial)
                answer=rpc('open',directory=str(data),session=value)
                if initial:assert not answer['ok'],(name,answer)
                else:
                    assert answer['ok'],answer;before=rpc('status');value=publish(model,changed,data,len(raw),previous=value,complete=True)
                    answer=rpc('publish',session=value);assert not answer['ok'],(name,answer)
                    assert rpc('status')['revision']==before['revision']
                    for k in (0,1):assert rpc('read',track=k,offset='0',length='8')['bytesRead']==-1
                    rpc('release')
                assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name)
            finally:rpc.close()
        # Patch only previously unconfirmed bytes; publish their actual SHA.
        start=fragments[1]
        mfhd=raw.index(b'mfhd',start)+8
        changed=bytearray(raw);changed[mfhd:mfhd+4]=(999).to_bytes(4,'big');case('fragment-sequence-error-closes-both-views',changed)
        trun=raw.index(b'trun',start);changed=bytearray(raw);changed[trun+12:trun+16]=(0x7fffffff).to_bytes(4,'big');case('payload-offset-outside-mdat-closes-both-views',changed)
        tfhd=raw.index(b'tfhd',start);changed=bytearray(raw);changed[tfhd+8:tfhd+12]=(999).to_bytes(4,'big');case('unknown-track-closes-both-views',changed)
        trun2=raw.index(b'trun',trun+4);changed=bytearray(raw);changed[trun2+12:trun2+16]=raw[trun+12:trun+16];case('overlapping-audio-video-payload-closes-both-views',changed)
        url=raw.index(b'url ');changed=bytearray(raw);changed[url+4:url+8]=b'\0\0\0\0';case('reject-external-data-reference',changed,True)
        stts=raw.index(b'stts');changed=bytearray(raw);changed[stts+8:stts+12]=b'\0\0\0\1';case('reject-nonfragmented-sample-table',changed,True)
        avc=raw.index(b'avc1',raw.index(b'stsd'));changed=bytearray(raw);changed[avc:avc+4]=b'hvc1';case('reject-unsupported-actual-codec',changed,True)
        mdat=next(a+h for a,n,k,h in atoms if k==b'mdat')
        for name,action in [('sha-rewrite-fails-both-views','rewrite'),('truncate-fails-both-views','truncate'),('file-replacement-protected-until-release','replace')]:
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-registry'))])
            try:
                value=publish(model,raw,data,len(raw));assert rpc('open',directory=str(data),session=value)['ok']
                path=data/'mux.part'
                if action=='replace':
                    temp=data/'replacement';temp.write_bytes(raw)
                    try:temp.replace(path)
                    except PermissionError:
                        if os.name!='nt':raise
                        replacement_blocked=True
                else:
                    with path.open('r+b') as f:
                        if action=='truncate':f.truncate(500)
                        else:f.seek(mdat+100);f.write(b'changed')
                for k in (0,1):
                    answer=rpc('read',track=k,offset='0',length='65536')
                    if action=='replace' and replacement_blocked:assert answer['bytesRead']>0,answer
                    else:assert answer['bytesRead']==-1 and not answer['hex'],answer
                rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
                if action=='replace' and replacement_blocked:temp.replace(path)
                checks.append(name)
            finally:rpc.close()
        data=root/'partial-complete';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'partial-complete-registry')])
        try:
            unknown=copy.deepcopy(model);unknown['resources'][0]['length']=None
            value=publish(unknown,raw,data,fragments[2]-1);assert rpc('open',directory=str(data),session=value)['ok']
            bad=copy.deepcopy(value);bad['revision']='2';bad['writer']['state']='complete';bad['resources'][0].update(length=str(fragments[2]-1),state='complete')
            for t in bad['tracks']:t['state']='complete'
            assert not rpc('publish',session=bad)['ok']
            for k in (0,1):assert rpc('read',track=k,offset='0',length='8')['bytesRead']==-1
            rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('partial-mdat-is-fatal-only-on-explicit-complete')
        finally:rpc.close()
        # Incomplete initial metadata is waiting and closes its temporary lease.
        data=root/'waiting';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'waiting-registry')])
        try:
            value=publish(model,raw,data,17);answer=rpc('open',directory=str(data),session=value);assert not answer['ok'] and answer['code']=='waiting_initialization',answer
            assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('partial-initial-header-waits-without-leak')
        finally:rpc.close()
        data=root/'cancel';data.mkdir();rpc=Rpc([runner,'--registry',str(root/'cancel-registry')])
        try:
            value=publish(model,raw,data,len(raw));assert rpc('open',directory=str(data),session=value)['ok'];assert rpc('cancel')['ok']
            for k in (0,1):assert rpc('read',track=k,offset='0',length='8')['bytesRead']==-1
            rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('cancellation-closes-both-views')
        finally:rpc.close()
    return dict(result='PASS',scope='muxed-fmp4-native',count=len(checks),checks=checks,realMedia=reports,playerDecoderTested=False,networkRequests=0,physicalAvSyncMeasured=False,replacementProtectedByOs=replacement_blocked)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
