"""Live public-source smoke test; no account cookies, signed URLs omitted from results.
Uses disposable player/profile and the unchanged Harvester CLI adapter (GUI stays off).
"""
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
import subprocess
import struct
import sys
import threading
import time
import traceback
import uuid
from pathlib import Path


def start_player(arguments):
    # Scheduled tasks may pass SW_HIDE to descendants. The measured player must show its own window.
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 1
    return subprocess.Popen(arguments, startupinfo=startup)


def main():
    root=Path(sys.argv[1]).absolute()
    name,url,formats,expect=sys.argv[2:6]
    resume=len(sys.argv)>8 and sys.argv[8]=='resume'
    proxy=sys.argv[6] if len(sys.argv)>6 else ''
    sys.path.insert(0,str(root))
    from scripts.progressive_session import read_json
    existing=Path(sys.argv[7]) if len(sys.argv)>7 else None
    run=existing or root/'source-runs'/f'{name}-{uuid.uuid4().hex[:8]}'
    run.mkdir(parents=True,exist_ok=bool(existing))
    env=dict(os.environ,YTD_TEMP_DIR=str(run),YTD_STOP_FILE=str(run/'stop'),PYTHONIOENCODING='utf-8')
    lines,events=[],[]
    player,backend,manifest=None,None,None
    user=C.WinDLL('user32')
    user.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    user.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
    user.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    user.IsWindowVisible.argtypes=[W.HWND]
    user.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    callback=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    result={'passed':False,'opened_incomplete':False,'played_incomplete':False,'url':url,'formats':formats,'expect':expect,'proxy_used':bool(proxy),'run':str(run)}
    result['player_sha256']=hashlib.sha256((root/'player'/'FoxicMP64.exe').read_bytes()).hexdigest()
    def close():
        if player and player.poll() is None:
            @callback
            def visit(h,_):
                pid=W.DWORD();user.GetWindowThreadProcessId(h,C.byref(pid))
                if pid.value==player.pid:user.PostMessageW(h,0x10,0,0)
                return True
            user.EnumWindows(visit,0)
            try:player.wait(8)
            except subprocess.TimeoutExpired:player.kill();player.wait();raise AssertionError('Player close timeout')
    def capture_output(label, attempt=0):
        found=[]
        @callback
        def main_window(h,_):
            pid=W.DWORD();user.GetWindowThreadProcessId(h,C.byref(pid))
            kind=C.create_unicode_buffer(256);user.GetClassNameW(h,kind,256)
            if pid.value==player.pid and ('FoxicMP' in kind.value or 'MediaPlayerClassic' in kind.value):
                title=C.create_unicode_buffer(2048);user.GetWindowTextW(h,title,2048)
                found.append((bool(title.value), bool(user.IsWindowVisible(h)), h))
            return True
        user.SetWindowPos.argtypes=[W.HWND,W.HWND,C.c_int,C.c_int,C.c_int,C.c_int,W.UINT]
        deadline=time.monotonic()+10
        while True:
            found.clear();user.EnumWindows(main_window,0)
            assert found,'Player main window missing'
            h=max(found)[2]
            user.SetWindowPos(h,W.HWND(-1),100,100,800,550,0x0040)
            if user.IsWindowVisible(h):break
            assert time.monotonic()<deadline,'Player window stayed hidden'
            time.sleep(.1)
        user.SetCursorPos(0,0)
        capture=run/(label+f'-{attempt}'+'.csv')
        subprocess.run([str(root/'av-capture.exe'),str(player.pid),str(h),'8',str(capture),str(run/'capture-ready')],check=True,timeout=20)
        video=[float(l.split(',')[3]) for l in capture.read_text().splitlines() if l.startswith('v,')]
        audio=[float(l.split(',')[3]) for l in Path(str(capture)+'.audio').read_text().splitlines() if l.startswith('a,')]
        if video and min(video)<0:
            # Reject an occluded capture in full, then select/show the current window again.
            # Loading/style changes or another desktop window may invalidate a prior HWND/area.
            result.setdefault('rejected_output_captures',[]).append({'label':label,'attempt':attempt,
                'invalid_video_samples':sum(v<0 for v in video),'samples':len(video)})
            assert attempt<2,'Player output remained obscured after three independent captures'
            time.sleep(1)
            return capture_output(label,attempt+1)
        assert video and audio and min(video)>=0 and max(video)-min(video)>5 and max(audio)>300,('No changing video/audible process output',min(video),max(video),max(audio,default=0))
        return {'brightness_range':[min(video),max(video)],'audio_peak':max(audio)}
    try:
        if existing and not resume:
            lines=(run/'backend.log').read_text(encoding='utf-8').splitlines()
            events=[json.loads(line[len('[FoxicMP] '):]) for line in lines if line.startswith('[FoxicMP] ')]
            assert not any(e.get('ready') for e in events),'Existing verification supports ordinary-only runs'
            result['verified_existing_download']=True
        else:
            command=[sys.executable,str(root/'scripts'/'progressive_download.py'),url,'-f',formats,'--no-playlist','--merge-output-format','mp4',
                     '--ffmpeg-location',str(root/'tools'),'--download-archive',str(run/'archive.txt'),'-o',str(run/'finished.%(ext)s'),
                     '--concurrent-fragments','1' if expect=='early' else '4','--limit-rate','800K','--socket-timeout','25','--retries','2','--newline','--no-warnings','--write-info-json']
            if proxy:command+=['--proxy',proxy]
            with (run/'backend.log').open('w',encoding='utf-8') as log:
                backend=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
                done=threading.Event()
                def output():
                    nonlocal player,manifest
                    try:
                        for line in backend.stdout:
                            lines.append(line);log.write(line);log.flush()
                            if line.startswith('[FoxicMP] '):
                                event=json.loads(line[len('[FoxicMP] '):]);events.append(event)
                                if event.get('ready') and not player:
                                    manifest=Path(event['manifest'])
                                    state=read_json(manifest)
                                    result['opened_incomplete']=state['state']=='downloading'
                                    player=start_player([str(root/'player'/'FoxicMP64.exe'),'/new','/harvester-session',str(manifest)])
                    finally:done.set()
                threading.Thread(target=output,daemon=True).start()
                until=time.monotonic()+900
                while not done.is_set() and time.monotonic()<until:
                    if player and manifest:
                        assert player.poll() is None, f'Player exited during download: {player.returncode}'
                        try:
                            state=read_json(manifest);status=read_json(manifest.parent/f'player-{player.pid}.json')
                            if state['state']=='downloading' and status.get('type')=='playing' and status.get('position100ns',0)>20000000:
                                result['played_incomplete']=True
                                if 'proof' not in result:result['proof']={'status':status,'tracks':state['tracks']}
                                if 'early_output' not in result:
                                    output=capture_output('early')
                                    after=read_json(manifest)
                                    assert after['state']=='downloading','Output capture finished after the download'
                                    result['early_output']=output
                                    result['proof']['capture_end_tracks']=after['tracks']
                            if status.get('type')=='error':raise AssertionError(status)
                        except (OSError,ValueError):pass
                    time.sleep(.1)
                assert done.is_set(),'Download timeout'
                assert backend.wait(10)==0,''.join(lines[-12:])
        final=run/'finished.mp4'
        assert final.exists(),'No final MP4'
        assert (run/'archive.txt').read_text().strip(),'Success absent from archive'
        details=json.loads(subprocess.check_output([str(root/'tools'/'ffprobe.exe'),'-v','quiet','-show_streams','-show_format','-of','json',str(final)]))
        result['streams']=[{k:s[k] for k in ['codec_type','codec_name','profile','extradata_size','duration','width','height','sample_rate','start_time'] if k in s} for s in details['streams']]
        result['duration']=float(details['format']['duration']);result['bytes']=final.stat().st_size
        info=json.loads((run/'finished.info.json').read_text(encoding='utf-8'))
        result['title']=info.get('title')
        result['extractor_duration']=info.get('duration')
        assert info.get('duration') and abs(result['duration']-info['duration'])<2,'Incomplete final media'
        chosen=info.get('requested_formats') or [info]
        result['selected_formats']=[{k:f.get(k) for k in ['format_id','ext','protocol','vcodec','acodec','height']} for f in chosen]
        result['sha256']=hashlib.file_digest(final.open('rb'),'sha256').hexdigest()
        assert sorted(s['codec_name'] for s in details['streams'])==['aac','h264'],details
        if expect=='early':assert result.get('opened_incomplete') and result.get('played_incomplete') and result.get('early_output'),result
        if expect=='fallback':assert not result.get('played_incomplete') and any(e.get('reason') for e in events),events
        session_manifest=manifest or next(run.glob('foxicmp-sessions/*/session.json'),None)
        if session_manifest:
            state=read_json(session_manifest)
            assert state['state']=='complete',state
            # Demuxed packet payloads must remain identical through FFmpeg stream copy.
            hashes={}
            for kind,selector in [('video','v:0'),('audio','a:0')]:
                track=session_manifest.parent/state['tracks'][kind]['path']
                atoms=[]
                with track.open('rb') as source:
                    offset=0
                    for _ in range(8):
                        source.seek(offset);header=source.read(16)
                        if len(header)<8:break
                        size,tag=struct.unpack('>I4s',header[:8])
                        if size==1:size=struct.unpack('>Q',header[8:16])[0]
                        atoms.append(tag.decode('ascii',errors='replace'))
                        if size<8:break
                        offset+=size
                result.setdefault('source_atoms',{})[kind]=atoms
                if 'moof' in atoms:
                    assert state['tracks'][kind]['initializationReady'],'Fragmented MP4 was not initialized'
                hashes[kind]=[]
                for path,select in [(track,'0:0'),(final,f'0:{selector}')]:
                    hashes[kind].append(subprocess.check_output([str(root/'tools'/'ffmpeg.exe'),'-v','error','-i',str(path),'-map',select,'-c','copy','-f','hash','-hash','sha256','-'],text=True).strip())
                assert hashes[kind][0]==hashes[kind][1],hashes
            result['bitstreams_identical']=hashes
            if all(t['path'].startswith('preview-') for t in state['tracks'].values()):
                # Local HLS normalization must preserve the original relative packet clock,
                # including any offsets/gaps; a payload hash alone cannot prove synchronization.
                result['packet_clocks_preserved']={}
                shift=state.get('timestampOffset100ns',0)/10000000
                for kind,selector in [('video','v:0'),('audio','a:0')]:
                    track=session_manifest.parent/state['tracks'][kind]['path']
                    indexes=[]
                    for path in [track,final]:
                        raw=subprocess.check_output([str(root/'tools'/'ffprobe.exe'),'-v','quiet','-select_streams',selector,
                            '-show_packets','-show_entries','packet=pts_time,dts_time','-of','json',str(path)])
                        indexes.append(json.loads(raw)['packets'])
                    assert len(indexes[0])==len(indexes[1]),'HLS packet count changed'
                    error=max(abs(float(a[key])-shift-float(b[key])) for a,b in zip(*indexes) for key in ['pts_time','dts_time'])
                    assert error<.001,('HLS relative packet clock changed',kind,error)
                    result['packet_clocks_preserved'][kind]={'packets':len(indexes[0]),'max_error_ms':error*1000}
        if not player:
            player=start_player([str(root/'player'/'FoxicMP64.exe'),'/new',str(final)])
            time.sleep(6)
            assert player.poll() is None,'Ordinary player exited'
            # A visible error dialog is not successful playback.
            dialogs=[]
            user.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
            user.IsWindowVisible.argtypes=[W.HWND]
            @callback
            def visit(h,_):
                pid=W.DWORD();user.GetWindowThreadProcessId(h,C.byref(pid))
                kind=C.create_unicode_buffer(256);user.GetClassNameW(h,kind,256)
                if pid.value==player.pid and kind.value=='#32770' and user.IsWindowVisible(h):dialogs.append(h)
                return True
            user.EnumWindows(visit,0)
            assert not dialogs,dialogs
            result['ordinary_open_no_error']=True
            result['ordinary_output']=capture_output('ordinary')
        result['events']=list({json.dumps({'ready':e.get('ready'),'reason':e.get('reason')},ensure_ascii=False):{'ready':e.get('ready'),'reason':e.get('reason')} for e in events}.values())
        result['passed']=True
    except BaseException:
        result['error']=traceback.format_exc();raise
    finally:
        close()
        if backend and backend.poll() is None:backend.kill();backend.wait()
        (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        (root/f'result-source-{name}.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')

if __name__=='__main__':main()
