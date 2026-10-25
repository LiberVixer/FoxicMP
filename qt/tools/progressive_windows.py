"""Bounded byte-mode Windows named-pipe exchange; standard library only."""
from __future__ import annotations
import ctypes
from ctypes import wintypes as w
import time


def exchange(endpoint: str, raw: bytes, timeout: float, limit: int) -> bytes:
    if not endpoint.startswith('\\\\.\\pipe\\'):
        raise ValueError('Expected a local Windows named pipe')
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    class Overlapped(ctypes.Structure):
        _fields_ = [('Internal', ctypes.c_size_t), ('InternalHigh', ctypes.c_size_t),
                    ('Offset', w.DWORD), ('OffsetHigh', w.DWORD), ('hEvent', w.HANDLE)]
    api.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, ctypes.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
    api.CreateFileW.restype = w.HANDLE
    api.CreateEventW.argtypes = [ctypes.c_void_p, w.BOOL, w.BOOL, w.LPCWSTR]
    api.CreateEventW.restype = w.HANDLE
    api.CloseHandle.argtypes = [w.HANDLE]
    api.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    api.CancelIoEx.argtypes = [w.HANDLE, ctypes.POINTER(Overlapped)]
    api.WaitNamedPipeW.argtypes = [w.LPCWSTR, w.DWORD]
    api.GetOverlappedResult.argtypes = [w.HANDLE, ctypes.POINTER(Overlapped), ctypes.POINTER(w.DWORD), w.BOOL]
    for name in ('ReadFile', 'WriteFile'):
        getattr(api, name).argtypes = [w.HANDLE, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD), ctypes.POINTER(Overlapped)]
    deadline = time.monotonic() + timeout
    while True:
        handle = api.CreateFileW(endpoint, 0xC0000000, 0, None, 3, 0x40000000 | 0x00100000 | 0x00010000, None)
        if handle != w.HANDLE(-1).value:
            break
        error = ctypes.get_last_error()
        if error != 231:  # Retry only PIPE_BUSY, before any request bytes were sent.
            raise ctypes.WinError(error)
        remaining = int((deadline - time.monotonic()) * 1000)
        if remaining <= 0:
            raise TimeoutError('FoxicMP API pipe stayed busy')
        api.WaitNamedPipeW(endpoint, min(remaining, 250))
    # Verify the server's account, just as SO_PEERCRED does on Linux.
    advapi = ctypes.WinDLL('advapi32', use_last_error=True)
    api.GetNamedPipeServerProcessId.argtypes = [w.HANDLE, ctypes.POINTER(w.ULONG)]
    api.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    api.OpenProcess.restype = w.HANDLE
    api.GetCurrentProcess.restype = w.HANDLE
    advapi.OpenProcessToken.argtypes = [w.HANDLE, w.DWORD, ctypes.POINTER(w.HANDLE)]
    advapi.GetTokenInformation.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD)]
    advapi.GetLengthSid.argtypes = [ctypes.c_void_p]
    advapi.GetLengthSid.restype = w.DWORD
    def user_sid(process):
        token = w.HANDLE()
        if not advapi.OpenProcessToken(process, 8, ctypes.byref(token)):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            size = w.DWORD()
            advapi.GetTokenInformation(token, 1, None, 0, ctypes.byref(size))
            buffer = ctypes.create_string_buffer(size.value)
            if not advapi.GetTokenInformation(token, 1, buffer, size.value, ctypes.byref(size)):
                raise ctypes.WinError(ctypes.get_last_error())
            sid = ctypes.cast(buffer, ctypes.POINTER(ctypes.c_void_p))[0]
            return ctypes.string_at(sid, advapi.GetLengthSid(sid))
        finally:
            api.CloseHandle(token)
    server = None
    try:
        pid = w.ULONG()
        if not api.GetNamedPipeServerProcessId(handle, ctypes.byref(pid)):
            raise ctypes.WinError(ctypes.get_last_error())
        server = api.OpenProcess(0x1000, False, pid.value)
        if not server or user_sid(server) != user_sid(api.GetCurrentProcess()):
            raise RuntimeError('FoxicMP API belongs to another user')
    except BaseException:
        api.CloseHandle(handle)
        raise
    finally:
        if server: api.CloseHandle(server)
    def operation(data, writing=False):
        event = api.CreateEventW(None, True, False, None)
        if not event:
            raise ctypes.WinError(ctypes.get_last_error())
        overlapped = Overlapped(hEvent=event)
        count = w.DWORD()
        try:
            function = api.WriteFile if writing else api.ReadFile
            ok = function(handle, data, len(data), ctypes.byref(count), ctypes.byref(overlapped))
            if not ok:
                error = ctypes.get_last_error()
                if error != 997:
                    raise ctypes.WinError(error)
                remaining = max(0, int((deadline - time.monotonic()) * 1000))
                if api.WaitForSingleObject(event, remaining) != 0:
                    api.CancelIoEx(handle, ctypes.byref(overlapped))
                    api.GetOverlappedResult(handle, ctypes.byref(overlapped), ctypes.byref(count), True)
                    raise TimeoutError('FoxicMP API did not reply')
                if not api.GetOverlappedResult(handle, ctypes.byref(overlapped), ctypes.byref(count), False):
                    raise ctypes.WinError(ctypes.get_last_error())
            return count.value
        finally:
            api.CloseHandle(event)
    try:
        sent = 0
        while sent < len(raw):
            data = ctypes.create_string_buffer(raw[sent:], len(raw) - sent)
            count = operation(data, True)
            if not count: raise RuntimeError('Pipe closed while sending request')
            sent += count
        reply = bytearray()
        while b'\n' not in reply:
            data = ctypes.create_string_buffer(min(4096, limit + 1 - len(reply)))
            count = operation(data)
            if not count: raise RuntimeError('FoxicMP API closed without a reply')
            reply.extend(data.raw[:count])
            if len(reply) > limit: raise RuntimeError('FoxicMP API reply exceeds the limit')
        return bytes(reply)
    finally:
        api.CloseHandle(handle)
