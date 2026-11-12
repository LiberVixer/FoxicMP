"""R01 preparation, not HEVC continuous playback acceptance.

Build ordinary HEVC MP4 fixtures, prove a packet-copy fMP4 oracle preserves
their clocks/pixels, and keep HEVC outside the existing H.264 worker modes.
No player, downloader, desktop/audio session or installed profile is opened.
"""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import subprocess
import time

from progressive_mux_mp4_fixture import boxes
from progressive_stream_remux_worker_test import probe, serve


PROFILES = (
    ("main-hvc1", "25", "yuv420p", "hvc1"),
    ("main-hev1-fractional", "30000/1001", "yuv420p", "hev1"),
    ("main10-hvc1", "25", "yuv420p10le", "hvc1"),
    ("main10-hev1-fractional", "30000/1001", "yuv420p10le", "hev1"),
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(args, directory, name):
    (directory / (name + ".command.json")).write_text(json.dumps(args, indent=2) + "\n")
    result = subprocess.run(args, capture_output=True, timeout=90)
    (directory / (name + ".stderr")).write_bytes(result.stderr)
    (directory / (name + ".exit")).write_text(str(result.returncode) + "\n")
    assert result.returncode == 0, (name, result.returncode, result.stderr[-2000:])
    return result.stdout


def frames(path, pix_fmt, directory, name):
    raw = command([
        "ffmpeg", "-v", "error", "-threads", "1", "-i", str(path),
        "-map", "0:v:0", "-threads", "1", "-pix_fmt", pix_fmt,
        "-f", "framemd5", "-",
    ], directory, name)
    (directory / (name + ".framemd5")).write_bytes(raw)
    return [line.rsplit(b",", 1)[-1].strip()
            for line in raw.splitlines() if line and not line.startswith(b"#")]


def run(worker, report_directory):
    worker = Path(worker).resolve()
    root = Path(report_directory).resolve()
    root.mkdir(parents=True, exist_ok=True)
    rows = []
    info = json.loads(command([str(worker), "--info"], root, "worker-info"))
    assert "continuous-h264-reordered-video-v1" in info["contract"]["profiles"]
    (root / "worker-info.json").write_text(json.dumps(info, indent=2) + "\n")
    for name, rate, pix_fmt, tag in PROFILES:
        directory = root / name
        directory.mkdir(exist_ok=True)
        source = directory / "ordinary.mp4"
        output = directory / "oracle.fmp4"
        command([
            "ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
            f"testsrc2=size=160x90:rate={rate}:duration=24", "-an",
            "-c:v", "libx265", "-threads", "1", "-filter_threads", "1",
            "-preset", "fast", "-pix_fmt", pix_fmt, "-tag:v", tag,
            "-x265-params", "pools=1:frame-threads=1:log-level=error:"
            "keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes=2",
            "-movflags", "+faststart", str(source),
        ], directory, "encode")
        before = sha(source)
        raw = source.read_bytes()
        top = list(boxes(raw))
        assert [kind for _, _, kind, _ in top].count(b"moov") == 1
        assert not any(kind == b"moof" for _, _, kind, _ in top)
        moov_end = next(at + size for at, size, kind, _ in top if kind == b"moov")
        mdat_start = next(at for at, _, kind, _ in top if kind == b"mdat")
        assert moov_end <= mdat_start
        original = probe(source)
        assert len(original["streams"]) == 1
        stream = original["streams"][0]
        assert stream["codec_name"] == "hevc" and stream["codec_tag_string"] == tag
        assert stream["pix_fmt"] == pix_fmt and stream["start_pts"] == 0
        assert stream["has_b_frames"] > 0
        clock = Fraction(stream["time_base"])
        packets = original["packets"]
        assert any(int(packet["pts"]) != int(packet["dts"]) for packet in packets)
        cut = next(int(packet["pos"]) + 13 for packet in packets
                   if int(packet["pts"]) * clock >= 20)
        assert moov_end < cut < len(raw)
        # Model confirmed AVIO prefix only; this is not a public-player test.
        rejection, data = serve(worker, source, growing=True, cut_seconds=(20,),
                                mode="continuous-h264-reordered-video-v1")
        assert rejection["exitCode"] == 1 and not data, rejection
        assert rejection["result"]["code"] == "unsupported-stream-codec", rejection
        assert not rejection["result"]["sourceComplete"], rejection
        assert not rejection["result"]["playbackSupported"], rejection
        # Full-source FFmpeg reference only: future growing playback must match it.
        command([
            "ffmpeg", "-v", "error", "-y", "-i", str(source), "-map", "0:v:0",
            "-c:v", "copy", "-avoid_negative_ts", "disabled",
            "-movflags", "delay_moov+frag_keyframe+default_base_moof",
            "-use_editlist", "1", "-movie_timescale", str(clock.denominator),
            "-f", "mp4", str(output),
        ], directory, "copy-oracle")
        target = probe(output)
        assert len(target["streams"]) == 1
        target_clock = Fraction(target["streams"][0]["time_base"])
        assert target["streams"][0]["pix_fmt"] == pix_fmt
        assert len(packets) == len(target["packets"]) > 128
        for left, right in zip(packets, target["packets"]):
            assert left["data_hash"] == right["data_hash"]
            for field in ("pts", "dts", "duration"):
                assert int(left[field]) * clock == int(right[field]) * target_clock, (
                    name, field, left, right)
        source_frames = frames(source, pix_fmt, directory, "source-decoded")
        assert source_frames == frames(output, pix_fmt, directory, "oracle-decoded")
        assert sha(source) == before
        rows.append(dict(
            profile=name, codec="hevc", pixelFormat=pix_fmt, sampleEntry=tag,
            frameRate=rate, seconds=24, packets=len(packets), frames=len(source_frames),
            inputBytes=len(raw), sourceSha256=before, outputSha256=sha(output),
            headMoovEnd=moov_end, simulatedConfirmedPrefixBytes=cut,
            sourceTimebase=str(clock), outputTimebase=str(target_clock),
            packetPayloadsIdentical=True, rationalClocksExact=True,
            decodedFrameHashesIdentical=True, sourceUnchanged=True,
            legacyH264ModeRejection=rejection,
        ))
    result = dict(
        result="PASS", scope="fixture-oracle-and-existing-mode-rejection",
        continuousHevcPlaybackAccepted=False, publicPlayerTested=False,
        proposedHevcModeDeclared="continuous-hevc-reordered-video-v1"
        in info["contract"]["profiles"],
        workerSha256=sha(worker), testSourceSha256=sha(Path(__file__)),
        ffmpegVersion=command(["ffmpeg", "-version"], root, "ffmpeg-version")
        .decode().splitlines()[0], rows=rows,
    )
    (root / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", required=True)
    parser.add_argument("--report-directory", required=True)
    args = parser.parse_args()
    start = time.monotonic()
    result = run(args.worker, args.report_directory)
    print(json.dumps(dict(result=result["result"], rows=len(result["rows"]),
                          seconds=time.monotonic() - start)))
