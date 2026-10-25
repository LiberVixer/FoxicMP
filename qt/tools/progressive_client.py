#!/usr/bin/env python3
"""FoxicMP progressive API client for Linux and Windows. Python standard library only.

Import ProgressiveClient in a downloader, or use this file as a CLI test client.
This client does not download, copy, remux or delete media.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import time

MAX_BYTES = 65536


class ApiError(RuntimeError):
    def __init__(self, reply: dict):
        self.reply = reply
        self.code = reply.get("code", "api_error")
        super().__init__(reply.get("error", self.code))


class ProgressiveClient:
    def __init__(self, player: str, profile: str | None = None):
        resolved = shutil.which(player) or str(Path(player).expanduser().resolve())
        self.player = str(Path(resolved).resolve())
        args = [self.player, "--progressive-api-info"]
        if profile is not None:
            args += ["--profile", str(Path(profile).resolve())]
        query = subprocess.run(args, capture_output=True, timeout=5,
                               env={**os.environ, "QT_QPA_PLATFORM": "offscreen"})
        if query.returncode or len(query.stdout) > MAX_BYTES:
            raise RuntimeError("Cannot discover FoxicMP progressive API: " +
                               query.stderr.decode(errors="replace")[:1024])
        self.info = json.loads(query.stdout)
        if self.info.get("version") != 1 or self.info.get("transport") not in {"unix-json-lines", "windows-pipe-json-lines"}:
            raise RuntimeError("Unsupported FoxicMP progressive API")
        self.endpoint = self.info["endpoint"]
        self.profile = self.info["profile"]
        if (profile is not None and self.info.get("profileSelectable") is False and
                os.path.normcase(os.path.abspath(profile)) != os.path.normcase(os.path.abspath(self.profile))):
            raise ValueError("This Classic installation uses its own portable profile directory")

    def request(self, message: dict, timeout: float = 5) -> dict:
        raw = json.dumps(message, ensure_ascii=False, allow_nan=False,
                         separators=(",", ":")).encode() + b"\n"
        if len(raw) > MAX_BYTES:
            raise ValueError("Request exceeds 65536 bytes")
        if self.info["transport"] == "windows-pipe-json-lines":
            from progressive_windows import exchange
            reply = exchange(self.endpoint, raw, timeout, MAX_BYTES)
        else:
            deadline = time.monotonic() + timeout
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                connection.settimeout(timeout)
                connection.connect(self.endpoint)
                _, uid, _ = struct.unpack("3i", connection.getsockopt(
                    socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("3i")))
                if uid != os.getuid():
                    raise RuntimeError("FoxicMP API belongs to another user")
                connection.sendall(raw)
                reply = bytearray()
                while b"\n" not in reply:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError("FoxicMP API did not reply")
                    connection.settimeout(remaining)
                    data = connection.recv(min(4096, MAX_BYTES + 1 - len(reply)))
                    if not data:
                        raise RuntimeError("FoxicMP API closed without a reply")
                    reply.extend(data)
                    if len(reply) > MAX_BYTES:
                        raise RuntimeError("FoxicMP API reply exceeds the limit")

        result = json.loads(reply)
        if result.get("version") != 1:
            raise RuntimeError("Invalid FoxicMP API reply")
        if result.get("ok") is not True:
            raise ApiError(result)
        return result

    def ensure_running(self, timeout: float = 10) -> None:
        try:
            self.request({"version": 1, "action": "capabilities"})
            return
        except (OSError, RuntimeError):
            pass
        arguments = ([self.player, "/nofocus"] if self.info.get("edition") == "classic" else
                     [self.player, "--profile", self.profile, "--reuse-instance", "--no-activate"])
        process = subprocess.Popen(
            arguments,
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, close_fds=True, start_new_session=True)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                self.request({"version": 1, "action": "capabilities"}, timeout=1)
                return
            except (OSError, RuntimeError):
                if process.poll() not in (None, 0):
                    raise RuntimeError("FoxicMP could not start; check the profile and desktop session")
                time.sleep(.05)
        raise TimeoutError("Progressive API unavailable; restart an older player if it is running")

    def start(self, session: dict, *, title: str = "", replace: bool = False,
              activate: bool = True, session_directory: str | None = None) -> dict:
        message = {"version": 1, "action": "start", "session": session,
                   "title": title, "replace": replace, "activate": activate}
        if session_directory is not None: message["sessionDirectory"] = str(Path(session_directory).resolve())
        return self.request(message)

    def update(self, session: dict, *, complete: bool = False,
               session_directory: str | None = None) -> dict:
        message = {"version": 1, "action": "complete" if complete else "update",
                   "sessionId": session["sessionId"], "session": session}
        if session_directory is not None: message["sessionDirectory"] = str(Path(session_directory).resolve())
        return self.request(message)

    def probe(self, session: dict, session_directory: str | None = None) -> dict:
        message = {"version": 1, "action": "probe", "session": session}
        if session_directory is not None:
            message["sessionDirectory"] = str(Path(session_directory).resolve())
        return self.request(message)

    def command(self, action: str, session_id: str, **fields) -> dict:
        return self.request({"version": 1, "action": action, "sessionId": session_id, **fields})

    def release(self, session_id: str, timeout: float = 2) -> dict:
        """Detach this reader, waiting for an explicit file-handle acknowledgement.

        released=False (or an exception) means the writer must retain its files.
        This never cancels the download or deletes any resource.
        """
        if not 0 <= timeout <= 60:
            raise ValueError("Release timeout must be between 0 and 60 seconds")
        deadline = time.monotonic() + timeout
        def send(action):
            return self.request(dict(version=1, action=action, sessionId=session_id),
                                timeout=max(.001, min(5, deadline - time.monotonic())))
        reply = send("release")
        while reply.get("released") is not True and time.monotonic() < deadline:
            time.sleep(min(.05, max(0, deadline - time.monotonic())))
            if time.monotonic() < deadline:
                reply = send("status")
        return reply


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_player = Path(__file__).resolve().parent.parent / "foxicmp-qt"
    parser.add_argument("--player", default=str(default_player))
    parser.add_argument("--profile")
    parser.add_argument("--ensure", action="store_true", help="start/reuse the player if needed")
    parser.add_argument("action", choices=("capabilities", "probe", "start", "update", "complete",
                                           "heartbeat", "status", "cancel", "release", "error"))
    parser.add_argument("--snapshot", type=Path, help="JSON session snapshot for start/update/complete")
    parser.add_argument("--session-directory", type=Path, help="Directory containing relative progressive tracks")
    parser.add_argument("--session-id")
    parser.add_argument("--title", default="")
    parser.add_argument("--replace", action="store_true")
    parser.add_argument("--message", default="")
    args = parser.parse_args()
    try:
        client = ProgressiveClient(args.player, args.profile)
        if args.ensure:
            client.ensure_running()
        if args.action == "probe":
            if args.snapshot is None:
                parser.error("--snapshot is required")
            with args.snapshot.open("rb") as stream:
                session = json.load(stream)
            reply = client.probe(session, args.session_directory)
        elif args.action in {"start", "update", "complete"}:
            if args.snapshot is None:
                parser.error("--snapshot is required")
            with args.snapshot.open("rb") as stream:
                raw = stream.read(MAX_BYTES + 1)
            if len(raw) > MAX_BYTES:
                raise ValueError("Session snapshot exceeds the limit")
            session = json.loads(raw)
            reply = client.start(session, title=args.title, replace=args.replace,
                                 session_directory=args.session_directory) if args.action == "start" \
                else client.update(session, complete=args.action == "complete",
                                   session_directory=args.session_directory)
        elif args.action == "capabilities":
            reply = client.request({"version": 1, "action": "capabilities"})
        else:
            if not args.session_id:
                parser.error("--session-id is required")
            fields = {"message": args.message} if args.action == "error" else {}
            reply = client.command(args.action, args.session_id, **fields)
        print(json.dumps(reply, ensure_ascii=False))
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(json.dumps({"ok": False, "error": str(error)}, ensure_ascii=False), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
