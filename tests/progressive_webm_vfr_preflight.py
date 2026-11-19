"""Read-only VFR characterization; never advertises or commits player playback.

The test-only EBML rewrite has a complete fixture boundary oracle. It is not a
growing production scanner. Actual packet clocks/payloads and decoded media are
checked independently with ffprobe/ffmpeg and native ResourceIO projections.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import traceback
import uuid
from fractions import Fraction

from progressive_growing_ts_fixture import publish
from progressive_mux_ts_conformance import Rpc
from progressive_stream_remux_worker_test import serve
from progressive_webm_fixtures import element, width
from progressive_webm_opus_remux_preflight import configuration, packets, pcm, seal
from progressive_webm_remux_preflight import ok, probe, scan, sha, unknown_clusters

PROFILES = (("p0-50-mono", "50", False, 1),
            ("p2-50-stereo", "50", True, 2),
            ("p0-ntsc60-stereo", "60000/1001", False, 2))
SELECT = "if(lt(t,8),not(mod(n,2)),if(lt(t,16),not(mod(n,3)),1))"


def decoded(path, pixels, folder, name):
    # The output muxer must not manufacture CFR duplicates after removing a
    # DefaultDuration header. Compare decoded frames, not inferred output fps.
    raw = ok(["ffmpeg", "-v", "error", "-threads", "1", "-i", path, "-map",
              "0:v:0", "-threads", "1", "-pix_fmt", pixels, "-fps_mode",
              "passthrough", "-f", "framemd5", "-"], folder, name)
    return [line.rsplit(b",", 1)[-1].strip().decode() for line in raw.splitlines()
            if line and not line.startswith(b"#")]


def fixture(folder, profile):
    name, rate, ten, channels = profile
    folder.mkdir(parents=True, exist_ok=True)
    pixels = "yuv420p10le" if ten else "yuv420p"
    video, output = folder / "video.webm", folder / "source.webm"
    args = ["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
            f"testsrc2=size=160x90:rate={rate}:duration=24", "-an", "-vf",
            "select='" + SELECT + "'", "-fps_mode", "vfr", "-c:v", "libvpx-vp9",
            "-threads", "2", "-filter_threads", "1", "-pix_fmt", pixels,
            "-profile:v", "2" if ten else "0", "-g", "25", "-cpu-used",
            "4" if ten else "8", "-lag-in-frames", "25" if ten else "0",
            "-auto-alt-ref", str(int(ten)), "-b:v", "300k"]
    if ten:
        args += ["-passlogfile", str(folder / "pass")]
        ok(args + ["-pass", "1", "-f", "null", "-"], folder, "encode-pass1")
        args += ["-pass", "2"]
    ok(args + ["-live", "1", video], folder, "encode-video")
    ok(["ffmpeg", "-v", "error", "-y", "-i", video, "-f", "lavfi", "-i",
        "sine=frequency=997:sample_rate=48000:duration=24", "-map", "0:v", "-map",
        "1:a", "-c:v", "copy", "-c:a", "libopus", "-ac", str(channels),
        "-frame_duration", "20", "-cluster_time_limit", "1000", "-live", "1",
        output], folder, "encode-mux")
    return output, pixels


def ebml(tag, body):
    ident = tag.to_bytes((tag.bit_length() + 7) // 8, "big")
    size_width = next(n for n in range(1, 9) if len(body) < (1 << (7*n)) - 1)
    return ident + ((1 << (7*size_width)) | len(body)).to_bytes(size_width, "big") + body


def unsigned(tag, value):
    return ebml(tag, value.to_bytes(max(1, (value.bit_length()+7)//8), "big"))


def explicit_durations(raw, *, remove_default=False, video_track=None):
    """Canonical fixture: retain payload/PTS; replace video SimpleBlocks only."""
    layout = scan(raw)
    if video_track is None:
        video_track = configuration(raw)[1]["number"]
    video = [b for b in layout["blocks"] if b["track"] == video_track]
    # The final displayed frame uses the fixture encoder's bounded base period.
    rate = Fraction(1000000000, layout["defaultDurationNs"])
    final_ticks = max(1, round(Fraction(1000, rate)))
    durations = {b["offset"]: video[i+1]["ticks"]-b["ticks"]
                 if i+1 < len(video) else final_ticks for i, b in enumerate(video)}
    assert all(0 < d <= 1000 for d in durations.values())

    def head(start, end):
        out = bytearray()
        at = start
        while at < end:
            tag, body, stop = element(raw, at)
            assert stop is not None and stop <= end
            if tag == 0x114d9b74 or (tag == 0x23e383 and remove_default):
                # Header/Cluster offsets change: discard the fixture SeekHead.
                pass
            elif tag in (0x1654ae6b, 0xae):
                out += ebml(tag, head(body, stop))
            else:
                out += raw[at:stop]
            at = stop
        return bytes(out)

    _, _, segment_at = element(raw, 0)
    _, segment_body, segment_end = element(raw, segment_at)
    assert segment_end is None
    result = bytearray(raw[:segment_body])
    result += head(segment_body, layout["headBytes"])
    for cluster in layout["clusters"]:
        contents = bytearray()
        at = cluster["body"]
        while at < cluster["end"]:
            tag, body, end = element(raw, at)
            assert end is not None
            if at in durations:
                assert tag == 0xa3, "Fixture encoder unexpectedly emits video BlockGroup"
                block = bytearray(raw[body:end])
                flags_at = width(block[0]) + 2
                key = bool(block[flags_at] & 0x80)
                block[flags_at] &= 0x7f  # Keyframe lives in ReferenceBlock for BlockGroup.
                group = ebml(0xa1, block) + unsigned(0x9b, durations[at])
                if not key:
                    group += ebml(0xfb, b"\0")  # Unknown previous dependency, per spec.
                contents += ebml(0xa0, group)
            else:
                contents += raw[at:end]
            at = end
        result += ebml(0x1f43b675, contents)
    # -live 1 controlled fixtures have no trailing Cues or Tags.
    assert layout["clusters"][-1]["end"] == len(raw)
    return bytes(result), list(durations.values())


def native(runner, raw, folder, *, complete, expected_images, expected_pcm,
           pixels, channels, initialization_allowed):
    folder.mkdir(parents=True, exist_ok=True)
    result = None
    with tempfile.TemporaryDirectory(prefix="fox-vfr-native-") as t:
        root = Path(t)
        directory = root / "session"
        directory.mkdir()
        model = dict(schemaVersion=2, sessionId=uuid.uuid4().hex, generation="1",
                     revision="1", writer=dict(instanceId=uuid.uuid4().hex,
                     state="downloading"), timeline=dict(timebase=dict(numerator=1,
                     denominator=10000000), live=False, duration="250000000",
                     timestampOffset="0"), resources=[dict(id="mux", name="mux.part",
                     length=str(len(raw)), state="downloading", ranges=[])],
                     tracks=[dict(id=k, kind=k, required=True, codecHint=c,
                     containerHint="webm-muxed", state="downloading",
                     initializationReady=True, source=dict(resourceId="mux"))
                     for k, c in (("video", "vp9"), ("audio", "opus"))])
        value = publish(model, raw, directory, len(raw), complete=complete)
        before = sha(directory / "mux.part")
        rpc = Rpc([str(runner), "--registry", str(root / "registry")])
        try:
            opened = rpc("open", directory=str(directory), session=value)
            if not opened["ok"]:
                assert not initialization_allowed, opened
                result = dict(result="REJECTED-known-initialization-gate", reply=opened)
                assert not rpc("readers", version=1, sessionId=model["sessionId"],
                               generation="1")["readers"]
            else:
                assert initialization_allowed
                index, layout = rpc("index"), rpc("layout")
                assert index["tracks"][0]["samples"] > 0
                comparisons = []
                for kind in (0, 1):
                    size = int(layout["tracks"][kind]["available"])
                    data = bytearray()
                    for at in range(0, size, 65536):
                        answer = rpc("read", track=kind, offset=str(at),
                                     length=str(min(65536, size-at)))
                        assert answer["ok"] and answer["bytesRead"] > 0, answer
                        data += bytes.fromhex(answer["hex"])
                    view = folder / ("video.webm" if kind == 0 else "audio.webm")
                    view.write_bytes(data)
                    end = rpc("read", decoder=True, track=kind, offset=str(size), length="1")
                    assert end["bytesRead"] == (0 if complete else -1), end
                    comparisons.append(probe(view, folder, "view-"+str(kind)))
                images = decoded(folder / "video.webm", pixels, folder, "native-video")
                sound = pcm(folder / "audio.webm", folder, "native-pcm")
                assert images == expected_images[:len(images)]
                assert sound == expected_pcm[:len(sound)]
                if complete:
                    assert images == expected_images and sound == expected_pcm
                rpc("release")
                assert not rpc("readers", version=1, sessionId=model["sessionId"],
                               generation="1")["readers"]
                result = dict(result="PASS", complete=complete, index=index,
                              videoPackets=packets(comparisons[0], "video"),
                              audioPackets=packets(comparisons[1], "audio"),
                              videoFrames=len(images), pcmSamples=len(sound)//(4*channels),
                              exactPcm=True, decodedVideoIdentical=True,
                              incompleteReadWaits=not complete, lastOwnerReleased=True)
            assert sha(directory / "mux.part") == before
        finally:
            rpc.close()
        result["nativeExitCode"] = rpc.p.returncode
    return result


def run(worker, runner, report):
    report = Path(report).resolve()
    report.mkdir(parents=True, exist_ok=True)
    result = dict(result="RUNNING", scope="preflight-only", sdkVersionUnchanged="1.100",
                  publicPlayerTested=False, productionScannerTested=False,
                  workerSha256=sha(worker), nativeRunnerSha256=sha(runner),
                  testSourceSha256=sha(__file__), rows=[])
    try:
        for profile in PROFILES:
            folder = report / profile[0]
            source, pixels = fixture(folder, profile)
            original = source.read_bytes()
            original_meta = probe(source, folder, "original-probe")
            video_packets = packets(original_meta, "video")
            deltas = sorted({Fraction(b[1])-Fraction(a[1])
                             for a, b in zip(video_packets, video_packets[1:])})
            assert len(deltas) >= 3 and min(deltas) > 0, deltas
            images = decoded(source, pixels, folder, "original-video")
            sound = pcm(source, folder, "original-pcm")
            assert len(sound)//(4*profile[3]) == 1152000
            assert len(images) == len(video_packets)
            for form in ("encoder-simple", "explicit-duration", "no-default"):
                if form == "encoder-simple":
                    raw, durations = original, None
                else:
                    raw, durations = explicit_durations(original, remove_default=form == "no-default")
                for unknown in (False, True):
                    name = form + ("-unknown" if unknown else "-finite")
                    data = folder / name
                    data.mkdir(exist_ok=True)
                    raw_variant = unknown_clusters(raw, scan(raw)) if unknown else raw
                    path = data / "source.webm"
                    path.write_bytes(raw_variant)
                    before = sha(path)
                    metadata = probe(path, data, "source-probe")
                    for kind in ("video", "audio"):
                        assert packets(metadata, kind) == packets(original_meta, kind)
                    assert decoded(path, pixels, data, "source-video") == images
                    assert pcm(path, data, "source-pcm") == sound
                    layout = scan(raw_variant)
                    if profile[2]:
                        assert any(b["indexedSuperframe"] for b in layout["blocks"])
                    sealed = seal(raw_variant, layout)
                    sealed_path = data / "sealed.webm"
                    sealed_path.write_bytes(sealed)
                    sealed_meta = probe(sealed_path, data, "sealed-probe")
                    for kind in ("video", "audio"):
                        assert packets(sealed_meta, kind) == packets(metadata, kind)
                    full = native(runner, sealed, data / "full", complete=True,
                                  expected_images=images, expected_pcm=sound, pixels=pixels,
                                  channels=profile[3], initialization_allowed=form != "no-default")
                    closed = next(i for i, c in enumerate(layout["clusters"])
                                  if c["timestamp"] >= 12000)
                    assert closed+1 < len(layout["clusters"])
                    prefix = seal(raw_variant, layout, closed+1)
                    partial = native(runner, prefix, data / "partial", complete=False,
                                     expected_images=images, expected_pcm=sound, pixels=pixels,
                                     channels=profile[3], initialization_allowed=form != "no-default")
                    for view in (full, partial):
                        if view["result"] == "PASS":
                            for kind in ("video", "audio"):
                                rows = view.pop(kind+"Packets")
                                assert rows == packets(metadata, kind)[:len(rows)]
                            view["exactPacketsAndClocks"] = True
                    refused = []
                    for mode in ("continuous-webm-vp9-opus-v1", "continuous-webm-vp9-opus-ntsc-v1",
                                 "continuous-webm-vp9-opus-cfr-v1"):
                        extra = dict(video_rate=dict(numerator=int(Fraction(profile[1]).numerator),
                                     denominator=int(Fraction(profile[1]).denominator))) if "cfr" in mode else {}
                        answer, _ = serve(worker, path, mode=mode, format_hint="webm",
                                          video_samples=len(images), audio_samples=1152000,
                                          growing=False, **extra)
                        code = answer["result"]["code"]
                        # Existing worker propagates this native initialization
                        # diagnostic verbatim; preserve it as an integration gate.
                        raw_reason = form == "no-default" and code == "WebM video requires a bounded DefaultDuration"
                        assert answer["exitCode"] == 1 and (code.startswith("unsupported-") or raw_reason), answer
                        refused.append(dict(mode=mode, exitCode=answer["exitCode"], code=answer["result"]["code"]))
                    assert sha(path) == before and source.read_bytes() == original
                    row = dict(profile=profile[0], form=name, sourceSha256=before,
                               videoFrames=len(images), videoPtsStepsSeconds=list(map(str, deltas)),
                               defaultDurationNs=layout["defaultDurationNs"],
                               explicitDurationTicks=sorted(set(durations)) if durations else None,
                               nativeFull=full, nativePartial=partial, sourceUnchanged=True,
                               legacyModesRejectVfr=refused, oracleUsesCompleteFixture=True)
                    result["rows"].append(row)
                    (report / "progress.json").write_text(json.dumps(dict(count=len(result["rows"]))))
        result.update(result="PASS-preflight-with-open-gates", count=len(result["rows"]),
                      ffmpegVersion=ok(["ffmpeg", "-version"], report, "ffmpeg-version").decode().splitlines()[0])
        (report / "result.json").write_text(json.dumps(result, indent=2)+"\n")
        return result
    except BaseException:
        result.update(result="FAIL", error=traceback.format_exc())
        (report / "failure.json").write_text(json.dumps(result, indent=2)+"\n")
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--report-directory", type=Path, required=True)
    args = parser.parse_args()
    answer = run(args.worker.resolve(), args.runner.resolve(), args.report_directory)
    print(json.dumps(dict(result=answer["result"], count=answer["count"])))
