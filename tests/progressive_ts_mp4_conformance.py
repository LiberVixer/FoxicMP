"""Native TS-to-virtual-fMP4 bridge; no player, hardware or network requests."""
import argparse,copy,hashlib,json,subprocess,tempfile
from pathlib import Path
from fractions import Fraction
from progressive_ts_playback import fixture
from progressive_segment_playback import publish_resources
from progressive_integrity import protect_snapshot
from progressive_catalog_playback import catalog_snapshot

def run(runner,muxed=False):
    if muxed:
        from progressive_mux_ts_playback import fixture as producer
    else:producer=fixture
    checks=[];reports=[]
    for profile,catalog in [('main-bframes',False),('high-fractional',False),('main-preroll',False),('main-bframes',True)]:
        with tempfile.TemporaryDirectory(prefix='foxicmp-ts-mp4-') as tmp:
            root=Path(tmp);data=root/'data';data.mkdir();plan,payload,_,models=producer(root,profile,seconds=(80 if muxed else 40) if catalog else 3)
            if catalog:value,expanded=catalog_snapshot(plan,payload,data,12)
            else:publish_resources(plan,payload,data,1);value=protect_snapshot(plan,data)
            proc=subprocess.Popen([runner,'--registry',str(root/'registry')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
            def rpc(action,**fields):
                proc.stdin.write(json.dumps(dict(action=action,**fields))+'\n');proc.stdin.flush();line=proc.stdout.readline();assert line,proc.stderr.read();return json.loads(line)
            def dump(track,path):
                size=int(rpc('layout')['tracks'][track]['available']);parts=[]
                for at in range(0,size,65536):
                    reply=rpc('read',track=track,offset=str(at),length=str(min(65536,size-at)));assert reply['bytesRead']>0,reply;parts.append(bytes.fromhex(reply['hex']))
                raw=b''.join(parts);assert len(raw)==size;path.write_bytes(raw);return raw
            try:
                a=rpc('preflight',session=value);assert a['ok'],a
                a=rpc('open',directory=str(data),session=value);assert a['ok'],a
                first=rpc('index');assert all(t['samples']>0 for t in first['tracks']),first
                prefix=[dump(i,root/('prefix-'+str(i)+'.mp4')) for i in range(2)]
                assert rpc('read',track=0,offset='0',length='0')['bytesRead']==0
                # Metadata rejection must leave the admitted source usable.
                bad=copy.deepcopy(value);bad['revision']='2';bad['timeline']['timestampOffset']='0';assert not rpc('publish',session=bad)['ok']
                assert rpc('read',track=0,offset='0',length='8')['hex']==prefix[0][:8].hex()
                if catalog:
                    plan['revision']='2';value,expanded=catalog_snapshot(plan,payload,data,100,previous=expanded,complete=True)
                else:
                    previous=copy.deepcopy(value);publish_resources(value,payload,data,100);value['revision']='2';value['writer']['state']='complete'
                    for t in value['tracks']:t['state']='complete'
                    value=protect_snapshot(value,data,previous=previous)
                a=rpc('publish',session=value);assert a['ok'],a
                layout=rpc('layout');index=rpc('index');assert all(t['total']==t['available'] for t in layout['tracks'])
                times=[]
                for kind,(name,original,_,_,packets,_,_) in enumerate(models):
                    file=root/(name+'.mp4');raw=dump(kind,file);assert raw.startswith(prefix[kind])
                    assert index['tracks'][kind]['samples']==len(packets) and index['tracks'][kind]['position']==str(len(raw)),index
                    assert rpc('read',track=kind,offset=str(len(raw)),length='8')['bytesRead']==0
                    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-of','json',str(file)]));found=probe['packets'];clock=Fraction(probe['streams'][0]['time_base']);origin=int(value['timeline']['timestampOffset'])
                    assert len(found)==len(packets),(profile,len(found),len(packets))
                    if profile=='main-preroll' and kind==1:assert found[0]['side_data_list'][0]['skip_samples']==(480 if muxed else 3840),found[0]
                    tolerance=0
                    for a,b in zip(found,packets):
                        for field in ('pts','dts'):
                            # One tick for TS index-to-rational cadence, another
                            # for ffprobe's per-AAC-frame 90 kHz rounding in PES.
                            delta=abs(int(Fraction(int(a[field]))*clock*10000000)-(int(b[field])*10000000//90000-origin));tolerance=max(tolerance,delta);assert delta<=224,(profile,name,field,a,b,delta)
                    times.append(tolerance)
                    def decoded_hash(path):
                        options=['-an','-pix_fmt','yuv420p','-c:v','rawvideo','-threads','1'] if kind==0 else ['-vn','-c:a','pcm_s16le']
                        if profile=='main-preroll' and kind==1 and path==original:options+=['-af','atrim=start_sample='+str(480 if muxed else 3840)]
                        return subprocess.check_output(['ffmpeg','-v','error','-i',str(path),*options,'-f','hash','-hash','sha256','-'],stderr=subprocess.PIPE).strip().decode()
                    assert decoded_hash(original)==decoded_hash(file),(profile,name,'decoded content differs')
                    # Random backward metadata/payload reads reproduce the exact virtual file.
                    for at in range(len(raw)-1,-1,-65536):
                        start=max(0,at-1023);assert rpc('read',track=kind,offset=str(start),length=str(at-start+1))['hex']==raw[start:at+1].hex()
                status=rpc('status');assert status['mediaMode']==('segmented-muxed-mpegts-h264-aac-lc' if muxed else 'segmented-mpegts-h264-aac-lc') and status['decoderContainer']=='fmp4',status
                if catalog:assert status['segmentCatalog']['resourceCount']>64 and status['segmentCatalog']['maxOpenResources']==8,status
                rpc('retain');rpc('release');a=rpc('readers',version=1,sessionId=value['sessionId'],generation='1');assert a['readers'];rpc('dropRetained');a=rpc('readers',version=1,sessionId=value['sessionId'],generation='1');assert not a['readers'],a
                label=profile+('-catalog' if catalog else '')
                checks.append('prefix-append-immutable-bytes-fragment-index-and-complete-'+label)
                checks.append('ffprobe-pts-dts-backward-reads-decoded-video-pcm-and-leases-'+label)
                reports.append(dict(profile=label,accessUnits=[len(m[4]) for m in models],maxTimestampDifference100ns=times,resources=len(expanded['resources']) if catalog else len(value['resources']),decodedHashesMatch=True))
            finally:
                proc.stdin.close()
                try:proc.wait(timeout=10)
                except subprocess.TimeoutExpired:proc.kill();proc.wait();raise
                error=proc.stderr.read();proc.stdout.close();proc.stderr.close();assert proc.returncode==0,error
    return dict(result='PASS',scope='muxed-ts-virtual-fmp4-bridge' if muxed else 'ts-virtual-fmp4-bridge',count=len(checks),checks=checks,realMedia=reports,playerDecoderTested=False,physicalAvSyncMeasured=False,networkRequests=0)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',required=True);p.add_argument('--muxed',action='store_true');a=p.parse_args();print(json.dumps(run(a.runner,a.muxed)))
