"""QuickTime MOV projection and ordinary-player acceptance on synthetic media."""
import argparse
from fractions import Fraction
import json
from pathlib import Path
import subprocess
import tempfile

from progressive_indexed_mp4_fixture import prefix_at
from progressive_mux_ts_conformance import Rpc
from progressive_tail_mp4_fixture import publish as publish_tail
from progressive_growing_ts_fixture import publish
from progressive_process_metrics import sample as metrics
from progressive_player_crash_recovery import Peer, wait
from progressive_v2 import V2PreviewSession

ENCODERS = {'h264': 'libx264', 'hevc': 'libx265', 'mpeg4-part2': 'mpeg4'}
TAGS = {'h264': 'avc1', 'hevc': 'hvc1', 'mpeg4-part2': 'mp4v'}


def probe(path):
    return json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-show_packets', '-show_streams', '-show_format',
        '-show_data_hash', 'sha256', '-of', 'json', str(path)]))


def prepare(root):
    import uuid
    for codec, encoder in ENCODERS.items():
        for paired in (False, True):
            for tail in (False, True):
                name = f'{codec}-{"av" if paired else "video"}-{"tail" if tail else "head"}'
                folder = root/name
                folder.mkdir(parents=True, exist_ok=True)
                path = folder/'source.mov'
                cmd = ['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i', 'testsrc2=size=160x96:rate=25:duration=16']
                if paired:
                    cmd += ['-f', 'lavfi', '-i', 'sine=frequency=697:sample_rate=48000:duration=16']
                cmd += ['-c:v', encoder, '-threads', '2', '-g', '25', '-bf', '2',
                        '-tag:v', TAGS[codec], '-pix_fmt', 'yuv420p']
                if codec == 'hevc':
                    cmd += ['-x265-params', 'pools=1:frame-threads=1:open-gop=0:log-level=error']
                if paired:
                    cmd += ['-c:a', 'aac', '-ac', '2']
                if not tail:
                    cmd += ['-movflags', '+faststart']
                subprocess.run([*cmd, str(path)], check=True, capture_output=True)
                reference = probe(path)
                model = dict(schemaVersion=2, sessionId=uuid.uuid4().hex, generation='1', revision='1',
                             writer=dict(instanceId=uuid.uuid4().hex, state='downloading'),
                             timeline=dict(timebase=dict(numerator=1, denominator=10000000), live=False,
                                           duration=str(int(Fraction(reference['format']['duration'])*10000000)),
                                           timestampOffset='0'),
                             resources=[dict(id='mov', name='source.part', length=str(path.stat().st_size),
                                             state='downloading', ranges=[])], tracks=[])
                for kind, hint in [('video', codec), ('audio', 'aac-lc')][:2 if paired else 1]:
                    model['tracks'].append(dict(id=kind, kind=kind, required=True, codecHint=hint, containerHint='mov',
                                                state='downloading', source=dict(resourceId='mov'), initializationReady=True))
                (folder/'model.json').write_text(json.dumps(model))
                (folder/'reference.json').write_text(json.dumps(reference))


def corpus(root, cases):
    for folder in sorted(root.iterdir()):
        if not (folder/'model.json').is_file() or cases and folder.name not in cases:
            continue
        path = folder/'source.mov'
        model = json.loads((folder/'model.json').read_text())
        reference = json.loads((folder/'reference.json').read_text())
        groups = [[p for p in reference['packets'] if p['codec_type'] == t['kind']] for t in model['tracks']]
        scales = [Fraction(s['time_base']) for s in reference['streams']]
        publisher = publish_tail if folder.name.endswith('-tail') else publish
        yield folder.name, path, path.read_bytes(), model, groups, scales, publisher


def native(args):
    rows = []
    for name, path, raw, model, groups, scales, publisher in corpus(args.corpus, args.case):
        with tempfile.TemporaryDirectory(prefix='foxicmp-mov-index-') as tmp:
            root = Path(tmp)
            rpc = Rpc([args.runner, '--registry', str(root/'readers')])
            data = root/'data'
            data.mkdir()
            readers = lambda: rpc('readers', version=1, sessionId=model['sessionId'], generation='1')['readers']
            try:
                cut = prefix_at(groups, scales, 5)
                if publisher is publish_tail:
                    unconfirmed = publisher(model, raw, data, cut, tail_bytes=0)
                    status = rpc('open', directory=str(data), session=unconfirmed)
                    assert status.get('code') == 'waiting-index' and not readers(), (name, status)
                value = publisher(model, raw, data, cut)
                answer = rpc('open', directory=str(data), session=value)
                assert answer['ok'], (name, answer)
                assert len(readers()) == 1
                prefixes = rpc('layout')['tracks']
                for k, size in enumerate(prefixes):
                    assert rpc('read', decoder=True, track=k, offset=size['available'], length='1')['bytesRead'] == -1
                value = publisher(model, raw, data, len(raw), previous=value)
                assert rpc('publish', session=value)['ok']
                value = publisher(model, raw, data, len(raw), previous=value, complete=True)
                assert rpc('publish', session=value)['ok']
                assert rpc('status')['videoCodec'] == model['tracks'][0]['codecHint']
                for k, track in enumerate(rpc('layout')['tracks']):
                    projected = root/f'projection-{k}.mp4'
                    size = int(track['available'])
                    with projected.open('wb') as stream:
                        for at in range(0, size, 65536):
                            response = rpc('read', decoder=True, track=k, offset=str(at),
                                           length=str(min(size-at, 65536)))
                            assert response['bytesRead'] > 0, (name, response)
                            stream.write(bytes.fromhex(response['hex']))
                    test = probe(projected)
                    assert len(test['streams']) == 1 and len(test['packets']) == len(groups[k]), (name, k)
                    clock = Fraction(test['streams'][0]['time_base'])
                    for a, b in zip(test['packets'], groups[k]):
                        assert a['data_hash'] == b['data_hash'], (name, k, 'packet differs')
                        for field in ('pts', 'dts'):
                            assert int(a[field])*clock == int(b[field])*scales[k], (name, k, field, a[field], b[field])
                    def decoded(source):
                        options = ['-vn', '-c:a', 'pcm_s16le'] if k else ['-an', '-pix_fmt', 'yuv420p', '-fps_mode', 'passthrough', '-c:v', 'rawvideo', '-threads', '1']
                        return subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(source),
                                                        *options, '-f', 'hash', '-hash', 'sha256', '-'])
                    assert decoded(projected) == decoded(path), (name, k, 'decoded bytes differ')
                    assert rpc('read', decoder=True, track=k, offset=str(size), length='1')['bytesRead'] == 0
                rpc('release')
                assert not readers() and (data/'source.part').read_bytes() == raw
                rows.append(dict(case=name, packets=[len(g) for g in groups], decodedEqual=True))
            finally:
                rpc.close()
    assert rows
    return rows


def rejection_cases(args):
    folder = args.corpus/'h264-av-head'
    model = json.loads((folder/'model.json').read_text())
    raw = (folder/'source.mov').read_bytes()

    def replace(tag, offset, value):
        at = raw.index(tag)+offset
        return raw[:at]+value+raw[at+len(value):]

    esds = raw.index(b'esds')
    asc = raw.index(b'\x11\x90\x56\xe5\x00', esds, esds+62)
    short_frame = raw[:asc+1]+bytes([raw[asc+1] | 4])+raw[asc+2:]
    implicit_sbr = raw[:asc+4]+b'\x80'+raw[asc+5:]
    cases = [
        ('wrong-brand', replace(b'ftyp', 4, b'isom')),
        ('external-reference', replace(b'dref', 20, b'\0'*4)),
        ('wrong-sample-entry', replace(b'avc1', 0, b'encv')),
        ('invalid-avcc', replace(b'avcC', 4, b'\x02')),
        ('unsupported-edit-rate', replace(b'elst', 20, b'\0'*4)),
        ('invalid-chunk-offset', replace(b'stco', 12, b'\xff'*4)),
        ('wrong-aac-wave-format', replace(b'frma', 4, b'alac')),
        ('missing-aac-config', replace(b'esds', 0, b'free')),
        ('unsupported-aac-frame-length', short_frame),
        ('unsupported-implicit-sbr', implicit_sbr),
    ]
    names = []
    for name, modified in cases:
        with tempfile.TemporaryDirectory(prefix='foxicmp-mov-negative-') as tmp:
            root = Path(tmp)
            data = root/'data'
            data.mkdir()
            rpc = Rpc([args.runner, '--registry', str(root/'readers')])
            try:
                value = publish(model, modified, data, len(modified), complete=True)
                status = rpc('open', directory=str(data), session=value)
                assert status.get('code') == 'unsupported_media', (name, status)
                assert not rpc('readers', version=1, sessionId=model['sessionId'], generation='1')['readers']
                names.append(name)
            finally:
                rpc.close()
    with tempfile.TemporaryDirectory(prefix='foxicmp-mov-mismatch-') as tmp:
        root = Path(tmp)
        value = publish(model, raw, root, len(raw), complete=True)
        value['tracks'][0]['codecHint'] = 'hevc'
        rpc = Rpc([args.runner, '--registry', str(root/'readers')])
        try:
            status = rpc('open', directory=str(root), session=value)
            assert status.get('code') == 'unsupported_media', status
            assert not rpc('readers', version=1, sessionId=model['sessionId'], generation='1')['readers']
            names.append('declared-codec-mismatch')
        finally:
            rpc.close()
    return names


def ordinary(args):
    rows = []
    registration = None
    if args.edition == 'classic':
        from windows.progressive_family_test import post_command, registration_snapshot
        registration = registration_snapshot()
    with tempfile.TemporaryDirectory(prefix='foxicmp-mov-main-') as tmp:
        root = Path(tmp)
        peer = Peer(args.edition, args.player, Path(args.player).parent if args.edition == 'classic' else root/'profile',
                    root, args.reader_runner)
        try:
            peer.launch()
            for name, _, raw, model, groups, scales, publisher in corpus(args.corpus, args.case):
                folder = root/name
                folder.mkdir()
                cut = prefix_at(groups, scales, 13)
                value = publisher(model, raw, folder, cut)
                preview = V2PreviewSession(peer.client, str(folder), title='Private MOV '+name)
                try:
                    preview.publish(value)
                    def status():
                        state = preview.heartbeat()
                        (args.report.parent/'last-status.json').write_text(json.dumps(state, indent=2))
                        assert state['state'] not in ('error', 'cancelled'), state
                        return state
                    def progressed():
                        state = status()
                        if state['state']=='playing' and state['positionSeconds']>=10.1:
                            return state
                    viewed = wait(progressed, name+' before complete', 24)
                    peak = metrics(peer.process.pid)
                    assert not viewed['complete'] and int(viewed['decoderIoBytes']['video'])>0, viewed
                    if len(model['tracks'])==2:
                        assert int(viewed['decoderIoBytes']['audio'])>0, viewed
                    if args.edition == 'classic':
                        post_command(peer.process.pid, 888)  # ID_PLAY_PAUSE
                        paused = {}
                        def capture_pause():
                            state = status()
                            if state['state']=='paused':
                                paused.update(state)
                                return True
                        wait(capture_pause, name+' paused', 5)
                        post_command(peer.process.pid, 901)  # ID_PLAY_SEEKBACKWARDMED
                        def sought():
                            state = status()
                            return state['state']=='paused' and state['positionSeconds']<paused['positionSeconds']-.5
                        wait(sought, name+' backward seek on the confirmed prefix', 5)
                        post_command(peer.process.pid, 887)  # ID_PLAY_PLAY
                        wait(lambda:status()['state']=='playing', name+' resumed', 5)
                    value = publisher(model, raw, folder, len(raw), previous=value)
                    preview.publish(value)
                    held = status()
                    assert not held['complete'] and held['state']!='finished', held
                    value = publisher(model, raw, folder, len(raw), previous=value, complete=True)
                    preview.publish(value)
                    wait(lambda: status()['state']=='finished', name+' natural EOF', 18)
                    assert (folder/'source.part').read_bytes()==raw
                    rows.append(dict(case=name, beforeCompleteSeconds=viewed['positionSeconds'],
                                     naturalEof=True, classicPauseSeek=args.edition=='classic', processAtTenSeconds=peak))
                finally:
                    receipt=preview.close(timeout=2)
                    assert receipt['released'], receipt
        finally:
            if peer.process and peer.process.poll() is None:
                peer.kill('cleanup-owned-idle-process')
            peer.cleanup()
            # Preserve decoder diagnostics and the exact test-owned exit code.
            for log in root.glob('launch-*.log'):
                (args.report.parent/log.name).write_bytes(log.read_bytes())
            if registration is not None:
                assert registration_snapshot()==registration, 'Installed Classic shell registration changed'
    assert rows
    return rows


def controls(args):
    rows = []
    for name, _, raw, model, groups, scales, publisher in corpus(args.corpus, args.case):
        with tempfile.TemporaryDirectory(prefix='foxicmp-mov-controls-') as tmp:
            root = Path(tmp)
            data = root/'data'
            data.mkdir()
            cut = prefix_at(groups, scales, 12)
            initial = publisher(model, raw, data, cut)
            full = publisher(model, raw, data, len(raw), previous=initial)
            complete = publisher(model, raw, data, len(raw), previous=full, complete=True)
            files = []
            for label, snapshot in [('initial', initial), ('full', full), ('complete', complete)]:
                snapshot['sessionDirectory'] = str(data)
                path = root/(label+'.json')
                path.write_text(json.dumps(snapshot))
                files.append(str(path))
            result = subprocess.run([args.controls, *files, str(root/'profile')],
                                    capture_output=True, text=True, timeout=50)
            assert result.returncode == 0, (name, result.returncode, result.stdout, result.stderr)
            assert (data/'source.part').read_bytes() == raw
            rows.append(dict(case=name, exitCode=result.returncode))
    assert rows
    return rows


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--runner')
    parser.add_argument('--controls')
    parser.add_argument('--case', action='append')
    parser.add_argument('--edition', choices=('qt', 'classic'))
    parser.add_argument('--player')
    parser.add_argument('--reader-runner')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.prepare:
        prepare(args.corpus)
    else:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        if args.player:
            rows = ordinary(args)
        elif args.controls:
            rows = controls(args)
        else:
            rows = native(args)
        negatives = rejection_cases(args) if args.runner and not args.player else []
        args.report.write_text(json.dumps(dict(result='PASS', rows=rows, rejected=negatives), indent=2)+'\n')
        print(f'PASS: {len(rows)} MOV scenarios')
