"""Index durations for the saved VFR preflight views, independently of playback."""
import argparse
import csv
from fractions import Fraction
import hashlib
import io
import json
from pathlib import Path

from progressive_webm_opus_remux_preflight import packets
from progressive_webm_remux_preflight import command, sha


def run(runner, directory):
    directory = Path(directory)
    preflight = json.loads((directory / "result.json").read_text())
    assert preflight["result"] == "PASS-preflight-with-open-gates"
    results = []
    for row in preflight["rows"]:
        if row["nativeFull"]["result"] != "PASS":
            continue
        folder = directory / row["profile"] / row["form"]
        path = folder / "full/video.webm"
        metadata = json.loads((folder / "source-probe.stdout").read_text())
        expected = packets(metadata, "video")
        p = command([runner, path, "0", "webm"], folder, "native-index-times")
        assert p.returncode == 0, p.stderr
        samples = list(csv.DictReader(io.StringIO(p.stdout.decode())))
        assert len(samples) == len(expected)
        raw = path.read_bytes()
        starts = [int(s["start"]) for s in samples]
        ends = [int(s["end"]) for s in samples]
        for sample, packet in zip(samples, expected):
            pos, size = int(sample["offset"]), int(sample["size"])
            assert "SHA256:"+hashlib.sha256(raw[pos:pos+size]).hexdigest() == packet[0]
            assert Fraction(sample["start"]) == Fraction(packet[1])*10000000
        gaps = [b-a for a, b in zip(ends, starts[1:])]
        if row["form"].startswith("encoder-simple"):
            assert all(b-a == row["defaultDurationNs"]//100 for a, b in zip(starts, ends))
            assert max(gaps) > 0
        else:
            assert all(g == 0 for g in gaps)
            final_ms = round(Fraction(row["defaultDurationNs"], 1000000))
            assert ends[-1]-starts[-1] == final_ms*10000
        results.append(dict(profile=row["profile"], form=row["form"], exitCode=p.returncode,
                            samples=len(samples), sourcePayloadAndPtsExact=True,
                            maxInterSampleGap100ns=max(gaps), lastEnd100ns=ends[-1],
                            firstSamples=[dict(start=a, end=b) for a, b in zip(starts[:4], ends[:4])]))
    assert len(results) == 12
    result = dict(result="PASS", count=len(results), rows=results,
                  indexRunnerSha256=sha(runner), testSourceSha256=sha(__file__),
                  publicPlayerTested=False, preflightReportSha256=sha(directory / "result.json"))
    (directory / "index-times.json").write_text(json.dumps(result, indent=2)+"\n")
    return result


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--runner", type=Path, required=True)
    p.add_argument("--report-directory", type=Path, required=True)
    a = p.parse_args()
    result = run(a.runner.resolve(), a.report_directory.resolve())
    print(json.dumps(dict(result=result["result"], count=result["count"])))
