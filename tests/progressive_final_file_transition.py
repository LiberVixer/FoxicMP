"""Pinned decoder handles through final rename, path substitution and EOF/reopen."""
import argparse
import copy
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
from progressive_player_crash_recovery import Peer,sha,wait
from progressive_writer_failures import media,append,save
from progressive_client import ApiError
from progressive_integrity import protect_snapshot
from progressive_resources import ResourceProbe
from progressive_v2 import V2PreviewSession
from progressive_v2_playback_conformance import forget_closed_readers


def exercise(peers,root,report,ffmpeg):
    rows=[];previews=[None,None];probe=None;events=[]
    def event(phase,**fields):events.append(dict(phase=phase,**fields));save(report/'events.json',events)
    try:
      for bits,sparse in ((16,False),(24,False),(16,True),(24,True)):
        directory=root/('pcm%d-%s'%(bits,'sparse' if sparse else 'prefix'))
        value,raw,g,ends,temporary=media(directory,bits,sparse);final=directory/('Итоговый PCM%d RF64.wav'%bits)
        protected=protect_snapshot(value,directory);event('begin',bits=bits,sparse=sparse)
        for i in range(2):
            previews[i]=V2PreviewSession(peers[i].client,str(directory),title='Final-file transition');previews[i].publish(protected)
            wait(lambda i=i:previews[i].heartbeat()['state']=='buffering','initial head')
        append(temporary,raw[ends[4]:ends[12]]);value['revision']='2';value['resources'][0]['ranges'][0]['length']=str(ends[12]);protected=protect_snapshot(value,directory,previous=protected)
        for p in previews:p.publish(protected)
        def status(i):
            s=previews[i].heartbeat();assert s.get('ok') and not s.get('error') and s['readerId']==previews[i].reader_id,s;return s
        def playing(i,minimum=.25,complete=None):
            s=status(i)
            return s if s['state']=='playing' and s['positionSeconds']>=minimum and int(s['decoderIoBytes']['audio'])>0 and (complete is None or s.get('complete')==complete) else None
        before=[wait(lambda i=i:playing(i,complete=False),'incomplete actual playback') for i in range(2)]
        readers=[p.reader_id for p in previews];receipts=[peers[i].client.reader_status(readers[i]) for i in range(2)]
        bind=lambda rs:{(x['device'],x['fileId']) for rr in rs for x in rr['resources']}
        original_binding=bind(receipts);assert len(original_binding)==1 and all(not rr['released'] for rr in receipts)
        # Prepare all final digests while the original path still names the file.
        # Publish complete only AFTER rename; active readers must use their pin.
        append(temporary,raw[ends[12]:]);value['revision']='3';value['writer']['state']='complete';value['resources'][0].update(state='complete',ranges=[dict(offset='0',length=str(len(raw)))]);value['tracks'][0]['state']='complete'
        complete=protect_snapshot(value,directory,previous=protected);original_stat=temporary.stat();assert not final.exists()
        temporary.rename(final);assert not temporary.exists() and sha(final)==hashlib.sha256(raw).hexdigest()
        assert (final.stat().st_dev,final.stat().st_ino)==(original_stat.st_dev,original_stat.st_ino)
        poison=raw[:g['offset']]+bytes(x^0x5a for x in raw[g['offset']:]);temporary.write_bytes(poison);poison_sha=sha(temporary)
        assert (temporary.stat().st_dev,temporary.stat().st_ino)!=(original_stat.st_dev,original_stat.st_ino)
        # Changing the descriptor name is forbidden even though the OS rename works.
        changed=copy.deepcopy(protected);changed['revision']='3';changed['resources'][0]['name']=final.name
        rejected=[]
        for i in range(2):
            try:peers[i].client.update(changed,session_directory=directory,reader_id=readers[i])
            except ApiError as error:rejected.append(error.reply)
            else:raise AssertionError('Resource name changed inside an active generation')
            assert status(i)['readerId']==readers[i] and not peers[i].client.reader_status(readers[i])['released']
        for p in previews:p.publish(complete)
        after=[]
        for i in range(2):
            s=wait(lambda i=i:playing(i,before[i]['positionSeconds']+.15,True),'complete after rename')
            assert int(s['decoderIoBytes']['audio'])>int(before[i]['decoderIoBytes']['audio']),s
            after.append(s)
        assert bind([peers[i].client.reader_status(readers[i]) for i in range(2)])==original_binding
        # A new source opening the old path sees the substituted file. It must
        # reject its payload against original hashes, not rebind existing readers.
        probe=ResourceProbe(peers[0].client);probe.start(complete,str(directory),[dict(resourceId='media',offset='0',length='16')],wait_milliseconds=2000)
        substituted=probe.wait(timeout=10);assert substituted['state']=='error' and substituted['errorCode']=='integrity-mismatch' and substituted['released'] and not substituted['allReadable'],substituted
        substituted_receipts=[peers[0].client.reader_status(x) for x in substituted['readerIds']]
        assert substituted_receipts and all(x['released'] and x['filesClosed'] for x in substituted_receipts) and bind(substituted_receipts)!=original_binding
        assert probe.forget()['state']=='forgotten';probe=None
        for i in range(2):assert not peers[i].client.reader_status(readers[i])['released']
        event('renamed-and-published-complete',bits=bits,sparse=sparse)
        # No injected seek/speed change: both genuine decoders reach natural EOF.
        finished=[]
        for i in range(2):
            s=wait(lambda i=i:(lambda v:v if v['state']=='finished' and v.get('released') else None)(status(i)),'natural EOF after rename',30)
            receipt=peers[i].client.reader_status(readers[i]);assert receipt['released'] and receipt['filesClosed'] and receipt['reason']=='files-closed'
            finished.append(dict(status=s,receipt=receipt));assert previews[i].close(timeout=2)['released'];previews[i]=None;forget_closed_readers(peers[i].client,complete)
        assert all(not p.client.readers(value['sessionId'],1) for p in peers)
        assert sha(final)==hashlib.sha256(raw).hexdigest() and sha(temporary)==poison_sha
        decoded=subprocess.run([ffmpeg,'-v','error','-nostdin','-i',str(final),'-map','0:a:0','-c:a','pcm_s%dle'%bits,'-f','s%dle'%bits,'pipe:1'],capture_output=True,timeout=20)
        assert decoded.returncode==0,decoded.stderr.decode(errors='replace');assert decoded.stdout==raw[g['offset']:]
        # New readers use the final pathname in a new generation, with fresh SHA.
        reopened=copy.deepcopy(complete);reopened.update(generation='2',revision='1');reopened['resources'][0]['name']=final.name;reopened['resources'][0].pop('integrity');reopened=protect_snapshot(reopened,directory)
        opened=[];released=[]
        for i in range(2):
            previews[i]=V2PreviewSession(peers[i].client,str(directory),title=final.name);previews[i].publish(reopened)
            s=wait(lambda i=i:playing(i,complete=True),'completed final pathname playback')
            receipt=peers[i].client.reader_status(previews[i].reader_id);assert bind([receipt])==original_binding and receipt['readerId'] not in readers
            opened.append(dict(status=s,receipt=receipt))
        for i in range(2):
            t=time.monotonic();closed=previews[i].close(timeout=2);duration=time.monotonic()-t;assert closed['released'] and closed['reader']['filesClosed'] and duration<2
            released.append(dict(seconds=duration,receipt=closed['reader']));previews[i]=None;forget_closed_readers(peers[i].client,reopened)
            if i==0:assert not peers[1].client.reader_status(opened[1]['receipt']['readerId'])['released']
        assert all(not p.client.readers(value['sessionId'],gen) for p in peers for gen in (1,2))
        assert sha(final)==hashlib.sha256(raw).hexdigest() and sha(temporary)==poison_sha
        final.unlink();temporary.unlink();directory.rmdir()
        rows.append(dict(result='PASS',bits=bits,sparse=sparse,initialReaders=readers,initialReceipts=receipts,beforeRename=before,descriptorNameChangeRejected=rejected,afterRenameComplete=after,substitutionProbe=substituted,substitutionReceipts=substituted_receipts,naturalEof=finished,finalReopened=opened,finalRelease=released,finalSha256=hashlib.sha256(raw).hexdigest(),poisonSha256=poison_sha,independentPcmSha256=hashlib.sha256(decoded.stdout).hexdigest(),independentPcmBytes=len(decoded.stdout),ffmpegExitCode=decoded.returncode,samePhysicalFile=True,originalNameImmutable=True,oldPathReplacementDidNotRebind=True,filesRemovedOnlyAfterAllReadersClosed=True))
        save(report/'progress.json',dict(completed=len(rows),rows=rows));event('case-complete',bits=bits,sparse=sparse)
      return dict(result='PASS',profiles=4,separateActualPlayerProcesses=True,activeRenameReaders=8,completedReopenReaders=8,naturalEofReaders=8,rows=rows)
    finally:
        if probe is not None:
            try:probe.cancel();probe.wait();probe.forget()
            except (OSError,RuntimeError):pass
        for p in previews:
            if p is not None:
                try:p.close(timeout=10)
                except (OSError,RuntimeError):pass


def run(args):
    report=Path(args.report_directory).resolve();report.mkdir(parents=True,exist_ok=True);peers=[];result=None;registration=None
    ffmpeg=shutil.which(args.ffmpeg) or str(Path(args.ffmpeg).resolve());assert Path(ffmpeg).is_file()
    if args.edition=='classic':
        from windows.progressive_family_test import registration_snapshot
        registration=registration_snapshot();assert len(args.player)==2
    else:assert len(args.player)==1
    with tempfile.TemporaryDirectory(prefix='foxicmp-final-file-') as directory:
        root=Path(directory)
        try:
            for i in range(2):
                logs=root/('peer-%d'%i);logs.mkdir();player=args.player[i] if args.edition=='classic' else args.player[0];profile=Path(player).parent if args.edition=='classic' else root/('profile-%d'%i)
                peer=Peer(args.edition,player,profile,logs,args.reader_runner);peers.append(peer);peer.launch()
            assert peers[0].client.endpoint!=peers[1].client.endpoint
            result=exercise(peers,root,report,ffmpeg)
        finally:
            errors=[];removed=[]
            for peer in peers:
                try:
                    if peer.process is not None and peer.process.poll() is None:peer.kill('cleanup-owned-idle-process' if result is not None else 'cleanup-after-test-failure')
                except Exception:errors.append(traceback.format_exc())
            for peer in peers:
                try:removed.extend(peer.cleanup())
                except Exception:errors.append(traceback.format_exc())
            for i,peer in enumerate(peers):
                dst=report/('peer-%d'%i);dst.mkdir(exist_ok=True)
                for path in peer.directory.glob('*.log'):shutil.copy2(path,dst/path.name)
            save(report/'cleanup.json',dict(runtimePathsRemoved=removed,processHistory=[p.history for p in peers],allOwnedProcessesExited=all(p.process.poll() is not None for p in peers)))
            if errors:(report/'cleanup-error.txt').write_text('\n'.join(errors),encoding='utf-8')
            assert not errors,errors
            if registration is not None:assert registration_snapshot()==registration,'Installed shell registration changed'
    result.update(edition=args.edition,processHistory=[p.history for p in peers],temporaryWorkRemoved=not root.exists(),playerSha256=[sha(p.player) for p in peers],readerRunnerSha256=sha(args.reader_runner),ffmpegSha256=sha(ffmpeg))
    save(report/'result.json',result);return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--player',action='append',required=True);p.add_argument('--reader-runner',required=True);p.add_argument('--ffmpeg',default='ffmpeg');p.add_argument('--report-directory',required=True);a=p.parse_args()
    try:print(json.dumps(run(a)))
    except Exception:
        Path(a.report_directory).mkdir(parents=True,exist_ok=True);(Path(a.report_directory)/'error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
