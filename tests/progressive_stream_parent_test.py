"""Managed continuous parent: real SHA ranges/leases, lifecycle, faults. No player API."""
import argparse,copy,hashlib,json,os,subprocess,tempfile,time
from pathlib import Path
from progressive_mux_ts_conformance import Rpc
from progressive_backend_demux_conformance import snapshot
from progressive_backend_remux_conformance import verify_artifact
from progressive_stream_remux_worker_test import fixture,complete_media
from progressive_integrity import protect_snapshot
SOURCE_SHA256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()


def run(runner,worker,report):
    rows=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-stream-parent-') as temp:
        root=Path(temp);original=fixture(root,'source');raw=original.read_bytes();original_hash=hashlib.sha256(raw).hexdigest()
        rpc=Rpc([str(runner),str(root/'registry'),str(worker)])
        def wait(predicate,timeout=15):
            until=time.monotonic()+timeout
            while time.monotonic()<until:
                state=rpc('status');assert state['ok'],state
                if predicate(state):return state
                time.sleep(.02)
            raise AssertionError(('parent timeout',state))
        def release(state,value):
            assert state['done'] and state['filesClosed'],state
            for rid in state['readerIds']:
                receipt=rpc('readerStatus',version=1,readerId=rid)
                assert receipt['ok'] and receipt['reader']['released'] and receipt['reader']['reason']=='files-closed',receipt
                assert rpc('readerForget',version=1,readerId=rid)['ok']
            assert not rpc('readers',version=1,sessionId=value['sessionId'],generation='1')['readers']
            assert rpc('drop')['ok']
        def begin(name,prefix=4,complete=False,**options):
            folder=root/name;folder.mkdir();path=folder/'input.part';value=snapshot(path,raw,complete=complete,prefix=prefix)
            output=folder/'output.mp4'
            if options.pop('physicalTail',False):
                with path.open('r+b') as f:f.write(raw);f.flush();os.fsync(f.fileno())
            if options.pop('corrupt',False):path.write_bytes(bytes([raw[0]^1])+raw[1:prefix])
            answer=rpc('start',directory=str(folder),output=str(output),session=value,**options);assert answer['ok'],answer
            return folder,path,value,output
        def publish(value,folder,path,n,complete=False,failed=False):
            previous=copy.deepcopy(value)
            with path.open('r+b') as f:f.write(raw[:n]);f.flush();os.fsync(f.fileno())
            value=copy.deepcopy(value);value['revision']=str(int(value['revision'])+1)
            state='failed' if failed else 'complete' if complete else 'downloading'
            value['writer']['state']=state;value['resources'][0].update(state=state,ranges=[dict(offset='0',length=str(n))]);value['tracks'][0]['state']=state
            value=protect_snapshot(value,folder,previous=previous);answer=rpc('publish',session=value);assert answer['ok'],answer;return value
        try:
            folder,path,value,output=begin('growing',heartbeat=2000,physicalTail=True)
            # Physical, unpublished tail must not become input, even with valid MP4 bytes.
            s=wait(lambda s:s['code']=='waiting');pid=s['workerId'];assert s['maximumReadEnd']==4 and s['readerIds'] and not s['done'],s
            for bad,expected in [('generation','generation_changed'),('revision','stale_revision')]:
                invalid=copy.deepcopy(value);invalid[bad]='2' if bad=='generation' else '1';answer=rpc('publish',session=invalid);assert not answer['ok'] and answer['code']==expected,answer
            began=time.monotonic()
            for _ in range(5):
                assert rpc('heartbeat')['ok'];time.sleep(1)
                s=rpc('status');assert s['workerId']==pid and s['code']=='waiting' and s['maximumReadEnd']==4 and not s['filesClosed'],s
            for n in (len(raw)//3,len(raw)*2//3):
                value=publish(value,folder,path,n)
                s=wait(lambda s:s['code']=='waiting' and s['maximumReadEnd']==n)
                assert s['workerId']==pid and not s['done'] and s['revision']==value['revision'],s
            assert complete_media(output.read_bytes()),'No complete fragment before source complete'
            partial_bytes=output.stat().st_size
            value=publish(value,folder,path,len(raw));s=wait(lambda s:s['code']=='waiting' and s['reason']=='source-complete')
            assert s['workerId']==pid and not s['sourceComplete'] and not s['done'],s
            # Writer may rename to final name without changing pinned input identity.
            final=folder/'final.mp4';path.rename(final);path.write_bytes(b'poisoned-old-name');previous=copy.deepcopy(value);value['revision']=str(int(value['revision'])+1);value['writer']['state']='complete';value['resources'][0]['state']='complete';value['tracks'][0]['state']='complete'
            assert rpc('publish',session=value)['ok']
            s=wait(lambda s:s['done']);assert s['code']=='data' and s['sourceComplete'] and s['maximumReadEnd']<=len(raw),s
            result=json.loads(s['result']);assert result['sha256']==hashlib.sha256(output.read_bytes()).hexdigest() and not result['playbackSupported'],result
            verified=verify_artifact(original,output,50000,True);assert hashlib.sha256(final.read_bytes()).hexdigest()==original_hash and path.read_bytes()==b'poisoned-old-name'
            release(s,value);rows.append(dict(profile='growing-one-worker',result='PASS',status=s,partialOutputBytes=partial_bytes,waitSeconds=time.monotonic()-began,unpublishedTailNotRead=True,**verified))
            for name in ('unknown-length','missing-integrity','extra-audio-track'):
                folder=root/name;folder.mkdir();path=folder/'input.part';invalid=snapshot(path,raw,complete=False,prefix=4)
                if name=='unknown-length':invalid['resources'][0]['length']=None
                elif name=='missing-integrity':invalid['resources'][0].pop('integrity')
                else:
                    audio=copy.deepcopy(invalid['tracks'][0]);audio.update(id='audio',kind='audio',codecHint='aac');invalid['tracks'].append(audio)
                answer=rpc('start',directory=str(folder),output=str(folder/'output.mp4'),session=invalid)
                assert not answer['ok'] and answer['code']=='unsupported-stream-source',answer
                assert (folder/'output.mp4').stat().st_size==0
                rows.append(dict(profile=name,result='PASS',rejectedBeforeSpawn=True,code=answer['code']))
            for name,expected in [('cancel','cancelled'),('writer-failed','writer-failed'),('heartbeat-loss','writer-heartbeat-timeout'),('lifetime','backend-time-limit'),('rewrite','source-rewritten'),('initial-integrity','integrity-mismatch'),('truncate','source-truncated'),('worker-killed','backend-worker-exited'),('sink-failed','output-store-failed')]:
                options=dict(heartbeat=700) if name=='heartbeat-loss' else dict(lifetime=1000,operation=500) if name=='lifetime' else {}
                folder,path,value,output=begin(name,prefix=len(raw)//3 if name=='sink-failed' else 4,corrupt=name=='initial-integrity',**options);s=wait(lambda s:s['code']=='waiting' or s['done']);began=time.monotonic()
                if name=='cancel':assert rpc('cancel')['ok']
                elif name=='writer-failed':value=publish(value,folder,path,4,failed=True)
                elif name=='rewrite':
                    with path.open('r+b') as f:f.write(bytes([raw[0]^1]))
                elif name=='truncate':
                    with path.open('r+b') as f:f.truncate(0)
                elif name=='worker-killed':
                    if os.name=='nt':subprocess.run(['taskkill','/PID',str(s['workerId']),'/F'],check=True,capture_output=True)
                    else:os.kill(s['workerId'],9)
                elif name=='sink-failed':
                    assert s['bytesWritten']>0 and not s['sourceComplete'],s
                    assert rpc('sinkFail')['ok'];value=publish(value,folder,path,len(raw),complete=True)
                s=wait(lambda s:s['done']);elapsed=time.monotonic()-began
                assert s['code']==expected and not s['result'] and s['filesClosed'],(name,s)
                if name=='cancel':assert elapsed<2,(name,elapsed)
                release(s,value);rows.append(dict(profile=name,result='PASS',elapsedSeconds=elapsed,status=s))
            for mode,expected in [('stall','backend-operation-time-limit'),('partial-frame','backend-operation-time-limit'),('bad-offset','backend-protocol-error')]:
                folder,path,value,output=begin(mode,childMode=mode,operation=300,lifetime=3000)
                s=wait(lambda s:s['done']);assert s['code']==expected and not s['result'],s
                release(s,value);rows.append(dict(profile=mode,result='PASS',status=s))
            # Cancellation during a blocked child receive uses the managed Job/process group.
            folder,path,value,output=begin('receive-cancel',childMode='stall',operation=10000)
            began=time.monotonic();assert rpc('cancel')['ok'];s=wait(lambda s:s['done']);elapsed=time.monotonic()-began
            assert s['code']=='cancelled' and elapsed<2,s;release(s,value);rows.append(dict(profile='receive-cancel',result='PASS',elapsedSeconds=elapsed,status=s))
            assert hashlib.sha256(original.read_bytes()).hexdigest()==original_hash
        finally:rpc.close()
    result=dict(result='PASS',count=len(rows),rows=rows,publicPlayerCapability=False,controllerOnly=True,temporaryWorkRemoved=not root.exists(),runnerSha256=hashlib.sha256(runner.read_bytes()).hexdigest(),workerSha256=hashlib.sha256(worker.read_bytes()).hexdigest(),testSourceSha256=SOURCE_SHA256)
    report.mkdir(parents=True,exist_ok=True);(report/'result.json').write_text(json.dumps(result,indent=2));return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);p.add_argument('--worker',type=Path,required=True);p.add_argument('--report-directory',type=Path,required=True);a=p.parse_args();print(json.dumps(run(a.runner.resolve(),a.worker.resolve(),a.report_directory.resolve())))
