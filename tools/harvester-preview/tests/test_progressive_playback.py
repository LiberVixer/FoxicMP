import json
import os
import sys
from unittest.mock import patch
import struct
import tempfile
import unittest
from pathlib import Path
from scripts.progressive_session import Session, Leases, atomic_json, read_json, head_index_ready, playback_ready
from scripts.progressive_download import eligible, progressive_arguments


def atom(kind, data=b""):
    return struct.pack(">I4s", len(data) + 8, kind) + data


class ProgressiveProtocolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        def table(position):
            full = b"\0" * 4
            stbl = atom(b"stsd", full + struct.pack(">I", 1) + atom(b"avc1"))
            stbl += atom(b"stts", full + struct.pack(">III", 1, 1, 10))
            stbl += atom(b"stsz", full + struct.pack(">II", 4, 1))
            stbl += atom(b"stsc", full + struct.pack(">IIII", 1, 1, 1, 1))
            stbl += atom(b"stco", full + struct.pack(">II", 1, position))
            mdia = atom(b"mdhd") + atom(b"hdlr", full * 2 + b"vide") + atom(b"minf", atom(b"stbl", stbl))
            return atom(b"moov", atom(b"mvhd") + atom(b"trak", atom(b"tkhd") + atom(b"mdia", mdia)))
        head = atom(b"ftyp", b"isom")
        position = len(head) + len(table(0)) + 8
        self.file = head + table(position) + atom(b"mdat", b"data" * 40)
        self.ready_prefix = position + 4

    def make_session(self):
        session = Session(self.root, ["video.mp4.part", "audio.m4a.part"], [None, None])
        for name in ["video.mp4.part", "audio.m4a.part"]:
            (self.root / name).write_bytes(self.file if name.startswith("video") else self.file.replace(b"vide", b"soun").replace(b"avc1", b"mp4a"))
        return session

    def test_readiness_is_metadata_not_percent(self):
        session = self.make_session()
        session.progress(0, self.ready_prefix, len(self.file))
        self.assertFalse(session.ready())
        session.progress(1, self.ready_prefix - 1, len(self.file))
        self.assertFalse(session.ready())  # first decoder packet is partial
        session.progress(1, self.ready_prefix, len(self.file))
        self.assertTrue(session.ready())
        self.assertTrue(playback_ready(session.path))
        session.state("cancelled")
        self.assertFalse(playback_ready(session.path))

    def test_unknown_length_tail_index_fragments_partial_header(self):
        path = self.root / "x.mp4"
        path.write_bytes(self.file)
        self.assertFalse(head_index_ready(path, self.ready_prefix, None))
        self.assertFalse(head_index_ready(path, self.ready_prefix - 1, len(self.file)))
        self.assertTrue(head_index_ready(path, self.ready_prefix, len(self.file)))
        path.write_bytes(atom(b"ftyp", b"isom") + atom(b"mdat", b"data") + atom(b"moov", b"index"))
        self.assertFalse(head_index_ready(path, path.stat().st_size, path.stat().st_size))
        path.write_bytes(atom(b"ftyp", b"isom") + atom(b"moof", b"data") + atom(b"moov", b"index"))
        self.assertFalse(head_index_ready(path, path.stat().st_size, path.stat().st_size))

    def test_no_unwritten_regression_total_change_or_premature_eof(self):
        session = self.make_session()
        for prefix, total in ((True, len(self.file)), (1, True)):
            with self.assertRaises(ValueError):
                session.progress(0, prefix, total)
        with self.assertRaises(ValueError):
            session.progress(0, len(self.file) + 1, len(self.file))
        session.progress(0, self.ready_prefix, len(self.file))
        with self.assertRaises(ValueError):
            session.progress(0, 24, len(self.file))
        with self.assertRaises(ValueError):
            session.progress(0, self.ready_prefix, len(self.file) + 1)
        with self.assertRaises(ValueError):
            session.progress(0, self.ready_prefix, len(self.file), True)
        with self.assertRaises(ValueError):
            session.state("merging")

    def test_merge_failure_keeps_tracks_and_terminal_states(self):
        session = self.make_session()
        for i in (0, 1):
            session.progress(i, len(self.file), len(self.file), True)
        session.state("merging")
        session.state("failed")
        with self.assertRaises(ValueError):
            session.state("complete")
        self.assertTrue((self.root / "video.mp4.part").exists())
        self.assertEqual(read_json(session.path)["tracks"]["audio"]["state"], "complete")

    def test_leases_multiple_clients_crashes_pid_reuse_and_uncertainty(self):
        leases = Leases(self.root / "leases.json")
        leases.attach(10, 100)
        leases.attach(11, 110)
        restored = Leases(self.root / "leases.json")
        restored.release(10, 99)
        self.assertEqual(len(restored.items), 2)
        restored.release(10, 100)
        self.assertTrue(restored.reap(lambda pid: None))
        self.assertFalse(restored.reap(lambda pid: 111))  # reused PID, old process is gone
        restored.attach(12, 120)
        self.assertFalse(restored.reap(lambda pid: 0))

    def test_malformed_persisted_leases_are_not_treated_as_all_players_released(self):
        path = self.root / "leases.json"
        for value in ({}, {"leases": []}, {"leases": {"12": True}},
                      {"leases": {"0": 100}}, {"leases": {"bad": 100}},
                      {"leases": {"12": -1}}):
            atomic_json(path, value)
            with self.assertRaises(ValueError):
                Leases(path)

    def test_json_duplicate_size_and_paths(self):
        path = self.root / "json"
        path.write_text('{"x":1,"x":2}')
        with self.assertRaises(ValueError):
            read_json(path)
        with self.assertRaises(ValueError):
            atomic_json(path, {"x": "x" * 65536})
        with self.assertRaises(ValueError):
            Session(self.root / "bad", ["../outside", "audio.m4a"], [1, 1])

    def test_command_preserves_proxy_cookies_limit_and_retry_arguments(self):
        options = ["--cookies", "private.cookies", "--proxy", "socks5://localhost:9000", "--limit-rate", "1M", "-f", "137+140", "--retries", "7", "https://source/video"]
        for prefix in (["yt-dlp.exe"], ["python.exe", "-m", "yt_dlp"], ["Harvester.exe", "--run-yt-dlp"], ["python.exe", "progressive_download.py"]):
            self.assertEqual(progressive_arguments(prefix + options), options)
        self.assertIsNone(progressive_arguments(["private-wrapper.cmd", *options]))

    def test_selected_formats_are_not_downgraded(self):
        video = {"format_id": "137", "protocol": "https", "ext": "mp4", "vcodec": "avc1.640028", "acodec": "none"}
        audio = {"format_id": "140", "protocol": "https", "ext": "m4a", "vcodec": "none", "acodec": "mp4a.40.2"}
        info = {"requested_formats": [video, audio]}
        self.assertTrue(eligible(info, {}))
        for change in ({"vcodec": "vp9"}, {"protocol": "http_dash_segments"}, {"has_drm": True}):
            self.assertFalse(eligible({"requested_formats": [dict(video, **change), audio]}, {}))
        for codec in ("mp4a.40.5", "mp4a.40.29", "mp4a.40.36", "mp4a.40.42"):
            self.assertFalse(eligible({"requested_formats": [video, dict(audio, acodec=codec)]}, {}))
        self.assertFalse(eligible({"requested_formats": [video, audio, audio]}, {}))
        self.assertFalse(eligible(info, {"external_downloader": {"default": "aria2c"}}))
        self.assertEqual(video["vcodec"], "avc1.640028")


if __name__ == "__main__":
    unittest.main()
