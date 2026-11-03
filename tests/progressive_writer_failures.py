"""Owned writer crash, unpublished partial frame, and injected ENOSPC; no downloader."""
import argparse
import copy
import errno
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import traceback
import uuid
from progressive_player_crash_recovery import Peer, sha, wait
from progressive_client import ApiError, ProgressiveClient
from progressive_integrity import protect_snapshot
from progressive_v2 import V2PreviewSession
from progressive_v2_playback_conformance import forget_closed_readers
from progressive_rf64_limits_fixture import geometry, header, pcm


def save(path,value):
    path=Path(path);pending=path.with_name(path.name+'.pending');pending.write_text(json.dumps(value),encoding='utf-8');end=time.monotonic()+3
    while True:
        try:pending.replace(path);return
        except PermissionError:
            assert time.monotonic()<end;time.sleep(.01)


def read(path):
    try:return json.loads(Path(path).read_text(encoding='utf-8'))
    except (OSError,ValueError):return None


def media(root,bits,sparse):
    root=Path(root);root.mkdir();g=geometry(bits,2,48000,20*48000);raw=header(g)+pcm(g,0,20*48000*g['align'])
    ends={s:g['offset']+s*48000*g['align'] for s in (4,12,16)};path=root/'writer.part';path.write_bytes(raw[:ends[4]])
    v=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='200000000',timestampOffset='0'),resources=[dict(id='media',name=path.name,length=str(len(raw)),ranges=[dict(offset='0',length=str(ends[4]))],state='downloading')],tracks=[dict(id='main',kind='audio',required=True,codecHint='pcm-s%dle'%bits,containerHint='rf64-'+('sparse-' if sparse else '')+'pcm%d'%bits,state='downloading',initializationReady=True,source=dict(resourceId='media'))])
    return v,raw,g,ends,path


def append(path,data):
    with path.open('ab') as file:file.write(data);file.flush();os.fsync(file.fileno())


class InjectedDiskFull(OSError):pass


def writer(config):
    c=read(config);assert c;root=Path(c['directory']);path=root/'writer.part';g=geometry(c['bits'],2,48000,20*48000);raw=header(g)+pcm(g,0,20*48000*g['align']);ends={s:g['offset']+s*48000*g['align'] for s in (4,12,16)}
    client=ProgressiveClient(c['player'],c.get('profile'));new_writer=uuid.uuid4().hex;conflict=None;fault=None
    if c['mode']=='recover':
        prior=c['checkpoint'];old=prior['snapshot'];size=path.stat().st_size
        assert sha_prefix(path,ends[12])==prior['publishedPrefixSha256'] and path.read_bytes()==raw[:size] and size==ends[12]+g['align']-1
        value=copy.deepcopy(old);value.update(generation='2',revision='1');value['writer']['instanceId']=new_writer
        value['resources'][0].pop('integrity',None);value=protect_snapshot(value,root)
        bad=copy.deepcopy(value);bad['generation']='1';bad['revision']='3'
        try:client.start(bad,session_directory=root,track_reader=True,activate=False)
        except ApiError as error:assert error.code=='generation_changed',error.reply;conflict=error.reply
        else:raise AssertionError('New writer rebound an active old generation')
        preview=V2PreviewSession(client,str(root),title='Recovered test writer',replace=True);preview.publish(value)
        append(path,raw[size:ends[16]]);value['revision']='2';value['resources'][0]['ranges'][0]['length']=str(ends[16]);value=protect_snapshot(value,root,previous=preview.last);preview.publish(value)
    else:
        value=copy.deepcopy(c['snapshot']);value['writer']['instanceId']=new_writer;value=protect_snapshot(value,root)
        preview=V2PreviewSession(client,str(root),title='Fault-injection writer');preview.publish(value)
        wait(lambda:preview.heartbeat()['state']=='buffering','writer initial buffering')
        append(path,raw[ends[4]:ends[12]]);value['revision']='2';value['resources'][0]['ranges'][0]['length']=str(ends[12]);value=protect_snapshot(value,root,previous=preview.last);preview.publish(value)
    def playing():
        s=preview.heartbeat();assert not s.get('error'),s
        return s if s['state']=='playing' and s['positionSeconds']>=.25 and int(s['decoderIoBytes']['audio'])>0 else None
    state=wait(playing,'writer actual playback')
    published=ends[16] if c['mode']=='recover' else ends[12]
    if c['mode']!='recover':
        try:
            append(path,raw[ends[12]:ends[12]+g['align']-1])
            if c['mode']=='enospc':raise InjectedDiskFull(errno.ENOSPC,'Injected append failure after a real short write')
        except InjectedDiskFull as error:
            fault=dict(errno=error.errno,injected=True,kernelDiskFull=False,shortWriteBytes=g['align']-1,message=str(error))
            client.command('error',value['sessionId'],readerId=preview.reader_id,message='ENOSPC (injected): append failed after short write')
    checkpoint=dict(snapshot=value,readerId=preview.reader_id,writerPid=os.getpid(),writerInstanceId=new_writer,publishedPrefixSha256=sha_prefix(path,published),physicalBytes=path.stat().st_size,publishedBytes=published,partialFrameBytes=0 if c['mode']=='recover' else g['align']-1,fault=fault,rebindConflict=conflict,playing=state)
    save(c['ready'],checkpoint)
    while not Path(c['finish']).exists():
        if c['mode']!='enospc':preview.heartbeat()
        time.sleep(.2)
    save(c['done'],dict(result='PASS',writerPid=os.getpid(),sourceRetained=path.exists(),sourceSha256=sha(path)))
    # Exiting a test writer never closes the player's independent reader.
    return checkpoint


def sha_prefix(path,count):
    with path.open('rb') as file:return hashlib.sha256(file.read(count)).hexdigest()


class WriterJob:
    def __init__(self,peer,root,config):
        self.directory=root;root.mkdir();self.config=dict(config,player=peer.player,profile=str(peer.profile) if peer.edition=='qt' else None,ready=str(root/'ready.json'),finish=str(root/'finish'),done=str(root/'done.json'))
        save(root/'config.json',self.config);self.log=(root/'writer.log').open('wb');self.process=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'--writer-config',str(root/'config.json')],stdin=subprocess.DEVNULL,stdout=self.log,stderr=subprocess.STDOUT);self.history=dict(pid=self.process.pid,mode=config['mode'],exitCode=None)
    def ready(self,pump):
        def check():
            pump();value=read(self.directory/'ready.json')
            if value:return value
            assert self.process.poll() is None,('Writer exited before ready',self.process.returncode)
        return wait(check,'writer ready')
    def kill(self):
        assert self.process.poll() is None;self.process.kill();self.process.wait(timeout=5);expected=1 if os.name=='nt' else -9;assert self.process.returncode==expected;self.history.update(exitCode=self.process.returncode,reason='intentional-writer-crash')
    def finish(self):
        (self.directory/'finish').touch();self.process.wait(timeout=5);assert self.process.returncode==0,self.process.returncode
        assert read(self.directory/'done.json')['result']=='PASS';self.history.update(exitCode=0,reason='normal-test-writer-exit')


def exercise(peers,root,report,jobs):
    rows=[];events=[];healthy=None;observed=[]
    def event(phase,**fields):events.append(dict(phase=phase,**fields));save(report/'events.json',events)
    for bits,sparse in ((16,False),(24,False),(16,True),(24,True)):
      for mode in ('crash','enospc'):
        case=root/('%d-%s-%s'%(bits,'sparse' if sparse else 'prefix',mode));case.mkdir();event('begin',bits=bits,sparse=sparse,mode=mode)
        control,control_raw,_,control_ends,control_path=media(case/'control',bits,sparse)
        healthy=V2PreviewSession(peers[1].client,str(control_path.parent),title='Healthy independent writer')
        protected=protect_snapshot(control,control_path.parent);healthy.publish(protected);wait(lambda:healthy.heartbeat()['state']=='buffering','healthy head')
        append(control_path,control_raw[control_ends[4]:control_ends[12]]);control['revision']='2';control['resources'][0]['ranges'][0]['length']=str(control_ends[12]);protected=protect_snapshot(control,control_path.parent,previous=protected);healthy.publish(protected)
        def pump():
            s=healthy.heartbeat();assert s.get('ok') and not s.get('error') and s['active'] and not s['complete'] and s['state'] in ('buffering','playing'),s
            return s
        wait(lambda:(lambda s:s if s['state']=='playing' and s['positionSeconds']>=.25 else None)(pump()),'healthy actual playback')
        value,raw,g,ends,path=media(case/'fault',bits,sparse)
        job=WriterJob(peers[0],case/'writer-initial',dict(mode=mode,bits=bits,directory=str(path.parent),snapshot=value));jobs.append(job);first=job.ready(pump);old=first['readerId'];sid=first['snapshot']['sessionId'];partial=g['align']-1
        assert first['writerPid']==job.process.pid and first['physicalBytes']==ends[12]+partial and path.read_bytes()==raw[:ends[12]+partial]
        def status():return peers[0].client.command('status',sid,readerId=old)
        def release(peer,wire,identifier):
            t=time.monotonic();peer.client.request(dict(version=1,action='readerRelease',readerId=identifier))
            receipt=wait(lambda:(lambda r:r if r['released'] and r['filesClosed'] else None)(peer.client.reader_status(identifier)),'positive filesClosed',2)
            elapsed=time.monotonic()-t;assert elapsed<2;assert peer.client.forget_reader(identifier)['forgotten'];forget_closed_readers(peer.client,wire)
            return dict(seconds=elapsed,receipt=receipt)
        old_receipt=peers[0].client.reader_status(old);assert old_receipt['pid']==peers[0].process.pid and old_receipt['pid']!=job.process.pid
        recovered=None;new_receipt=None;gap=[];old_release=None;failure=None
        if mode=='crash':
            job.kill();event('writer-killed',pid=job.process.pid,bits=bits,sparse=sparse);t=time.monotonic();last_second=-1
            def quiet_gap():
                nonlocal last_second
                pump();s=status();elapsed=time.monotonic()-t
                assert s['active'] and not s['complete'] and not s.get('released') and not s.get('error') and s['state'] not in ('finished','error','cancelled') and s['availableSeconds']<=12,s
                if int(elapsed)!=last_second:
                    last_second=int(elapsed);gap.append(dict(seconds=elapsed,status=s))
                return s if elapsed>=16 and not s['connected'] and s['state']=='waiting-downloader' else None
            disconnected=wait(quiet_gap,'writer heartbeat expires without EOF',23)
            held=peers[0].client.reader_status(old);assert not held['released'] and not held['filesClosed'] and held['pid']==peers[0].process.pid
            native=peers[0].native('readerStatus',readerId=old)['reader'];assert not native['released'] and native['reason']=='lease-held'
            assert path.stat().st_size==ends[12]+partial and sha_prefix(path,ends[12])==first['publishedPrefixSha256']
            recovery=WriterJob(peers[0],case/'writer-recovery',dict(mode='recover',bits=bits,directory=str(path.parent),checkpoint=first));jobs.append(recovery);recovered=recovery.ready(pump)
            assert recovered['writerInstanceId']!=first['writerInstanceId'] and recovered['readerId']!=old and recovered['snapshot']['generation']=='2' and recovered['rebindConflict']['code']=='generation_changed'
            assert recovered['playing']['positionSeconds']>=.25 and not recovered['playing']['complete']
            old_release=wait(lambda:(lambda r:r if r['released'] and r['filesClosed'] else None)(peers[0].client.reader_status(old)),'replacement closes old reader',2)
            assert old_release['pid']==held['pid'] and old_release['reason']=='files-closed'
            assert peers[0].client.forget_reader(old)['forgotten'];forget_closed_readers(peers[0].client,first['snapshot'])
            recovery.finish();final_wire=recovered['snapshot'];final_id=recovered['readerId']
            assert not peers[0].client.reader_status(final_id)['released'],'Writer normal exit closed the player reader'
            new_receipt=peers[0].client.reader_status(final_id)
            assert new_receipt['pid']==old_receipt['pid'] and not new_receipt['released']
            binding=lambda receipt:{(x['device'],x['fileId']) for x in receipt['resources']}
            assert binding(new_receipt)==binding(old_receipt)
            expected=raw[:ends[16]]
        else:
            assert first['fault']['errno']==errno.ENOSPC and first['fault']['injected'] and not first['fault']['kernelDiskFull']
            def failed():
                pump();s=status();assert not s.get('complete') and s['state']!='finished',s
                return s if s['state']=='error' and 'ENOSPC' in s.get('error','') else None
            failure=wait(failed,'explicit writer error is not EOF');job.finish();final_wire=first['snapshot'];final_id=old;expected=raw[:ends[12]+partial]
            assert path.exists() and path.read_bytes()==expected
        # The unrelated healthy writer still owns its source and can grow/resume.
        control_id=healthy.reader_id;before_state=pump();before=before_state['positionSeconds'];append(control_path,control_raw[control_ends[12]:])
        control['revision']='3';control['resources'][0]['ranges'][0]['length']=str(len(control_raw));protected=protect_snapshot(control,control_path.parent,previous=protected);healthy.publish(protected)
        other=wait(lambda:(lambda s:s if s['state']=='playing' and s['positionSeconds']>=before+.15 else None)(pump()),'independent writer continues')
        assert healthy.reader_id==control_id and not peers[1].client.reader_status(control_id)['released']
        closed=release(peers[0],final_wire,final_id);assert control_path.exists() and not peers[1].client.reader_status(control_id)['released']
        closed_control=release(peers[1],protected,control_id);healthy=None
        for gen in (1,2):assert not peers[0].client.readers(sid,gen)
        assert not peers[1].client.readers(control['sessionId'],1)
        assert path.read_bytes()==expected and control_path.read_bytes()==control_raw
        digest=sha(path);path.unlink();path.parent.rmdir();control_path.unlink();control_path.parent.rmdir()
        rows.append(dict(result='PASS',bits=bits,sparse=sparse,mode=mode,writer=job.history,initialCheckpoint=first,writerMissingObservations=gap,errorStatus=failure,recoveredCheckpoint=recovered,recoveredReaderReceipt=new_receipt,oldReaderAfterReplacement=old_release,independentWriterBeforeResume=before_state,independentWriterStatus=other,independentReaderIdUnchanged=True,publishedPrefixUnchanged=True,unpublishedPartialFrameBytes=partial,physicalSourceSha256=digest,release=closed,healthyRelease=closed_control,removedOnlyAfterReadersClosed=True))
        save(report/'progress.json',dict(completed=len(rows),rows=rows));event('case-complete',bits=bits,sparse=sparse,mode=mode)
    return dict(result='PASS',profiles=4,scenarios=8,writerCrashes=4,injectedEnospcScenarios=4,kernelDiskFullTested=False,writerCrashWaitMinimumSeconds=16,rows=rows)


def run(args):
    report=Path(args.report_directory).resolve();report.mkdir(parents=True,exist_ok=True);peers=[];jobs=[];result=None;registration=None
    if args.edition=='classic':
        from windows.progressive_family_test import registration_snapshot
        registration=registration_snapshot();assert len(args.player)==2
    else:assert len(args.player)==1
    with tempfile.TemporaryDirectory(prefix='foxicmp-writer-faults-') as directory:
        root=Path(directory)
        try:
            for i in range(2):
                logs=root/('peer-%d'%i);logs.mkdir();player=args.player[i] if args.edition=='classic' else args.player[0];profile=Path(player).parent if args.edition=='classic' else root/('profile-%d'%i)
                peer=Peer(args.edition,player,profile,logs,args.reader_runner);peers.append(peer);peer.launch()
            result=exercise(peers,root,report,jobs)
        finally:
            errors=[];removed=[]
            for job in jobs:
                try:
                    if job.process.poll() is None:job.process.kill();job.process.wait(timeout=5);job.history.update(exitCode=job.process.returncode,reason='failure-cleanup')
                    job.log.close()
                except Exception:errors.append(traceback.format_exc())
            for peer in peers:
                try:
                    if peer.process is not None and peer.process.poll() is None:peer.kill('cleanup-owned-idle-process' if result is not None else 'cleanup-after-test-failure')
                except Exception:errors.append(traceback.format_exc())
            for peer in peers:
                try:removed.extend(peer.cleanup())
                except Exception:errors.append(traceback.format_exc())
            for i,job in enumerate(jobs):
                dst=report/('writer-%02d'%i);dst.mkdir(exist_ok=True)
                for p in job.directory.iterdir():
                    if p.is_file():shutil.copy2(p,dst/p.name)
            for i,peer in enumerate(peers):
                dst=report/('peer-%d'%i);dst.mkdir(exist_ok=True)
                for p in peer.directory.glob('*.log'):shutil.copy2(p,dst/p.name)
            save(report/'cleanup.json',dict(runtimePathsRemoved=removed,writerProcesses=[j.history for j in jobs],playerProcesses=[p.history for p in peers],allOwnedProcessesExited=all(j.process.poll() is not None for j in jobs) and all(p.process.poll() is not None for p in peers)))
            if errors:(report/'cleanup-error.txt').write_text('\n'.join(errors),encoding='utf-8')
            assert not errors,errors
            if registration is not None:assert registration_snapshot()==registration,'Installed shell registration changed'
    result.update(edition=args.edition,writerProcesses=[j.history for j in jobs],playerProcesses=[p.history for p in peers],temporaryWorkRemoved=not root.exists(),playerSha256=[sha(p.player) for p in peers],readerRunnerSha256=sha(args.reader_runner))
    save(report/'result.json',result);return result


if __name__=='__main__':
    if '--writer-config' in sys.argv:
        p=argparse.ArgumentParser();p.add_argument('--writer-config',required=True);a=p.parse_args();writer(a.writer_config)
    else:
        p=argparse.ArgumentParser();p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--player',action='append',required=True);p.add_argument('--reader-runner',required=True);p.add_argument('--report-directory',required=True);a=p.parse_args()
        try:print(json.dumps(run(a)))
        except Exception:
            Path(a.report_directory).mkdir(parents=True,exist_ok=True);(Path(a.report_directory)/'error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
