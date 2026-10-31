"""Player-owned continuous TS fixture; never accesses YouTubeHarvester."""
import copy
import json
import subprocess
import sys
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'qt' / 'tools'))
from progressive_integrity import protect_snapshot


def fixture(root, profile='main-bframes', seconds=20, unknown_length=False):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    rate = '30000/1001' if profile == 'high-fractional' else '25'
    audio_rate = 44100 if profile == 'high-fractional' else 48000
    high = profile == 'high-fractional'
    original = root / 'continuous-original.ts'
    command = [
        'ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
        f'testsrc2=size=160x90:rate={rate}:duration={seconds}',
        '-f', 'lavfi', '-i', f'sine=frequency=880:sample_rate={audio_rate}:duration={seconds}',
        '-map', '0:v', '-map', '1:a', '-c:v', 'libx264', '-preset', 'veryfast',
        '-profile:v', 'high' if high else 'main', '-pix_fmt', 'yuv420p', '-x264-params',
        'force-cfr=1:repeat-headers=1:keyint=25:min-keyint=25:scenecut=0:open-gop=0:bframes=2'
        + (':8x8dct=1' if high else ''),
        '-c:a', 'aac', '-ac', '2', '-b:a', '96k', '-avoid_negative_ts', 'make_zero',
        '-muxdelay', '0.3', '-f', 'mpegts', str(original)]
    subprocess.run(command, check=True, capture_output=True)
    raw = original.read_bytes()
    packets = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-show_packets', '-show_data_hash', 'sha256',
        '-of', 'json', str(original)]))['packets']
    groups = [[p for p in packets if p['codec_type'] == k] for k in ('video', 'audio')]
    assert all(groups) and len(raw) % 188 == 0
    origin = int(groups[1][0]['pts'])
    ticks = lambda n: n * 10000000 // 90000
    offset = ticks(origin) + (100000 if profile == 'main-preroll' else 0)
    duration = max(ticks(int(p['pts']) + int(p['duration'])) - offset for p in groups[0])
    duration = max(duration, len(groups[1]) * 1024 * 10000000 // audio_rate - (offset - ticks(origin)))
    model = dict(schemaVersion=2, sessionId=uuid.uuid4().hex, generation='1', revision='1',
                 writer=dict(instanceId=uuid.uuid4().hex, state='downloading'),
                 timeline=dict(timebase=dict(numerator=1, denominator=10000000), live=False,
                               duration=str(duration), timestampOffset=str(offset)),
                 resources=[dict(id='mux', name='mux.part', length=None if unknown_length else str(len(raw)),
                                 state='downloading', ranges=[])], tracks=[])
    for name, codec in (('video', 'h264'), ('audio', 'aac-lc')):
        model['tracks'].append(dict(id=name, kind=name, required=True, codecHint=codec,
                                    containerHint='mpegts-growing', state='downloading',
                                    initializationReady=True, source=dict(resourceId='mux')))
    model['metadata'] = dict(revision='1', title='Growing MPEG-TS ' + profile,
                             chapters=[dict(id='intro', time='0', title='Intro'),
                                       dict(id='two', time='20000000', title='Two seconds')])
    return model, raw, original, groups


def publish(model, raw, directory, prefix, *, previous=None, complete=False):
    """Append physical bytes first; preserve old SHA block boundaries thereafter."""
    value = copy.deepcopy(model)
    if previous is not None:
        value['revision'] = str(int(previous['revision']) + 1)
    resource = value['resources'][0]
    old = int(previous['resources'][0]['ranges'][0]['length']) if previous and previous['resources'][0]['ranges'] else 0
    assert old <= prefix <= len(raw)
    path = Path(directory) / resource['name']
    with path.open('ab') as stream:
        assert stream.tell() == old
        stream.write(raw[old:prefix])
        stream.flush()
    resource['ranges'] = [dict(offset='0', length=str(prefix))] if prefix else []
    if complete:
        assert prefix == len(raw)
        resource.update(length=str(len(raw)), state='complete')
        value['writer']['state'] = 'complete'
        for track in value['tracks']:
            track['state'] = 'complete'
    return protect_snapshot(value, directory, previous=previous)
