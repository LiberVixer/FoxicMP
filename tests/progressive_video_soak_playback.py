"""Real-time growing H.264/AAC fMP4 and MPEG-TS soak in separate players."""
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
import uuid
from progressive_player_crash_recovery import Peer,sha,wait
from progressive_writer_failures import save
from progressive_rf64_limits_fixture import geometry,header,pcm
from progressive_integrity import protect_snapshot
from progressive_v2 import V2PreviewSession
from progressive_v2_playback_conformance import forget_closed_readers
from progressive_process_metrics import sample

BUDGET=dict(steadyHandlesGrowth=8,steadyThreadsGrowth=2,rssPrivateGrowthBytes=256*1024**2,oneCoreCpuRatio=1.0,sessionBytes=512*1024**2,maxApiSeconds=2,stopReleaseSeconds=2)


class Stream:
    def __init__(self,peer,root,container,seconds):
        from progressive_mux_mp4_fixture import fixture as mp4_fixture
        from progressive_growing_ts_fixture import fixture as ts_fixture
        self.peer=peer;self.root=root;self.container=container;self.written=0;self.inline=None;self.publications=0;self.digest=hashlib.sha256();self.max_request_bytes=0;self.max_request_seconds=0.;self.original_request=peer.client.request
        if container=='fmp4':
            self.model,self.raw,original,groups,scales=mp4_fixture(root,seconds=seconds+180)
            self.times=[float((int(p['pts'])-int(groups[0][0]['pts']))*scales[0]) for p in groups[0]]
        else:
            self.model,self.raw,original,groups=ts_fixture(root,seconds=seconds+180)
            self.times=[(int(p['pts'])-int(groups[0][0]['pts']))/90000 for p in groups[0]]
        # Decode-order PTS can be non-monotonic with B-frames: preserve the
        # existing fixture's first qualifying packet, rather than bisect PTS.
        self.positions=[int(p['pos']) for p in groups[0]];self.path=root/self.model['resources'][0]['name'];self.path.touch();self.original_sha=sha(original);self.original=original
        self.mode='growing-muxed-'+('fmp4' if container=='fmp4' else 'mpegts')+'-h264-aac-lc'
        def request(message,timeout=5):
            n=len(json.dumps(message,ensure_ascii=False,separators=(',',':')).encode())+1;assert n<=65536
            self.max_request_bytes=max(self.max_request_bytes,n);t=time.monotonic();answer=self.original_request(message,timeout);self.max_request_seconds=max(self.max_request_seconds,time.monotonic()-t);return answer
        peer.client.request=request
        self.preview=V2PreviewSession(peer.client,str(root),title='Private real-time video soak '+container)
    def publish(self,seconds=None,paused=False):
        if seconds is not None:
            end=next(pos-97 for t,pos in zip(self.times,self.positions) if t>=seconds);assert self.written<=end<len(self.raw)
            if end>self.written:
                with self.path.open('ab') as f:
                    data=self.raw[self.written:end];assert f.write(data)==len(data);f.flush();self.digest.update(data);self.written=end
        self.publications+=1;self.model['revision']=str(self.publications);state='paused' if paused else 'downloading';self.model['writer']['state']=state;self.model['resources'][0].update(state=state,ranges=[dict(offset='0',length=str(self.written))])
        for track in self.model['tracks']:track['state']=state
        self.inline=protect_snapshot(self.model,self.root,previous=self.inline,block_bytes=4*1024**2)
        return self.preview.publish(self.inline)
    def status(self):
        s=self.preview.heartbeat();assert s.get('ok') and s['readerId']==self.preview.reader_id and s['active'] and not s['complete'] and not s.get('released') and not s.get('error') and s['connected'],s
        assert s['state'] in ('playing','buffering') and s['mediaMode']==self.mode,s
        assert abs(s.get('controls',{}).get('speed',1)-1)<.01,s
        return s
    def disk(self):
        files=list(self.root.iterdir());return dict(mediaBytes=self.path.stat().st_size,indexBytes=sum(p.stat().st_size for p in files if p.suffix=='.idx'),indexFiles=sum(p.suffix=='.idx' for p in files),files=len(files),fixtureBytes=self.original.stat().st_size)


def exercise(peers,root,report,seconds):
    full=seconds>=600;assert 8<=seconds<=600
    assert shutil.disk_usage(root).free>1024**3,'Need1GiB free for private bounded fixture'
    streams=[];samples=[];pause_events=[];transitions=[]
    def event(phase,**fields):transitions.append(dict(phase=phase,**fields));save(report/'events.json',transitions)
    try:
        for peer,container in ((peers[0],'fmp4'),(peers[1],'mpegts')):
            streams.append(Stream(peer,root/container,container,seconds))
        # Finish both expensive fixture encodes before either playback starts.
        for stream in streams:
            stream.publish(4)
            wait(lambda:stream.status()['state']=='buffering','short prefix')
            stream.publish(16)
            wait(lambda:(lambda s:s if s['state']=='playing' and s['positionSeconds']>=.25 and all(int(s['decoderIoBytes'][kind])>0 for kind in ('video','audio')) else None)(stream.status()),'initial decoded playback')
        readers=[s.preview.reader_id for s in streams];pids=[p.process.pid for p in peers];assert pids[0]!=pids[1]
        initial=[peers[i].client.reader_status(readers[i]) for i in range(2)];assert all(not x['released'] and x['pid']==pids[i] for i,x in enumerate(initial))
        start=time.monotonic();next_publish=start;next_sample=start;pause_end=None;paused=False;pause_index=0;steps=(.35,.75,1.1,.45);iteration=0;pause_buffered=[False,False];resume_targets=None;resumes=[];max_gap=0.;last_tick=start
        plan=[(30,10),(100,30),(210,60)] if full else [(2,1),(6,2),(9,1)]
        event('started',seconds=seconds,acceptance=full,pids=pids,readers=readers,budgets=BUDGET)
        while True:
            now=time.monotonic();elapsed=now-start
            if elapsed>=seconds:break
            max_gap=max(max_gap,now-last_tick);last_tick=now;states=[s.status() for s in streams]
            assert all(p.process.poll() is None for p in peers)
            if paused:
                for i,s in enumerate(states):pause_buffered[i]|=s['state']=='buffering'
                if now>=pause_end:
                    duration=now-pause_events[-1]['startedMonotonic'];pause_events[-1].update(actualSeconds=duration,bufferingObserved=pause_buffered)
                    assert duration>=pause_events[-1]['requestedSeconds']
                    for stream,state in zip(streams,states):stream.publish(2*(int(state['positionSeconds'])//2)+16,False)
                    resumes.append(dict(atSeconds=elapsed,positions=[s['positionSeconds'] for s in states],firstPlayingSeconds=[None,None]))
                    resume_targets=[(now,s['positionSeconds']) for s in states];paused=False;pause_index+=1;next_publish=now+steps[iteration%4]
                    event('writer-resumed',seconds=elapsed,pause=pause_events[-1])
            elif pause_index<len(plan) and elapsed>=plan[pause_index][0]:
                duration=plan[pause_index][1];paused=True;pause_end=now+duration;pause_buffered=[False,False]
                for stream in streams:stream.publish(paused=True)
                pause_events.append(dict(requestedSeconds=duration,startedSeconds=elapsed,startedMonotonic=now))
                event('writer-paused',seconds=elapsed,duration=duration)
            elif now>=next_publish:
                for stream,state in zip(streams,states):stream.publish(2*(int(state['positionSeconds'])//2)+16,False)
                iteration+=1;next_publish=time.monotonic()+steps[iteration%4]
            if resume_targets:
                for i,s in enumerate(states):
                    if resumes[-1]['firstPlayingSeconds'][i] is None and s['state']=='playing' and s['positionSeconds']>=resume_targets[i][1]+.15:resumes[-1]['firstPlayingSeconds'][i]=now-resume_targets[i][0]
                if all(x is not None for x in resumes[-1]['firstPlayingSeconds']):resume_targets=None
            if now>=next_sample:
                measured=[]
                for i,stream in enumerate(streams):
                    receipt=peers[i].client.reader_status(readers[i]);assert not receipt['released'] and not receipt['filesClosed']
                    disk=stream.disk();assert disk['mediaBytes']+disk['indexBytes']<BUDGET['sessionBytes']
                    s=states[i];assert s['positionSeconds']<=elapsed+20
                    measured.append(dict(container=stream.container,metrics=sample(pids[i]),disk=disk,state=s['state'],connected=s['connected'],positionSeconds=s['positionSeconds'],availableSeconds=s['availableSeconds'],decoderIoBytes={k:int(s['decoderIoBytes'][k]) for k in ('video','audio')},physicalReadMaxEndBytes=None,publishedBytes=stream.written,revision=s['revision']))
                samples.append(dict(seconds=elapsed,writerPaused=paused,peers=measured));save(report/'measurements.json',dict(secondsRequested=seconds,elapsedSeconds=elapsed,samples=samples,pauses=pause_events,resumes=resumes))
                save(report/'progress.json',dict(elapsedSeconds=elapsed,totalSeconds=seconds,samples=len(samples),paused=paused,last=samples[-1]));next_sample=now+(5 if full else 1)
            time.sleep(.1)
        elapsed=time.monotonic()-start;assert elapsed>=seconds
        final_states=[s.status() for s in streams];assert pause_index==len(plan) and not paused and all(x['actualSeconds']>=x['requestedSeconds'] for x in pause_events)
        if full:
            assert len(samples)>=110 and all(all(x['bufferingObserved']) for x in pause_events if x['requestedSeconds']>=30)
            for s in final_states:assert s['positionSeconds']>=seconds*.7 and s['state']=='playing' and all(int(s['decoderIoBytes'][k])>0 for k in ('video','audio')),s
            for i in range(2):
                early=[x['peers'][i]['metrics'] for x in samples if 60<=x['seconds']<=90];late=[x['peers'][i]['metrics'] for x in samples if x['seconds']>=seconds-60];assert early and late
                assert max(x['handles'] for x in late)<=max(x['handles'] for x in early)+BUDGET['steadyHandlesGrowth']
                assert max(x['threads'] for x in late)<=max(x['threads'] for x in early)+BUDGET['steadyThreadsGrowth']
                for key in ('rssBytes','privateBytes'):assert max(x[key] for x in late)<=max(x[key] for x in early)+BUDGET['rssPrivateGrowthBytes']
                first=next(x for x in samples if x['seconds']>=seconds-60);last=samples[-1];ratio=(last['peers'][i]['metrics']['cpuSeconds']-first['peers'][i]['metrics']['cpuSeconds'])/(last['seconds']-first['seconds']);assert ratio<=BUDGET['oneCoreCpuRatio']
        integrity=[];releases=[]
        for stream in streams:
            with stream.path.open('rb') as f:actual=hashlib.file_digest(f,'sha256').hexdigest()
            assert actual==stream.digest.hexdigest() and stream.path.stat().st_size==stream.written<len(stream.raw)
            assert stream.max_request_seconds<BUDGET['maxApiSeconds']
            assert sha(stream.original)==stream.original_sha
            assert stream.path.read_bytes()==stream.raw[:stream.written]
            integrity.append(dict(container=stream.container,originalSha256=stream.original_sha,sourcePrefixMatches=True,sha256=actual,writtenBytes=stream.written,declaredBytes=len(stream.raw),disk=stream.disk(),publications=stream.publications,integrityRecords=len(stream.inline['resources'][0]['integrity']['blocks']),maxApiRequestBytes=stream.max_request_bytes,maxApiSeconds=stream.max_request_seconds))
        for i,stream in enumerate(streams):
            t=time.monotonic();peers[i].client.request(dict(version=1,action='readerRelease',readerId=readers[i]))
            receipt=wait(lambda i=i:(lambda r:r if r['released'] and r['filesClosed'] else None)(peers[i].client.reader_status(readers[i])),'soak Stop filesClosed',2);duration=time.monotonic()-t;assert duration<2
            assert stream.preview.close(timeout=2)['released'];forget_closed_readers(peers[i].client,stream.inline);assert not peers[i].client.readers(stream.model['sessionId'],1)
            releases.append(dict(seconds=duration,receipt=receipt));shutil.rmtree(stream.root)
        result=dict(result='PASS',physicalReadAuditAvailable=False,soakAcceptance=full,secondsRequested=seconds,elapsedSeconds=elapsed,realTime=True,readers=2,profiles=[dict(container='fmp4',video='h264',audio='aac-lc'),dict(container='mpegts',video='h264',audio='aac-lc')],readerIds=readers,initialReceipts=initial,budgets=BUDGET,pauses=pause_events,resumes=resumes,maxObserverGapSeconds=max_gap,samples=samples,finalStates=final_states,integrity=integrity,release=releases,sourceDirectoriesRemoved=all(not s.root.exists() for s in streams))
        save(report/'measurements.json',result);return result
    finally:
        for stream in streams:
            try:
                if not stream.preview.closed:stream.preview.close(timeout=10)
            except (OSError,RuntimeError):pass
            stream.peer.client.request=stream.original_request


def run(args):
    report=Path(args.report_directory).resolve();report.mkdir(parents=True,exist_ok=True);peers=[];result=None;registration=None
    if args.edition=='classic':
        from windows.progressive_family_test import registration_snapshot
        registration=registration_snapshot();assert len(args.player)==2
    else:assert len(args.player)==1
    with tempfile.TemporaryDirectory(prefix='foxicmp-video-soak-') as directory:
        root=Path(directory)
        try:
            for i in range(2):
                logs=root/('peer-%d'%i);logs.mkdir();player=args.player[i] if args.edition=='classic' else args.player[0];profile=Path(player).parent if args.edition=='classic' else root/('profile-%d'%i)
                peer=Peer(args.edition,player,profile,logs,args.reader_runner);peers.append(peer);peer.launch()
            result=exercise(peers,root,report,args.seconds)
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
                for p in peer.directory.glob('*.log'):shutil.copy2(p,dst/p.name)
            save(report/'cleanup.json',dict(runtimePathsRemoved=removed,processHistory=[p.history for p in peers],allOwnedProcessesExited=all(p.process.poll() is not None for p in peers)))
            if errors:(report/'cleanup-error.txt').write_text('\n'.join(errors),encoding='utf-8')
            assert not errors,errors
            if registration is not None:assert registration_snapshot()==registration,'Installed shell registration changed'
    result.update(edition=args.edition,processHistory=[p.history for p in peers],temporaryWorkRemoved=not root.exists(),playerSha256=[sha(p.player) for p in peers],readerRunnerSha256=sha(args.reader_runner))
    save(report/'result.json',result);return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--player',action='append',required=True);p.add_argument('--reader-runner',required=True);p.add_argument('--report-directory',required=True);p.add_argument('--seconds',type=int,default=int(os.environ.get('FOXICMP_VIDEO_SOAK_SECONDS','600')));a=p.parse_args()
    try:print(json.dumps(run(a)))
    except Exception:
        Path(a.report_directory).mkdir(parents=True,exist_ok=True);(Path(a.report_directory)/'error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
