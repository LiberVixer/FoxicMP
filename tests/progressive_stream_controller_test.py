"""Shared async controller: admission vs application, bounded queues, real remux/leases.
No ordinary player API capability or Harvester change is claimed.
"""
import argparse,copy,hashlib,json,os,signal,subprocess,tempfile,time,uuid
from pathlib import Path
from progressive_mux_ts_conformance import Rpc
from progressive_backend_demux_conformance import snapshot
from progressive_stream_remux_worker_test import fixture
from progressive_integrity import protect_snapshot

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def run(runner,worker,report):
    rows=[];exits=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-stream-controller-') as tmp:
        root=Path(tmp);original=fixture(root,'source');raw=original.read_bytes();original_sha=sha(original)
        def case(name,fn):
            folder=root/name;folder.mkdir();path=folder/'input.part';value=snapshot(path,raw,complete=False,prefix=4);value['timeline']['duration']='240000000'
            # Physical tail exists, but only announced SHA-covered bytes are readable.
            with path.open('ab') as f:f.write(raw[f.tell():])
            rpc=Rpc([str(runner),str(folder/'registry'),str(folder/'cache'),str(worker)]);state=None
            def cmd(action,**fields):
                answer=rpc(action,**fields);assert answer['ok'],(action,answer);return answer
            def start(model=None,request=None):
                nonlocal state
                request=request or uuid.uuid4().hex
                fields=dict(version=1,requestId=request,sessionDirectory=str(folder),resourceId='clip',session=model or value)
                state=cmd('continuousRemuxStart',**fields);return fields,state
            def status():return cmd('continuousRemuxStatus',version=1,jobId=state['jobId'])
            def fenced(action,**extra):return cmd(action,version=1,jobId=state['jobId'],sessionId=state['sessionId'],generation=state['generation'],writerInstanceId=state['writerInstanceId'],**extra)
            def wait(predicate,timeout=15,hb=False):
                until=time.monotonic()+timeout
                while time.monotonic()<until:
                    s=status()
                    if predicate(s):return s
                    if hb and not s['outputComplete'] and s['state'] not in ['error','cancelled','forgotten']:fenced('continuousRemuxHeartbeat')
                    time.sleep(.02)
                raise AssertionError((name,'timeout',s))
            def publish(n,complete=False):
                nonlocal value
                old=copy.deepcopy(value);value=copy.deepcopy(value);value['revision']=str(int(value['revision'])+1);phase='complete' if complete else 'downloading';value['writer']['state']=value['resources'][0]['state']=value['tracks'][0]['state']=phase;value['resources'][0]['ranges']=[dict(offset='0',length=str(n))];value=protect_snapshot(value,folder,previous=old)
                return cmd('continuousRemuxUpdate',version=1,jobId=state['jobId'],session=value)
            def forget():
                fenced('continuousRemuxForget');s=wait(lambda s:s['forgotten']);assert s['released'] and s['sourceFilesClosed'],s;return s
            def gate(stage,hold):return cmd('gate',stage=stage,hold=hold)
            def wait_gate(stage):
                until=time.monotonic()+3
                while not cmd('gateStatus',stage=stage)['entered']:
                    assert time.monotonic()<until;time.sleep(.01)
            try:
                detail=fn(folder,path,rpc,cmd,start,status,fenced,wait,publish,forget,gate,wait_gate)
                rows.append(dict(profile=name,result='PASS',**(detail or {})))
            finally:
                cmd('gate',stage='prepare',hold=False);cmd('gate',stage='publish',hold=False);rpc.close();exits.append(dict(profile=name,pid=rpc.p.pid,exitCode=rpc.p.returncode))
        def growing(folder,path,rpc,c,start,status,f,w,p,forget,g,wg):
            request,s=start();sid=s['jobId'];assert c('continuousRemuxStart',**request)['jobId']==sid
            assert c('continuousRemuxStatus',version=1,requestId=request['requestId'])['jobId']==sid
            conflict=copy.deepcopy(request);conflict['sessionDirectory']=str(folder/'other');v=rpc('continuousRemuxStart',**conflict);assert not v['ok'] and v['code']=='continuous-conflict',v
            busy=copy.deepcopy(request);busy['requestId']=uuid.uuid4().hex;assert rpc('continuousRemuxStart',**busy)['code']=='continuous-busy'
            s=w(lambda s:s['producerCode']=='waiting');pid=s['workerId'];assert s['maximumReadEnd']=='4' and not s['playbackReady'],s
            for field in ['generation','writer']:
                bad=copy.deepcopy(request['session']);bad['revision']='2'
                if field=='generation':bad[field]='2'
                else:bad['writer']['instanceId']=uuid.uuid4().hex
                v=rpc('continuousRemuxUpdate',version=1,jobId=sid,session=bad);assert not v['ok'] and v['code']=='generation_changed',v
            v=rpc('continuousRemuxUpdate',version=1,jobId=sid,session=request['session']);assert v['code']=='stale_revision',v
            assert status()['acceptedRevision']=='1'
            p(len(raw)//3);s=w(lambda s:s['playbackReady'] and s['appliedRevision']=='2');assert s['workerId']==pid
            rid=c('reader',jobId=sid,name='decoder')['readerId'];assert c('read',name='decoder',offset='0')['code']=='data'
            p(len(raw)*2//3);w(lambda s:s['appliedRevision']=='3');p(len(raw));w(lambda s:s['appliedRevision']=='4' and s['producerCode']=='waiting');assert not status()['outputComplete']
            p(len(raw),True);s=w(lambda s:s['outputComplete']);assert s['workerId']==pid and s['sourceFilesClosed'] and s['samples']==600 and s['playbackReady'],s
            f('continuousRemuxForget');time.sleep(.1);pending=status();assert not pending['released'] and not pending['forgotten'];assert list((folder/'cache').glob('remux-*/output.fmp4'))
            c('close',name='decoder');final=w(lambda s:s['forgotten']);assert final['released'] and not final['playbackReady'] and not list((folder/'cache').glob('remux-*'))
            v=rpc('readerStatus',version=1,readerId=rid);assert not v['ok'],v
            assert c('continuousRemuxStart',**request)['forgotten'];return dict(singleWorkerPid=pid,final=final,heldReaderGatedCleanup=pending)
        case('growing-idempotence-fences-completion-reader-gate',growing)
        def stalled(folder,path,rpc,c,start,status,f,w,p,forget,g,wg):
            g('prepare',True);before=time.monotonic();start();elapsed=time.monotonic()-before;wg('prepare');assert elapsed<.2
            latencies=[]
            for _ in range(20):
                before=time.monotonic();s=status();f('continuousRemuxHeartbeat');latencies.append(time.monotonic()-before);assert s['workerId']==0 and s['appliedRevision']==''
            before=time.monotonic();f('continuousRemuxCancel');latencies.append(time.monotonic()-before);d=c('dropManager');assert d['seconds']<.2 and max(latencies)<.2
            assert c('retirement')['occupied']>0;g('prepare',False);until=time.monotonic()+5
            while c('retirement')['occupied']:
                assert time.monotonic()<until;time.sleep(.02)
            assert not (folder/'cache').exists();return dict(startSeconds=elapsed,maxAdmissionSeconds=max(latencies),destructionSeconds=d['seconds'],noWorkerSpawnAfterCancel=True)
        case('blocked-prepare-status-heartbeat-cancel-destroy-off-caller',stalled)
        def queue(folder,path,rpc,c,start,status,f,w,p,forget,g,wg):
            g('prepare',True);req,s=start();wg('prepare');value=copy.deepcopy(req['session'])
            for revision in range(2,10):value['revision']=str(revision);c('continuousRemuxUpdate',version=1,jobId=s['jobId'],session=value)
            value['revision']='10';v=rpc('continuousRemuxUpdate',version=1,jobId=s['jobId'],session=value);assert v['code']=='continuous-backpressure',v
            s=status();assert s['acceptedRevision']=='9' and s['appliedRevision']=='' and s['queuedUpdates']==8,s
            f('continuousRemuxCancel');g('prepare',False);final=w(lambda s:s['released']);forget();return dict(queueLimit=8,acceptedUnapplied=s,final=final)
        case('bounded-update-queue-and-accepted-vs-applied',queue)
        def blocked_update(folder,path,rpc,c,start,status,f,w,p,forget,g,wg):
            start();w(lambda s:s['producerCode']=='waiting');g('publish',True);p(len(raw)//3);wg('publish');began=time.monotonic();s=status();f('continuousRemuxCancel');elapsed=time.monotonic()-began
            assert elapsed<.2 and s['acceptedRevision']=='2' and s['appliedRevision']=='1',s;g('publish',False);s=w(lambda s:s['released']);assert s['appliedRevision']=='1' and not s['playbackReady'];forget();return dict(admissionSeconds=elapsed,final=s)
        case('blocked-publication-does-not-block-cancel-or-status',blocked_update)
        for mode in ['loss','keepalive','lifetime']:
            def deadline(folder,path,rpc,c,start,status,f,w,p,forget,g,wg,mode=mode):
                c('configure',heartbeat=700 if mode!='lifetime' else 30000,lifetime=15000 if mode!='lifetime' else 1000);start();s=w(lambda s:s['producerCode']=='waiting',hb=True);pid=s['workerId']
                if mode=='keepalive':
                    until=time.monotonic()+1.4
                    while time.monotonic()<until:f('continuousRemuxHeartbeat');time.sleep(.08)
                    s=status();assert s['workerId']==pid and s['producerCode']=='waiting' and not s['released'] and int(s['appliedHeartbeat'])>0,s;f('continuousRemuxCancel')
                final=w(lambda s:s['released']);assert final['sourceFilesClosed']
                if mode=='loss':assert final['errorCode']=='writer-heartbeat-timeout',final
                if mode=='lifetime':assert final['errorCode']=='backend-time-limit',final
                forget();return dict(final=final)
            case('writer-'+mode,deadline)
        def completed_cycles(folder,path,rpc,c,start,status,f,w,p,forget,g,wg):
            # One service fence/budget survives successive forgotten jobs.
            count=0
            for _ in range(4):
                value=snapshot(path,raw,complete=True);value['timeline']['duration']='240000000';request,s=start(value);s=w(lambda s:s['outputComplete']);assert s['playbackReady'] and s['samples']==600,s
                c('reader',jobId=s['jobId'],name='r');assert c('read',name='r',offset='0')['bytes']>0;c('close',name='r');forget();count+=1
            return dict(cycles=count)
        case('completed-source-ready-and-repeated-service-reuse',completed_cycles)
        def fences(folder,path,rpc,c,start,status,f,w,p,forget,g,wg):
            req,s=start();value=req['session'];checks=[]
            for field in ['jobId','sessionId','generation','writerInstanceId']:
                fields=dict(version=1,jobId=s['jobId'],sessionId=value['sessionId'],generation='1',writerInstanceId=value['writer']['instanceId']);fields[field]='2' if field=='generation' else uuid.uuid4().hex
                for action in ['continuousRemuxHeartbeat','continuousRemuxCancel','continuousRemuxForget']:
                    reply=rpc(action,**fields);assert not reply['ok'] and reply['code']==('unknown-continuous-job' if field=='jobId' else 'generation_changed'),reply;checks.append(action+'-'+field)
            assert status()['state'] not in ['cancelled','forgetting'];forget();return dict(rejectedCommands=checks)
        case('all-mutating-command-identity-fences',fences)
        def live_retirement(folder,path,rpc,c,start,status,f,w,p,forget,g,wg):
            start();p(len(raw)//3);s=w(lambda s:s['playbackReady']);rid=c('reader',jobId=s['jobId'],name='held')['readerId'];d=c('dropManager');assert d['seconds']<.2
            time.sleep(.1);assert c('retirement')['occupied']>0 and list((folder/'cache').glob('remux-*/output.fmp4'))
            c('close',name='held');until=time.monotonic()+5
            while c('retirement')['occupied']:
                assert time.monotonic()<until;time.sleep(.02)
            assert not list((folder/'cache').glob('remux-*'));return dict(destructionSeconds=d['seconds'],readerId=rid,retainedUntilActualClose=True)
        case('manager-destruction-retains-fence-until-reader-close',live_retirement)
        def writer_failed(folder,path,rpc,c,start,status,f,w,p,forget,g,wg):
            req,s=start();w(lambda s:s['producerCode']=='waiting');value=copy.deepcopy(req['session']);value['revision']='2';value['writer']['state']=value['resources'][0]['state']=value['tracks'][0]['state']='failed'
            c('continuousRemuxUpdate',version=1,jobId=s['jobId'],session=value);final=w(lambda s:s['released']);assert final['errorCode']=='writer-failed' and not final['outputComplete'],final;forget();return dict(final=final)
        case('explicit-writer-failure-publication',writer_failed)
        for failed_part in ['track','resource']:
            def required_failed(folder,path,rpc,c,start,status,f,w,p,forget,g,wg,failed_part=failed_part):
                req,s=start();w(lambda s:s['producerCode']=='waiting');value=copy.deepcopy(req['session']);value['revision']='2';value['tracks' if failed_part=='track' else 'resources'][0]['state']='failed'
                c('continuousRemuxUpdate',version=1,jobId=s['jobId'],session=value);final=w(lambda s:s['released'],timeout=3);assert final['errorCode'] in ['writer-failed','resource-failed'] and not final['playbackReady'],final;forget();return dict(final=final)
            case('required-'+failed_part+'-failure-publication',required_failed)

        for mode in ['integrity','missing','killed-worker']:
            def fault(folder,path,rpc,c,start,status,f,w,p,forget,g,wg,mode=mode):
                if mode=='integrity':
                    with path.open('r+b') as file:file.write(b'X')
                if mode=='missing':path.unlink()
                start()
                if mode=='missing':
                    before=w(lambda s:s['producerCode']=='waiting');assert before['maximumReadEnd']=='0' and not before['playbackReady'],before
                    path.write_bytes(raw);p(len(raw),True);final=w(lambda s:s['outputComplete']);assert final['workerId']==before['workerId'] and final['samples']==600 and final['sourceFilesClosed'],final
                    forget();return dict(initiallyWaiting=before,completed=final)
                if mode=='killed-worker':
                    s=w(lambda s:s['producerCode']=='waiting');os.kill(s['workerId'],signal.SIGTERM if os.name=='nt' else signal.SIGKILL)
                s=w(lambda s:s['released']);assert s['state']=='error' and s['errorCode'] and not s['playbackReady'] and s['sourceFilesClosed'],s
                if mode=='integrity':assert s['errorCode']=='integrity-mismatch',s
                if mode=='killed-worker':assert s['errorCode']=='backend-worker-exited',s
                forget();return dict(final=s)
            case('late-resource-waits-until-published' if mode=='missing' else 'failure-'+mode,fault)
        def rejected(folder,path,rpc,c,start,status,f,w,p,forget,g,wg):
            req=dict(version=1,requestId=uuid.uuid4().hex,sessionDirectory=str(folder),resourceId='clip',session=snapshot(path,raw,complete=False,prefix=4));req['session']['timeline']['duration']='240000000';checks=[]
            for mode in ['audio','failed-track','failed-resource','live','unknown-length','missing-integrity','clock','extra-resource','relative-root','caller-worker','request-size']:
                bad=copy.deepcopy(req)
                if mode=='audio':bad['session']['tracks'][0]['kind']='audio'
                if mode=='failed-track':bad['session']['tracks'][0]['state']='failed'
                if mode=='failed-resource':bad['session']['resources'][0]['state']='failed'
                if mode=='live':bad['session']['timeline']['live']=True
                if mode=='unknown-length':bad['session']['resources'][0]['length']=None
                if mode=='missing-integrity':bad['session']['resources'][0].pop('integrity')
                if mode=='clock':bad['session']['timeline']['timestampOffset']='1'
                if mode=='extra-resource':r=copy.deepcopy(bad['session']['resources'][0]);r['id']='extra';bad['session']['resources'].append(r)
                if mode=='relative-root':bad['sessionDirectory']='.'
                if mode=='caller-worker':bad['workerPath']='evil'
                if mode=='request-size':bad['padding']='x'*65536
                v=rpc('continuousRemuxStart',**bad);assert not v['ok'],(mode,v);checks.append(dict(mode=mode,code=v['code']))
            assert not (folder/'cache').exists();return dict(rejections=checks,noWorkerOrCacheCreated=True)
        case('reject-before-spawn-profile-schema-path-limits',rejected)
        assert sha(original)==original_sha
    result=dict(result='PASS',count=len(rows),rows=rows,exactDriverExits=exits,runnerSha256=sha(runner),workerSha256=sha(worker),testSourceSha256=sha(__file__),temporaryWorkRemoved=not root.exists(),internalControllerOnly=True,ordinaryPlayerApi=False)
    report.mkdir(parents=True,exist_ok=True);(report/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(result='PASS',count=len(rows))))
if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('--runner',type=Path,required=True);a.add_argument('--worker',type=Path,required=True);a.add_argument('--report-directory',type=Path,required=True);v=a.parse_args();run(v.runner.resolve(),v.worker.resolve(),v.report_directory.resolve())
