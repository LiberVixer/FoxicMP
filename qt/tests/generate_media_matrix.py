#!/usr/bin/env python3
"""Generate a bounded, local codec corpus and record actual ffprobe metadata."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    root.mkdir(parents=True, exist_ok=True)
    cases = []

    def run(arguments):
        subprocess.run(['ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error',
                        '-y', '-filter_threads', '1', *arguments], check=True, timeout=90)

    def record(name, expected='play', **extra):
        path = root / name
        streams = []
        if expected == 'play':
            probe = subprocess.run(['ffprobe', '-v', 'error', '-show_streams',
                                    '-show_format', '-of', 'json', str(path)],
                                   check=True, capture_output=True, text=True, timeout=10)
            metadata = json.loads(probe.stdout)
            streams = metadata['streams']
        case = dict(file=name, expected=expected, bytes=path.stat().st_size,
                    sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    video=sum(s['codec_type'] == 'video' for s in streams),
                    audio=sum(s['codec_type'] == 'audio' for s in streams),
                    subtitles=sum(s['codec_type'] == 'subtitle' for s in streams),
                    streams=[{k: s[k] for k in ('codec_type', 'codec_name', 'pix_fmt',
                                              'sample_rate', 'channels', 'tags') if k in s}
                             for s in streams], **extra)
        cases.append(case)
        print('generated', name, flush=True)

    video = ['-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=24:duration=3']
    tone = ['-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=3']
    recipes = [
        ('Русский H264.mp4', 'libx264', 'aac', ['-movflags', '+faststart']),
        ('h264-10bit.mkv', 'libx264', 'aac', ['-pix_fmt', 'yuv420p10le']),
        ('hevc-main10.mkv', 'libx265', 'aac', ['-pix_fmt', 'yuv420p10le', '-x265-params', 'pools=1:frame-threads=1:log-level=error']),
        ('vp8.webm', 'libvpx', 'libvorbis', ['-deadline', 'realtime']),
        ('vp9.webm', 'libvpx-vp9', 'libopus', ['-deadline', 'realtime', '-cpu-used', '8']),
        ('av1.mkv', 'libaom-av1', 'libopus', ['-cpu-used', '8']),
        ('mpeg2.mpg', 'mpeg2video', 'mp2', []),
        ('mpeg4.avi', 'mpeg4', 'libmp3lame', []),
        ('wmv2.asf', 'wmv2', 'wmav2', []),
        ('theora.ogv', 'libtheora', 'libvorbis', []),
        ('ffv1-flac.mkv', 'ffv1', 'flac', []),
        ('mjpeg-pcm.avi', 'mjpeg', 'pcm_s16le', ['-pix_fmt', 'yuvj420p']),
        ('h264-ac3.ts', 'libx264', 'ac3', []),
        ('h264-fragmented.mp4', 'libx264', 'aac', ['-movflags', '+empty_moov+default_base_moof+frag_keyframe']),
    ]
    for name, codec, audio, options in recipes:
        run([*video, *tone, '-map', '0:v', '-map', '1:a', '-c:v', codec,
             '-threads', '2', '-g', '24', '-c:a', audio, *options, str(root / name)])
        record(name)
    for name, codec, options in [
        ('mp3.mp3', 'libmp3lame', []), ('aac.m4a', 'aac', []),
        ('opus.ogg', 'libopus', []), ('vorbis.ogg', 'libvorbis', []),
        ('lossless.flac', 'flac', []), ('lossless.m4a', 'alac', []),
        ('pcm24.wav', 'pcm_s24le', []), ('ac3.ac3', 'ac3', []),
        ('eac3.eac3', 'eac3', []), ('dts.dts', 'dca', ['-strict', '-2']),
    ]:
        run([*tone, '-c:a', codec, '-threads', '2', *options, str(root / name)])
        record(name)
    run([*video, '-an', '-c:v', 'libx264', '-threads', '2', str(root / 'video-only.mp4')])
    record('video-only.mp4')
    (root / 'captions.srt').write_text('1\n00:00:00,000 --> 00:00:03,000\nFoxicMP matrix\n', encoding='utf-8')
    (root / 'chapters.ffmeta').write_text(';FFMETADATA1\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=1500\ntitle=First\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=1500\nEND=3000\ntitle=Second\n', encoding='utf-8')
    run(['-i', str(root / 'Русский H264.mp4'), '-f', 'lavfi', '-i',
         'sine=frequency=880:sample_rate=48000:duration=3', '-i', str(root / 'captions.srt'),
         '-i', str(root / 'chapters.ffmeta'), '-map', '0:v', '-map', '0:a', '-map', '1:a',
         '-map', '2:s', '-map', '2:s', '-map_chapters', '3', '-c:v', 'copy', '-c:a:0', 'aac',
         '-c:a:1', 'ac3', '-c:s:0', 'srt', '-c:s:1', 'ass', '-metadata:s:a:0', 'language=rus',
         '-metadata:s:a:1', 'language=eng', '-t', '3', str(root / 'multi-track.mkv')])
    record('multi-track.mkv', chapters=2)
    # Three different invalid-input paths, with recovery tested after each.
    for name, data in [('empty.mp4', b''), ('invalid.mkv', b'FoxicMP invalid container\n' * 32),
                       ('header-only.mp4', (root / 'Русский H264.mp4').read_bytes()[:32])]:
        (root / name).write_bytes(data)
        record(name, 'reject')
    manifest = dict(schema=1, generator='synthetic-local', seconds=3,
                    ffmpeg=subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0],
                    cases=cases)
    (root / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'{len(cases)} cases recorded in {root / "manifest.json"}', flush=True)


if __name__ == '__main__':
    main()
