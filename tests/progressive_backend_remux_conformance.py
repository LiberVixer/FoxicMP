"""Bounded copy-remux staging, not playback activation or general MP4 readiness."""
import argparse,copy,hashlib,json,subprocess,sys,tempfile,time,os,shutil,uuid
from fractions import Fraction
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parents[1]/'qt/tools')]
from progressive_backend_demux_conformance import snapshot,wait,forget,ApiRpc
from progressive_mux_ts_conformance import Rpc
from progressive_client import ProgressiveClient
from progressive_integrity import protect_snapshot


def fixtures(root):
    root.mkdir();out={}
    for name,kind,ext in [('av','av','mp4'),('bframes','av','mp4'),('video','v','mp4'),('audio','a','m4a'),('audio44100','a','m4a'),('fractional','av','mp4'),('reorder','av','mp4'),('mov','av','mov'),('partial','av','mp4'),('unsupported','av','mp4')]:
        path=root/(name+'.'+ext);seconds='8' if name in ('partial','reorder') else '1'
        args=['ffmpeg','-v','error','-y']
        if 'v' in kind:args+=['-f','lavfi','-i','testsrc2=size=160x90:rate='+('30000/1001' if name=='fractional' else '25')+':duration='+seconds]
        if 'a' in kind:args+=['-f','lavfi','-i','sine=sample_rate='+('44100' if name=='audio44100' else '48000')+':duration='+seconds]
        if 'v' in kind:args+=['-c:v','mpeg4' if name=='unsupported' else 'libx264','-threads','2']+(['-bf','2','-g','25'] if name in ('bframes','reorder') else ['-g','25'])+([] if name=='unsupported' else ['-preset','ultrafast'])
        if 'a' in kind:args+=['-c:a','aac','-ac','2' if name in ('av','audio44100') else '1']
        args+=['-movflags','+faststart',str(path)];subprocess.run(args,check=True,capture_output=True);out[name]=path
    return out


def packets(path):
    d=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets','-show_streams','-show_data_hash','sha256','-of','json',str(path)]));return d


def verify_artifact(original,output,limit,full,decode=True):
    source=packets(original);target=packets(output);a=source['packets'][:limit];b=target['packets']
    assert len(a)==len(b),(len(a),len(b))
    for index,stream in enumerate(source['streams']):
        left=[p for p in a if p['stream_index']==index];right=[p for p in b if p['stream_index']==index]
        assert [p['data_hash'] for p in left]==[p['data_hash'] for p in right],('encoded packets changed',index)
        original_time=Fraction(stream['time_base']);new_time=Fraction(target['streams'][index]['time_base'])
        for x,y in zip(left,right):
            for field in ('pts','dts'):assert abs(int(x[field])*original_time-int(y[field])*new_time)<=new_time,(field,x,y)
        if full and decode:
            kind='v' if stream['codec_type']=='video' else 'a'
            def hashes(path):
                raw=subprocess.check_output(['ffmpeg','-v','error','-threads','2','-i',str(path),'-map','0:'+kind,'-threads','2','-f','framemd5' if kind=='v' else 'hash','-'])
                return [line.rsplit(b',',1)[-1].strip() for line in raw.splitlines() if not line.startswith(b'#')]
            left_hash,right_hash=hashes(original),hashes(output)
            if left_hash!=right_hash and os.environ.get('FOXICMP_REMUX_TEST_FAILURE_DIR'):
                debug=Path(os.environ['FOXICMP_REMUX_TEST_FAILURE_DIR'])/uuid.uuid4().hex;debug.mkdir(parents=True)
                shutil.copyfile(original,debug/'original.mp4');shutil.copyfile(output,debug/'remux.mp4')
                (debug/'packets.json').write_text(json.dumps(dict(source=source,target=target),indent=2))
            assert left_hash==right_hash,('decoded media changed',original.name,kind,left_hash[:2],right_hash[:2])
    return dict(packets=len(a),encodedPacketHashesMatch=True,ptsDtsPreservedWithinOneOutputTick=True,decodedHashesMatch=bool(full and decode))


def start(rpc,value,folder,hint='mp4',limit=128,wait_ms=0):
    return rpc('resourceProbe',version=1,session=value,sessionDirectory=str(folder),reads=[],waitMilliseconds=wait_ms,media=dict(resourceId='clip',formatHint=hint,packetLimit=limit,remux='mp4-clip'))


def run(runner=None,client=None):
    checks=[];reports=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-remux-') as temp:
        root=Path(temp);media=fixtures(root/'originals');rpc=ApiRpc(client) if client else Rpc([runner,'--registry',str(root/'registry')])
        try:
            for name in ('av','bframes','video','audio','audio44100','fractional','mov','partial'):
                folder=root/name;folder.mkdir();raw=media[name].read_bytes();partial=name=='partial';path=folder/'media.part'
                value=snapshot(path,raw,complete=not partial,prefix=len(raw)//2 if partial else None)
                before=path.read_bytes()
                hint='m4a' if name in ('audio','audio44100') else 'mov' if name=='mov' else 'mp4';limit=64 if partial else 128
                v=start(rpc,value,folder,hint,limit);assert v['ok'],v;answer=wait(rpc,v['probeId']);result=answer['media']
                assert answer['state']=='finished' and result['code']=='data' and result['analysisOnly'] is False and result['preparationOnly'] and not result['playbackSupported'],answer
                artifact=result['remux'];assert artifact['gaplessEditsSealed'] and artifact['retained'] and not artifact['transcoded'] and int(artifact['bytes'])>0 and len(artifact['sha256'])==64,artifact
                assert artifact['sourceEof']==(not partial) and artifact['packetLimited']==partial,artifact
                report=dict(profile=name,output=artifact)
                if not client:
                    output=folder/'staged.mp4';assert rpc('dumpRemux',probeId=v['probeId'],path=str(output))['retained'];assert hashlib.sha256(output.read_bytes()).hexdigest()==artifact['sha256']
                    report.update(verify_artifact(media[name],output,limit,not partial))
                assert path.read_bytes()==before;forget(rpc,v['probeId']);assert path.read_bytes()==before
                assert not rpc('resourceProbeStatus',version=1,probeId=v['probeId'])['media']['remux']['retained']
                if not client:assert not rpc('dumpRemux',probeId=v['probeId'],path=str(folder/'forgotten.mp4'))['retained']
                assert not rpc('readers',version=1,sessionId=value['sessionId'],generation='1')['readers'];reports.append(report);checks.append(name+'-copy-remux-and-forget')
            folder=root/'failures';folder.mkdir();path=folder/'input.part';raw=media['av'].read_bytes()
            for name,value,hint,expected in [
                ('missing-header',snapshot(path,raw,complete=False,prefix=4),'mp4','waiting-range'),
            ]:
                v=start(rpc,value,folder,hint);answer=wait(rpc,v['probeId']);assert answer['media']['code']==expected and 'remux' not in answer['media'],answer;forget(rpc,v['probeId']);checks.append(name+'-has-no-artifact')
            # Missing payload after header and any temporary mux output must not retain a stage.
            growing=media['partial'].read_bytes();value=snapshot(path,growing,complete=False,prefix=len(growing)//8)
            v=start(rpc,value,folder);answer=wait(rpc,v['probeId']);assert answer['media']['code']=='waiting-range' and 'remux' not in answer['media'],answer;forget(rpc,v['probeId']);checks.append('missing-payload-discards-partial-mux-output')
            value=snapshot(path,raw,complete=False,prefix=4);path.write_bytes(raw)
            v=start(rpc,value,folder);answer=wait(rpc,v['probeId']);assert answer['media']['code']=='waiting-range',answer;forget(rpc,v['probeId']);checks.append('physical-tail-without-publication-is-not-remuxed')
            value=snapshot(path,raw,complete=False,prefix=4)
            with path.open('r+b') as file:file.truncate(len(raw))
            v=start(rpc,value,folder);answer=wait(rpc,v['probeId']);assert answer['media']['code']=='waiting-range',answer;forget(rpc,v['probeId']);checks.append('sparse-hole-is-not-remuxed-as-zeros')
            # A writer publication wakes a waiting remux without accepting a false EOF.
            value=snapshot(path,raw,complete=False,prefix=4);v=start(rpc,value,folder,wait_ms=10000);id=v['probeId']
            until=time.monotonic()+3
            while time.monotonic()<until:
                state=rpc('resourceProbeStatus',version=1,probeId=id)
                if state.get('media',{}).get('code')=='waiting-range':break
                time.sleep(.01)
            else:raise AssertionError(state)
            path.write_bytes(raw);new=copy.deepcopy(value);new['revision']='2';new['writer']['state']='complete';new['resources'][0].update(state='complete',ranges=[dict(offset='0',length=str(len(raw)))]);new['tracks'][0]['state']='complete';new=protect_snapshot(new,folder,previous=value)
            assert rpc('resourceProbeUpdate',version=1,probeId=id,session=new)['ok'];answer=wait(rpc,id);assert answer['media']['code']=='data' and answer['appliedRevision']=='2',answer
            rpc('resourceProbeCancel',version=1,probeId=id);answer=wait(rpc,id);assert answer['state']=='cancelled' and 'remux' not in answer['media'] and not answer['allReadable'],answer
            forget(rpc,id);checks.append('publication-retries-remux');checks.append('cancel-after-success-drops-private-stage')
            for complete in (False,True):
                reorder=media['reorder'].read_bytes();value=snapshot(path,reorder,complete=complete,prefix=None if complete else len(reorder)//2)
                v=start(rpc,value,folder,limit=64);answer=wait(rpc,v['probeId']);assert answer['state']=='error' and answer['media']['code']=='unsupported-remux-reorder' and 'remux' not in answer['media'],answer;forget(rpc,v['probeId']);checks.append('incomplete-bframe-gop-is-not-staged-'+str(complete))
            offset=root/'offset.mp4';subprocess.run(['ffmpeg','-v','error','-y','-i',str(media['av']),'-c','copy','-output_ts_offset','1',str(offset)],check=True,capture_output=True)
            value=snapshot(path,offset.read_bytes());v=start(rpc,value,folder);answer=wait(rpc,v['probeId']);assert answer['state']=='error' and answer['media']['code']=='unsupported-remux-clock',answer;forget(rpc,v['probeId']);checks.append('nonzero-start-is-not-silently-rebased')
            encrypted=root/'encrypted.mp4'
            subprocess.run(['ffmpeg','-v','error','-y','-i',str(media['av']),'-c','copy','-encryption_scheme','cenc-aes-ctr','-encryption_key','0123456789abcdef0123456789abcdef','-encryption_kid','fedcba9876543210fedcba9876543210',str(encrypted)],check=True,capture_output=True)
            value=snapshot(path,encrypted.read_bytes());v=start(rpc,value,folder);answer=wait(rpc,v['probeId']);assert answer['state']=='error' and answer['media']['code'] in ('unsupported-remux-encryption','unsupported-remux-aac-config'),answer;forget(rpc,v['probeId']);checks.append('encrypted-packets-are-not-declared-ready')
            value=snapshot(path,media['unsupported'].read_bytes());v=start(rpc,value,folder);answer=wait(rpc,v['probeId']);assert answer['state']=='error' and answer['media']['code']=='unsupported-remux-codec',answer;forget(rpc,v['probeId']);checks.append('unsupported-codec-is-not-transcoded')
            value=snapshot(path,raw);path.write_bytes(bytes([raw[0]^1])+raw[1:]);v=start(rpc,value,folder);answer=wait(rpc,v['probeId']);assert answer['media']['code']=='integrity-mismatch' and 'remux' not in answer['media'],answer;forget(rpc,v['probeId']);checks.append('integrity-error-discards-stage')
            value=snapshot(path,raw,complete=False,prefix=4);v=start(rpc,value,folder,wait_ms=10000);time.sleep(.1);began=time.monotonic();rpc('resourceProbeCancel',version=1,probeId=v['probeId']);answer=wait(rpc,v['probeId']);assert answer['state']=='cancelled' and time.monotonic()-began<2 and 'remux' not in answer['media'],answer;forget(rpc,v['probeId']);checks.append('cancel-discards-pending-stage')
            value=snapshot(path,raw)
            for hint in ('webm','mpegts','http'):
                v=start(rpc,value,folder,hint);assert not v['ok'] and v['code']=='invalid_request',v;checks.append('reject-'+hint+'-remux-before-worker')
        finally:rpc.close()
    return dict(result='PASS',count=len(checks),checks=checks,reports=reports,preparationOnly=True,playbackTested=False,remuxTested=True,originalsUnchanged=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group(required=True);g.add_argument('--runner');g.add_argument('--player');p.add_argument('--profile');a=p.parse_args();print(json.dumps(run(a.runner,ProgressiveClient(a.player,a.profile) if a.player else None)))
