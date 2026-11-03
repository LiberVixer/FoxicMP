"""Kill only owned test players; recover persisted leases using real OS identity."""
import argparse
import configparser
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

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_client import ApiError, ProgressiveClient
from progressive_integrity import protect_snapshot
from progressive_resources import ResourceProbe
from progressive_v2 import V2PreviewSession
from progressive_rf64_limits_fixture import geometry, header, pcm
from progressive_v2_playback_conformance import forget_closed_readers


def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def wait(check, label, timeout=20):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        value=check()
        if value:return value
        time.sleep(.025)
    raise AssertionError(('Crash recovery deadline',label))


class Peer:
    def __init__(self, edition, player, profile, directory, reader_runner):
        self.edition=edition;self.player=str(Path(player).resolve());self.profile=Path(profile).resolve()
        self.directory=Path(directory);self.reader_runner=str(Path(reader_runner).resolve())
        self.process=None;self.logs=[];self.history=[];self.client=None
        if edition=='qt':
            self.profile.mkdir()
            (self.profile/'FoxicMPQt.ini').write_text('[profile]\nversion=1\n[integration]\nprompted=true\ncheckOnStartup=false\n[input]\nmpris=false\n[audio]\nmute=true\nvolume=0\n[playback]\nhardwareDecoding=false\nrememberPosition=false\n[video]\noutput=software\n[ui]\nosd=false\n',encoding='utf-8')
        else:
            raw=Path(self.player).with_suffix('.ini').read_bytes();settings=configparser.ConfigParser()
            settings.read_string(raw.decode('utf-16') if raw.startswith(b'\xff\xfe') else raw.decode('utf-8-sig'))
            assert settings.get('Settings','ContextMenuPrompted')=='1' and settings.get('Settings','ContextMenuEnabled')=='0'
        self.client=ProgressiveClient(self.player,str(self.profile) if edition=='qt' else None)
        if os.name!='nt':self.registry=Path(self.client.endpoint+'.readers')
        else:self.registry=Path(tempfile.gettempdir())/('FoxicMPq' if edition=='qt' else '')/(self.client.endpoint.rsplit('\\',1)[-1]+'.readers')

    def launch(self):
        assert self.process is None or self.process.poll() is not None
        log=(self.directory/('launch-%d.log'%len(self.history))).open('wb');self.logs.append(log)
        args=[self.player,'--profile',str(self.profile),'--no-activate'] if self.edition=='qt' else [self.player,'/new','/nofocus','/minimized']
        self.process=subprocess.Popen(args,cwd=Path(self.player).parent,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
        self.history.append(dict(pid=self.process.pid,reason='running',exitCode=None))
        def ready():
            assert self.process.poll() is None,('Test player exited during launch',self.process.returncode)
            try:reply=self.client.request(dict(version=1,action='capabilities'),timeout=.5)
            except (OSError,RuntimeError):return None
            assert reply['pid']==self.process.pid,('Unexpected endpoint owner',reply['pid'],self.process.pid)
            return reply
        return wait(ready,'owned endpoint after launch')

    def kill(self,reason):
        assert self.process is not None and self.process.poll() is None
        began=time.monotonic();self.process.kill();self.process.wait(timeout=5)
        elapsed=time.monotonic()-began;code=self.process.returncode
        assert code==(1 if os.name=='nt' else -9),(reason,code)
        self.history[-1].update(reason=reason,exitCode=code,terminationSeconds=elapsed)
        return dict(pid=self.process.pid,exitCode=code,terminationSeconds=elapsed)

    def native(self,action,**fields):
        p=subprocess.run([self.reader_runner,'--request',str(self.registry)],input=json.dumps(dict(version=1,action=action,**fields))+'\n',capture_output=True,text=True,timeout=5)
        assert p.returncode==0,(p.returncode,p.stderr)
        value=json.loads(p.stdout);assert value['ok'],value
        return value

    def cleanup(self):
        assert self.process is None or self.process.poll() is not None
        for log in self.logs:log.close()
        removed=[]
        # Only runtime paths derived from these random private profiles/copies.
        if self.registry.exists():
            for path in self.registry.glob('*.lease'):
                # Failure cleanup is separate from scenario assertions: all
                # owned children have already exited, verified by Popen.wait.
                assert len(path.stem)==32 and all(c in '0123456789abcdef' for c in path.stem)
                receipt=self.native('readerStatus',readerId=path.stem)['reader']
                assert receipt['released'] and receipt['reason'] in ('files-closed','process-exited','pid-reused'),receipt
                assert self.native('readerForget',readerId=path.stem)['forgotten']
            shutil.rmtree(self.registry);removed.append(str(self.registry))
        if self.edition=='qt':
            endpoint=self.client.endpoint.rsplit('\\',1)[-1] if os.name=='nt' else self.client.endpoint
            base=Path(tempfile.gettempdir())/'FoxicMPq'/endpoint if os.name=='nt' else Path(endpoint)
            for path in (base,Path(str(base)+'.lock'),Path(str(base).removesuffix('.progressive')),Path(str(base).removesuffix('.progressive')+'.lock')):
                if path.exists():path.unlink();removed.append(str(path))
        return removed


def exercise(peers,root,report):
    root=Path(root);events=[];rows=[];old_ids=set();began=time.monotonic();previews=[None,None];probe=None
    def event(phase,**fields):
        events.append(dict(phase=phase,seconds=time.monotonic()-began,**fields));(report/'events.json').write_text(json.dumps(events,indent=2),encoding='utf-8')
    def reject(client,action,code,**fields):
        try:client.request(dict(version=1,action=action,**fields))
        except ApiError as error:assert error.code==code,error.reply
        else:raise AssertionError(('Accepted stale command',action))
    try:
        for bits,sparse in ((16,False),(24,False),(16,True),(24,True)):
            phase='buffering' if sparse else 'playing';event('begin',bits=bits,sparse=sparse,crashPhase=phase)
            directory=root/('pcm%d-%s'%(bits,phase));directory.mkdir()
            g=geometry(bits,2,48000,20*48000);data=header(g)+pcm(g,0,20*48000*g['align'])
            head=g['offset']+4*48000*g['align'];ready=g['offset']+12*48000*g['align'];later=g['offset']+16*48000*g['align']
            path=directory/'crash.part';path.write_bytes(data[:head])
            value=dict(schemaVersion=2,sessionId=uuid.uuid4().hex,generation='1',revision='1',writer=dict(instanceId=uuid.uuid4().hex,state='downloading'),timeline=dict(timebase=dict(numerator=1,denominator=10000000),live=False,duration='200000000',timestampOffset='0'),resources=[dict(id='media',name=path.name,length=str(len(data)),ranges=[dict(offset='0',length=str(head))],state='downloading')],tracks=[dict(id='main',kind='audio',required=True,codecHint='pcm-s%dle'%bits,containerHint='rf64-'+('sparse-' if sparse else '')+'pcm%d'%bits,state='downloading',initializationReady=True,source=dict(resourceId='media'))])
            protected=protect_snapshot(value,directory)
            def readers(i):return peers[i].client.readers(value['sessionId'],1)
            def active(i):return [r for r in readers(i) if not r['released']]
            def status(i):
                s=previews[i].heartbeat();assert s.get('ok') and not s.get('error') and s['readerId']==previews[i].reader_id,s
                return s
            def playing(i,minimum=.25):
                s=status(i);return s if s['state']=='playing' and s['positionSeconds']>=minimum and int(s['decoderIoBytes']['audio'])>0 else None
            for i in range(2):
                previews[i]=V2PreviewSession(peers[i].client,str(directory),title='Crash recovery private test');previews[i].publish(protected)
                wait(lambda i=i:status(i)['state']=='buffering','initial buffering')
            with path.open('ab') as f:f.write(data[head:ready]);f.flush()
            value['revision']='2';value['resources'][0]['ranges'][0]['length']=str(ready);protected=protect_snapshot(value,directory,previous=protected)
            previews[1].publish(protected);survivor=wait(lambda:playing(1),'survivor actual playback')
            if phase=='playing':previews[0].publish(protected);before=wait(lambda:playing(0),'crashing player actual playback')
            else:before=status(0);assert before['state']=='buffering' and not before['complete'],before
            probe=ResourceProbe(peers[0].client);probe.start(protected,str(directory),[dict(resourceId='media',offset='0',length='16'),dict(resourceId='media',offset=str(g['offset']+18*48000*g['align']),length='16')],wait_milliseconds=10000)
            owned=wait(lambda:active(0) if len(active(0))==3 else None,'three owned receipts before crash')
            live=active(1);assert len(live)==2 and peers[0].process.pid!=peers[1].process.pid
            assert all(r['pid']==peers[0].process.pid and not r['filesClosed'] for r in owned)
            for receipt in owned:
                assert receipt['readerId'] not in old_ids;old_ids.add(receipt['readerId'])
            identity={(r['resources'][0]['device'],r['resources'][0]['fileId']) for r in owned+live};assert len(identity)==1
            old_group=previews[0].reader_id;old_probe=probe.probe_id
            native_live=peers[0].native('readerStatus',readerId=old_group)['reader'];assert not native_live['released'] and native_live['reason']=='lease-held'
            assert probe.status()['state']=='running'
            reject(peers[0].client,'readerForget','reader_in_use',readerId=old_group)
            before_sha=sha(path);event('kill-active',pid=peers[0].process.pid,crashPhase=phase)
            killed=peers[0].kill('intentional-active-crash');assert killed['terminationSeconds']<2
            previews[0]=None;probe=None
            failed_ipc=False
            try:peers[0].client.request(dict(version=1,action='capabilities'),timeout=.2)
            except (OSError,RuntimeError):failed_ipc=True
            assert failed_ipc,'Killed endpoint still answers'
            recovered=[peers[0].native('readerStatus',readerId=x['readerId'])['reader'] for x in owned]
            assert all(r['released'] and not r['filesClosed'] and r['reason'] in ('process-exited','pid-reused') for r in recovered),recovered
            assert {(r['readerId'],r['pid'],r['birth']) for r in recovered}=={(r['readerId'],r['pid'],r['birth']) for r in owned}
            assert len(active(1))==2 and all(r['pid']==peers[1].process.pid for r in active(1))
            assert sha(path)==before_sha and path.read_bytes()==data[:ready]
            # Writer keeps appending while the first endpoint is down.
            with path.open('ab') as f:f.write(data[ready:later]);f.flush()
            value['revision']='3';value['resources'][0]['ranges'][0]['length']=str(later);protected=protect_snapshot(value,directory,previous=protected)
            previews[1].publish(protected);advanced=wait(lambda:playing(1,survivor['positionSeconds']+.15),'survivor advances after other crash')
            event('restart-same-profile');peers[0].launch()
            rediscovered=[peers[0].client.reader_status(x['readerId']) for x in owned]
            assert all(r['released'] and r['reason'] in ('process-exited','pid-reused') for r in rediscovered)
            previews[0]=V2PreviewSession(peers[0].client,str(directory),title='Recovered after forced exit');previews[0].publish(protected)
            restarted=wait(lambda:playing(0),'actual playback after restart');new_id=previews[0].reader_id
            assert new_id not in {x['readerId'] for x in owned+live}
            current=peers[0].client.reader_status(new_id)
            assert (current['pid'],current['birth'])!=(owned[0]['pid'],owned[0]['birth'])
            assert {(rr['device'],rr['fileId']) for rr in current['resources']}==identity
            old_release=peers[0].client.release_reader(old_group,timeout=2);assert old_release['released']
            reject(peers[0].client,'cancel','reader_mismatch',sessionId=value['sessionId'],readerId=old_group)
            reject(peers[0].client,'resourceProbeStatus','unknown_probe',probeId=old_probe)
            assert playing(0) and playing(1)
            for receipt in owned:assert peers[0].client.forget_reader(receipt['readerId'])['forgotten']
            reject(peers[0].client,'readerStatus','unknown_reader',readerId=old_group)
            assert len(active(0))==2 and len(active(1))==2
            releases=[]
            for i in range(2):
                identifier=previews[i].reader_id;t=time.monotonic();peers[i].client.request(dict(version=1,action='readerRelease',readerId=identifier))
                receipt=wait(lambda i=i,identifier=identifier:(lambda r:r if r['released'] and r['filesClosed'] else None)(peers[i].client.reader_status(identifier)),'normal post-recovery filesClosed',2)
                elapsed=time.monotonic()-t;assert elapsed<2
                assert previews[i].close(timeout=2)['released'];previews[i]=None;forget_closed_readers(peers[i].client,protected);releases.append(dict(seconds=elapsed,receipt=receipt))
                if i==0:assert len(active(1))==2 and path.exists()
            assert all(not readers(i) for i in range(2))
            assert all(not peers[i].native('readers',sessionId=value['sessionId'],generation='1')['readers'] for i in range(2))
            assert path.read_bytes()==data[:later];digest=sha(path);path.unlink();directory.rmdir()
            rows.append(dict(bits=bits,sparse=sparse,crashPhase=phase,result='PASS',killed=killed,initialState=before['state'],initialPosition=before['positionSeconds'],survivorBefore=survivor['positionSeconds'],survivorAfter=advanced['positionSeconds'],restartedPosition=restarted['positionSeconds'],restartedDecoderIoBytes=restarted['decoderIoBytes']['audio'],oldReceipts=owned,osRecoveredReceipts=recovered,rediscoveredReceipts=rediscovered,newReceipt=current,oldGroupRelease=old_release,oldProbeId=old_probe,newReaderId=new_id,ipcFailureObserved=failed_ipc,sourceIdentityPreserved=True,sourceBeforeAppendSha256=before_sha,sourceAfterAppendSha256=digest,writerAppendedWhileEndpointDown=True,release=releases,sourceRemovedAfterAllReleased=True))
            (report/'progress.json').write_text(json.dumps(dict(completed=len(rows),rows=rows),indent=2),encoding='utf-8');event('case-complete')
        return dict(result='PASS',profiles=4,intentionalActiveCrashes=4,separateProcesses=True,crashModes=['playing','buffering'],persistedDeadReceipts=12,sourceIdentityPreserved=True,actualReopenedPlayback=True,elapsedSeconds=time.monotonic()-began,rows=rows)
    finally:
        if probe is not None:
            try:probe.cancel();probe.wait();probe.forget()
            except (OSError,RuntimeError):pass
        for preview in previews:
            if preview is not None:
                try:preview.close(timeout=10)
                except (OSError,RuntimeError):pass


def run(args):
    report=Path(args.report_directory).resolve();report.mkdir(parents=True,exist_ok=True)
    classic=args.edition=='classic';registration=None
    if classic:
        from windows.progressive_family_test import registration_snapshot
        registration=registration_snapshot();assert len(args.player)==2
    else:assert len(args.player)==1
    peers=[];result=None
    with tempfile.TemporaryDirectory(prefix='foxicmp-crash-recovery-') as directory:
        root=Path(directory)
        try:
            for i in range(2):
                logs=root/('peer-%d'%i);logs.mkdir()
                player=args.player[i] if classic else args.player[0];profile=Path(player).parent if classic else root/('profile-%d'%i)
                peer=Peer(args.edition,player,profile,logs,args.reader_runner);peers.append(peer);peer.launch()
            assert peers[0].client.endpoint!=peers[1].client.endpoint
            result=exercise(peers,root,report)
        finally:
            cleanup=[];errors=[]
            for peer in peers:
                try:
                    if peer.process is not None and peer.process.poll() is None:peer.kill('cleanup-owned-idle-process' if result is not None else 'cleanup-after-test-failure')
                except Exception:errors.append(traceback.format_exc())
            for peer in peers:
                # On failure retain a separate cleanup error instead of silently
                # declaring uncertain receipts released. Kill still targets Popen.
                try:cleanup.extend(peer.cleanup())
                except Exception:
                    errors.append(traceback.format_exc())
            for i,peer in enumerate(peers):
                dest=report/('peer-%d-logs'%i);dest.mkdir(exist_ok=True)
                for path in peer.directory.glob('*.log'):shutil.copy2(path,dest/path.name)
            if errors:(report/'cleanup-error.txt').write_text('\n'.join(errors),encoding='utf-8')
            assert not errors,errors
            if classic:assert registration_snapshot()==registration,'Installed shell registration changed'
            (report/'cleanup.json').write_text(json.dumps(dict(runtimePathsRemoved=cleanup,processHistory=[p.history for p in peers],allOwnedProcessesExited=all(p.process.poll() is not None for p in peers)),indent=2),encoding='utf-8')
    result['processHistory']=[p.history for p in peers];result['edition']=args.edition;result['temporaryWorkRemoved']=not root.exists();result['playerSha256']=[sha(p.player) for p in peers];result['readerRunnerSha256']=sha(args.reader_runner)
    (report/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--player',action='append',required=True);p.add_argument('--reader-runner',required=True);p.add_argument('--report-directory',required=True);a=p.parse_args()
    try:print(json.dumps(run(a)))
    except Exception:
        Path(a.report_directory).mkdir(parents=True,exist_ok=True);(Path(a.report_directory)/'error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
