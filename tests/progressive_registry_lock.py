"""Hold only the supplied private test registry guard, without changing receipts."""
from contextlib import contextmanager
from pathlib import Path
import ctypes
import os
import time

@contextmanager
def held_guard(registry):
    path=Path(registry)/'.guard';assert path.is_file()
    if os.name!='nt':
        import fcntl
        with path.open('r+b') as file:
            deadline=time.monotonic()+2
            while True:
                try:fcntl.flock(file,fcntl.LOCK_EX|fcntl.LOCK_NB);break
                except BlockingIOError:
                    assert time.monotonic()<deadline;time.sleep(.005)
            try:yield
            finally:fcntl.flock(file,fcntl.LOCK_UN)
        return
    from ctypes import wintypes as w
    class Overlapped(ctypes.Structure):
        _fields_=[('Internal',ctypes.c_size_t),('InternalHigh',ctypes.c_size_t),('Offset',w.DWORD),('OffsetHigh',w.DWORD),('hEvent',w.HANDLE)]
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    k.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,ctypes.c_void_p,w.DWORD,w.DWORD,w.HANDLE];k.CreateFileW.restype=w.HANDLE
    k.CloseHandle.argtypes=[w.HANDLE];k.LockFileEx.argtypes=[w.HANDLE,w.DWORD,w.DWORD,w.DWORD,w.DWORD,ctypes.POINTER(Overlapped)];k.UnlockFileEx.argtypes=[w.HANDLE,w.DWORD,w.DWORD,w.DWORD,ctypes.POINTER(Overlapped)]
    file=k.CreateFileW(str(path),0xc0000000,3,None,3,0,None)
    if file==w.HANDLE(-1).value:raise ctypes.WinError(ctypes.get_last_error())
    locked=False
    try:
        overlap=Overlapped();overlap.Offset=65536;deadline=time.monotonic()+2
        while not k.LockFileEx(file,3,0,1,0,ctypes.byref(overlap)):
            assert time.monotonic()<deadline;time.sleep(.005)
        locked=True;yield
    finally:
        if locked:k.UnlockFileEx(file,0,1,0,ctypes.byref(overlap))
        k.CloseHandle(file)
