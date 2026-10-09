"""Create local H.264/AAC signals and packet indexes for Windows playback tests."""
import argparse
import json
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", required=True)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--audio-offset-ms", nargs="+", type=int, default=[])
    parser.add_argument("--fragmented", action="store_true", help="Also remux signals to fMP4 on a shared +1 second clock")
    args = parser.parse_args()
    root = Path(args.work).absolute()
    root.mkdir(parents=True, exist_ok=True)
    # Flash/beep pairs every two seconds permit a separate physical A/V sync check.
    common = [args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y"]
    subprocess.run([*common, "-f", "lavfi", "-i", "color=c=black:s=640x360:r=25:d=90",
                    "-vf", "drawbox=c=white:t=fill:enable='lt(mod(t,2),0.08)'",
                    "-c:v", "libx264", "-preset", "veryfast", "-g", "50", "-bf", "2",
                    "-movflags", "+faststart", str(root / "video.mp4")], check=True)
    subprocess.run([*common, "-f", "lavfi", "-i",
                    r"aevalsrc=if(lt(mod(t\,2)\,0.08)\,0.6*sin(2*PI*880*t)\,0):s=48000:d=90",
                    "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart",
                    str(root / "audio.m4a")], check=True)
    offsets = []
    for milliseconds in args.audio_offset_ms:
        name = f"audio-offset-{milliseconds}.m4a"
        subprocess.run([*common, "-itsoffset", str(milliseconds / 1000),
                        "-i", str(root / "audio.m4a"), "-c", "copy",
                        "-movflags", "+faststart", str(root / name)], check=True)
        offsets.append(name)
    names = ["video.mp4", "audio.m4a", *offsets]
    if args.fragmented:
        for name in tuple(names):
            target = name.replace("video.", "video-fragmented.").replace("audio.", "audio-fragmented.").replace("audio-offset-", "audio-fragmented-offset-")
            subprocess.run([*common, "-copyts", "-i", str(root / name), "-map", "0:0", "-c", "copy",
                            "-movflags", "+empty_moov+frag_keyframe+default_base_moof+skip_trailer+frag_discont",
                            "-use_editlist", "0", "-output_ts_offset", "1", "-avoid_negative_ts", "disabled",
                            "-frag_duration", "2000000", str(root / target)], check=True)
            names.append(target)
    for name in names:
        media = root / name
        raw = subprocess.check_output([args.ffprobe, "-v", "quiet", "-show_packets",
                                       "-of", "json", str(media)])
        json.loads(raw)  # Do not leave an invalid packet fixture after a tool failure.
        media.with_suffix(".packets.json").write_bytes(raw)


if __name__ == "__main__":
    main()
