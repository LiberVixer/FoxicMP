"""Public continuous SDK: owned player close/crash, worker exit and cache recovery.

No user player, downloader, desktop session or installed profile is controlled.
Linux normal close sends WM_DELETE_WINDOW only on the private test X11 display.
Windows normal close sends WM_CLOSE only to a window of the owned Popen PID.
"""
import argparse
import copy
import ctypes
import json
import os
from pathlib import Path
import time
import traceback

from progressive_continuous_api_test import run
from progressive_continuous_heartbeat_conformance import WorkerExit
from progressive_player_crash_recovery import ApiError, sha, wait
from progressive_stream_remux_worker_test import fixture, probe
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_continuous import ContinuousRemux


def normal_close(peer):
    assert peer.process.poll() is None
    pid=peer.process.pid
    if os.name=='nt':
        from ctypes import wintypes as w
        user=ctypes.WinDLL('user32',use_last_error=True)
        callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
        user.EnumWindows.argtypes=[callback,w.LPARAM]
        user.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
        user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
        user.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
        found=[]
        @callback
        def visit(window,unused):
            owner=w.DWORD();user.GetWindowThreadProcessId(window,ctypes.byref(owner))
            name=ctypes.create_unicode_buffer(256);user.GetClassNameW(window,name,256)
            if owner.value==pid and (name.value=='FoxicMP' or name.value.endswith('QWindowIcon')):
                found.append(window)
            return True
        assert user.EnumWindows(visit,0)
        assert len(found)==1,('Owned main window',pid,found)
        began=time.monotonic()
        assert user.PostMessageW(found[0],0x10,0,0),ctypes.get_last_error()
        method='WM_CLOSE-owned-PID'
    else:
        # run_tests.sh owns this server. Refuse an inherited desktop display.
        assert os.environ.get('FOXICMP_TEST_PRIVATE_BUS')=='1'
        x=ctypes.CDLL('libX11.so.6')
        U=ctypes.c_ulong;P=ctypes.c_void_p;I=ctypes.c_int
        x.XOpenDisplay.argtypes=[ctypes.c_char_p];x.XOpenDisplay.restype=P
        x.XDefaultRootWindow.argtypes=[P];x.XDefaultRootWindow.restype=U
        x.XInternAtom.argtypes=[P,ctypes.c_char_p,I];x.XInternAtom.restype=U
        x.XQueryTree.argtypes=[P,U,ctypes.POINTER(U),ctypes.POINTER(U),ctypes.POINTER(ctypes.POINTER(U)),ctypes.POINTER(ctypes.c_uint)]
        x.XGetWindowProperty.argtypes=[P,U,U,ctypes.c_long,ctypes.c_long,I,U,ctypes.POINTER(U),ctypes.POINTER(I),ctypes.POINTER(U),ctypes.POINTER(U),ctypes.POINTER(P)]
        x.XFree.argtypes=[P];x.XCloseDisplay.argtypes=[P]
        x.XFetchName.argtypes=[P,U,ctypes.POINTER(P)]
        display=x.XOpenDisplay(None);assert display
        class Data(ctypes.Union):_fields_=[('b',ctypes.c_char*20),('s',ctypes.c_short*10),('l',ctypes.c_long*5)]
        class Message(ctypes.Structure):_fields_=[('type',I),('serial',U),('send',I),('display',P),('window',U),('message',U),('format',I),('data',Data)]
        class Event(ctypes.Union):_fields_=[('client',Message),('pad',ctypes.c_long*24)]
        x.XSendEvent.argtypes=[P,U,I,ctypes.c_long,ctypes.POINTER(Event)];x.XFlush.argtypes=[P]
        try:
            protocols=x.XInternAtom(display,b'WM_PROTOCOLS',0);delete=x.XInternAtom(display,b'WM_DELETE_WINDOW',0);pid_atom=x.XInternAtom(display,b'_NET_WM_PID',0)
            def property(window,atom):
                actual=U();fmt=I();count=U();remaining=U();data=P()
                assert x.XGetWindowProperty(display,window,atom,0,1024,0,0,ctypes.byref(actual),ctypes.byref(fmt),ctypes.byref(count),ctypes.byref(remaining),ctypes.byref(data))==0
                try:return list(ctypes.cast(data,ctypes.POINTER(U))[:count.value]) if fmt.value==32 and data else []
                finally:
                    if data:x.XFree(data)
            root=U();parent=U();children=ctypes.POINTER(U)();count=ctypes.c_uint()
            assert x.XQueryTree(display,x.XDefaultRootWindow(display),ctypes.byref(root),ctypes.byref(parent),ctypes.byref(children),ctypes.byref(count))
            def named(window):
                name=P();x.XFetchName(display,window,ctypes.byref(name))
                try:return bool(name and b'FoxiqMP' in ctypes.string_at(name))
                finally:
                    if name:x.XFree(name)
            # libmpv can create a separate renderer helper window with this PID.
            try:found=[window for window in children[:count.value] if property(window,pid_atom)==[pid] and delete in property(window,protocols) and named(window)]
            finally:
                if children:x.XFree(children)
            assert len(found)==1,('Owned private X11 window',pid,found)
            event=Event();event.client=Message(33,0,1,display,found[0],protocols,32,Data());event.client.data.l[0]=delete
            began=time.monotonic();assert x.XSendEvent(display,found[0],0,0,ctypes.byref(event));x.XFlush(display)
            method='WM_DELETE_WINDOW-private-X11-owned-PID'
        finally:x.XCloseDisplay(display)
    peer.process.wait(timeout=2)
    seconds=time.monotonic()-began
    assert peer.process.returncode==0 and seconds<2,(peer.process.returncode,seconds)
    peer.history[-1].update(reason='normal-continuous-window-close',exitCode=0,terminationSeconds=seconds)
    return dict(pid=pid,exitCode=0,terminationSeconds=seconds,method=method)


def exercise(peers,root,report,require_clean=False):
    rows=[];active=[None,None];watches=[]
    raw_path=fixture(root,'reference');raw=raw_path.read_bytes();packets=probe(raw_path)['packets']
    cuts={s:int(next(p['pos'] for p in packets if float(p['pts_time'])>=s))+13 for s in (12,20)}
    def model_for(name,prefix,complete=False):
        directory=root/name;directory.mkdir();path=directory/'input.part'
        model=snapshot(path,raw,complete=complete,prefix=prefix);model['timeline']['duration']='240000000';path.write_bytes(raw[:prefix])
        return directory,path,model
    def prepared(sdk):
        def poll():
            s=sdk.status();assert not s['errorCode'],s
            if s['playbackReady']:return s
            sdk.heartbeat();return None
        return wait(poll,'shutdown-suite ready')
    def playing(sdk):
        def poll():
            s=sdk.playback_status();assert s['state'] not in ('error','closed','finished'),s
            return s if s['state']=='playing' and s['positionSeconds']>.2 and s['decoderReaders'] else None
        return wait(poll,'shutdown-suite actual playback')
    def closed(sdk):
        p=sdk.close(timeout=2);assert p['released'] and p['sourceFilesClosed'],p
        f=sdk.forget(timeout=2);assert f['forgotten'],f
        return dict(proof=p,forgotten=f)
    def reject(client,action,code,**fields):
        try:client.request(dict(version=1,action=action,**fields))
        except ApiError as e:assert e.code==code,e.reply
        else:raise AssertionError(('Stale event accepted',action))
    try:
        other_dir,other_path,other_model=model_for('independent',len(raw),True)
        other=active[1]=ContinuousRemux(peers[1].client,str(other_dir));other.prepare(other_model,'clip');prepared(other)
        wait(lambda:other.status()['outputComplete'],'independent completed output')
        other.play(title='Independent shutdown control');other_initial=playing(other);other.control('pause')
        for kind in ('normal','crash'):
            for phase in ('preparation','playing','paused','buffering'):
                name=kind+'-'+phase
                (report/'current.json').write_text(json.dumps(dict(kind=kind,phase=phase))+'\n')
                directory,path,model=model_for(name,4 if phase=='preparation' else cuts[12])
                before=sha(path);sdk=active[0]=ContinuousRemux(peers[0].client,str(directory));sdk.prepare(model,'clip')
                if phase=='preparation':
                    initial=wait(lambda:(lambda s:s if s['producerCode']=='waiting' else None)(sdk.status()),'preparation before exit')
                    assert initial['maximumReadEnd']=='4' and not initial['playbackReady']
                    actual=None
                else:
                    prepared(sdk);sdk.play(title='Shutdown '+name);actual=playing(sdk)
                    if phase=='paused':
                        sdk.control('pause');actual=wait(lambda:(lambda s:s if s['state']=='paused' else None)(sdk.playback_status()),'paused before exit')
                    if phase=='buffering':
                        def starving():
                            sdk.heartbeat();s=sdk.playback_status();assert s['state'] not in ('finished','closed','error'),s
                            return s if s['state']=='buffering' and s['positionSeconds']>.2 and s['decoderReaders'] else None
                        actual=wait(starving,'real tail starvation before exit',20)
                    initial=sdk.status();assert not initial['outputComplete']
                watcher=WorkerExit(initial['workerId']);watches.append(watcher)
                old_reader_ids=initial['readerIds'];assert len(old_reader_ids)>=(1 if phase=='preparation' else 2),initial
                old_job=sdk.job_id;old_playback=sdk.playback_id
                cache=Path(str(peers[0].registry)+'.continuous-cache');old_cache={p.name for p in cache.glob('remux-*')};assert len(old_cache)==1,old_cache
                other.control('seek',seconds=0);other.control('play');control_before=playing(other)
                termination=normal_close(peers[0]) if kind=='normal' else peers[0].kill('continuous-player-crash-'+phase)
                assert termination['terminationSeconds']<2,termination
                active[0]=None
                began=time.monotonic();wait(watcher.exited,'owned continuous worker exits after parent',2);worker_seconds=time.monotonic()-began;worker=watcher.proof()
                assert sha(path)==before,'Player changed partial download'
                def independent_advances():
                    s=other.playback_status();assert s['state']=='playing' and s['playbackId']==other_initial['playbackId'] and s['readerIds']==other_initial['readerIds'],s
                    return s if s['positionSeconds']>control_before['positionSeconds']+.1 else None
                control_after=wait(independent_advances,'independent playback continues',2);other.control('pause')
                remaining=[p.name for p in cache.glob('remux-*')]
                # Simulated live writer continues its real file after player exit.
                offset=path.stat().st_size
                with path.open('ab') as file:
                    assert file.write(raw[offset:cuts[20]])==cuts[20]-offset;file.flush();os.fsync(file.fileno())
                assert path.read_bytes()==raw[:cuts[20]]
                previous=model;model=copy.deepcopy(model);model['revision']='2';model['resources'][0]['ranges']=[dict(offset='0',length=str(cuts[20]))]
                model=protect_snapshot(model,directory,previous=previous);after_append=sha(path)
                peers[0].launch()
                replacement=active[0]=ContinuousRemux(peers[0].client,str(directory));replacement.prepare(model,'clip');recovered=prepared(replacement)
                assert replacement.job_id!=old_job
                current_cache={p.name for p in cache.glob('remux-*')};assert len(current_cache)==1 and not current_cache & old_cache,(old_cache,current_cache)
                # Recovery must have occurred before the test inspects/forgets old receipts.
                receipts=[peers[0].native('readerStatus',readerId=rid)['reader'] for rid in old_reader_ids]
                assert all(r['released'] and r['reason'] in ('files-closed','process-exited','pid-reused') for r in receipts),receipts
                if kind=='normal':assert all(r['filesClosed'] and r['reason']=='files-closed' for r in receipts),receipts
                reject(peers[0].client,'continuousRemuxHeartbeat','unknown-continuous-job',jobId=old_job,**sdk.identity)
                reject(peers[0].client,'continuousRemuxUpdate','unknown-continuous-job',jobId=old_job,session=model)
                if old_playback:reject(peers[0].client,'continuousPlaybackStatus','unknown-continuous-playback',playbackId=old_playback)
                assert len(recovered['readerIds'])==1,'Preparation implicitly opened decoder'
                replacement.play(title='Explicit new preview after '+name);restarted=playing(replacement)
                assert restarted['playbackId']!=old_playback and not set(restarted['readerIds']) & set(old_reader_ids),restarted
                final=closed(replacement);active[0]=None
                for rid in old_reader_ids:assert peers[0].native('readerForget',readerId=rid)['forgotten']
                assert sha(path)==after_append and not list(cache.glob('remux-*'))
                if require_clean and kind=='normal' and remaining:
                    (report/'normal-exit-cache-failure.json').write_text(json.dumps(dict(profile=name,remaining=remaining,termination=termination,receipts=receipts),indent=2)+'\n')
                    raise AssertionError(('Normal exit retained continuous cache',name,remaining))
                rows.append(dict(normalExitCacheClean=not remaining,profile=name,result='PASS',initialProducer=initial,actualPlayback=actual,termination=termination,workerExit=worker,workerExitObservedSeconds=worker_seconds,oldReaderReceipts=receipts,cacheAfterExit=remaining,recoveredProducer=recovered,restartedPlayback=restarted,independentBefore=control_before,independentAfter=control_after,partialUnchangedByExit=True,writerAppendAfterExit=True,appendedOriginalSha256=after_append,originalUnchangedAfterRestart=True,explicitNewAttempt=True,**final))
                (report/'progress.json').write_text(json.dumps(dict(completed=len(rows),rows=rows),indent=2)+'\n')
                print(json.dumps(dict(profile=name,result='PASS')),flush=True)
        other_final=closed(other);active[1]=None;assert sha(other_path)==sha(raw_path)
        return dict(result='PASS',count=len(rows),rows=rows,independentEndpointPreserved=True,explicitNewAttempt=True,automaticPositionResume=False,requireCleanNormalExit=require_clean,otherFinal=other_final)
    finally:
        for i,sdk in enumerate(active):
            if sdk is not None and peers[i].process.poll() is None:
                closed(sdk)
        for watch in watches:watch.close()


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--player',action='append',required=True);p.add_argument('--reader-runner',required=True);p.add_argument('--report-directory',required=True);p.add_argument('--require-clean-normal-exit',action='store_true');args=p.parse_args()
    report=Path(args.report_directory).resolve();report.mkdir(parents=True,exist_ok=True)
    try:run(args,lambda peers,root:exercise(peers,root,report,args.require_clean_normal_exit),__file__)
    except Exception:
        (report/'error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
