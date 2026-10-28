#!/usr/bin/env python3
"""Small reproducible codec/track/chapter fixtures; no network media required."""
import pathlib, subprocess, sys
root=pathlib.Path(sys.argv[1]).resolve(); root.mkdir(parents=True,exist_ok=True)
(root/'subtitle.srt').write_text('1\n00:00:00,000 --> 00:00:08,000\nFoxicMP — русский текст\n',encoding='utf-8')
(root/'external.ass').write_text('''[Script Info]
ScriptType: v4.00+
PlayResX: 640
PlayResY: 360
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,DejaVu Sans,22,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.00,0:00:08.00,Default,,0,0,0,,FoxicMP ASS
''',encoding='utf-8')
(root/'chapters.ffmeta').write_text(';FFMETADATA1\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=4000\ntitle=Начало\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=4000\nEND=8000\ntitle=Вторая глава\n',encoding='utf-8')
def run(args):subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y',*args],check=True)
base=['-f','lavfi','-i','testsrc2=size=320x180:rate=24','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-t','8']
for name,codec,audio,extra in [('Русский H264.mp4','libx264','aac',[]),('HEVC.mov','libx265','aac',['-x265-params','pools=1:frame-threads=1:log-level=error']),('VP9.webm','libvpx-vp9','libopus',['-deadline','realtime','-cpu-used','8']),('AV1.mkv','libaom-av1','libopus',['-cpu-used','8','-row-mt','1']),('MPEG4.avi','mpeg4','mp3',[])]:
 run([*base,'-c:v',codec,'-threads','2',*extra,'-c:a',audio,str(root/name)])
run(['-i',str(root/'Русский H264.mp4'),'-f','lavfi','-i','sine=frequency=880:sample_rate=48000','-i',str(root/'subtitle.srt'),'-i',str(root/'chapters.ffmeta'),'-map','0:v','-map','0:a','-map','1:a','-map','2:s','-map_metadata','3','-map_chapters','3','-metadata:s:a:0','language=rus','-metadata:s:a:1','language=eng','-c:v','copy','-c:a','aac','-c:s','srt','-t','8',str(root/'chapters.mkv')])
(root/'progressive').mkdir(exist_ok=True)
run(['-f','lavfi','-i','testsrc2=size=320x180:rate=25','-t','20','-an','-c:v','libx264','-threads','2','-g','25','-keyint_min','25','-sc_threshold','0','-movflags','+empty_moov+default_base_moof+frag_keyframe',str(root/'progressive'/'video.mp4')])
run(['-f','lavfi','-i','sine=frequency=880:sample_rate=48000','-t','20','-vn','-c:a','aac','-movflags','+empty_moov+default_base_moof+frag_keyframe','-frag_duration','1000000',str(root/'progressive'/'audio.m4a')])
(root/'progressive-indexed').mkdir(exist_ok=True)
run(['-f','lavfi','-i','testsrc2=size=320x180:rate=25','-t','20','-an','-c:v','libx264','-threads','2','-g','25','-keyint_min','25','-sc_threshold','0','-movflags','+faststart',str(root/'progressive-indexed'/'video.mp4')])
run(['-f','lavfi','-i','sine=frequency=880:sample_rate=48000','-t','20','-vn','-c:a','aac','-movflags','+faststart',str(root/'progressive-indexed'/'audio.m4a')])

from generate_hevc_media import generate as generate_hevc
generate_hevc(root)

from generate_av1_media import generate as generate_av1
generate_av1(root)

from generate_vp9_media import generate as generate_vp9
generate_vp9(root)

from generate_opus_media import generate as generate_opus
generate_opus(root)

from generate_webm_media import generate as generate_webm
generate_webm(root)

from generate_vp8_vorbis_media import generate as generate_vp8_vorbis
generate_vp8_vorbis(root)

from generate_progressive_seek_media import generate as generate_seek
generate_seek(root)
