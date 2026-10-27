#!/usr/bin/env python3
"""Local VP9 Profile 0/2 8/10-bit fMP4 fixtures; no network or production transcoding."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys


def generate(root):
    root = Path(root).resolve()
    report = []
    for depth in (8, 10):
        folder = root / ('progressive-vp9-' + str(depth))
        folder.mkdir(parents=True, exist_ok=True)
        video = folder / 'video.mp4'
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
            'testsrc2=size=320x180:rate=25', '-t', '20', '-an', '-c:v', 'libvpx-vp9',
            '-threads', '2', '-filter_threads', '1', '-pix_fmt', 'yuv420p' if depth == 8 else 'yuv420p10le',
            '-deadline', 'good', '-cpu-used', '4', '-row-mt', '1', '-g', '25', '-keyint_min', '25', '-crf', '40', '-b:v', '0',
            '-lag-in-frames', '25', '-auto-alt-ref', '1', '-movflags', '+delay_moov+default_base_moof+frag_keyframe', str(video)], check=True)
        shutil.copyfile(root / 'progressive/audio.m4a', folder / 'audio.m4a')
        stream = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-of', 'json', str(video)]))['streams'][0]
        assert stream['codec_name'] == 'vp9' and stream['pix_fmt'] == ('yuv420p' if depth == 8 else 'yuv420p10le')
        report.append(dict(directory=folder.name, depth=depth, stream=stream,
            sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (video, folder / 'audio.m4a')}))
    (root / 'progressive-vp9-fixtures.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


if __name__ == '__main__':
    generate(sys.argv[1])
