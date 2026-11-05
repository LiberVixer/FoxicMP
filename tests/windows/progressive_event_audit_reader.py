"""Read the exported audit buffer of an owned, separately linked test player."""
import collections
import ctypes
from ctypes import wintypes as w
from pathlib import Path
import struct
import time

from progressive_process_metrics import windows_handle_types
from progressive_windows_handle_trace import Trace


def export_rva(path, name='FoxicMPEventAuditData'):
    data = Path(path).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe+4] == b'PE\0\0'
    count = struct.unpack_from('<H', data, pe+6)[0]
    optional = pe+24
    size = struct.unpack_from('<H', data, pe+20)[0]
    assert struct.unpack_from('<H', data, optional)[0] == 0x20b
    sections = [struct.unpack_from('<IIII', data, optional+size+i*40+8) for i in range(count)]
    def offset(rva):
        for virtual_size, start, raw_size, raw_start in sections:
            if start <= rva < start+max(virtual_size, raw_size):
                result = raw_start+rva-start
                assert rva-start < raw_size and result < len(data)
                return result
        raise AssertionError(('Unmapped PE RVA', rva))
    export, length = struct.unpack_from('<II', data, optional+112)
    assert export and length
    directory = offset(export)
    functions, names, functions_rva, names_rva, ordinals_rva = struct.unpack_from('<IIIII', data, directory+20)
    assert names <= 100000 and functions <= 100000
    for index in range(names):
        entry = offset(struct.unpack_from('<I', data, offset(names_rva)+index*4)[0])
        end = data.index(b'\0', entry)
        if data[entry:end] == name.encode():
            ordinal = struct.unpack_from('<H', data, offset(ordinals_rva)+index*2)[0]
            assert ordinal < functions
            rva = struct.unpack_from('<I', data, offset(functions_rva)+ordinal*4)[0]
            assert not export <= rva < export+length, 'Audit data must not be a forwarded export'
            return rva
    raise AssertionError('Test executable has no Event audit export')


def capture(pid, executable, directory, kernel, process):
    modules = Trace.modules(type('Identity', (), dict(k=kernel, pid=pid))())
    image = next(m for m in modules if Path(m['path']).resolve() == Path(executable).resolve())
    address = image['base']+export_rva(executable)
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                        ctypes.POINTER(ctypes.c_size_t)]
    kernel.ReadProcessMemory.restype = w.BOOL
    def read(at, size):
        out = ctypes.create_string_buffer(size)
        count = ctypes.c_size_t()
        if not kernel.ReadProcessMemory(process, at, out, size, ctypes.byref(count)) or count.value != size:
            raise ctypes.WinError(ctypes.get_last_error())
        return out.raw
    stable = False
    for _ in range(5):
        before = struct.unpack('<8I5q2i', read(address, 80))
        magic, version, total, entry_size, capacity, module_size, module_capacity, enabled = before[:8]
        assert magic == 0x31415645 and version == 1 and total == 80+entry_size*capacity+module_size*module_capacity
        assert entry_size == 40 and capacity == 65536 and module_size == 192 and module_capacity == 4096
        assert total < 16*1024**2 and before[-1] == 0, before
        data = read(address, total)
        after = struct.unpack('<8I5q2i', read(address, 80))
        if before == after:
            stable = True
            break
        time.sleep(.01)
    Path(directory, 'event-audit-%d.bin'%pid).write_bytes(data)
    history = []
    for index in range(min(after[-2], module_capacity)):
        at = 80+capacity*entry_size+index*module_size
        base, size, sequence, reason, ready, _ = struct.unpack_from('<QQIIII', data, at)
        if ready:
            name = data[at+32:at+192].decode('utf-16-le').split('\0',1)[0]
            history.append(dict(base=base, size=size, sequence=sequence, reason=reason, name=name))
    typed = windows_handle_types(pid, True)
    events = {r['handle'] for r in typed[1] if r['type'] == 'Event'}
    attributed = []
    for index in range(capacity):
        handle, caller, sequence, thread, kind, module_sequence, _ = struct.unpack_from('<QQQIIII', data, 80+index*entry_size)
        if handle not in events:
            continue
        candidates = [m for m in history if m['sequence'] <= module_sequence and m['base'] <= caller < m['base']+m['size']]
        owner = max(candidates, key=lambda m:m['sequence']) if candidates else None
        attributed.append(dict(handle=handle, sequence=sequence, thread=thread, kind=kind,
            module=owner['name'] if owner else None, rva=hex(caller-owner['base']) if owner else None))
    counts = collections.Counter((r['module'],r['rva'],r['kind']) for r in attributed)
    hook_data = read(image['base']+export_rva(executable, 'FoxicMPEventAuditHooks'),32+32*72)
    Path(directory, 'event-audit-hooks-%d.bin'%pid).write_bytes(hook_data)
    hmagic,hversion,hbytes,hrow,hcapacity,hused,_,_ = struct.unpack_from('<8I',hook_data)
    assert (hmagic,hversion,hbytes,hrow,hcapacity)==(0x31485645,1,2336,72,32)
    hooks=[]
    kernel.OpenThread.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenThread.restype=w.HANDLE
    kernel.GetExitCodeThread.argtypes=[w.HANDLE,ctypes.POINTER(w.DWORD)];kernel.GetExitCodeThread.restype=w.BOOL
    kernel.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD];kernel.WaitForSingleObject.restype=w.DWORD
    kernel.GetThreadTimes.argtypes=[w.HANDLE]+[ctypes.POINTER(w.FILETIME)]*4;kernel.GetThreadTimes.restype=w.BOOL
    kernel.GetProcessIdOfThread.argtypes=[w.HANDLE];kernel.GetProcessIdOfThread.restype=w.DWORD
    for index in range(min(hused,hcapacity)):
        hook,caller,callback,created,thread,low,high,process_id,target,flags,kind,result,ready,_ = struct.unpack_from('<4Q10I',hook_data,32+index*hrow)
        if not ready:
            continue
        def resolve(pc):
            owner=next((m for m in modules if m['base']<=pc<m['base']+m['size']),None)
            return dict(module=owner['name'] if owner else None,rva=hex(pc-owner['base']) if owner else None)
        state=dict()
        identity=kernel.OpenThread(0x100800,False,thread)
        if identity:
            try:
                times=[w.FILETIME() for _ in range(4)]
                owner=kernel.GetProcessIdOfThread(identity)
                if kernel.GetThreadTimes(identity,*[ctypes.byref(t) for t in times]):
                    actual=times[0].dwLowDateTime|(times[0].dwHighDateTime<<32)
                    state['identityMatches']=owner==pid and actual==created and created!=0
                if state.get('identityMatches'):
                    code=w.DWORD()
                    if kernel.GetExitCodeThread(identity,ctypes.byref(code)):
                        state['exitCode']=code.value
                    state['signaled']=kernel.WaitForSingleObject(identity,0)==0
                else:
                    state['threadIdReusedOrUnverified']=True
            finally:
                kernel.CloseHandle(identity)
        else:
            state['openError']=ctypes.get_last_error()
        hooks.append(dict(hook=hook,caller=resolve(caller),callback=resolve(callback),thread=thread,
            eventMin=low,eventMax=high,process=process_id,targetThread=target,flags=flags,
            kind=kind,result=result,threadState=state))
    stack_address = image['base']+export_rva(executable, 'FoxicMPEventAuditStacks')
    stack_data = read(stack_address, 32+8*216)
    Path(directory, 'event-audit-stacks-%d.bin'%pid).write_bytes(stack_data)
    smagic, sversion, sbytes, row_bytes, scapacity, sused, senabled, _ = struct.unpack_from('<8I', stack_data)
    assert (smagic,sversion,sbytes,row_bytes,scapacity)==(0x31535645,1,1760,216,8)
    stacks = []
    for index in range(min(sused,scapacity)):
        values = struct.unpack_from('<25Q4I', stack_data, 32+index*row_bytes)
        handle, frames = values[0],values[1:25]
        thread,count,sequence,ready = values[25:]
        if not ready:
            continue
        resolved=[]
        for pc in frames[:count]:
            candidates=[m for m in history if m['sequence']<=sequence and m['base']<=pc<m['base']+m['size']]
            owner=max(candidates,key=lambda m:m['sequence']) if candidates else None
            resolved.append(dict(module=owner['name'] if owner else None,rva=hex(pc-owner['base']) if owner else None))
        stacks.append(dict(handle=handle,thread=thread,frames=resolved))
    user=ctypes.WinDLL('user32',use_last_error=True)
    callback_type=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    for name in ('EnumWindows','EnumChildWindows'):
        getattr(user,name).restype=w.BOOL
    user.EnumWindows.argtypes=[callback_type,w.LPARAM]
    user.EnumChildWindows.argtypes=[w.HWND,callback_type,w.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
    user.GetWindowThreadProcessId.restype=w.DWORD
    user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
    windows={}
    @callback_type
    def child(hwnd,unused):
        owner=w.DWORD();thread=user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        if owner.value==pid:
            name=ctypes.create_unicode_buffer(256);user.GetClassNameW(hwnd,name,len(name))
            windows[int(hwnd)]=dict(className=name.value,thread=thread)
        return True
    @callback_type
    def top(hwnd,unused):
        child(hwnd,unused)
        owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        if owner.value==pid:
            user.EnumChildWindows(hwnd,child,0)
        return True
    assert user.EnumWindows(top,0),ctypes.get_last_error()
    user.GetGuiResources.argtypes=[w.HANDLE,w.DWORD];user.GetGuiResources.restype=w.DWORD
    return dict(pid=pid, enabled=bool(enabled), snapshotStable=stable, version=version,
        recorded=after[8], closed=after[9], collisions=after[10], unidentifiedDuplicates=after[11],
        moduleOverflow=after[12], moduleNotifications=after[-2], queryableTypeCounts=typed[0],
        liveEventCount=len(events), attributed=attributed,
        unattributedEvents=sorted(events-{r['handle'] for r in attributed}),
        sites=[dict(module=k[0],rva=k[1],kind=k[2],count=v) for k,v in counts.most_common()],
        creationStacksEnabled=bool(senabled), creationStacksObserved=sused, creationStacks=stacks,
        winEventHooks=hooks,winEventHooksObserved=hused,
        windowsByClass=dict(collections.Counter(v['className'] for v in windows.values())),
        guiResources=dict(gdi=user.GetGuiResources(process,0),user=user.GetGuiResources(process,1)),
        rawBufferKeptLocal=True, noPlayerRequests=True)
