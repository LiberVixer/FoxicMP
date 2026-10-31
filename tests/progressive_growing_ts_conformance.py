"""Continuous TS: incremental parsing, verified ranges, B-frames and virtual MP4.

No player sessions, network, desktop or downloader are used by this native suite.
"""
import argparse
import copy
import hashlib
import json
import subprocess
import tempfile
from fractions import Fraction
from pathlib import Path

from progressive_growing_ts_fixture import fixture, publish
from progressive_mux_ts_conformance import Rpc
from progressive_integrity import protect_snapshot


def run(stream_runner, media_runner):
    checks, reports = [], []
    for profile, unknown in [('main-bframes', False), ('high-fractional', True), ('main-preroll', False)]:
        with tempfile.TemporaryDirectory(prefix='foxicmp-growing-ts-') as tmp:
            root = Path(tmp)
            data = root / 'data'
            data.mkdir()
            model, raw, original, groups = fixture(root, profile, seconds=4, unknown_length=unknown)
            rpc = Rpc([stream_runner, '--registry', str(root / 'registry')])
            value = publish(model, raw, data, 0)
            try:
                answer = rpc('open', directory=str(data), session=value)
                assert answer['ok'], answer
                # Arbitrary byte cuts, including PSI/PES headers and B-frame GOPs.
                stops = sorted(set([1, 17, 187, 188, 189, 375, 376, 563, 564, 565, 1031]
                                   + list(range(4091, len(raw), 4091)) + [len(raw)]))
                previous_units = [0, 0]
                for stop in stops:
                    value = publish(model, raw, data, stop, previous=value)
                    answer = rpc('publish', session=value)
                    assert answer['ok'], (profile, stop, answer)
                    status = rpc('status')
                    assert not status['complete'] and not status['errorCode'], status
                    for kind in (0, 1):
                        n = status['tracks'][kind]['units']
                        assert n >= previous_units[kind]
                        previous_units[kind] = n
                        assert rpc('sample', track=kind, index=str(n))['code'] == 'waiting-segment'
                    assert rpc('read', track=0, offset=str(stop), length='1')['code'] == 'waiting-range'
                assert all(previous_units), status
                # Even the entire physical file is not EOF until explicitly complete.
                value = publish(model, raw, data, len(raw), previous=value, complete=True)
                answer = rpc('publish', session=value)
                assert answer['ok'], answer
                status = rpc('status')
                assert status['complete'] and status['resources'] == 1, status
                for kind in (0, 1):
                    assert status['tracks'][kind]['units'] == len(groups[kind]), status
                    for i, packet in enumerate(groups[kind]):
                        answer = rpc('sample', track=kind, index=str(i), encoded=True)
                        assert answer['code'] == 'data', answer
                        assert answer['sha256'] == packet['data_hash'].split(':')[1].lower(), (profile, kind, i, answer, packet)
                    assert rpc('sample', track=kind, index=str(len(groups[kind])))['code'] == 'eof'
                    assert rpc('read', track=kind, offset=str(len(raw)), length='0')['code'] == 'data'
                    assert rpc('read', track=kind, offset=str(len(raw)), length='1')['code'] == 'eof'
                    assert rpc('read', track=kind, offset=str(len(raw)+1), length='1')['code'] == 'out-of-range'
                readers = rpc('readers', version=1, sessionId=model['sessionId'], generation='1')['readers']
                assert len(readers) == 1, readers
                rpc('retain')
                rpc('release')
                assert not rpc('released')['released']
                rpc('dropRetained')
                assert rpc('released')['released']
                assert not rpc('readers', version=1, sessionId=model['sessionId'], generation='1')['readers']
                checks.append('arbitrary-byte-cuts-sha-one-file-pes-gop-exact-hashes-and-last-owner-' + profile)
            finally:
                rpc.close()

            # Open a playable prefix; append to the same inode and compare fMP4
            # demux timestamps and decoded video/PCM with the source TS.
            rawdir = root / 'remux'
            rawdir.mkdir()
            first_stop = int(next(p['pos'] for p in groups[0] if int(p['pts'])-int(groups[0][0]['pts']) >= 180000)) - 97
            value = publish(model, raw, rawdir, first_stop)
            rpc = Rpc([media_runner, '--registry', str(root / 'media-registry')])
            def dump(kind, path):
                layout = rpc('layout')['tracks'][kind]
                size = int(layout['available'])
                parts = []
                for at in range(0, size, 65536):
                    part = rpc('read', track=kind, offset=str(at), length=str(min(65536, size-at)))
                    assert part['ok'] and part['bytesRead'] > 0, part
                    parts.append(bytes.fromhex(part['hex']))
                contents = b''.join(parts)
                assert len(contents) == size
                path.write_bytes(contents)
                return contents
            try:
                assert rpc('preflight', session=value)['ok']
                answer = rpc('open', directory=str(rawdir), session=value)
                assert answer['ok'], answer
                prefix = [dump(i, root / f'prefix-{i}.mp4') for i in (0, 1)]
                value = publish(model, raw, rawdir, len(raw), previous=value, complete=True)
                answer = rpc('publish', session=value)
                assert answer['ok'], answer
                index = rpc('index')
                max_deltas = []
                for kind in (0, 1):
                    path = root / f'track-{kind}.mp4'
                    contents = dump(kind, path)
                    assert contents.startswith(prefix[kind])
                    assert index['tracks'][kind]['samples'] == len(groups[kind]), index
                    probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_packets', '-show_streams', '-of', 'json', str(path)]))
                    found = probe['packets']
                    assert len(found) == len(groups[kind])
                    clock = Fraction(probe['streams'][0]['time_base'])
                    delta = 0
                    for a, b in zip(found, groups[kind]):
                        for field in ('pts', 'dts'):
                            d = abs(int(int(a[field])*clock*10000000) - (int(b[field])*10000000//90000-int(value['timeline']['timestampOffset'])))
                            delta = max(delta, d)
                            assert d <= 224, (profile, kind, field, a, b, d)
                    max_deltas.append(delta)
                    def decoded(source):
                        args = ['-an', '-pix_fmt', 'yuv420p', '-c:v', 'rawvideo', '-threads', '1'] if kind == 0 else ['-vn', '-c:a', 'pcm_s16le']
                        if profile == 'main-preroll' and kind == 1 and source == original:
                            args += ['-af', 'atrim=start_sample=480']
                        return subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(source), *args, '-f', 'hash', '-hash', 'sha256', '-'], stderr=subprocess.PIPE).strip()
                    assert decoded(original) == decoded(path), (profile, kind, 'decoded data differs')
                    for at in range(len(contents)-1, -1, -4093):
                        start = max(0, at-1023)
                        assert rpc('read', track=kind, offset=str(start), length=str(at-start+1))['hex'] == contents[start:at+1].hex()
                assert rpc('status')['mediaMode'] == 'growing-muxed-mpegts-h264-aac-lc'
                assert sorted(p.name for p in rawdir.iterdir()) == ['mux.part']
                reports.append(dict(profile=profile, unknownLength=unknown, accessUnits=[len(g) for g in groups],
                                    maxTimestampDifference100ns=max_deltas, decodedVideoAndPcmMatch=True))
                checks.append('immutable-virtual-mp4-backward-reads-timestamps-decoded-video-pcm-' + profile)
            finally:
                rpc.close()

    with tempfile.TemporaryDirectory(prefix='foxicmp-growing-faults-') as tmp:
        root = Path(tmp)
        model, raw, _, groups = fixture(root, seconds=4)
        first_stop = len(raw)//2
        def fault(name, change, *, metadata=False):
            data = root / name
            data.mkdir()
            rpc = Rpc([stream_runner, '--registry', str(root / (name+'-registry'))])
            value = publish(model, raw, data, first_stop)
            try:
                answer = rpc('open', directory=str(data), session=value)
                assert answer['ok'], answer
                before = rpc('status')
                bad = copy.deepcopy(value)
                bad['revision'] = '2'
                change(bad, data)
                answer = rpc('publish', session=bad)
                assert not answer['ok'], (name, answer)
                after = rpc('status')
                assert after['revision'] == before['revision'], (name, after)
                if metadata:
                    assert before == after
                    assert rpc('read', track=0, offset='0', length='8')['hex'] == raw[:8].hex()
                else:
                    assert after['errorCode'], (name, after)
                    answer = rpc('read', track=0, offset='0', length='8')
                    assert answer['bytesRead'] == 0 and answer['untouchedOnError'], answer
                rpc('release')
                assert not rpc('readers', version=1, sessionId=model['sessionId'], generation='1')['readers']
                checks.append(name)
            finally:
                rpc.close()
        def payload_offset(at):
            return at+4+(1+raw[at+4] if raw[at+3]&32 else 0)
        starts=[at for at in range((first_stop//188+2)*188,len(raw),188) if raw[at+1]&64]
        audio=next(at for at in starts if ((raw[at+1]&31)<<8 | raw[at+2])==257)
        sdt=next(at for at in starts if ((raw[at+1]&31)<<8 | raw[at+2])==17)
        for name, at, xor in [('new-audio-pes-failure-closes-both-views',payload_offset(audio),0x01),
                              ('counter-gap-fails-closed',audio+3,0x02),
                              ('sdt-crc-fails-closed',payload_offset(sdt)+10,0x01)]:
            data=root/name
            data.mkdir()
            rpc=Rpc([stream_runner,'--registry',str(root/(name+'-registry'))])
            try:
                value=publish(model,raw,data,first_stop)
                assert rpc('open',directory=str(data),session=value)['ok']
                before=rpc('status')
                changed=bytearray(raw)
                changed[at]^=xor
                bad=publish(model,changed,data,len(raw),previous=value,complete=True)
                answer=rpc('publish',session=bad)
                assert not answer['ok'],(name,answer)
                after=rpc('status')
                assert after['revision']==before['revision'] and after['tracks']==before['tracks'] and after['errorCode'],after
                for kind in (0,1):
                    answer=rpc('read',track=kind,offset='0',length='8')
                    assert answer['bytesRead']==0 and answer['untouchedOnError'],answer
                rpc('release')
                assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
                checks.append(name)
            finally:
                rpc.close()
        data=root/'cancel'
        data.mkdir()
        rpc=Rpc([stream_runner,'--registry',str(root/'cancel-registry')])
        try:
            value=publish(model,raw,data,first_stop)
            assert rpc('open',directory=str(data),session=value)['ok']
            assert rpc('cancel')['ok']
            assert rpc('sample',track=0,index='0')['code']=='cancelled'
            assert rpc('read',track=1,offset='0',length='8')['code']=='cancelled'
            rpc('release')
            assert not rpc('readers',version=1,sessionId=model['sessionId'],generation='1')['readers']
            checks.append('cancel-stops-both-logical-views-and-releases-single-file')
        finally:
            rpc.close()
        for name, change in [
            ('reject-mixed-mode-preserves-session', lambda v, d: v['tracks'][1].update(containerHint='mpegts-muxed')),
            ('reject-live-preserves-session', lambda v, d: v['timeline'].update(live=True)),
            ('reject-readiness-mismatch-preserves-session', lambda v, d: v['tracks'][1].update(initializationReady=False)),
            ('reject-codec-change-preserves-session', lambda v, d: v['tracks'][0].update(codecHint='hevc')),
        ]:
            fault(name, change, metadata=True)
        def rewritten(v, d):
            with (d/'mux.part').open('r+b') as f:
                f.seek(1000)
                f.write(b'changed')
        def truncated(v, d):
            with (d/'mux.part').open('r+b') as f:
                f.truncate(999)
        for name, change in [('confirmed-rewrite-fails-both-views', rewritten), ('truncate-fails-both-views', truncated)]:
            # Metadata publication itself does not read old bytes: force verify
            # or an actual selected-payload read after changing the file.
            data = root/name
            data.mkdir()
            rpc = Rpc([stream_runner, '--registry', str(root/(name+'-registry'))])
            try:
                value = publish(model, raw, data, first_stop)
                assert rpc('open', directory=str(data), session=value)['ok']
                change(value, data)
                answer = rpc('read', track=0, offset='0', length='2000')
                assert answer['bytesRead'] == 0 and answer['untouchedOnError'], answer
                assert rpc('status')['errorCode']
                assert rpc('sample', track=1, index='0')['code'] != 'data'
                checks.append(name)
            finally:
                rpc.close()
        # A truncated tail waits during download but is an error on completion.
        data = root/'partial-complete'
        data.mkdir()
        model['resources'][0]['length'] = None
        rpc = Rpc([stream_runner, '--registry', str(root/'partial-registry')])
        try:
            value = publish(model, raw, data, len(raw)-1)
            assert rpc('open', directory=str(data), session=value)['ok']
            bad = copy.deepcopy(value)
            bad['revision'] = '2'
            bad['resources'][0].update(length=str(len(raw)-1), state='complete')
            bad['writer']['state'] = 'complete'
            for t in bad['tracks']:
                t['state'] = 'complete'
            answer = rpc('publish', session=bad)
            assert not answer['ok'] and answer['code'] == 'ts-truncated', answer
            checks.append('partial-packet-waits-until-explicit-completion-then-fails-closed')
        finally:
            rpc.close()
    return dict(result='PASS', scope='growing-ts-native', count=len(checks), checks=checks, realMedia=reports,
                playerDecoderTested=False, physicalAvSyncMeasured=False, networkRequests=0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--stream-runner', required=True)
    parser.add_argument('--media-runner', required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.stream_runner, args.media_runner)))
