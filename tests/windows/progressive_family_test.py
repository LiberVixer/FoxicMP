"""Exercise the version-1 downloader contract against an isolated Classic copy.
No downloader repository, installed profile or shell registration is modified.
"""
import argparse
import configparser
import ctypes
from ctypes import wintypes as w
import json
from pathlib import Path
import subprocess
import sys
import time
import traceback
import uuid
import concurrent.futures

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'qt' / 'tools'))
from progressive_client import ProgressiveClient, ApiError
from progressive_writer import run as run_sdk_writer
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from progressive_v1_conformance import exercise_directory_contract
from progressive_v2_conformance import check_player as check_v2_model
from progressive_reader_api_conformance import exercise as exercise_reader_contract
from progressive_resource_api_conformance import exercise as exercise_resource_contract
from progressive_segment_api_conformance import exercise as exercise_segment_contract
from progressive_segment_playback import exercise as exercise_segment_playback, PLAYBACK_PROFILES
from progressive_catalog_playback import exercise as exercise_catalog_playback
from progressive_v2_playback_conformance import exercise as exercise_v2_playback, MEDIA_PROFILES
from progressive_metadata_conformance import exercise as exercise_metadata
from progressive_multitrack_conformance import exercise as exercise_multitrack
from progressive_partial_audio_conformance import exercise as exercise_partial_audio, PROFILES as PARTIAL_AUDIO_PROFILES, ERROR_PROFILES as ACTIVE_AUDIO_PROFILES
from progressive_optional_audio_conformance import exercise as exercise_optional_audio, PROFILES as OPTIONAL_AUDIO_PROFILES
from progressive_subtitle_isolation_conformance import exercise as exercise_srt_isolation, PROFILES as SRT_ISOLATION_PROFILES


def registration_snapshot():
    """Read installed Classic shell commands/icons without changing HKCU."""
    import winreg
    result = {}
    def visit(path):
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
                count, values, _ = winreg.QueryInfoKey(key)
                result[path] = [winreg.EnumValue(key, i) for i in range(values)]
                for i in range(count):
                    visit(path + '\\' + winreg.EnumKey(key, i))
        except FileNotFoundError:
            pass
    base = r'Software\Classes'
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, base) as key:
        for i in range(winreg.QueryInfoKey(key)[0]):
            name = winreg.EnumKey(key, i)
            if name.lower().startswith('foxicmp'):
                visit(base + '\\' + name)
    associations = base + r'\SystemFileAssociations'
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, associations) as key:
        for i in range(winreg.QueryInfoKey(key)[0]):
            visit(associations + '\\' + winreg.EnumKey(key, i) + r'\shell\FoxicMP')
    visit(base + r'\Directory\shell\FoxicMP64.play')
    visit(base + r'\Directory\shell\FoxicMP64.enqueue')
    return result


def prefix(data, seconds):
    at = fragments = 0
    while at + 8 <= len(data):
        size = int.from_bytes(data[at:at+4], 'big')
        kind = data[at+4:at+8]
        if size == 1: size = int.from_bytes(data[at+8:at+16], 'big')
        if size < 8 or at + size > len(data): raise ValueError('Bad fixture MP4')
        at += size
        if kind == b'mdat':
            fragments += 1
            if fragments == seconds: return at
    return len(data)


def post_command(pid, command):
    user = ctypes.WinDLL('user32', use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback_type, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    found = []
    @callback_type
    def visit(hwnd, _):
        owner = w.DWORD();user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(256);user.GetClassNameW(hwnd, name, 256)
        if owner.value == pid and name.value == 'FoxicMP': found.append(hwnd)
        return True
    user.EnumWindows(visit, 0)
    if not found: raise RuntimeError('Owned test Classic window missing')
    user.PostMessageW(found[0], 0x111 if command else 0x10, command, 0)


def check_window_title(pid,expected):
    user=ctypes.WinDLL('user32',use_last_error=True)
    callback_type=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    user.EnumWindows.argtypes=[callback_type,w.LPARAM];user.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int];user.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
    titles=[]
    @callback_type
    def visit(hwnd,_):
        owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner));name=ctypes.create_unicode_buffer(128);user.GetClassNameW(hwnd,name,128)
        if owner.value==pid and name.value=='FoxicMP':
            title=ctypes.create_unicode_buffer(8192);user.GetWindowTextW(hwnd,title,8192);titles.append(title.value)
        return True
    user.EnumWindows(visit,0)
    assert len(titles)==1 and titles[0].startswith(expected),('Actual Classic caption',expected,titles)


def seek_backward(pid,seconds):
    """The ordinary 2-second jump command in this isolated test INI."""
    assert seconds==2
    post_command(pid,901) # ID_PLAY_SEEKBACKWARDMED; test profile JumpDistM=2000.


def wait_status(client, session, predicate, seconds=15):
    deadline = time.monotonic()+seconds
    last = {}
    while time.monotonic() < deadline:
        last = client.command('status', session)
        if predicate(last): return last
        if last.get('state') == 'error': raise AssertionError(last)
        time.sleep(.1)
    raise AssertionError(('Timed out', last))


def exercise_preparation(client, pid, fixtures, work):
    """Force many real file reads and ping the actual Classic UI concurrently."""
    user=ctypes.WinDLL('user32',use_last_error=True)
    callback_type=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    user.EnumWindows.argtypes=[callback_type,w.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
    user.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,ctypes.POINTER(ctypes.c_size_t)]
    windows=[]
    @callback_type
    def visit(hwnd,_):
        owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        name=ctypes.create_unicode_buffer(256);user.GetClassNameW(hwnd,name,256)
        if owner.value==pid and name.value=='FoxicMP':windows.append(hwnd)
        return True
    user.EnumWindows(visit,0);assert len(windows)==1,windows
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    class Counters(ctypes.Structure):
        _fields_=[(name,ctypes.c_ulonglong) for name in ('reads','writes','others','readBytes','writeBytes','otherBytes')]
    kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE
    kernel.GetProcessIoCounters.argtypes=[w.HANDLE,ctypes.POINTER(Counters)];kernel.CloseHandle.argtypes=[w.HANDLE]
    handle=kernel.OpenProcess(0x1000,False,pid);assert handle
    def count():
        value=Counters();assert kernel.GetProcessIoCounters(handle,ctypes.byref(value));return value.reads
    work=Path(work);work.mkdir()
    value=dict(schemaVersion=1,sessionId=uuid.uuid4().hex,generation=1,revision=1,state='downloading',duration100ns=200000000,tracks={})
    for kind,name in [('video','video.mp4'),('audio','audio.m4a')]:
        data=(Path(fixtures)/'progressive'/name).read_bytes();data=data[:prefix(data,5)]
        if kind=='video':
            ftyp=int.from_bytes(data[:4],'big');data=data[:ftyp]+b'\x00\x00\x00\x08free'*100000+data[ftyp:]
        path=work/(kind+'.part');path.write_bytes(data)
        value['tracks'][kind]=dict(path=path.name,container='mp4' if kind=='video' else 'm4a',fragmented=True,initializationReady=True,availablePrefixBytes=len(data),expectedTotalBytes=None,state='downloading')
    reports=[]
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            for action in ('probe','start'):
                before=count();future=pool.submit(client.request,dict(version=1,action=action,session=value,sessionDirectory=str(work),**(dict(activate=False,trackReader=True) if action=='start' else {})),10)
                deadline=time.monotonic()+4
                while count()<before+500 and not future.done() and time.monotonic()<deadline:time.sleep(.002)
                assert not future.done() and count()>=before+500,'Preparation finished before actual concurrent UI trial'
                durations=[]
                for _ in range(3):
                    out=ctypes.c_size_t();start=time.perf_counter_ns()
                    assert user.SendMessageTimeoutW(windows[0],0,0,0,2,500,ctypes.byref(out)),'UI did not respond during disk indexing'
                    durations.append((time.perf_counter_ns()-start)/1000000)
                command=803 if action=='probe' else 890  # Close / Stop before media exists.
                post_command(pid,command)
                try:reply=future.result(timeout=10)
                except ApiError as error:reply=error.reply
                assert reply.get('code')=='preparation_cancelled',reply
                reports.append(dict(action=action,cancelCommand='close' if command==803 else 'stop',actualReadOperations=count()-before,uiPingMilliseconds=durations,result=reply))
        recovered=client.readers(value['sessionId'],1)
        for reader in recovered:
            assert client.release_reader(reader['readerId'],timeout=5)['released'],reader
            client.forget_reader(reader['readerId'])
        assert client.readers(value['sessionId'],1)==[]
        return dict(result='PASS',cases=reports,recoveredClosedReaders=len(recovered))
    finally:kernel.CloseHandle(handle)


def exercise_publication(client,pid,fixtures,work):
    """Index appended bytes while the real UI can stop an existing session."""
    user=ctypes.WinDLL('user32',use_last_error=True);kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    callback_type=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM);windows=[]
    user.EnumWindows.argtypes=[callback_type,w.LPARAM];user.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
    user.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,ctypes.POINTER(ctypes.c_size_t)]
    @callback_type
    def visit(hwnd,_):
        owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner));name=ctypes.create_unicode_buffer(256);user.GetClassNameW(hwnd,name,256)
        if owner.value==pid and name.value=='FoxicMP':windows.append(hwnd)
        return True
    user.EnumWindows(visit,0);assert len(windows)==1
    class Counters(ctypes.Structure):
        _fields_=[(name,ctypes.c_ulonglong) for name in ('reads','writes','others','readBytes','writeBytes','otherBytes')]
    kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE
    kernel.GetProcessIoCounters.argtypes=[w.HANDLE,ctypes.POINTER(Counters)];kernel.CloseHandle.argtypes=[w.HANDLE]
    handle=kernel.OpenProcess(0x1000,False,pid);assert handle
    def count():
        value=Counters();assert kernel.GetProcessIoCounters(handle,ctypes.byref(value));return value.reads
    results=[];work=Path(work)
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            for action in ('update','complete'):
                root=work/action;root.mkdir(parents=True)
                value=dict(schemaVersion=1,sessionId=uuid.uuid4().hex,generation=1,revision=1,state='downloading',duration100ns=200000000,tracks={})
                originals={};paths={};prefixes={}
                for kind,name in [('video','video.mp4'),('audio','audio.m4a')]:
                    data=(Path(fixtures)/'progressive'/name).read_bytes();originals[kind]=data;prefixes[kind]=prefix(data,5)
                    path=root/(kind+'.part');paths[kind]=path;path.write_bytes(data[:prefixes[kind]])
                    value['tracks'][kind]=dict(path=path.name,container='mp4' if kind=='video' else 'm4a',fragmented=True,initializationReady=True,availablePrefixBytes=prefixes[kind],expectedTotalBytes=None,state='downloading')
                started=client.start(value,session_directory=root,activate=False,track_reader=True);reader=started['readerId']
                try:
                    wait_status(client,value['sessionId'],lambda v:v.get('availableSeconds',0)>=4 and v.get('state')=='buffering',15)
                    value['revision']=2
                    for kind in ('video','audio'):
                        target=len(originals[kind]) if action=='complete' else prefix(originals[kind],12)
                        with paths[kind].open('ab') as stream:
                            if kind=='video':stream.write(b'\x00\x00\x00\x08free'*100000)
                            stream.write(originals[kind][prefixes[kind]:target])
                        item=value['tracks'][kind];item['availablePrefixBytes']=paths[kind].stat().st_size
                        if action=='complete':item.update(expectedTotalBytes=item['availablePrefixBytes'],state='complete')
                    if action=='complete':value['state']='complete'
                    before=count();future=pool.submit(client.request,dict(version=1,action=action,sessionId=value['sessionId'],readerId=reader,session=value),10)
                    deadline=time.monotonic()+4
                    while count()<before+500 and not future.done() and time.monotonic()<deadline:time.sleep(.002)
                    assert not future.done() and count()>=before+500,'No overlapping update indexing/UI trial'
                    durations=[]
                    for _ in range(3):
                        out=ctypes.c_size_t();start=time.perf_counter_ns()
                        assert user.SendMessageTimeoutW(windows[0],0,0,0,2,500,ctypes.byref(out)),'UI blocked during publication'
                        durations.append((time.perf_counter_ns()-start)/1000000)
                    post_command(pid,803 if action=='update' else 890)
                    try:reply=future.result(timeout=10)
                    except ApiError as error:reply=error.reply
                    assert reply.get('code')=='preparation_cancelled',reply
                    status=client.command('status',value['sessionId'],readerId=reader);assert not status['active'] and not status.get('error'),status
                    released=client.release_reader(reader,timeout=10);assert released['released'],released;client.forget_reader(reader)
                    results.append(dict(action=action,cancelCommand='close' if action=='update' else 'stop',readOperationsBeforeCancel=count()-before,uiPingMilliseconds=durations,result=reply,release=released))
                finally:
                    try:
                        if client.release_reader(reader,timeout=5).get('released'):client.forget_reader(reader)
                    except (OSError,RuntimeError):pass
        return dict(result='PASS',cases=results)
    finally:kernel.CloseHandle(handle)


def exercise_registry_oplock(client,pid,fixtures,work):
    """Block actual registry CreateFile until an oplock is acknowledged; ping UI."""
    root=Path(work);root.mkdir(parents=True)
    value=dict(schemaVersion=1,sessionId=uuid.uuid4().hex,generation=1,revision=1,state='downloading',duration100ns=200000000,tracks={})
    for kind,name in [('video','video.mp4'),('audio','audio.m4a')]:
        data=(Path(fixtures)/'progressive'/name).read_bytes();length=prefix(data,5);(root/(kind+'.part')).write_bytes(data[:length])
        value['tracks'][kind]=dict(path=kind+'.part',container='mp4' if kind=='video' else 'm4a',fragmented=True,initializationReady=True,availablePrefixBytes=length,expectedTotalBytes=None,state='downloading')
    opened=client.start(value,session_directory=root,activate=False,track_reader=True);reader=opened['readerId']
    assert client.release_reader(reader,timeout=10)['released']
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);user=ctypes.WinDLL('user32',use_last_error=True)
    class Overlapped(ctypes.Structure):
        _fields_=[('Internal',ctypes.c_size_t),('InternalHigh',ctypes.c_size_t),('Offset',w.DWORD),('OffsetHigh',w.DWORD),('hEvent',w.HANDLE)]
    kernel.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,ctypes.c_void_p,w.DWORD,w.DWORD,w.HANDLE];kernel.CreateFileW.restype=w.HANDLE
    kernel.CreateEventW.argtypes=[ctypes.c_void_p,w.BOOL,w.BOOL,w.LPCWSTR];kernel.CreateEventW.restype=w.HANDLE
    kernel.DeviceIoControl.argtypes=[w.HANDLE,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),ctypes.POINTER(Overlapped)]
    kernel.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD];kernel.GetOverlappedResult.argtypes=[w.HANDLE,ctypes.POINTER(Overlapped),ctypes.POINTER(w.DWORD),w.BOOL]
    kernel.CloseHandle.argtypes=[w.HANDLE];kernel.GetTempPathW.argtypes=[w.DWORD,w.LPWSTR]
    temp=ctypes.create_unicode_buffer(32768);assert kernel.GetTempPathW(len(temp),temp)
    registry=Path(temp.value)/(client.endpoint.rsplit('\\',1)[-1]+'.readers');path=registry/(reader+'.lease');assert path.exists(),path
    callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM);windows=[]
    user.EnumWindows.argtypes=[callback,w.LPARAM];user.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)];user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
    user.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,ctypes.POINTER(ctypes.c_size_t)]
    @callback
    def visit(hwnd,_):
        owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner));name=ctypes.create_unicode_buffer(256);user.GetClassNameW(hwnd,name,256)
        if owner.value==pid and name.value=='FoxicMP':windows.append(hwnd)
        return True
    user.EnumWindows(visit,0);assert len(windows)==1
    results=[]
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            for action in ('readerStatus','readerRelease','readers','readerForget'):
                handle=kernel.CreateFileW(str(path),0xC0000000,3,None,3,0x40000000,None);assert handle!=ctypes.c_void_p(-1).value,ctypes.get_last_error()
                event=kernel.CreateEventW(None,True,False,None);overlapped=Overlapped();overlapped.hEvent=event;count=w.DWORD()
                future=None
                try:
                    ok=kernel.DeviceIoControl(handle,0x90000,None,0,None,0,ctypes.byref(count),ctypes.byref(overlapped))
                    assert not ok and ctypes.get_last_error()==997,('Oplock was not granted',ctypes.get_last_error())
                    query=dict(version=1,action=action,readerId=reader)
                    if action=='readers':query=dict(version=1,action=action,sessionId=value['sessionId'],generation='1')
                    future=pool.submit(client.request,query,8)
                    assert kernel.WaitForSingleObject(event,2000)==0,'Registry query did not break the real file oplock'
                    assert not future.done(),'Query bypassed the held registry file'
                    times=[]
                    for _ in range(3):
                        out=ctypes.c_size_t();started=time.perf_counter_ns()
                        assert user.SendMessageTimeoutW(windows[0],0,0,0,2,500,ctypes.byref(out)),'Classic UI blocked on reader-registry I/O'
                        times.append((time.perf_counter_ns()-started)/1000000)
                    assert not future.done()
                    kernel.GetOverlappedResult(handle,ctypes.byref(overlapped),ctypes.byref(count),False)
                finally:
                    kernel.CloseHandle(handle);kernel.CloseHandle(event)
                result=future.result(timeout=8);assert result['ok'],result
                if action=='readerForget':assert result['forgotten']
                elif action=='readers':assert any(r['readerId']==reader and r['released'] for r in result['readers'])
                else:assert result['reader']['released']
                results.append(dict(action=action,actualFileOplockBlockedQuery=True,uiPingMilliseconds=times,result=result))
        # A separate live source forces final cleanup to wait on the registry
        # guard. The UI must keep pumping while the close marker is still false.
        value['sessionId']=uuid.uuid4().hex
        opened=client.start(value,session_directory=root,activate=False,track_reader=True);reader=opened['readerId']
        handle=kernel.CreateFileW(str(registry/'.guard'),0xC0000000,3,None,3,0x40000000,None);assert handle!=ctypes.c_void_p(-1).value
        event=kernel.CreateEventW(None,True,False,None);overlapped=Overlapped();overlapped.hEvent=event;count=w.DWORD()
        try:
            ok=kernel.DeviceIoControl(handle,0x90000,None,0,None,0,ctypes.byref(count),ctypes.byref(overlapped))
            assert not ok and ctypes.get_last_error()==997,('Cleanup guard oplock was not granted',ctypes.get_last_error())
            post_command(pid,803)
            assert kernel.WaitForSingleObject(event,3000)==0,'Source destructor did not reach the held registry guard'
            raw=json.loads((registry/(reader+'.lease')).read_text(encoding='utf-8'));assert raw['filesClosed'] is False,raw
            times=[]
            for _ in range(3):
                out=ctypes.c_size_t();started=time.perf_counter_ns()
                assert user.SendMessageTimeoutW(windows[0],0,0,0,2,500,ctypes.byref(out)),'UI waited for the final source destructor'
                times.append((time.perf_counter_ns()-started)/1000000)
            kernel.GetOverlappedResult(handle,ctypes.byref(overlapped),ctypes.byref(count),False)
        finally:kernel.CloseHandle(handle);kernel.CloseHandle(event)
        receipt=client.release_reader(reader,timeout=10);assert receipt['released'] and receipt['reader']['filesClosed'],receipt
        results.append(dict(action='sourceRetirement',actualCleanupGuardBlocked=True,filesClosedWhileBlocked=False,uiPingMilliseconds=times,release=receipt))
        return dict(result='PASS',cases=results)
    finally:
        try:client.forget_reader(reader)
        except (OSError,RuntimeError):pass


def run_legacy_opening(args):
    """Hold the actual first media CreateFile on an oplock, then cancel/commit."""
    player=Path(args.player).resolve();registration=registration_snapshot()
    ini=player.with_suffix('.ini').read_bytes();settings=configparser.ConfigParser();settings.read_string(ini.decode('utf-16') if ini.startswith((b'\xff\xfe',b'\xfe\xff')) else ini.decode('utf-8-sig'))
    assert settings.get('Settings','ContextMenuPrompted')=='1' and settings.get('Settings','ContextMenuEnabled')=='0'
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);user=ctypes.WinDLL('user32',use_last_error=True)
    class Overlapped(ctypes.Structure):
        _fields_=[('Internal',ctypes.c_size_t),('InternalHigh',ctypes.c_size_t),('Offset',w.DWORD),('OffsetHigh',w.DWORD),('hEvent',w.HANDLE)]
    kernel.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,ctypes.c_void_p,w.DWORD,w.DWORD,w.HANDLE];kernel.CreateFileW.restype=w.HANDLE
    kernel.CreateEventW.argtypes=[ctypes.c_void_p,w.BOOL,w.BOOL,w.LPCWSTR];kernel.CreateEventW.restype=w.HANDLE
    kernel.DeviceIoControl.argtypes=[w.HANDLE,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),ctypes.POINTER(Overlapped)]
    kernel.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD];kernel.CloseHandle.argtypes=[w.HANDLE]
    user.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,ctypes.POINTER(ctypes.c_size_t)]
    callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM);user.EnumWindows.argtypes=[callback,w.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)];user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
    def read(path):
        try:return json.loads(path.read_text(encoding='utf-8'))
        except (OSError,ValueError):return {}
    def wait(check,seconds=10):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            result=check()
            if result:return result
            time.sleep(.03)
        raise AssertionError('Opening test condition timed out')
    cases=[]
    for mode in ('commit','close','stop','timeout'):
        root=Path(args.report)/mode;root.mkdir(parents=True)
        value=dict(schemaVersion=1,sessionId=uuid.uuid4().hex,generation=1,revision=1,state='downloading',duration100ns=200000000,tracks={})
        for kind,name in [('video','video.mp4'),('audio','audio.m4a')]:
            data=(Path(args.fixtures)/'progressive'/name).read_bytes();length=prefix(data,12);(root/(kind+'.part')).write_bytes(data[:length])
            value['tracks'][kind]=dict(path=kind+'.part',container='mp4' if kind=='video' else 'm4a',fragmented=True,initializationReady=True,availablePrefixBytes=length,expectedTotalBytes=None,state='downloading')
        manifest=root/'session.json';manifest.write_text(json.dumps(value),encoding='utf-8')
        provider=subprocess.Popen([sys.executable,str(Path(__file__).resolve().parents[1]/'progressive_legacy_fixture.py'),'--manifest',str(manifest),'--fixtures',str(Path(args.fixtures).resolve())],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        process=None;handle=None;event=None
        try:
            wait(lambda:(root/'fixture-ready.json').exists(),8)
            handle=kernel.CreateFileW(str(root/'video.part'),0xC0000000,3,None,3,0x40000000,None);assert handle!=ctypes.c_void_p(-1).value
            event=kernel.CreateEventW(None,True,False,None);ov=Overlapped();ov.hEvent=event;count=w.DWORD()
            ok=kernel.DeviceIoControl(handle,0x90000,None,0,None,0,ctypes.byref(count),ctypes.byref(ov));assert not ok and ctypes.get_last_error()==997
            process=subprocess.Popen([str(player),'/harvester-session',str(manifest),'/nofocus'],cwd=player.parent)
            assert kernel.WaitForSingleObject(event,10000)==0,'Opening never reached real media file'
            windows=[]
            @callback
            def visit(hwnd,_):
                owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner));name=ctypes.create_unicode_buffer(256);user.GetClassNameW(hwnd,name,256)
                if owner.value==process.pid and name.value=='FoxicMP':windows.append(hwnd)
                return True
            user.EnumWindows(visit,0);assert len(windows)==1
            times=[]
            for _ in range(3):
                out=ctypes.c_size_t();start=time.perf_counter_ns()
                assert user.SendMessageTimeoutW(windows[0],0,0,0,2,500,ctypes.byref(out)),'UI blocked in legacy opening'
                times.append((time.perf_counter_ns()-start)/1000000)
            client=ProgressiveClient(str(player));caps=client.request(dict(version=1,action='capabilities'));assert caps['pid']==process.pid and caps['legacyPreparationPending'],caps
            if mode in ('close','stop'):
                post_command(process.pid,803 if mode=='close' else 890);time.sleep(.15)
            elif mode=='timeout':time.sleep(5.3)
            kernel.CloseHandle(handle);handle=None;kernel.CloseHandle(event);event=None
            message=root/('player-'+str(process.pid)+'.json')
            if mode=='commit':
                notification=wait(lambda:read(message) if read(message).get('type')=='playing' else None,12)
                post_command(process.pid,803)
            else:
                # The detached open may attach after cancellation, but it must
                # never commit playback and must release the acquired lease.
                wait(lambda:not client.request(dict(version=1,action='capabilities'))['legacyPreparationPending'],10)
                wait(lambda:read(root/'leases.json').get('leases')=={},10)
                time.sleep(.8);notification=read(message);assert notification.get('type')!='playing',notification
            wait(lambda:read(root/'leases.json').get('leases')=={},10)
            cases.append(dict(mode=mode,actualMediaOpenBlocked=True,uiPingMilliseconds=times,notification=notification,providerLeasesReleased=True))
        finally:
            if handle is not None:kernel.CloseHandle(handle)
            if event is not None:kernel.CloseHandle(event)
            if process is not None and process.poll() is None:
                post_command(process.pid,0)
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait();raise
            (root/'fixture-stop').touch()
            try:provider.wait(timeout=8)
            except subprocess.TimeoutExpired:provider.kill();provider.wait();raise
            error=provider.stderr.read();provider.stdout.close();provider.stderr.close();assert provider.returncode==0,error
            if process is not None:assert process.returncode==0,process.returncode
            assert registration_snapshot()==registration,'Installed Classic shell registration changed'
    return dict(contract='legacy-opening',result='PASS',cases=cases)


def run_legacy_maintenance(args):
    """Overlap real legacy fragment-index reads with UI messages and Stop."""
    player=Path(args.player).resolve();registration=registration_snapshot()
    ini=player.with_suffix('.ini').read_bytes();settings=configparser.ConfigParser();settings.read_string(ini.decode('utf-16') if ini.startswith((b'\xff\xfe',b'\xfe\xff')) else ini.decode('utf-8-sig'))
    assert settings.get('Settings','ContextMenuPrompted')=='1' and settings.get('Settings','ContextMenuEnabled')=='0'
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);user=ctypes.WinDLL('user32',use_last_error=True)
    class Io(ctypes.Structure):
        _fields_=[(name,ctypes.c_ulonglong) for name in ('reads','writes','other','readBytes','writeBytes','otherBytes')]
    kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE
    kernel.GetProcessIoCounters.argtypes=[w.HANDLE,ctypes.POINTER(Io)];kernel.CloseHandle.argtypes=[w.HANDLE]
    callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM);user.EnumWindows.argtypes=[callback,w.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)];user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
    user.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,ctypes.POINTER(ctypes.c_size_t)]
    def read(path):
        try:return json.loads(path.read_text(encoding='utf-8'))
        except (OSError,ValueError):return {}
    def wait(check,seconds=12):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            result=check()
            if result:return result
            time.sleep(.01)
        raise AssertionError('Legacy maintenance condition timed out')
    cases=[]
    for mode in ('grow','stop','unchanged-truncate'):
        root=Path(args.report)/mode;root.mkdir(parents=True)
        value=dict(schemaVersion=1,sessionId=uuid.uuid4().hex,generation=1,revision=1,state='downloading',duration100ns=200000000,tracks={})
        for kind,name in [('video','video.mp4'),('audio','audio.m4a')]:
            data=(Path(args.fixtures)/'progressive'/name).read_bytes();length=prefix(data,5);(root/(kind+'.part')).write_bytes(data[:length])
            value['tracks'][kind]=dict(path=kind+'.part',container='mp4' if kind=='video' else 'm4a',fragmented=True,initializationReady=True,availablePrefixBytes=length,expectedTotalBytes=None,state='downloading')
        manifest=root/'session.json';manifest.write_text(json.dumps(value),encoding='utf-8')
        provider=subprocess.Popen([sys.executable,str(Path(__file__).resolve().parents[1]/'progressive_legacy_fixture.py'),'--manifest',str(manifest),'--fixtures',str(Path(args.fixtures).resolve())],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        process=None;handle=None
        try:
            wait(lambda:(root/'fixture-ready.json').exists(),8)
            process=subprocess.Popen([str(player),'/harvester-session',str(manifest),'/nofocus'],cwd=player.parent)
            message=root/('player-'+str(process.pid)+'.json');initial=wait(lambda:read(message));assert initial['revision']==1
            client=ProgressiveClient(str(player));caps=client.request(dict(version=1,action='capabilities'));assert caps['pid']==process.pid and caps['legacyRefresh']['fileIoOutsideMetadataLock']
            windows=[]
            @callback
            def visit(hwnd,_):
                owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner));name=ctypes.create_unicode_buffer(256);user.GetClassNameW(hwnd,name,256)
                if owner.value==process.pid and name.value=='FoxicMP':windows.append(hwnd)
                return True
            user.EnumWindows(visit,0);assert len(windows)==1
            handle=kernel.OpenProcess(0x1000,False,process.pid);assert handle
            def count():
                value=Io();assert kernel.GetProcessIoCounters(handle,ctypes.byref(value));return value.reads
            times=[];reads=0
            if mode!='unchanged-truncate':
                before=count();(root/'fixture-control').write_text('slow-grow')
                end=time.monotonic()+6
                while count()<before+500 and time.monotonic()<end:time.sleep(.002)
                reads=count()-before;assert reads>=500,'No actual fragment-index reads observed'
                assert read(message).get('revision',1)==1,'Legacy indexing finished before the overlap trial'
                for _ in range(3):
                    out=ctypes.c_size_t();started=time.perf_counter_ns()
                    assert user.SendMessageTimeoutW(windows[0],0,0,0,2,500,ctypes.byref(out)),'UI blocked in legacy maintenance'
                    times.append((time.perf_counter_ns()-started)/1000000)
                assert read(message).get('revision',1)==1,'No sustained overlap with legacy indexing'
                if mode=='stop':
                    post_command(process.pid,890)
                    wait(lambda:read(root/'leases.json').get('leases')=={},10)
                    notification=read(message);assert notification.get('type')!='error',notification
                else:
                    notification=wait(lambda:read(message) if read(message).get('revision')==2 and read(message).get('readyEnd100ns',0)>=100000000 else None)
            else:
                with (root/'video.part').open('r+b') as stream:stream.truncate(0)
                # An unchanged manifest must not suppress physical-file checks.
                # The UI may consume the error before the provider sees it; in
                # either case the acquired lease must be released without EOF.
                wait(lambda:read(root/'leases.json').get('leases')=={},10)
                notification=read(message);assert notification.get('type')!='playing',notification
            post_command(process.pid,803);wait(lambda:read(root/'leases.json').get('leases')=={},10)
            cases.append(dict(mode=mode,readsAtOverlap=reads,uiPingMilliseconds=times,notification=notification,providerLeasesReleased=True))
        finally:
            if handle:kernel.CloseHandle(handle)
            if process is not None and process.poll() is None:
                post_command(process.pid,0)
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait();raise
            (root/'fixture-stop').touch()
            try:provider.wait(timeout=8)
            except subprocess.TimeoutExpired:provider.kill();provider.wait();raise
            error=provider.stderr.read();provider.stdout.close();provider.stderr.close();assert provider.returncode==0,error
            if process is not None:assert process.returncode==0,process.returncode
            assert registration_snapshot()==registration,'Installed Classic shell registration changed'
    return dict(contract='legacy-maintenance',result='PASS',cases=cases)


def run_legacy(args):
    """Check the shared Classic index refactor through its retained provider CLI."""
    player=Path(args.player).resolve();registration=registration_snapshot();work=Path(args.report)/'legacy';work.mkdir(parents=True)
    ini=player.with_suffix('.ini').read_bytes();settings=configparser.ConfigParser();settings.read_string(ini.decode('utf-16') if ini.startswith((b'\xff\xfe',b'\xfe\xff')) else ini.decode('utf-8-sig'))
    assert settings.get('Settings','ContextMenuPrompted')=='1' and settings.get('Settings','ContextMenuEnabled')=='0'
    value=dict(schemaVersion=1,sessionId=uuid.uuid4().hex,generation=1,revision=1,state='downloading',duration100ns=200000000,tracks={})
    for kind,name in [('video','video.mp4'),('audio','audio.m4a')]:
        data=(Path(args.fixtures)/'progressive'/name).read_bytes();length=prefix(data,5);(work/(kind+'.part')).write_bytes(data[:length])
        value['tracks'][kind]=dict(path=kind+'.part',container='mp4' if kind=='video' else 'm4a',fragmented=True,initializationReady=True,availablePrefixBytes=length,expectedTotalBytes=None,state='downloading')
    manifest=work/'session.json';manifest.write_text(json.dumps(value),encoding='utf-8')
    provider=subprocess.Popen([sys.executable,str(Path(__file__).resolve().parents[1]/'progressive_legacy_fixture.py'),'--manifest',str(manifest),'--fixtures',str(Path(args.fixtures).resolve())],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    process=None
    def wait(check,seconds=20):
        deadline=time.monotonic()+seconds;last=None
        while time.monotonic()<deadline:
            if provider.poll() is not None:raise AssertionError(('Provider exited',provider.returncode,provider.stderr.read()))
            last=check()
            if last:return last
            time.sleep(.05)
        raise AssertionError(('Legacy condition timed out',last))
    def read(path):
        try:return json.loads(path.read_text(encoding='utf-8'))
        except (OSError,ValueError):return {}
    try:
        wait(lambda:(work/'fixture-ready.json').exists(),8)
        process=subprocess.Popen([str(player),'/harvester-session',str(manifest),'/nofocus','/minimized'],cwd=player.parent)
        message=work/('player-'+str(process.pid)+'.json')
        attached=wait(lambda:read(message))
        assert attached['pid']==process.pid
        (work/'fixture-control').write_text('grow')
        seen_playing=False
        def playing():
            nonlocal seen_playing
            current=read(message)
            if current.get('type')=='error':raise AssertionError(current)
            seen_playing=seen_playing or current.get('type')=='playing'
            # The one-second provider sampling can skip the narrow playing
            # interval at 10 s and next report normal buffering near 11/12 s.
            return dict(current,observedPlaying=True) if seen_playing and current.get('type') in {'playing','buffering'} and current.get('position100ns',0)>=100000000 else None
        before=wait(playing,25)
        assert all((work/(kind+'.part')).stat().st_size<(Path(args.fixtures)/'progressive'/name).stat().st_size for kind,name in [('video','video.mp4'),('audio','audio.m4a')])
        (work/'fixture-control').write_text('complete')
        complete=wait(lambda:(read(message) if read(message).get('readyEnd100ns',0)>=190000000 else None),8)
        post_command(process.pid,803)
        wait(lambda:(work/'leases.json').exists() and read(work/'leases.json').get('leases')=={},10)
        return dict(contract='legacy-provider',result='PASS',beforeComplete=before,complete=complete,filesClosedLeaseReleased=True)
    finally:
        if process is not None and process.poll() is None:
            post_command(process.pid,0)
            try:process.wait(timeout=10)
            except subprocess.TimeoutExpired:process.kill();process.wait();raise
        (work/'fixture-stop').touch()
        try:provider.wait(timeout=8)
        except subprocess.TimeoutExpired:provider.kill();provider.wait();raise
        output=provider.stderr.read();provider.stdout.close();provider.stderr.close()
        assert provider.returncode==0,output
        if process is not None:assert process.returncode==0,process.returncode
        assert registration_snapshot()==registration,'Installed Classic shell registration changed'


def run(args):
    player = Path(args.player).resolve()
    ini = player.with_suffix('.ini')
    before = ini.read_bytes()
    settings = configparser.ConfigParser()
    settings.read_string(before.decode('utf-16') if before.startswith((b'\xff\xfe', b'\xfe\xff')) else before.decode('utf-8-sig'))
    assert settings.get('Settings', 'ContextMenuPrompted') == '1'
    assert settings.get('Settings', 'ContextMenuEnabled') == '0'
    registration = registration_snapshot()
    client = ProgressiveClient(str(player))
    assert ini.read_bytes() == before, 'Read-only discovery modified the profile'
    process = subprocess.Popen([str(player), '/new', '/nofocus', '/minimized'], cwd=player.parent)
    results = []
    try:
        deadline = time.monotonic()+20
        while True:
            try:
                caps = client.request({'version': 1, 'action': 'capabilities'})
                assert caps['pid'] == process.pid, 'Unexpected test endpoint owner'
                break
            except (OSError, RuntimeError):
                if process.poll() is not None: raise RuntimeError(('Player exited', process.returncode))
                if time.monotonic() >= deadline: raise
                time.sleep(.1)
        if args.catalog_playback_only:
            return [dict(contract='actual-paged-catalog-playback',result=exercise_catalog_playback(client,args.fixtures,Path(args.report)/('catalog-playback-'+profile),pause=lambda:post_command(process.pid,889),seek=lambda seconds:post_command(process.pid,4400+{2:1,35:2,68:3,87:4}[seconds]),profile=profile)) for profile in ('h264','hevc10-opus','av1-10')]
        if args.mux_ts_playback_only:
            from progressive_ts_playback import exercise as exercise_ts
            return [dict(contract='actual-muxed-mpegts-playback',result=exercise_ts(client,args.fixtures,Path(args.report)/('mux-ts-'+profile+('-catalog' if catalog else '')),pause=lambda:post_command(process.pid,889),seek=lambda seconds:post_command(process.pid,4401 if seconds==2 else 4402),profile=profile,catalog=catalog,muxed=True)) for profile,catalog in [('main-bframes',False),('high-fractional',False),('main-preroll',False),('main-bframes',True)]]
        if args.ts_playback_only:
            from progressive_ts_playback import exercise as exercise_ts
            return [dict(contract='actual-mpegts-playback',result=exercise_ts(client,args.fixtures,Path(args.report)/('ts-'+profile+('-catalog' if catalog else '')),pause=lambda:post_command(process.pid,889),seek=lambda seconds:post_command(process.pid,4401 if seconds==2 else 4402),profile=profile,catalog=catalog)) for profile,catalog in [('main-bframes',False),('high-fractional',False),('main-preroll',False),('main-bframes',True)]]
        if args.webm_segment_playback_only:
            from progressive_webm_segment_playback import PROFILES as WEBM_PROFILES
            from progressive_webm_segment_playback import exercise_catalog as exercise_webm_catalog
            inline=[dict(contract='actual-webm-segmented-playback',result=exercise_segment_playback(client,args.fixtures,Path(args.report)/('webm-segment-playback-'+profile),pause=lambda:post_command(process.pid,889),seek=lambda seconds:post_command(process.pid,4401),profile=profile,webm=True)) for profile in WEBM_PROFILES]
            paged=[dict(contract='actual-webm-catalog-playback',result=exercise_webm_catalog(client,args.fixtures,Path(args.report)/('webm-catalog-'+profile),pause=lambda:post_command(process.pid,889),seek=lambda seconds:post_command(process.pid,4401 if seconds==2 else 4402),profile=profile)) for profile in ('vp8','vp9-10','av1-10')]
            return inline+paged
        if args.segment_playback_only:
            return [dict(contract='actual-segmented-playback',result=exercise_segment_playback(client,args.fixtures,Path(args.report)/('segment-playback-'+profile),pause=lambda:post_command(process.pid,889),seek=lambda seconds:post_command(process.pid,4401),profile=profile)) for profile in PLAYBACK_PROFILES]
        if args.segment_only:
            return [dict(contract='segment-staging-index-only',result=exercise_segment_contract(client,Path(args.report)/'segment-probe',args.fixtures))]
        if args.directory_only:
            return [dict(contract='sessionDirectory',scenarios=exercise_directory_contract(client,args.fixtures,Path(args.report)/'relative-contract'))]
        if args.metadata_only:
            folder=Path(args.report)/'metadata'
            result=exercise_metadata(client,args.fixtures,folder,select_chapter=lambda index:post_command(process.pid,4400+index),check_title=lambda title:check_window_title(process.pid,title))
            (folder/'conformance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
            return [dict(contract='actual-metadata-ui',result=result)]
        if args.partial_audio_profile or args.partial_audio_only or args.active_audio_only:
            results=[]
            for profile in ([args.partial_audio_profile] if args.partial_audio_profile else ACTIVE_AUDIO_PROFILES if args.active_audio_only else PARTIAL_AUDIO_PROFILES):
                folder=Path(args.report)/('partial-audio-'+profile)
                result=exercise_partial_audio(client,args.fixtures,folder,profile,select_audio=lambda index:post_command(process.pid,2200+index),pause=lambda:post_command(process.pid,889),seek=lambda seconds:post_command(process.pid,4401))
                (folder/'conformance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
                results.append(dict(contract='partial-optional-audio',result=result))
            return results
        if args.optional_audio_only or args.optional_audio_profile:
            results=[]
            for profile in ([args.optional_audio_profile] if args.optional_audio_profile else OPTIONAL_AUDIO_PROFILES):
                folder=Path(args.report)/('audio-'+profile)
                result=exercise_optional_audio(client,args.fixtures,folder,profile,select_audio=lambda:post_command(process.pid,952),seek=lambda seconds:seek_backward(process.pid,seconds))
                (folder/'conformance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
                results.append(dict(contract='optional-audio',result=result))
            return results
        if args.subtitle_only:
            results=[]
            for profile in SRT_ISOLATION_PROFILES:
                folder=Path(args.report)/('srt-'+profile)
                result=exercise_srt_isolation(client,args.fixtures,folder,profile,select_subtitle=lambda:post_command(process.pid,954),seek=lambda seconds:seek_backward(process.pid,seconds))
                (folder/'conformance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
                results.append(dict(contract='optional-srt-isolation',result=result))
            return results
        if args.multi_only:
            return [dict(contract='multi-audio-subtitles',result=exercise_multitrack(client,args.fixtures,Path(args.report)/'multi-audio-subtitles',select_audio=lambda _:post_command(process.pid,952),select_subtitle=lambda:post_command(process.pid,954),seek=lambda seconds:seek_backward(process.pid,seconds)))]
        if args.media_profile:
            return [dict(contract='v2-media',result=exercise_v2_playback(client,args.fixtures,Path(args.report)/('schema-2-media-'+args.media_profile),args.media_profile,seek=lambda seconds:seek_backward(process.pid,seconds)))]
        if args.registry_only:
            return [dict(contract='reader-registry-oplock',result=exercise_registry_oplock(client,process.pid,args.fixtures,Path(args.report)/'registry-oplock'))]
        if args.publication_only:
            return [dict(contract='async-publication',result=exercise_publication(client,process.pid,args.fixtures,Path(args.report)/'async-publication'))]
        if args.preparation_only:
            return [dict(contract='async-preparation',result=exercise_preparation(client,process.pid,args.fixtures,Path(args.report)/'async-preparation'))]
        if args.sdk_only:
            model_cases = check_v2_model(client)
            readers = exercise_reader_contract(client, args.fixtures, Path(args.report)/'reader-contract')
            active_root=Path(args.report)/'resource-active-v1';active_root.mkdir()
            active_snapshot=dict(schemaVersion=1,sessionId=uuid.uuid4().hex,generation=1,revision=1,
                state='downloading',duration100ns=200000000,tracks={})
            for kind,name in [('video','video.mp4'),('audio','audio.m4a')]:
                data=(Path(args.fixtures)/'progressive'/name).read_bytes();count=prefix(data,12)
                target=active_root/(kind+'.part');target.write_bytes(data[:count])
                active_snapshot['tracks'][kind]=dict(path=target.name,container='mp4' if kind=='video' else 'm4a',
                    fragmented=True,initializationReady=True,availablePrefixBytes=count,expectedTotalBytes=None,state='downloading')
            active_reply=client.start(active_snapshot,session_directory=active_root,activate=False,track_reader=True)
            active_reader=active_reply['readerId']
            try:
                wait_status(client,active_snapshot['sessionId'],lambda reply:reply['state']=='playing',15)
                resources=exercise_resource_contract(client,Path(args.report)/'resource-contract')
                preserved=client.command('status',active_snapshot['sessionId'],readerId=active_reader)
                assert preserved['active'] and preserved['readerId']==active_reader
                resources['activeV1PlaybackPreserved']=True
            finally:
                if client.release_reader(active_reader,timeout=10)['released']:client.forget_reader(active_reader)
            for _ in range(100):
                assert client.request(dict(version=1, action='capabilities'))['pid'] == process.pid
            sdk = run_sdk_writer(client, Path(args.fixtures)/'progressive/video.mp4',
                Path(args.fixtures)/'progressive/audio.m4a', Path(args.report)/'sdk-writer')
            media = {profile:exercise_v2_playback(client,args.fixtures,Path(args.report)/('schema-2-media-'+profile),profile,seek=lambda seconds:seek_backward(process.pid,seconds)) for profile in MEDIA_PROFILES}
            multi = exercise_multitrack(client,args.fixtures,Path(args.report)/'multi-audio-subtitles',select_audio=lambda _:post_command(process.pid,952),select_subtitle=lambda:post_command(process.pid,954),seek=lambda seconds:seek_backward(process.pid,seconds))
            registry = exercise_registry_oplock(client,process.pid,args.fixtures,Path(args.report)/'registry-oplock')
            publication = exercise_publication(client,process.pid,args.fixtures,Path(args.report)/'async-publication')
            preparation = exercise_preparation(client,process.pid,args.fixtures,Path(args.report)/'async-preparation')
            return [dict(contract='reader-registry-oplock',result=registry),dict(contract='async-publication',result=publication),dict(contract='async-preparation',result=preparation),dict(contract='sdk-writer', result=sdk),dict(contract='v2-metadata-validation',passed=model_cases),dict(contract='reader-leases',result=readers),dict(contract='resource-io',result=resources),dict(contract='v2-media',result=media),dict(contract='multi-audio-subtitles',result=multi)]
        common = exercise_directory_contract(client, args.fixtures, Path(args.report)/'relative-contract')
        results.append(dict(contract='sessionDirectory', scenarios=common))
        sdk = run_sdk_writer(client, Path(args.fixtures)/'progressive/video.mp4',
            Path(args.fixtures)/'progressive/audio.m4a', Path(args.report)/'sdk-writer')
        results.append(dict(contract='sdk-writer', result=sdk))
        for fragmented in (False, True):
            folder = Path(args.report)/('fragmented' if fragmented else 'indexed')
            folder.mkdir(parents=True)
            fixture = Path(args.fixtures)/('progressive' if fragmented else 'progressive-indexed')
            data = [(fixture / name).read_bytes() for name in ('video.mp4','audio.m4a')]
            paths = [folder/'видео.part', folder/'звук.part']
            snapshot = dict(schemaVersion=1, sessionId=uuid.uuid4().hex, generation=1, revision=1,
                            state='downloading', duration100ns=200000000, tracks={})
            for i, kind in enumerate(('video','audio')):
                count = prefix(data[i], 5) if fragmented else len(data[i])*3//10
                paths[i].write_bytes(data[i][:count])
                snapshot['tracks'][kind] = dict(path=str(paths[i]), container='m4a' if i else 'mp4',
                    fragmented=fragmented, initializationReady=True, availablePrefixBytes=count,
                    expectedTotalBytes=None if fragmented else len(data[i]), state='downloading')
            first = client.start(snapshot, title="Проверка недокачанного ролика", activate=False)
            assert first['state'] == 'buffering', first
            assert client.start(snapshot, title="Проверка недокачанного ролика", activate=False)['sessionId'] == snapshot['sessionId']
            try:
                client.command('cancel', 'f'*32)
                raise AssertionError('Stale cancel was accepted')
            except ApiError as error: assert error.code == 'unknown_session'
            time.sleep(.3)
            initial = client.command('status', snapshot['sessionId'])
            assert initial['positionSeconds'] < .2, initial
            snapshot['revision'] += 1
            for i, kind in enumerate(('video','audio')):
                count = prefix(data[i], 12) if fragmented else len(data[i])*7//10
                with paths[i].open('ab') as stream: stream.write(data[i][stream.tell():count])
                snapshot['tracks'][kind]['availablePrefixBytes'] = count
            client.update(snapshot)
            playing = wait_status(client, snapshot['sessionId'], lambda r:r['state']=='playing' and r['positionSeconds']>.2)
            try:
                client.update(snapshot)
                raise AssertionError('Stale update was accepted')
            except ApiError as error: assert error.code == 'invalid_update'
            post_command(process.pid, 888)
            wait_status(client, snapshot['sessionId'], lambda r:r['state']=='paused')
            for i, kind in enumerate(('video','audio')):
                final = paths[i].with_suffix('.done'); paths[i].rename(final)
                with final.open('ab') as stream: stream.write(data[i][stream.tell():])
                snapshot['tracks'][kind].update(availablePrefixBytes=len(data[i]), expectedTotalBytes=len(data[i]), state='complete')
            snapshot.update(state='complete', revision=snapshot['revision']+1)
            completed = client.update(snapshot, complete=True)
            assert completed['complete'] and completed['state']=='paused', completed
            post_command(process.pid, 887)
            terminal = wait_status(client, snapshot['sessionId'], lambda r:r['state']=='finished', 28)
            cancelled = client.command('cancel', snapshot['sessionId'])
            assert not cancelled['active'], cancelled
            results.append(dict(fragmented=fragmented, playing=playing, complete=completed, terminal=terminal))
        return results
    finally:
        if process.poll() is None:
            post_command(process.pid, 0)
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill();process.wait();raise
        assert process.returncode == 0, ('Player exit', process.returncode)
        assert registration_snapshot() == registration, 'Installed Classic shell registration changed'


if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--player',required=True);parser.add_argument('--fixtures',required=True);parser.add_argument('--report',required=True)
    parser.add_argument('--media-profile',choices=MEDIA_PROFILES)
    parser.add_argument('--active-audio-only',action='store_true')
    parser.add_argument('--partial-audio-only',action='store_true')
    parser.add_argument('--partial-audio-profile',choices=PARTIAL_AUDIO_PROFILES)
    parser.add_argument('--optional-audio-profile',choices=OPTIONAL_AUDIO_PROFILES)
    parser.add_argument('--optional-audio-only',action='store_true')
    parser.add_argument('--subtitle-only',action='store_true')
    parser.add_argument('--metadata-only',action='store_true')
    parser.add_argument('--directory-only',action='store_true')
    parser.add_argument('--segment-only',action='store_true')
    parser.add_argument('--segment-playback-only',action='store_true')
    parser.add_argument('--webm-segment-playback-only',action='store_true')
    parser.add_argument('--ts-playback-only',action='store_true')
    parser.add_argument('--mux-ts-playback-only',action='store_true')
    parser.add_argument('--catalog-playback-only',action='store_true')
    parser.add_argument('--multi-only',action='store_true')
    parser.add_argument('--sdk-only', action='store_true')
    parser.add_argument('--preparation-only', action='store_true')
    parser.add_argument('--publication-only', action='store_true')
    parser.add_argument('--legacy-only', action='store_true')
    parser.add_argument('--registry-only', action='store_true')
    parser.add_argument('--legacy-opening-only', action='store_true')
    parser.add_argument('--legacy-maintenance-only', action='store_true')
    arguments=parser.parse_args();report=Path(arguments.report);report.mkdir(parents=True,exist_ok=True)
    try:
        result=run_legacy_maintenance(arguments) if arguments.legacy_maintenance_only else run_legacy_opening(arguments) if arguments.legacy_opening_only else run_legacy(arguments) if arguments.legacy_only else run(arguments);(report/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    except BaseException:
        (report/'exception.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
