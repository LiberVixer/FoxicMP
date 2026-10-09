"""Measure rendered flash/beep edges using process audio and 8x8 screen pixels.
Only use the disposable player and profile in root/player. No raw audio/images saved.
"""
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import subprocess
import sys
import threading
import time
import traceback
import uuid
from pathlib import Path


def edges(rows, threshold):
    """Rising edges after >=100ms dark/silent; reject failed timestamps/captures."""
    result, previous, low_since, previous_begin = [], False, None, None
    for kind, begin, end, value, flags in rows:
        if value < 0 or (kind == 'a' and (flags & 4 or begin <= 0)):
            raise ValueError('Invalid capture/timestamp')
        high = value >= threshold
        if high and not previous and low_since is not None and begin - low_since >= 1000000:
            result.append((begin, end, previous_begin if previous_begin is not None else begin))
        if not high and previous:
            low_since = begin
        elif not high and low_since is None:
            low_since = begin
        previous = high
        previous_begin = begin
    return result


def analyze(path, expected_ms=0, transitions=()):
    def read(file):
        rows = []
        for line in file.read_text().splitlines():
            if line.startswith(('a,', 'v,')):
                k,b,e,v,f = line.split(',')
                rows.append((k,int(b),int(e),float(v),int(f)))
        return rows
    video, audio = read(path), read(Path(str(path)+'.audio'))
    # Exclude the partial pulse at capture/startup boundary; first full pulse is at 2s.
    cutoff=video[0][1]+10000000
    ve, ae = edges([r for r in video if r[1]>=cutoff],180), edges([r for r in audio if r[1]>=cutoff],1200)
    # A seek can land inside a flash after its matching beep has already passed.
    # Measure full pairs after the transition, retaining strict matching elsewhere.
    ve=[v for v in ve if not any(t<=v[0]<t+8000000 for t in transitions)]
    pairs, used = [], set()
    for v in ve:
        candidates = [(abs(a[0]-v[0]),i,a) for i,a in enumerate(ae) if i not in used and abs(a[0]-v[0]) < 5000000]
        if candidates:
            _,i,a = min(candidates)
            used.add(i)
            pairs.append({'time100ns':v[0], 'audio_minus_video_ms':(a[0]-v[0])/10000,
                          'error_ms':(a[0]-v[0])/10000-expected_ms,
                          'error_interval_ms':[(a[2]-v[1])/10000-expected_ms,(a[1]-v[2])/10000-expected_ms]})
    assert len(pairs) >= 5, (len(ve),len(ae),pairs)
    gaps = [(r[1]-l[2])/10000 for l,r in zip(video,video[1:])]
    assert len(pairs) == len(ve), ('unmatched visible flashes',len(ve),len(pairs))
    return {'pairs':pairs,'flash_count':len(ve),'beep_count':len(ae),
            'expected_offset_ms':expected_ms,
            'max_abs_ms':max(abs(p['error_ms']) for p in pairs),
            'max_abs_interval_ms':max(abs(b) for p in pairs for b in p['error_interval_ms']),
            'max_video_sampling_gap_ms':max(gaps),
            'audio_discontinuity_packets':sum(bool(r[4]&1) for r in audio)}


def start_player(arguments):
    # Scheduled tasks may pass SW_HIDE to descendants. The measured player must show its own window.
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 1
    return subprocess.Popen(arguments, startupinfo=startup)


def main():
    root = Path(sys.argv[1]).absolute()
    mode = sys.argv[2] if len(sys.argv)>2 else 'baseline'
    sys.path.insert(0,str(root))
    from scripts.progressive_session import Session, read_json
    from scripts.progressive_pipe import Provider
    run = root/'av-runs'/f'{mode}-{uuid.uuid4().hex[:8]}'
    run.mkdir(parents=True)
    fragmented=len(sys.argv)>4 and sys.argv[4]=='fragmented'
    expected_ms=float(sys.argv[3]) if len(sys.argv)>3 else 0
    media = [root/('video-fragmented.mp4' if fragmented else 'video.mp4'),root/(('audio-fragmented.m4a' if expected_ms==0 else f'audio-fragmented-offset-{int(expected_ms)}.m4a') if fragmented else 'audio.m4a' if expected_ms==0 else f'audio-offset-{int(expected_ms)}.m4a')]
    user = C.WinDLL('user32',use_last_error=True)
    user.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    user.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
    user.SetWindowPos.argtypes=[W.HWND,W.HWND,C.c_int,C.c_int,C.c_int,C.c_int,W.UINT]
    user.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    user.SetCursorPos.argtypes=[C.c_int,C.c_int]
    user.IsWindowVisible.argtypes=[W.HWND]
    user.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    callback=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    player,probe,provider=None,None,None
    handles=[]
    result={'mode':mode,'passed':False,'run':str(run),'events':[]}
    result['player_sha256']=hashlib.sha256((root/'player'/'FoxicMP64.exe').read_bytes()).hexdigest()
    def mark(value):result['events'].append(dict(value,capture_time100ns=time.perf_counter_ns()//100))
    def window():
        found=[]
        @callback
        def visit(h,_):
            pid=W.DWORD();user.GetWindowThreadProcessId(h,C.byref(pid))
            kind=C.create_unicode_buffer(256);user.GetClassNameW(h,kind,256)
            if pid.value==player.pid and ('FoxicMP' in kind.value or 'MediaPlayerClassic' in kind.value):
                title=C.create_unicode_buffer(2048);user.GetWindowTextW(h,title,2048)
                found.append((bool(title.value), bool(user.IsWindowVisible(h)), h))
            return True
        user.EnumWindows(visit,0)
        return max(found)[2] if found else None
    def visible_window():
        h=window()
        if h:
            ok=user.SetWindowPos(h,W.HWND(-1),100,100,800,550,0x0040)
            result["window_state"]={"hwnd":h,"visible":bool(user.IsWindowVisible(h)),"set_position":bool(ok),"error":C.get_last_error()}
            return h if user.IsWindowVisible(h) else None
    def wait(check,seconds=20):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            value=check()
            if value:return value
            time.sleep(.1)
        raise AssertionError('Timed out')
    def command(value):user.PostMessageW(window(),0x111,value,0)
    try:
        args=[str(root/'player'/'FoxicMP64.exe'),'/new']
        if mode=='baseline':
            # A muxed reference retains relative timestamps; ordinary /dub normalizes
            # each input's start and cannot serve as an offset-preserving reference.
            if expected_ms:
                reference=root/f'av-reference-{int(expected_ms)}.mp4'
                subprocess.run([str(root/'tools'/'ffmpeg.exe'),'-v','error','-y','-copyts','-i',str(media[0]),'-i',str(media[1]),
                                '-map','0:v:0','-map','1:a:0','-c','copy','-avoid_negative_ts','disabled','-movflags','+faststart',str(reference)],check=True)
                args+=['/open',str(reference)]
            else:args+=['/open',str(media[0]),'/dub',str(media[1])]
        else:
            session=Session(run,['video.mp4.part','audio.m4a.part'],[p.stat().st_size for p in media],900000000 if fragmented else 0)
            if fragmented:
                session.document["timestampOffset100ns"]=10000000;session.publish()
            handles=[(run/n).open('wb',buffering=0) for n in ['video.mp4.part','audio.m4a.part']]
            packets=[json.loads(p.with_suffix('.packets.json').read_text())['packets'] for p in media]
            prefixes=[0,0]
            def fill(seconds):
                for i in range(2):
                    n=media[i].stat().st_size if seconds>=90 else max(int(p['pos'])+int(p['size']) for p in packets[i] if float(p.get('dts_time',p['pts_time']))<seconds)
                    with media[i].open('rb') as f:f.seek(prefixes[i]);handles[i].write(f.read(n-prefixes[i]))
                    prefixes[i]=n;session.progress(i,n,media[i].stat().st_size,seconds>=90)
            fill(5 if fragmented else 3)
            assert session.ready(),"The test prefix lacks a complete initial fragment"
            provider=Provider(session.path)
            threading.Thread(target=provider.run,daemon=True).start()
            wait(lambda:(run/'provider.json').exists())
            args+=['/harvester-session',str(session.path)]
            def status():
                try:return read_json(run/f'player-{player.pid}.json')
                except (OSError,ValueError):return {}
        player=start_player(args)
        h=wait(visible_window)
        user.SetWindowPos(h,W.HWND(-1),100,100,800,550,0x0040)
        time.sleep(3)
        if mode=='baseline':command(888)
        else:wait(lambda:status().get('type')=='buffering')
        # Loading/style changes can recreate the main HWND; capture its current handle.
        h=wait(visible_window)
        user.SetWindowPos(h,W.HWND(-1),100,100,800,550,0x0040)
        user.SetCursorPos(0,0)  # Keep pointer-triggered tooltips outside the measured video.
        capture=run/'capture.csv'
        ready=run/'ready'
        with (run/'capture.log').open('w') as log:
            result["probe_target"]={"pid":player.pid,"window":h}
            probe=subprocess.Popen([str(root/'av-capture.exe'),str(player.pid),str(h),'42' if mode!='baseline' else '18',str(capture),str(ready)],stdout=log,stderr=log)
            wait(lambda:ready.exists() or probe.poll() is not None)
            assert probe.poll() is None,(run/'capture.log').read_text()
            if mode=='baseline':command(887)
            else:
                fill(16 if fragmented else 12)
                wait(lambda:status().get('type')=='playing')
                mark(status())
                wait(lambda:status().get('type')=='buffering',25)
                mark(status())
                time.sleep(3)
                fill(90)
                wait(lambda:status().get('type')=='playing')
                mark(status())
                time.sleep(8)
                before_seek = status()["position100ns"]
                mark({"phase":"seek-command", "position100ns":before_seek})
                command(904)
                time.sleep(3)
                after_seek = status()
                assert after_seek.get("position100ns", 0) > before_seek + 15 * 10**7, after_seek
                mark(after_seek)
            assert probe.wait(55)==0,(run/'capture.log').read_text()
        transitions=[e['capture_time100ns'] for e in result['events'] if e.get('phase')=='seek-command']
        result.update(analyze(capture,expected_ms,transitions))
        if mode!='baseline':
            start=result['events'][0]['capture_time100ns']
            resume=result['events'][2]['capture_time100ns']
            seek=next(e['capture_time100ns'] for e in result['events'] if e.get('phase')=='seek-command')
            groups={'start':[],'resume':[],'seek':[]}
            for pair in result['pairs']:
                phase='seek' if pair['time100ns']>=seek else 'resume' if pair['time100ns']>=resume else 'start'
                groups[phase].append(pair)
            result['phases']={k:{'pairs':len(v),'max_error_ms':max(abs(p['error_ms']) for p in v)} for k,v in groups.items() if v}
            assert all(groups.values()),'Missing signal pairs in a playback phase'
        result['passed']=result['max_abs_interval_ms']<=100
        assert result['passed'],result
    except BaseException:
        result['error']=traceback.format_exc()
        raise
    finally:
        if probe and probe.poll() is None:probe.kill();probe.wait()
        if player and player.poll() is None:
            h=window()
            if h:user.PostMessageW(h,0x10,0,0)
            try:player.wait(8)
            except subprocess.TimeoutExpired:player.kill();player.wait();result['passed']=False
        for f in handles:f.close()
        if provider:provider.stop.set()
        (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        (root/f'result-av-{mode}-{int(expected_ms)}{"-fragmented" if fragmented else ""}.json').write_text(json.dumps(result,indent=2),encoding='utf-8')

if __name__=='__main__':main()
