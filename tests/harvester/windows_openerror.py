"""Regression: missing AAC initialization must fail opening without playlist traversal/crash.
Run against a disposable Windows player/profile and the existing signal fixtures.
"""
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path


def main():
    root=Path(sys.argv[1]).absolute()
    sys.path.insert(0,str(root))
    from scripts.progressive_session import Session,read_json
    from scripts.progressive_pipe import Provider
    from windows_playback import start_player
    run=root/'runs'/('openerror-'+uuid.uuid4().hex[:8]);run.mkdir(parents=True)
    audio=run/'audio.m4a';transport=run/'cached.ts'
    ffmpeg=str(root/'tools'/'ffmpeg.exe')
    subprocess.run([ffmpeg,'-v','error','-y','-i',str(root/'audio.m4a'),'-t','12','-c','copy','-f','mpegts',str(transport)],check=True)
    # Reproduce the former HLS path: AAC config is learned after the empty moov was written.
    subprocess.run([ffmpeg,'-v','error','-y','-i',str(transport),'-c','copy','-bsf:a','aac_adtstoasc',
                    '-movflags','+empty_moov+frag_keyframe+default_base_moof+skip_trailer','-frag_duration','2000000',str(audio)],check=True)
    metadata=json.loads(subprocess.check_output([str(root/'tools'/'ffprobe.exe'),'-v','quiet','-show_streams','-of','json',str(audio)]))['streams'][0]
    assert not metadata.get('extradata_size',0),'Fixture unexpectedly contains AAC initialization'
    video=run/'video.mp4';video.write_bytes((root/'video-fragmented.mp4').read_bytes())
    media=[video,audio]
    session=Session(run,[p.name for p in media],[p.stat().st_size for p in media],900000000)
    for i,p in enumerate(media):session.progress(i,p.stat().st_size,p.stat().st_size,True)
    session.state('complete')
    provider=Provider(session.path);threading.Thread(target=provider.run,daemon=True).start()
    deadline=time.monotonic()+5
    while not (run/'provider.json').exists():
        assert time.monotonic()<deadline;time.sleep(.05)
    player=start_player([str(root/'player'/'FoxicMP64.exe'),'/new','/harvester-session',str(session.path)])
    user=C.WinDLL('user32');user.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    user.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
    callback=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    result={'passed':False,'run':str(run),'missing_aac_config':True}
    result['player_sha256']=hashlib.sha256((root/'player'/'FoxicMP64.exe').read_bytes()).hexdigest()
    try:
        deadline=time.monotonic()+10;seen=False
        while time.monotonic()<deadline:
            assert player.poll() is None,f'Player crashed on open error: {player.returncode}'
            seen=seen or (run/f'player-{player.pid}.json').exists()
            time.sleep(.1)
        assert seen,'Session was never attached'
        assert not provider.leases.items,'Failed open retained reader lease'
        result['passed']=True
    finally:
        @callback
        def close(h,_):
            pid=W.DWORD();user.GetWindowThreadProcessId(h,C.byref(pid))
            if pid.value==player.pid:user.PostMessageW(h,0x10,0,0)
            return True
        user.EnumWindows(close,0)
        try:player.wait(8)
        except subprocess.TimeoutExpired:player.kill();raise AssertionError('Close timeout after failed open')
        provider.stop.set()
        (root/'result-openerror.json').write_text(json.dumps(result,indent=2),encoding='utf-8')

if __name__=='__main__':main()
