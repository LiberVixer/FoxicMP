"""Linux session provider. Local IPC only; no media URLs or network fetching.

The abstract Unix socket name is derived from the random session identifier.
Both sides must verify SO_PEERCRED. A dropped connection does not release a
persisted reader lease; /proc identity is checked before reclaiming it.
"""
from __future__ import annotations

import hashlib
import json
import os
import socket
import stat
import struct
import threading
import time
from pathlib import Path

from scripts.progressive_session import Leases, MAX_JSON, atomic_json, read_json


def process_identity(pid: int) -> int | None:
    """0: dead, None: uncertain, positive: boot and process creation identity."""
    if type(pid) is not int or not 0 < pid < 2**32:
        return 0
    try:
        raw = Path(f"/proc/{pid}/stat").read_text()
        # comm may contain spaces and parentheses. Fields after ')' start at 3.
        fields = raw[raw.rfind(")") + 2:].split()
        if fields[0] in {"Z", "X", "x"}:
            return 0
        start = fields[19]
        boot = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
        digest = hashlib.blake2b(f"{boot}:{start}".encode(), digest_size=8).digest()
        return int.from_bytes(digest, "big") or 1
    except FileNotFoundError:
        # boot_id disappearing is uncertainty, not proof the process is gone.
        return 0 if not Path(f"/proc/{pid}").exists() else None
    except (OSError, IndexError, ValueError):
        return None


def socket_address(session_id: str) -> str:
    if not isinstance(session_id, str) or len(session_id) != 32 or any(c not in "0123456789abcdef" for c in session_id):
        raise ValueError("Invalid session identifier")
    return "\0FoxicMP.Harvester." + session_id


def _number(value, positive=False):
    if type(value) is not int or not (1 if positive else 0) <= value < 2**63:
        raise ValueError("Invalid session number")
    return value


def validate_snapshot(value: dict, root: Path, previous: dict | None = None) -> dict:
    if type(value.get("schemaVersion")) is not int or value["schemaVersion"] != 1:
        raise ValueError("Unsupported session schema")
    socket_address(value.get("sessionId", ""))
    for key in ("generation", "revision"):
        _number(value.get(key), True)
    states = {"downloading", "paused", "merging", "complete", "failed", "cancelled"}
    if value.get("state") not in states:
        raise ValueError("Invalid session state")
    for key in ("duration100ns", "timestampOffset100ns"):
        if key in value:
            _number(value[key])
    tracks = value.get("tracks")
    if not isinstance(tracks, dict) or set(tracks) != {"video", "audio"}:
        raise ValueError("Exactly two tracks required")
    names = set()
    identities = set()
    for kind in ("video", "audio"):
        track = tracks[kind]
        if not isinstance(track, dict):
            raise ValueError("Invalid track object")
        name = track.get("path")
        if (not isinstance(name, str) or not name or name in {".", ".."}
                or len(name.encode()) > 255 or any(c in name for c in '/\\:\0<>|?*"')
                or name.endswith((".", " ")) or name in names):
            raise ValueError("Tracks must be distinct plain filenames")
        names.add(name)
        if track.get("container") != ("mp4" if kind == "video" else "m4a"):
            raise ValueError("Unsupported track container")
        prefix = _number(track.get("availablePrefixBytes"))
        total = track.get("expectedTotalBytes")
        if total is not None:
            _number(total, True)
            if prefix > total:
                raise ValueError("Prefix exceeds total")
        if type(track.get("initializationReady")) is not bool:
            raise ValueError("Invalid initialization flag")
        if "fragmented" in track and type(track["fragmented"]) is not bool:
            raise ValueError("Invalid fragmented flag")
        if track.get("state") not in states - {"merging"}:
            raise ValueError("Invalid track state")
        if track["state"] == "complete" and (total is None or prefix != total):
            raise ValueError("Unconfirmed completion")
        if track["initializationReady"] and total is None and not (
                track.get("fragmented") and value.get("duration100ns", 0) > 0):
            raise ValueError("Unknown initialized length")
        try:
            file = (root / name).lstat()
        except FileNotFoundError:
            if prefix or track["initializationReady"]:
                raise ValueError("Published track is missing")
            continue
        if not stat.S_ISREG(file.st_mode) or file.st_uid != os.getuid():
            raise ValueError("Unsafe track path")
        identity = (file.st_dev, file.st_ino)
        if identity in identities or file.st_size < prefix:
            raise ValueError("Aliased track or unwritten bytes")
        identities.add(identity)
    if value["state"] in {"complete", "merging"} and any(
            t["state"] != "complete" for t in tracks.values()):
        raise ValueError("Session completed before its tracks")
    if previous:
        if value["sessionId"] != previous["sessionId"]:
            raise ValueError("Session identity changed")
        if value["generation"] < previous["generation"]:
            return previous
        if value["generation"] == previous["generation"]:
            if value["revision"] <= previous["revision"]:
                return previous
            if previous["state"] in {"complete", "failed", "cancelled"} and value["state"] != previous["state"]:
                raise ValueError("Terminal session changed")
            for key in ("duration100ns", "timestampOffset100ns"):
                if value.get(key, 0) != previous.get(key, 0):
                    raise ValueError("Session clock changed")
            for kind in tracks:
                before, after = previous["tracks"][kind], tracks[kind]
                if (before["path"] != after["path"] or before["container"] != after["container"]
                        or after["availablePrefixBytes"] < before["availablePrefixBytes"]
                        or (before["expectedTotalBytes"] is not None and before["expectedTotalBytes"] != after["expectedTotalBytes"])
                        or (before["initializationReady"] and not after["initializationReady"])
                        or (before["initializationReady"] and before.get("fragmented", False) != after.get("fragmented", False))
                        or (before["state"] in {"complete", "failed", "cancelled"} and after["state"] != before["state"])):
                    raise ValueError("Track identity or published prefix changed")
    return value


class Provider:
    def __init__(self, manifest: Path, owner: int = 0, stop_file: Path | None = None,
                 *, poll_interval=1.0, cleanup_delay=30.0, unopened_delay=600.0):
        self.path = manifest.absolute()
        root = self.path.parent.lstat()
        if not stat.S_ISDIR(root.st_mode) or root.st_uid != os.getuid() or root.st_mode & 0o077:
            raise ValueError("Session directory must be private to the current user")
        if self.path.is_symlink() or self.path.parent.resolve() != self.path.parent:
            raise ValueError("Unsafe manifest path")
        if (self.path.parent / "leases.json").is_symlink():
            raise ValueError("Unsafe lease path")
        self.state = validate_snapshot(read_json(self.path), self.path.parent)
        self.leases = Leases(self.path.parent / "leases.json")
        # Persisted leases lack per-reader detail. Retain them conservatively
        # until process exit; explicit release cannot erase an unknown reader.
        self.readers = {pid: {"restored"} for pid in self.leases.items}
        self.owner = owner
        self.owner_born = process_identity(owner) if owner else None
        self.stop_file = stop_file
        self.poll_interval = poll_interval
        self.cleanup_delay, self.unopened_delay = cleanup_delay, unopened_delay
        self.stop = threading.Event()
        self.ready = threading.Event()
        self.lock = threading.RLock()
        self.pending = 0
        self.seen_client = bool(self.leases.items)
        self.error = None
        self.listener = None
        self.file_identities = {}

    def check_files(self):
        for track in self.state["tracks"].values():
            name = track["path"]
            try:
                info = (self.path.parent / name).lstat()
            except FileNotFoundError:
                if name in self.file_identities:
                    raise ValueError("Pinned source removed")
                continue
            identity = (info.st_dev, info.st_ino)
            if name in self.file_identities and self.file_identities[name] != identity:
                raise ValueError("Pinned source replaced")
            self.file_identities[name] = identity

    def client(self, connection: socket.socket):
        attached_generation = None
        attached_reader_id = None
        try:
            pid, uid, _ = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            if uid != os.getuid():
                return
            born = process_identity(pid)
            if not born:
                return
            connection.settimeout(.2)
            raw, last = b"", 0.0
            while not self.stop.is_set():
                with self.lock:
                    state = self.state
                if time.monotonic() - last >= 1:
                    value = {k: state[k] for k in ("schemaVersion", "sessionId", "generation", "revision")}
                    value["type"] = "heartbeat"
                    connection.sendall((json.dumps(value) + "\n").encode())
                    last = time.monotonic()
                try:
                    data = connection.recv(4096)
                except socket.timeout:
                    continue
                if not data:
                    return
                raw += data
                if len(raw) > MAX_JSON:
                    return
                while b"\n" in raw:
                    line, raw = raw.split(b"\n", 1)
                    def unique(pairs):
                        result = {}
                        for key, value in pairs:
                            if key in result:
                                raise ValueError("Duplicate IPC field")
                            result[key] = value
                        return result
                    message = json.loads(line, object_pairs_hook=unique)
                    with self.lock:
                        # A downloader can publish faster than the heartbeat
                        # timer. Authenticate attachment against fresh state.
                        self.state = state = validate_snapshot(read_json(self.path), self.path.parent, self.state)
                        if (not isinstance(message, dict)
                                or type(message.get("schemaVersion")) is not int
                                or any(message.get(k) != state[k] for k in ("schemaVersion", "sessionId"))
                                or type(message.get("pid")) is not int or message["pid"] != pid
                                or type(message.get("revision")) is not int or not 0 < message["revision"] <= state["revision"]
                                or type(message.get("generation")) is not int):
                            return
                        kind = message.get("type")
                        reader_id = message.get("readerId", "legacy")
                        if reader_id != "legacy" and (not isinstance(reader_id, str) or len(reader_id) != 32 or any(c not in "0123456789abcdef" for c in reader_id)):
                            return
                        if attached_reader_id is not None and reader_id != attached_reader_id:
                            return
                        if kind == "released" and attached_generation == message["generation"]:
                            readers = self.readers.get(str(pid), set())
                            readers.discard(reader_id)
                            if not readers:
                                self.readers.pop(str(pid), None)
                                self.leases.release(pid, born)
                            return
                        if message["generation"] != state["generation"]:
                            if attached_generation == message["generation"] and kind in {"error", "buffering", "playing", "paused"}:
                                continue
                            return
                        if kind == "attached":
                            if self.stop.is_set() or attached_generation is not None:
                                return
                            self.leases.attach(pid, born)
                            self.readers.setdefault(str(pid), set()).add(reader_id)
                            attached_generation = state["generation"]
                            attached_reader_id = reader_id
                            self.seen_client = True
                            if message.get("leaseAck") is True:
                                reply = {k: state[k] for k in ("schemaVersion", "sessionId", "generation", "revision")}
                                reply.update(type="attached", readerId=reader_id)
                                connection.sendall((json.dumps(reply) + "\n").encode())
                        elif kind not in {"buffering", "playing", "paused", "error"} or attached_generation is None:
                            return
        except (OSError, ValueError, TypeError, RecursionError):
            pass
        finally:
            connection.close()
            with self.lock:
                self.pending -= 1

    def accept(self):
        while not self.stop.is_set():
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with self.lock:
                if self.stop.is_set() or self.pending >= 255:
                    connection.close()
                    continue
                self.pending += 1
            threading.Thread(target=self.client, args=(connection,), daemon=True).start()

    def run(self):
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.listener = listener
        bound = False
        try:
            listener.bind(socket_address(self.state["sessionId"]))
            bound = True
            listener.listen(16)
            listener.settimeout(.2)
            atomic_json(self.path.parent / "provider.json", {
                "pid": os.getpid(), "born": process_identity(os.getpid()),
                "ready": True, "transport": "unix-abstract"})
            self.ready.set()
            acceptor = threading.Thread(target=self.accept, daemon=True)
            acceptor.start()
            idle = None
            while not self.stop.wait(self.poll_interval):
                with self.lock:
                    if self.path.is_symlink():
                        raise ValueError("Unsafe manifest path")
                    self.state = state = validate_snapshot(read_json(self.path), self.path.parent, self.state)
                    self.check_files()
                    if self.owner and state["state"] in {"downloading", "paused", "merging"}:
                        actual = process_identity(self.owner)
                        if actual == 0 or (actual is not None and self.owner_born is not None and actual != self.owner_born):
                            state["state"] = "cancelled" if self.stop_file and self.stop_file.exists() else "failed"
                            state["revision"] += 1
                            for track in state["tracks"].values():
                                if track["state"] != "complete":
                                    track["state"] = state["state"]
                            atomic_json(self.path, state)
                    active = self.leases.reap(process_identity)
                    self.readers = {pid: readers for pid, readers in self.readers.items() if pid in self.leases.items}
                    if state["state"] in {"failed", "cancelled"} and not active and not self.pending:
                        break
                    quiet = state["state"] == "complete" and not active and not self.pending
                    idle = time.monotonic() if quiet and idle is None else idle if quiet else None
                    if idle is not None and time.monotonic() - idle >= (self.cleanup_delay if self.seen_client else self.unopened_delay):
                        self.stop.set()  # Attach and cleanup share the same lock.
                        for track in state["tracks"].values():
                            path = self.path.parent / track["path"]
                            if not path.is_symlink():
                                path.unlink(missing_ok=True)
                        break
            return 0
        except (OSError, ValueError, KeyError, TypeError) as error:
            self.error = str(error)
            return 1
        finally:
            self.stop.set()
            self.ready.set()
            listener.close()
            if bound:
                atomic_json(self.path.parent / "provider.json", {"pid": os.getpid(), "ready": False, "transport": "unix-abstract"})
