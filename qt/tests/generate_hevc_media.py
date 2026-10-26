#!/usr/bin/env python3
"""Deterministic local 20 s fMP4 Main/Main10 HEVC fixtures, no network or remux."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys


def generate(root):
    root = Path(root).resolve()
    report = []
    for depth, tag in [(8, 'hvc1'), (10, 'hev1')]:
        folder = root / ('progressive-hevc' + str(depth))
        folder.mkdir(parents=True, exist_ok=True)
        video = folder / 'video.mp4'
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
            'testsrc2=size=320x180:rate=25', '-t', '20', '-an', '-c:v', 'libx265',
            '-threads', '2', '-filter_threads', '1', '-pix_fmt', 'yuv420p' if depth == 8 else 'yuv420p10le',
            '-preset', 'fast', '-x265-params', 'pools=1:frame-threads=1:log-level=error:keyint=25:min-keyint=25:scenecut=0',
            '-tag:v', tag, '-movflags', '+empty_moov+default_base_moof+frag_keyframe', str(video)], check=True)
        shutil.copyfile(root / 'progressive/audio.m4a', folder / 'audio.m4a')
        result = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-of', 'json', str(video)]))['streams'][0]
        assert result['codec_name'] == 'hevc' and result['pix_fmt'] == ('yuv420p' if depth == 8 else 'yuv420p10le')
        report.append(dict(directory=folder.name, tag=tag, depth=depth, stream=result,
            sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (video, folder / 'audio.m4a')}))
    (root / 'progressive-hevc-fixtures.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


if __name__ == '__main__':
    generate(sys.argv[1])
