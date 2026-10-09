"""Local FoxicMP session protocol. No source URLs or credentials are persisted."""
from __future__ import annotations

import copy
import json
import os
import struct
import threading
import time
import uuid
from pathlib import Path

MAX_JSON = 65536
MAX_MOOV = 64 * 1024 * 1024
TERMINAL = {"complete", "cancelled", "failed"}


def atomic_json(path: Path, value: dict) -> None:
    raw = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(raw) > MAX_JSON:
        raise ValueError("Session state too large")
    temporary = path.with_name(path.name + ".new-" + uuid.uuid4().hex[:12])
    with temporary.open("wb", buffering=0) as stream:
        stream.write(raw)
        os.fsync(stream.fileno())
    # Reader shares DELETE for the manifest, but deliberately pins track names.
    try:
        # Windows scanners and older clients can briefly hold a handle without DELETE share.
        # Never expose a partially written JSON; only retry the atomic replacement itself.
        deadline=time.monotonic()+1
        while True:
            try:
                os.replace(temporary,path)
                break
            except OSError as error:
                if os.name!='nt' or getattr(error,'winerror',None) not in (5,32,33) or time.monotonic()>=deadline:raise
                time.sleep(.01)
    finally:
        try:temporary.unlink(missing_ok=True)
        except OSError:pass


def _json_reader(path):
    if os.name!='nt':return path.open('rb')
    import ctypes
    import msvcrt
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_void_p]
    kernel.CreateFileW.restype=ctypes.c_void_p
    kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    deadline=time.monotonic()+1
    while True:
        handle=kernel.CreateFileW(str(path),0x80000000,7,None,3,0,None)
        if handle!=ctypes.c_void_p(-1).value:break
        error=ctypes.get_last_error()
        if error not in (5,32,33) or time.monotonic()>=deadline:raise ctypes.WinError(error)
        time.sleep(.01)
    try:fd=msvcrt.open_osfhandle(handle,os.O_RDONLY|os.O_BINARY)
    except BaseException:kernel.CloseHandle(handle);raise
    return os.fdopen(fd,'rb')


def read_json(path: Path) -> dict:
    with _json_reader(path) as stream:
        raw = stream.read(MAX_JSON + 1)
    if len(raw) > MAX_JSON:
        raise ValueError("Session state too large")
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate JSON field")
            result[key] = value
        return result
    result = json.loads(raw, object_pairs_hook=unique)
    if not isinstance(result, dict):
        raise ValueError("Expected JSON object")
    return result


def _atoms(data: bytes, depth=0):
    if depth > 16:
        raise ValueError("Excessive MP4 nesting")
    result, position = {}, 0
    while position < len(data):
        if len(data) - position < 8:
            raise ValueError("Incomplete MP4 atom")
        size, kind = struct.unpack_from(">I4s", data, position)
        header = 8
        if size == 1:
            if len(data) - position < 16:
                raise ValueError("Incomplete extended MP4 atom")
            size = struct.unpack_from(">Q", data, position + 8)[0]
            header = 16
        if size < header or size > len(data) - position:
            raise ValueError("Invalid MP4 atom bounds")
        result.setdefault(kind, []).append(data[position + header:position + size])
        if sum(len(items) for items in result.values()) > 128:
            raise ValueError("Excessive MP4 atom count")
        position += size
    return result


def _one(atoms, kind):
    values = atoms.get(kind, [])
    if len(values) != 1:
        raise ValueError("Missing or duplicate MP4 index atom")
    return values[0]


def _indexed_moov(data: bytes, total: int, prefix: int, expected_kind=None) -> bool:
    try:
        movie = _atoms(data)
        _one(movie, b"mvhd")
        if b"mvex" in movie:
            return False  # Fragmented files need different index handling.
        track = _atoms(_one(movie, b"trak"))
        _one(track, b"tkhd")
        media = _atoms(_one(track, b"mdia"))
        _one(media, b"mdhd")
        handler = _one(media, b"hdlr")[8:12]
        if handler not in (b"vide", b"soun") or (expected_kind is not None and handler != (b"vide" if expected_kind == "video" else b"soun")):
            return False
        table = _atoms(_one(_atoms(_one(media, b"minf")), b"stbl"))
        description = _one(table, b"stsd")
        if len(description) < 8 or struct.unpack_from(">I", description, 4)[0] != 1:
            return False
        entries = _atoms(description[8:])
        _one(entries, b"avc1" if handler == b"vide" else b"mp4a")
        sizes = _one(table, b"stsz")
        if len(sizes) < 12:
            return False
        fixed, count = struct.unpack_from(">II", sizes, 4)
        if not 0 < count <= 2000000 or len(sizes) != 12 + (0 if fixed else count * 4):
            return False
        first_size = fixed or struct.unpack_from(">I", sizes, 12)[0]
        times = _one(table, b"stts")
        n = struct.unpack_from(">I", times, 4)[0]
        if not 0 < n <= count or len(times) != 8 + n * 8:
            return False
        timing = [struct.unpack_from(">II", times, 8 + i * 8) for i in range(n)]
        if sum(c for c, _ in timing) != count or any(not c or not d for c, d in timing):
            return False
        chunks = _one(table, b"stsc")
        n = struct.unpack_from(">I", chunks, 4)[0]
        if not 0 < n <= count or len(chunks) != 8 + n * 12:
            return False
        mapping = [struct.unpack_from(">III", chunks, 8 + i * 12) for i in range(n)]
        if mapping[0][0] != 1 or any(not a or not b or c != 1 for a, b, c in mapping):
            return False
        if any(mapping[i][0] >= mapping[i + 1][0] for i in range(n - 1)):
            return False
        if (b"stco" in table) == (b"co64" in table):
            return False
        wide = b"co64" in table
        offsets = _one(table, b"co64" if wide else b"stco")
        n = struct.unpack_from(">I", offsets, 4)[0]
        width = 8 if wide else 4
        if not 0 < n <= count or len(offsets) != 8 + n * width:
            return False
        positions = [struct.unpack_from(">Q" if wide else ">I", offsets, 8 + i * width)[0] for i in range(n)]
        if any(not p or p >= total for p in positions):
            return False
        if handler == b"vide" and b"stss" in table:
            sync = _one(table, b"stss")
            n = struct.unpack_from(">I", sync, 4)[0]
            if not n or len(sync) != 8 + n * 4 or struct.unpack_from(">I", sync, 8)[0] != 1:
                return False
        # The first decoder packet must be available as well as all index tables.
        return first_size > 0 and positions[0] <= prefix and first_size <= prefix - positions[0]
    except (ValueError, struct.error):
        return False


def _head(path: Path, prefix: int, total: int | None):
    """Return a bounded moov and the byte after it; never inspect unpublished bytes."""
    if prefix < 0 or (total is not None and prefix > total):
        return None
    position, ftyp = 0, False
    with path.open("rb", buffering=0) as stream:
        while position + 8 <= prefix:
            stream.seek(position)
            header = stream.read(8)
            if len(header) != 8:
                return None
            size, kind = struct.unpack(">I4s", header)
            header_size = 8
            if size == 1:
                if position + 16 > prefix:
                    return None
                size = struct.unpack(">Q", stream.read(8))[0]
                header_size = 16
            if size < header_size or (total is not None and size > total-position):
                return None
            if kind == b"ftyp":
                ftyp = position == 0 and size <= 4096
            if kind in (b"mdat", b"moof"):
                return None
            if kind == b"moov":
                if not ftyp or size > MAX_MOOV or position + size > prefix:
                    return None
                return stream.read(size-header_size), position+size
            if position + size > prefix:
                return None
            position += size
    return None


def fragmented_head(path: Path, prefix: int, total: int | None) -> bool:
    try:
        head = _head(path, prefix, total)
        return bool(head and b"mvex" in _atoms(head[0]))
    except (ValueError, struct.error, OSError):
        return False


def head_index_ready(path: Path, prefix: int, total: int | None, expected_kind=None) -> bool:
    """Indexed MP4 or a complete initial single-track moof/mdat for fragmented MP4."""
    try:
        head = _head(path, prefix, total)
        if not head:
            return False
        data, position = head
        movie = _atoms(data)
        if b"mvex" not in movie:
            return bool(total and _indexed_moov(data, total, prefix, expected_kind))
        track = _atoms(_one(movie, b"trak"))
        media = _atoms(_one(track, b"mdia"))
        handler = _one(media, b"hdlr")[8:12]
        if expected_kind and handler != (b"vide" if expected_kind=="video" else b"soun"):
            return False
        description = _one(_atoms(_one(_atoms(_one(media, b"minf")), b"stbl")), b"stsd")
        if struct.unpack_from(">I", description, 4)[0] != 1:
            return False
        _one(_atoms(description[8:]), b"avc1" if handler==b"vide" else b"mp4a")
        trex = _one(_atoms(_one(movie, b"mvex")), b"trex")
        if len(trex)!=24 or struct.unpack_from(">I",trex,8)[0]!=1:
            return False
        with path.open("rb", buffering=0) as stream:
            def atom(at):
                if at+8>prefix:return None
                stream.seek(at);size,kind=struct.unpack(">I4s",stream.read(8));h=8
                if size==1:
                    if at+16>prefix:return None
                    size=struct.unpack(">Q",stream.read(8))[0];h=16
                if size<h or size>prefix-at:return None
                return size,kind,h
            while position<prefix:
                box=atom(position)
                if not box:return False
                size,kind,h=box
                if kind==b"moof":
                    if size>MAX_MOOV:return False
                    stream.seek(position+h);traf=_atoms(_one(_atoms(stream.read(size-h)),b"traf"))
                    _one(traf,b"tfdt")
                    tfhd=_one(traf,b"tfhd")
                    if struct.unpack_from(">I",tfhd,4)[0]!=struct.unpack_from(">I",trex,4)[0]:return False
                    runs=traf.get(b"trun",[])
                    if not runs or not all(len(r)>=8 and 0<struct.unpack_from(">I",r,4)[0]<=2000000 for r in runs):return False
                    payload=atom(position+size)
                    return bool(payload and payload[1]==b"mdat" and payload[0]>payload[2])
                if kind in (b"mdat",b"moov",b"ftyp"):return False
                position+=size
    except (ValueError, struct.error, OSError):
        return False
    return False


class Session:
    def __init__(self, root: Path, names: list[str], totals: list[int | None], duration100ns=0):
        self.id = uuid.uuid4().hex
        self.root = root
        root.mkdir(parents=True, exist_ok=True, mode=0o700)
        if os.name == "nt":
            from scripts.progressive_pipe import Win32
            Win32().protect_directory(root)
        self.path = root / "session.json"
        self.lock = threading.RLock()
        self.document = {
            "schemaVersion": 1, "sessionId": self.id, "generation": 1,
            "revision": 1, "state": "downloading", "tracks": {
                kind: {"path": name, "container": "mp4" if i == 0 else "m4a",
                       "availablePrefixBytes": 0, "expectedTotalBytes": totals[i],
                       "initializationReady": False, "state": "downloading"}
                for i, (kind, name) in enumerate(zip(("video", "audio"), names))},
        }
        if duration100ns:
            if type(duration100ns) is not int or not 0<duration100ns<2**63:raise ValueError("Invalid session duration")
            self.document["duration100ns"]=duration100ns
        for name in names:
            if Path(name).name != name or any(c in name for c in '/\\:<>|?*"'):
                raise ValueError("Tracks must be contained plain filenames")
        atomic_json(self.path, self.document)

    def publish(self):
        self.document["revision"] += 1
        atomic_json(self.path, self.document)

    def progress(self, index: int, prefix: int, total: int | None, finished=False):
        with self.lock:
            track = self.document["tracks"][("video", "audio")[index]]
            if track["state"] in TERMINAL:
                if track["state"] == "complete" and prefix == track["availablePrefixBytes"]:
                    return
                raise ValueError("Terminal track changed")
            if type(prefix) is not int or not 0 <= prefix < 2**63 or prefix < track["availablePrefixBytes"]:
                raise ValueError("Confirmed prefix decreased")
            path = self.root / track["path"]
            if prefix > path.stat().st_size:
                raise ValueError("Unwritten bytes cannot be published")
            known = track["expectedTotalBytes"]
            if total is not None:
                if type(total) is not int or total <= 0 or total >= 2**63:
                    raise ValueError("Invalid content length")
                if known is not None and known != total:
                    raise ValueError("Content length changed within generation")
                track["expectedTotalBytes"] = total
            known = track["expectedTotalBytes"]
            if known is not None and prefix > known:
                raise ValueError("Prefix exceeds content length")
            if finished:
                if known is not None and prefix != known:
                    raise ValueError("Premature EOF")
                track["expectedTotalBytes"] = known = prefix
                track["state"] = "complete"
            track["availablePrefixBytes"] = prefix
            if fragmented_head(path,prefix,known):track["fragmented"]=True
            track["initializationReady"] = track["initializationReady"] or head_index_ready(path, prefix, known, ("video", "audio")[index])
            self.publish()

    def state(self, state: str):
        with self.lock:
            old = self.document["state"]
            if old in TERMINAL and state != old:
                raise ValueError("Terminal session changed")
            if state in {"complete", "merging"} and any(t["state"] != "complete" for t in self.document["tracks"].values()):
                raise ValueError("Both tracks must finish before merge")
            self.document["state"] = state
            if state in {"cancelled", "failed"}:
                for track in self.document["tracks"].values():
                    if track["state"] not in TERMINAL:
                        track["state"] = state
            self.publish()

    def ready(self) -> bool:
        with self.lock:
            return self.document["state"] not in {"failed", "cancelled"} and all(
                (not t.get("fragmented") or self.document.get("duration100ns")) and (t["expectedTotalBytes"] or t.get("fragmented")) and t["initializationReady"] for t in self.document["tracks"].values())


def playback_ready(path: Path) -> bool:
    try:
        state = read_json(path)
        if state["schemaVersion"] != 1 or state["state"] in {"failed", "cancelled"}:
            return False
        tracks = state["tracks"]
        for kind in ("video", "audio"):
            t = tracks[kind]
            name = t["path"]
            if Path(name).name != name or any(c in name for c in '/\\:<>|?*"'):
                return False
            file = path.parent / name
            if file.is_symlink() or not t["initializationReady"] or (t.get("fragmented") and not state.get("duration100ns")) or not (t["expectedTotalBytes"] or t.get("fragmented")):
                return False
            if file.stat().st_size < t["availablePrefixBytes"]:
                return False
        return True
    except (OSError, ValueError, KeyError, TypeError):
        return False


class Leases:
    """Process identity includes creation time. Disconnection alone never releases a lease."""
    def __init__(self, path: Path):
        self.path = path
        try:
            value = read_json(path)
            if "leases" not in value or not isinstance(value["leases"], dict):
                raise ValueError("Invalid persisted player leases")
            self.items = value["leases"]
            if len(self.items) > 255 or any(
                not pid.isascii() or not pid.isdecimal() or not 0 < int(pid) < 2**32
                or type(born) is not int or not 0 < born < 2**64
                for pid, born in self.items.items()
            ):
                raise ValueError("Invalid persisted process identity")
        except FileNotFoundError:
            self.items = {}
        self.lock = threading.RLock()

    def attach(self, pid: int, born: int):
        with self.lock:
            self.items[str(pid)] = born
            self.save()

    def release(self, pid: int, born: int):
        with self.lock:
            if self.items.get(str(pid)) == born:
                self.items.pop(str(pid), None)
                self.save()

    def reap(self, identity):
        with self.lock:
            for pid, born in list(self.items.items()):
                # None means inaccessible/uncertain; never delete on uncertainty.
                actual = identity(int(pid))
                if actual == 0 or (actual is not None and actual != born):
                    self.items.pop(pid, None)
            self.save()
            return bool(self.items)

    def save(self):
        atomic_json(self.path, {"leases": self.items})


def snapshot(value: dict) -> dict:
    return copy.deepcopy(value)
