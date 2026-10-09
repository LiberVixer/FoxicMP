"""Real HTTP writers and FFmpeg merger; provider transport is tested separately on Windows."""
import contextlib
import http.server
import json
import os
import shutil
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch


@unittest.skipUnless(shutil.which("ffmpeg"), "FFmpeg required")
class ProgressiveHttpTests(unittest.TestCase):
    def test_pair_parallel_once_headers_same_quality_and_merge(self):
        self.exercise_pair()

    def test_merger_failure_retains_sources_and_does_not_write_archive(self):
        self.exercise_pair(merge_failure=True)

    def exercise_pair(self, merge_failure=False):
        try:
            import yt_dlp
            from yt_dlp.downloader.http import HttpFD
        except ImportError:
            self.skipTest("yt-dlp required")
        from scripts.progressive_download import make_downloader_class
        from scripts.progressive_session import atomic_json, read_json
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc2=s=320x180:r=25:d=3", "-c:v", "libx264", "-preset", "ultrafast", "-movflags", "+faststart", str(root / "v.mp4")], check=True)
            subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=880:duration=3", "-c:a", "aac", "-movflags", "+faststart", str(root / "a.m4a")], check=True)
            requests = []
            barrier = threading.Barrier(2)
            class Handler(http.server.BaseHTTPRequestHandler):
                def do_GET(self):
                    requests.append((self.path, self.headers.get("X-Session-Test"), time.monotonic()))
                    data = (root / self.path.lstrip("/")).read_bytes()
                    self.send_response(200)
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    barrier.wait(timeout=5)  # A sequential downloader cannot pass this.
                    for position in range(0, len(data), 2048):
                        self.wfile.write(data[position:position + 2048])
                        self.wfile.flush()
                        time.sleep(.005)
                def log_message(self, *args):
                    pass
            server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
            self.addCleanup(server.server_close)
            threading.Thread(target=server.serve_forever, daemon=True).start()
            self.addCleanup(server.shutdown)
            base = f"http://127.0.0.1:{server.server_port}"
            params = {"format": "137+140", "outtmpl": str(root / "finished.mp4"), "quiet": True,
                      "noprogress": True, "ratelimit": 1000000, "http_headers": {"X-Session-Test": "preserved"},
                      "download_archive": str(root / "archive.txt")}
            if merge_failure:
                # A real FFmpeg error, after both HTTP downloads have completed.
                params["postprocessor_args"] = {"merger+ffmpeg_o": ["-map", "9:a:0"]}
            info = {"id": "local-test", "title": "Local test", "extractor": "test", "extractor_key": "Test", "webpage_url": base,
                    "formats": [
                        {"format_id": "137", "url": base + "/v.mp4", "protocol": "http", "ext": "mp4", "vcodec": "avc1.64001e", "acodec": "none", "height": 180},
                        {"format_id": "140", "url": base + "/a.m4a", "protocol": "http", "ext": "m4a", "vcodec": "none", "acodec": "mp4a.40.2"}]}
            original = subprocess.Popen
            limits = []
            original_fd = HttpFD.__init__
            def fd_init(fd, ydl, options):
                limits.append(options.get("ratelimit"))
                original_fd(fd, ydl, options)
            def provider_launch(command, *args, **kwargs):
                if "--provider" in command:
                    position = command.index("--provider")
                    manifest = Path(command[position + 1])
                    atomic_json(manifest.parent / "provider.json", {"ready": True})
                    class Fake:
                        def poll(self):
                            return None
                    return Fake()
                return original(command, *args, **kwargs)
            with patch("scripts.progressive_download.subprocess.Popen", side_effect=provider_launch), patch.object(HttpFD, "__init__", fd_init), patch.dict(os.environ, {"YTD_TEMP_DIR": str(root)}, clear=False):
                with make_downloader_class(yt_dlp)(params) as ydl:
                    if merge_failure:
                        with self.assertRaises(yt_dlp.utils.DownloadError):
                            ydl.process_ie_result(info, download=True)
                    else:
                        ydl.process_ie_result(info, download=True)
            self.assertEqual(sorted(r[0] for r in requests), ["/a.m4a", "/v.mp4"])
            self.assertTrue(all(r[1] == "preserved" for r in requests))
            self.assertEqual(limits, [500000, 500000])

            state = read_json(next((root / "foxicmp-sessions").glob("*/session.json")))
            self.assertEqual(state["state"], "failed" if merge_failure else "complete")
            self.assertTrue(all(t["state"] == "complete" for t in state["tracks"].values()))
            if merge_failure:
                self.assertFalse((root / "archive.txt").exists())
                self.assertFalse((root / "finished.mp4").exists())
            else:
                self.assertIn("local-test", (root / "archive.txt").read_text())
            session = next((root / "foxicmp-sessions").iterdir())
            # The merger must preserve the exact downloaded bitstreams.
            for track, source in (("video", "v.mp4"), ("audio", "a.m4a")):
                self.assertEqual((session / state["tracks"][track]["path"]).read_bytes(), (root / source).read_bytes())
            if merge_failure:
                return
            self.assertTrue((root / "finished.mp4").exists())
            details = json.loads(subprocess.check_output(["ffprobe", "-v", "quiet", "-show_streams", "-of", "json", str(root / "finished.mp4")]))
            self.assertEqual(sorted(s["codec_name"] for s in details["streams"]), ["aac", "h264"])


if __name__ == "__main__":
    unittest.main()
