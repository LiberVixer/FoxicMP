"""Ordinary MP4 sample-table projection, decoded equality and failure isolation."""
import argparse,copy,json,os,subprocess,tempfile
from fractions import Fraction
from pathlib import Path
from progressive_indexed_mp4_fixture import fixture,publish,prefix_at,boxes
from progressive_mux_ts_conformance import Rpc

def run(runner):
    checks=[];reports=[]
    for profile,seconds in [('main-bframes',4),('fractional',4),('no-bframes',4),('vfr',4),('main-bframes',80)]:
        with tempfile.TemporaryDirectory(prefix='foxicmp-indexed-mp4-') as temp:
            root=Path(temp);data=root/'data';data.mkdir();model,raw,original,groups,scales=fixture(root,profile,seconds)
            rpc=Rpc([runner,'--registry',str(root/'readers')])
            def dump(k,path,decoder=True):
                size=int(rpc('layout')['tracks'][k]['available']);parts=[]
                for at in range(0,size,65536):
                    r=rpc('read',decoder=decoder,track=k,offset=str(at),length=str(min(65536,size-at)));assert r['bytesRead']>0,r;parts.append(bytes.fromhex(r['hex']))
                value=b''.join(parts);assert len(value)==size;path.write_bytes(value);return value
            try:
                stop=prefix_at(groups,scales,1);value=publish(model,raw,data,stop)
                assert rpc('preflight',session=value)['ok'];answer=rpc('open',directory=str(data),session=value);assert answer['ok'],answer
                before=[dump(k,root/f'prefix-{k}.mp4') for k in (0,1)]
                stops=list(range(stop+8191,len(raw),8191))+[len(raw)] if seconds==4 else [len(raw)]
                for stop in stops:
                    value=publish(model,raw,data,stop,previous=value);answer=rpc('publish',session=value);assert answer['ok'],answer
                    index=rpc('index');assert 'tracks' in index,index
                    for k in (0,1):
                        expected=sum(int(p['pos'])+int(p['size'])<=stop for p in groups[k]);assert index['tracks'][k]['samples']==expected,(profile,k,index,expected)
                for k in (0,1):assert rpc('read',decoder=True,track=k,offset=rpc('layout')['tracks'][k]['available'],length='1')['bytesRead']==-1
                value=publish(model,raw,data,len(raw),previous=value,complete=True);assert rpc('publish',session=value)['ok']
                for k in (0,1):
                    path=root/f'final-{k}.mp4';view=dump(k,path);assert view.startswith(before[k]);probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_packets','-show_data_hash','sha256','-of','json',str(path)]));assert len(probe['streams'])==1 and len(probe['packets'])==len(groups[k]),probe
                    internal=dump(k,root/f'internal-{k}.mp4',decoder=False)
                    for packet in probe['packets']:
                        start=int(packet['pos']);end=start+int(packet['size']);assert internal[start:end]==view[start:end],(profile,k,'native packet payload differs')
                    clock=Fraction(probe['streams'][0]['time_base'])
                    for a,b in zip(probe['packets'],groups[k]):
                        assert a['data_hash']==b['data_hash'],(profile,k,'compressed bytes differ')
                        for field in ('pts','dts'):assert Fraction(int(a[field]))*clock==Fraction(int(b[field]))*scales[k],(profile,k,field,a,b)
                    def decoded(file):
                        options=['-an','-pix_fmt','yuv420p','-fps_mode','passthrough','-c:v','rawvideo','-threads','1'] if k==0 else ['-vn','-c:a','pcm_s16le']
                        return subprocess.check_output(['ffmpeg','-v','error','-i',str(file),*options,'-f','hash','-hash','sha256','-'],stderr=subprocess.PIPE).strip()
                    assert decoded(path)==decoded(original),(profile,k,'decoded bytes differ')
                    assert rpc('read',decoder=True,track=k,offset=str(len(view)),length='1')['bytesRead']==0
                    for at in range(len(view)-1,-1,-65521):
                        start=max(0,at-1000);assert rpc('read',decoder=True,track=k,offset=str(start),length=str(at-start+1))['hex']==view[start:at+1].hex()
                status=rpc('status');assert status['mediaMode']=='growing-muxed-mp4-h264-aac-lc' and status['physicalResources']==1 and status['videoContainer']=='mp4' and status['decoderContainer']=='fmp4',status
                readers=lambda:rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
                assert len(readers())==1;rpc('retain');rpc('release');assert readers();rpc('dropRetained');assert not readers()
                assert (data/'mux.part').read_bytes()==raw and sorted(p.name for p in data.iterdir())==['mux.part']
                checks+=['confirmed-samples-stable-prefix-'+profile+str(seconds),'packet-hashes-and-pts-dts-exact-'+profile+str(seconds),'decoded-video-and-pcm-exact-'+profile+str(seconds),'single-lease-retention-and-no-original-writes-'+profile+str(seconds)]
                reports.append(dict(profile=profile,seconds=seconds,samples=[len(g) for g in groups],packetBytesAndPtsDtsExact=True,decodedHashesEqual=True,classicPacketPayloadsEqual=True))
            finally:rpc.close()
    with tempfile.TemporaryDirectory(prefix='foxicmp-indexed-errors-') as temp:
        root=Path(temp);model,raw,_,groups,scales=fixture(root,seconds=4)
        def bad(name,changed,edit=None):
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-readers'))])
            try:
                value=publish(model,changed,data,len(changed),complete=True)
                if edit:edit(value)
                answer=rpc('open',directory=str(data),session=value);assert not answer['ok'],(name,answer)
                assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name)
            finally:rpc.close()
        def patch(at,value):
            changed=bytearray(raw);changed[at:at+len(value)]=value;return changed
        at=raw.index(b'stco');bad('sample-offset-outside-mdat',patch(at+12,b'\xff'*4))
        at=raw.index(b'stsz');bad('oversized-sample-count',patch(at+12,(250001).to_bytes(4,'big')))
        at=raw.index(b'url ');bad('external-data-reference',patch(at+4,b'\0'*4))
        at=raw.index(b'avc1',raw.index(b'stsd'));bad('unsupported-actual-codec',patch(at,b'hvc1'))
        at=raw.index(b'stts');bad('invalid-duration-run',patch(at+16,b'\0'*4))
        bad('unknown-source-length',raw,lambda v:v['resources'][0].update(length=None))
        at=raw.index(b'elst');bad('non-unit-edit-rate',patch(at+20,b'\0'*4))
        at=raw.index(b'mvhd');bad('zero-movie-timescale',patch(at+16,b'\0'*4))
        first=raw.index(b'stco');second=raw.index(b'stco',first+4);bad('overlapping-track-chunks',patch(second+12,raw[first+12:first+16]))
        atoms=list(boxes(raw));moov=next((a,n) for a,n,k,h in atoms if k==b'moov');a,n=moov;tail=raw[:a]+raw[a+n:]+raw[a:a+n];bad('tail-moov-requires-another-profile',tail)
        bad('noncontiguous-confirmed-ranges',raw,lambda v:v['resources'][0].update(ranges=[dict(offset='1',length=str(len(raw)-1))]))

        for name,action in [('sha-rewrite','rewrite'),('source-truncated','truncate'),('cancel','cancel')]:
            data=root/name;data.mkdir();rpc=Rpc([runner,'--registry',str(root/(name+'-readers'))])
            try:
                value=publish(model,raw,data,len(raw));assert rpc('open',directory=str(data),session=value)['ok']
                if action=='cancel':rpc('cancel')
                else:
                    with (data/'mux.part').open('r+b') as stream:
                        if action=='truncate':stream.truncate(500)
                        else:stream.seek(int(groups[0][0]['pos']));stream.write(b'changed')
                for k in (0,1):assert rpc('read',decoder=True,track=k,offset='0',length='65536')['bytesRead']==-1
                rpc('release');assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append(name+'-closes-both-views')
            finally:rpc.close()
        for stop in (17,raw.index(b'moov')+20):
            data=root/str(stop);data.mkdir();rpc=Rpc([runner,'--registry',str(root/(str(stop)+'-readers'))])
            try:
                value=publish(model,raw,data,stop);answer=rpc('open',directory=str(data),session=value);assert not answer['ok'] and answer['code']=='waiting_initialization',answer
                assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'];checks.append('incomplete-head-index-waits-'+str(stop))
            finally:rpc.close()
    return dict(result='PASS',count=len(checks),checks=checks,realMedia=reports,wholeSourceProjectionTested=True,playerDecoderTested=False,transcoding=False,originalsUnchanged=True,physicalAvSyncMeasured=False)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);a=p.parse_args();print(json.dumps(run(a.runner)))
