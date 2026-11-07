"""Read-only Windows thread start addresses for owned test-player diagnostics.

The inventory follows a metric sample; it is a separate observation, never a
replacement for the original count or proof that an earlier budget passed.
"""
import ctypes
import os
import time


def windows_thread_inventory(pid):
    assert os.name == 'nt'
    from ctypes import wintypes as w
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    native = ctypes.WinDLL('ntdll')
    kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenThread.restype = w.HANDLE
    native.NtQueryInformationThread.argtypes = [w.HANDLE, w.ULONG, ctypes.c_void_p, w.ULONG, ctypes.c_void_p]
    native.NtQueryInformationThread.restype = ctypes.c_long

    class Thread(ctypes.Structure):
        _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('tid', w.DWORD), ('pid', w.DWORD),
                    ('priority', w.LONG), ('delta', w.LONG), ('flags', w.DWORD)]

    class Module(ctypes.Structure):
        _fields_ = [('size', w.DWORD), ('id', w.DWORD), ('pid', w.DWORD), ('globalCount', w.DWORD),
                    ('processCount', w.DWORD), ('base', ctypes.c_void_p), ('imageBytes', w.DWORD),
                    ('module', w.HMODULE), ('name', w.WCHAR*256), ('path', w.WCHAR*260)]

    for name, kind in (('Thread32First', Thread), ('Thread32Next', Thread),
                       ('Module32FirstW', Module), ('Module32NextW', Module)):
        method = getattr(kernel, name)
        method.argtypes = [w.HANDLE, ctypes.POINTER(kind)]
        method.restype = w.BOOL

    def entries(flags, target, kind, first, next):
        snapshot = kernel.CreateToolhelp32Snapshot(flags, target)
        if snapshot == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            value = kind()
            value.size = ctypes.sizeof(value)
            valid = getattr(kernel, first)(snapshot, ctypes.byref(value))
            while valid:
                yield value
                valid = getattr(kernel, next)(snapshot, ctypes.byref(value))
            if ctypes.get_last_error() != 18:
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            kernel.CloseHandle(snapshot)

    began = time.monotonic()
    modules = [dict(name=m.name, path=m.path, base=int(m.base), imageBytes=m.imageBytes)
               for m in entries(8|16, pid, Module, 'Module32FirstW', 'Module32NextW')]
    rows = []
    for thread in entries(4, 0, Thread, 'Thread32First', 'Thread32Next'):
        if thread.pid != pid:
            continue
        handle = kernel.OpenThread(0x40, False, thread.tid)
        if not handle:
            rows.append(dict(tid=thread.tid, queryError=ctypes.get_last_error()))
            continue
        try:
            address = ctypes.c_void_p()
            status = native.NtQueryInformationThread(handle, 9, ctypes.byref(address), ctypes.sizeof(address), None)
            row = dict(tid=thread.tid, status=status, start=hex(address.value or 0))
            if status >= 0:
                module = next((m for m in modules if m['base'] <= (address.value or 0) < m['base']+m['imageBytes']), None)
                if module:
                    row.update(module=module['name'], offset=hex(address.value-module['base']))
            rows.append(row)
        finally:
            kernel.CloseHandle(handle)
    return dict(pid=pid, startedMonotonic=began, finishedMonotonic=time.monotonic(), threads=rows, modules=modules)
