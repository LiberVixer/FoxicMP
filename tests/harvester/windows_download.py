"""Entire Windows path: real parallel HTTP -> Harvester -> FoxicMP -> FFmpeg merge.
Run interactively; uses only test paths, local HTTP and a disposable player profile.
"""
import ctypes as C
from ctypes import wintypes as W
import http.server
import json
import os
import subprocess
import sys
import threading
import time
import traceback
import uuid
from pathlib import Path


def main():
    root = Path(sys.argv[1]).absolute()
    sys.path.insert(0, str(root))
    from scripts.progressive_session import read_json
    media = {"/video.mp4": root / "video.mp4", "/audio.m4a": root / "audio.m4a"}
    requests, events = [], []
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            if self.path not in media:
                self.send_error(404)
                return
            source = media[self.path]
            self.send_response(200)
            self.send_header("Content-Length", str(source.stat().st_size))
            self.end_headers()
            with source.open("rb") as stream:
                while data := stream.read(1024):
                    self.wfile.write(data)
                    self.wfile.flush()
                    time.sleep(.2)
        def log_message(self, *args):
            pass
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_port}"
    run = root / "http-runs" / uuid.uuid4().hex[:8]
    run.mkdir(parents=True)
    info = {"id": "local-windows", "title": "Local Windows playback", "extractor": "test", "extractor_key": "Test", "webpage_url": base,
            "formats": [
                {"format_id": "137", "url": base + "/video.mp4", "protocol": "http", "ext": "mp4", "vcodec": "avc1.64001e", "acodec": "none", "height": 360},
                {"format_id": "140", "url": base + "/audio.m4a", "protocol": "http", "ext": "m4a", "vcodec": "none", "acodec": "mp4a.40.2"}]}
    (run / "input.json").write_text(json.dumps(info), encoding="utf-8")
    env = dict(os.environ, YTD_TEMP_DIR=str(run), YTD_STOP_FILE=str(run / "stop"))
    player, backend = None, None
    manifest, started_incomplete, played_incomplete = None, False, False
    result = {"passed": False, "run": str(run)}
    user = C.WinDLL("user32")
    user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def close_player():
        if not player or player.poll() is not None:
            return
        @callback
        def close(handle, _):
            pid = W.DWORD()
            user.GetWindowThreadProcessId(handle, C.byref(pid))
            if pid.value == player.pid:
                user.PostMessageW(handle, 0x10, 0, 0)
            return True
        user.EnumWindows(close, 0)
        try:
            player.wait(8)
        except subprocess.TimeoutExpired:
            player.kill()
            raise AssertionError("Player hung on Close")
    try:
        command = [sys.executable, str(root / "scripts" / "progressive_download.py"), "--load-info-json", str(run / "input.json"),
                   "-f", "137+140", "--merge-output-format", "mp4", "--ffmpeg-location", str(root / "tools"),
                   "--download-archive", str(run / "archive.txt"), "-o", str(run / "finished.mp4"), "--newline", "--no-warnings"]
        backend = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        lines = []
        finished = threading.Event()
        def stdout():
            nonlocal player, manifest, started_incomplete
            try:
                for line in backend.stdout:
                    lines.append(line)
                    if line.startswith("[FoxicMP] "):
                        event = json.loads(line[len("[FoxicMP] "):])
                        if event.get("ready") and not player:
                            manifest = Path(event["manifest"])
                            state = read_json(manifest)
                            started_incomplete = state["state"] == "downloading"
                            player = subprocess.Popen([str(root / "player" / "FoxicMP64.exe"), "/new", "/harvester-session", str(manifest)])
            finally:
                finished.set()
        threading.Thread(target=stdout, daemon=True).start()
        deadline = time.monotonic() + 100
        while time.monotonic() < deadline and not finished.is_set():
            if player and manifest:
                try:
                    state = read_json(manifest)
                    playback = read_json(manifest.parent / f"player-{player.pid}.json")
                    if not events or playback.get("type") != events[-1].get("type"):
                        events.append(playback)
                    if state["state"] == "downloading" and playback.get("type") == "playing" and playback.get("position100ns", 0) >= 2 * 10**7:
                        played_incomplete = True
                        if not any(e.get("proof") for e in events):
                            events.append(dict(playback, proof="playing-before-EOF", tracks=state["tracks"]))
                except (OSError, ValueError):
                    pass
            time.sleep(.1)
        assert finished.is_set(), "Downloader timeout"
        assert backend.wait(5) == 0, "".join(lines[-20:])
        assert started_incomplete and played_incomplete, (started_incomplete, played_incomplete, events, lines[-15:])
        assert sorted(requests) == ["/audio.m4a", "/video.mp4"], requests
        assert (run / "finished.mp4").exists()
        assert "local-windows" in (run / "archive.txt").read_text()
        state = read_json(manifest)
        assert state["state"] == "complete", state
        for kind, original in (("video", "video.mp4"), ("audio", "audio.m4a")):
            track = state["tracks"][kind]
            assert (manifest.parent / track["path"]).read_bytes() == (root / original).read_bytes()
        leases = read_json(manifest.parent / "leases.json")["leases"]
        assert str(player.pid) in leases, leases
        details = json.loads(subprocess.check_output([str(root / "tools" / "ffprobe.exe"), "-v", "quiet", "-show_streams", "-of", "json", str(run / "finished.mp4")]))
        assert sorted(s["codec_name"] for s in details["streams"]) == ["aac", "h264"], details
        result.update(passed=True, started_incomplete=started_incomplete, played_incomplete=played_incomplete,
                      requests=requests, events=events, final_bytes=(run / "finished.mp4").stat().st_size,
                      streams=[{k: s[k] for k in ("codec_name", "codec_type", "duration") if k in s} for s in details["streams"]])
        close_player()
        # Provider continues after the downloader process exits, then collects released sources.
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            if all(not (manifest.parent / t["path"]).exists() for t in state["tracks"].values()):
                result["deferred_cleanup"] = True
                break
            time.sleep(1)
        assert result.get("deferred_cleanup"), "Provider did not collect released tracks"
        assert (run / "finished.mp4").exists()
    except BaseException:
        result["passed"] = False
        result["error"] = traceback.format_exc()
        raise
    finally:
        close_player()
        if backend and backend.poll() is None:
            backend.kill()
        server.shutdown()
        server.server_close()
        (root / "result-http.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        (root / "http-backend.log").write_text("".join(locals().get("lines", [])), encoding="utf-8")


if __name__ == "__main__":
    main()
