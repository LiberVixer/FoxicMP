"""Windows-only current-user named-pipe provider; retained leases survive restart."""
from __future__ import annotations
import ctypes as C
from ctypes import wintypes as W
import json
import os
import threading
import time
from pathlib import Path
from scripts.progressive_session import Leases, MAX_JSON, atomic_json, read_json

INVALID = C.c_void_p(-1).value


class Win32:
    def __init__(self):
        if os.name != "nt":
            raise OSError("Progressive playback requires Windows")
        self.k = C.WinDLL("kernel32", use_last_error=True)
        self.a = C.WinDLL("advapi32", use_last_error=True)
        class Overlapped(C.Structure):
            _fields_ = [("Internal", C.c_size_t), ("InternalHigh", C.c_size_t),
                        ("Offset", W.DWORD), ("OffsetHigh", W.DWORD), ("hEvent", W.HANDLE)]
        self.Overlapped = Overlapped
        signatures = {
            "CreateNamedPipeW": (W.HANDLE, [W.LPCWSTR, W.DWORD, W.DWORD, W.DWORD, W.DWORD, W.DWORD, W.DWORD, C.c_void_p]),
            "CreateEventW": (W.HANDLE, [C.c_void_p, W.BOOL, W.BOOL, W.LPCWSTR]),
            "OpenProcess": (W.HANDLE, [W.DWORD, W.BOOL, W.DWORD]),
            "GetCurrentProcess": (W.HANDLE, []),
            "GetProcessTimes": (W.BOOL, [W.HANDLE, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p]),
            "GetExitCodeProcess": (W.BOOL, [W.HANDLE, C.POINTER(W.DWORD)]),
            "CloseHandle": (W.BOOL, [W.HANDLE]),
            "ConnectNamedPipe": (W.BOOL, [W.HANDLE, C.c_void_p]),
            "DisconnectNamedPipe": (W.BOOL, [W.HANDLE]),
            "GetNamedPipeClientProcessId": (W.BOOL, [W.HANDLE, C.POINTER(W.ULONG)]),
            "ReadFile": (W.BOOL, [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p]),
            "WriteFile": (W.BOOL, [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p]),
            "WaitForSingleObject": (W.DWORD, [W.HANDLE, W.DWORD]),
            "GetOverlappedResult": (W.BOOL, [W.HANDLE, C.c_void_p, C.POINTER(W.DWORD), W.BOOL]),
            "CancelIoEx": (W.BOOL, [W.HANDLE, C.c_void_p]),
            "LocalFree": (W.HANDLE, [W.HANDLE]),
        }
        for name, (result, arguments) in signatures.items():
            fn = getattr(self.k, name)
            fn.restype, fn.argtypes = result, arguments
        self.a.OpenProcessToken.argtypes = [W.HANDLE, W.DWORD, C.POINTER(W.HANDLE)]
        self.a.GetTokenInformation.argtypes = [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)]
        self.a.ConvertSidToStringSidW.argtypes = [C.c_void_p, C.POINTER(W.LPWSTR)]
        self.a.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes = [W.LPCWSTR, W.DWORD, C.POINTER(C.c_void_p), C.c_void_p]

    def identity(self, pid):
        handle = self.k.OpenProcess(0x1000, False, pid)
        if not handle:
            return 0 if C.get_last_error() == 87 else None
        try:
            status = W.DWORD()
            if not self.k.GetExitCodeProcess(handle, C.byref(status)):
                return None
            if status.value != 259:
                return 0
            created, exited, kernel, user = (C.c_uint64() for _ in range(4))
            if not self.k.GetProcessTimes(handle, C.byref(created), C.byref(exited), C.byref(kernel), C.byref(user)):
                return None
            return created.value
        finally:
            self.k.CloseHandle(handle)

    def security(self, inherit=False):
        token = W.HANDLE()
        if not self.a.OpenProcessToken(self.k.GetCurrentProcess(), 8, C.byref(token)):
            raise C.WinError(C.get_last_error())
        try:
            length = W.DWORD()
            self.a.GetTokenInformation(token, 1, None, 0, C.byref(length))
            data = C.create_string_buffer(length.value)
            if not self.a.GetTokenInformation(token, 1, data, length, C.byref(length)):
                raise C.WinError(C.get_last_error())
            sid = C.cast(data, C.POINTER(C.c_void_p))[0]
            text = W.LPWSTR()
            if not self.a.ConvertSidToStringSidW(sid, C.byref(text)):
                raise C.WinError(C.get_last_error())
            try:
                descriptor = C.c_void_p()
                if not self.a.ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    f"D:P(A;{'OICI' if inherit else ''};GA;;;{text.value})", 1, C.byref(descriptor), None):
                    raise C.WinError(C.get_last_error())
                return descriptor
            finally:
                self.k.LocalFree(C.cast(text, W.HANDLE))
        finally:
            self.k.CloseHandle(token)

    def protect_directory(self, path):
        descriptor = self.security(inherit=True)
        self.a.SetFileSecurityW.argtypes = [W.LPCWSTR, W.DWORD, C.c_void_p]
        try:
            if not self.a.SetFileSecurityW(str(path), 4 | 0x80000000, descriptor):
                raise C.WinError(C.get_last_error())
        finally:
            self.k.LocalFree(descriptor)

    def operation(self, handle, op, timeout=1000, raw=None):
        ov = self.Overlapped()
        ov.hEvent = self.k.CreateEventW(None, True, False, None)
        count = W.DWORD()
        buffer = C.create_string_buffer(raw if raw is not None else 4096)
        try:
            if op == "connect":
                ok = self.k.ConnectNamedPipe(handle, C.byref(ov))
                error = C.get_last_error()
                if not ok and error == 535:  # connected between Create and Connect
                    return True
            else:
                fn = self.k.ReadFile if op == "read" else self.k.WriteFile
                ok = fn(handle, buffer, len(raw) if raw is not None else 4096, C.byref(count), C.byref(ov))
                error = C.get_last_error()
            if not ok and error == 997:
                if self.k.WaitForSingleObject(ov.hEvent, timeout) != 0:
                    self.k.CancelIoEx(handle, C.byref(ov))
                    self.k.GetOverlappedResult(handle, C.byref(ov), C.byref(count), True)
                    return None
                ok = self.k.GetOverlappedResult(handle, C.byref(ov), C.byref(count), False)
            if not ok:
                raise C.WinError(C.get_last_error())
            if op == "read":
                if not count.value:
                    raise OSError("Pipe closed")
                return buffer.raw[:count.value]
            return op == "connect" or count.value == len(raw)
        finally:
            self.k.CloseHandle(ov.hEvent)


class Provider:
    def __init__(self, manifest: Path, owner: int = 0, stop_file: Path | None = None):
        self.win = Win32()
        self.path = manifest
        self.initial = read_json(manifest)
        self.owner, self.owner_born = owner, self.win.identity(owner) if owner else None
        self.stop_file = stop_file
        self.leases = Leases(manifest.parent / "leases.json")
        self.stop = threading.Event()
        self.lock = threading.RLock()
        self.pending = 0
        self.seen_client = bool(self.leases.items)
        self.ready = threading.Event()
        self.error = None

    def notify(self, handle, state):
        value = {key: state[key] for key in ("schemaVersion", "sessionId", "generation", "revision")}
        value["type"] = "heartbeat"
        return self.win.operation(handle, "write", raw=(json.dumps(value) + "\n").encode())

    def client(self, handle):
        pid = W.ULONG()
        born = None
        try:
            if not self.win.k.GetNamedPipeClientProcessId(handle, C.byref(pid)):
                return
            born = self.win.identity(pid.value)
            if not born:
                return
            raw, attached, attached_generation, last = b"", False, None, 0.0
            while not self.stop.is_set():
                state = read_json(self.path)
                if time.monotonic() - last >= 1:
                    if not self.notify(handle, state):
                        return
                    last = time.monotonic()
                data = self.win.operation(handle, "read", timeout=200)
                if data is None:
                    continue
                raw += data
                if len(raw) > MAX_JSON:
                    return
                while b"\n" in raw:
                    line, raw = raw.split(b"\n", 1)
                    message = json.loads(line)
                    state = read_json(self.path)
                    if any(message.get(k) != state[k] for k in ("schemaVersion", "sessionId")):
                        return
                    if type(message.get("revision")) is not int or not 0 < message["revision"] <= state["revision"]:
                        return
                    if message.get("pid") != pid.value:
                        return
                    kind = message.get("type")
                    # A previous generation may only release its pinned files, never attach/read anew.
                    if kind == "released" and type(message.get("generation")) is int and 0 < message["generation"] <= state["generation"]:
                        self.leases.release(pid.value, born)
                        return
                    if message.get("generation") != state["generation"]:
                        if attached and message.get("generation") == attached_generation and kind in {"error", "buffering", "playing", "paused"}:
                            continue  # Keep the old connection long enough to receive released.
                        return
                    if kind == "attached":
                        with self.lock:
                            if self.stop.is_set():
                                return
                            self.leases.attach(pid.value, born)
                            self.seen_client = attached = True
                            attached_generation = message["generation"]
                    elif kind not in {"buffering", "playing", "paused", "error"} or not attached:
                        return
                    atomic_json(self.path.parent / f"player-{pid.value}.json", message)
        except (OSError, ValueError, KeyError, TypeError):
            pass
        finally:
            # A broken pipe is not a release. Reap only after confirmed process exit.
            self.win.k.DisconnectNamedPipe(handle)
            self.win.k.CloseHandle(handle)
            with self.lock:
                self.pending -= 1

    def accept(self):
        descriptor = self.win.security()
        class SecurityAttributes(C.Structure):
            _fields_ = [("nLength", W.DWORD), ("lpSecurityDescriptor", C.c_void_p), ("bInheritHandle", W.BOOL)]
        attrs = SecurityAttributes(C.sizeof(SecurityAttributes), descriptor.value, False)
        first = True
        try:
            while not self.stop.is_set():
                handle = self.win.k.CreateNamedPipeW(
                    "\\\\.\\pipe\\FoxicMP.Harvester." + self.initial["sessionId"],
                    3 | 0x40000000 | (0x80000 if first else 0), 8, 255, 65536, 65536, 0, C.byref(attrs))
                if handle == INVALID:
                    raise C.WinError(C.get_last_error())
                first = False
                self.ready.set()
                connected = False
                try:
                    while not self.stop.is_set() and not connected:
                        connected = bool(self.win.operation(handle, "connect", timeout=500))
                    with self.lock:
                        if connected and not self.stop.is_set():
                            self.pending += 1
                            threading.Thread(target=self.client, args=(handle,), daemon=True).start()
                            handle = INVALID
                finally:
                    if handle != INVALID:
                        self.win.k.CloseHandle(handle)
        except OSError as exc:
            self.error = str(exc)
            self.ready.set()
            self.stop.set()
        finally:
            self.win.k.LocalFree(descriptor)

    def run(self):
        threading.Thread(target=self.accept, daemon=True).start()
        if not self.ready.wait(5) or self.error:
            raise OSError(self.error or "Pipe did not start")
        atomic_json(self.path.parent / "provider.json", {"pid": os.getpid(), "born": self.win.identity(os.getpid()), "ready": True})
        idle = None
        while not self.stop.wait(1):
            state = read_json(self.path)
            if self.owner and state["state"] in {"downloading", "merging", "paused"}:
                actual = self.win.identity(self.owner)
                if actual == 0 or (actual is not None and actual != self.owner_born):
                    # The writer died. Never infer completion from leftover JSON/files.
                    state["state"] = "cancelled" if self.stop_file and self.stop_file.exists() else "failed"
                    state["revision"] += 1
                    for track in state["tracks"].values():
                        if track["state"] != "complete":
                            track["state"] = state["state"]
                    atomic_json(self.path, state)
            active = self.leases.reap(self.win.identity)
            # Failed/cancelled sources are retained for diagnosis/retry.
            if state["state"] in {"failed", "cancelled"} and not active and not self.pending:
                self.stop.set()
                break
            with self.lock:
                quiet = state["state"] == "complete" and not active and not self.pending
                idle = time.monotonic() if quiet and idle is None else idle if quiet else None
                if idle is not None and time.monotonic() - idle >= (30 if self.seen_client else 600):
                    self.stop.set()  # Reject new attachments before cleanup.
                    for track in state["tracks"].values():
                        name = track["path"]
                        if Path(name).name != name or any(c in name for c in '/\\:<>|?*"'):
                            raise ValueError("Unsafe cleanup path")
                        path = self.path.parent / name
                        if path.is_symlink():
                            raise ValueError("Unsafe cleanup symlink")
                        try:
                            path.unlink(missing_ok=True)
                        except OSError:
                            pass  # Windows file pin remains a final protection.
                    atomic_json(self.path.parent / "provider.json", {"pid": os.getpid(), "ready": False})
        return 0
