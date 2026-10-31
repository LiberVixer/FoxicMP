"""Player-owned continuous TS writer and actual playback contract."""
import argparse
import json
import time
from pathlib import Path
from progressive_growing_ts_fixture import fixture, publish
from progressive_client import ProgressiveClient
from progressive_v2 import V2PreviewSession
from progressive_v2_playback_conformance import forget_closed_readers


def exercise(client, fixtures, work, pause, seek, profile='main-bframes'):
    root = Path(work).resolve()
    root.mkdir(parents=True, exist_ok=True)
    plan, raw, _, groups = fixture(root, profile, unknown_length=profile=='high-fractional')
    plan['metadata']['chapters'].append(dict(id='tail', time='170000000', title='Tail'))
    caps = client.request(dict(version=1, action='capabilities'))['schema2Playback']['segmentedPlayback']['mpegts']['growing']
    mode = 'growing-muxed-mpegts-h264-aac-lc'
    assert caps['mode']==mode and caps['integrityRequired'] and caps['decoderContainer']=='fmp4', caps
    def cut(seconds):
        return int(next(p['pos'] for p in groups[0] if int(p['pts'])-int(groups[0][0]['pts'])>=seconds*90000))-97
    value = publish(plan, raw, root, 17)
    preview = V2PreviewSession(client, str(root), title='Growing TS '+profile)
    report = dict(profile=profile, phases=[], networkRequests=0, physicalResources=1)
    try:
        answer = preview.publish(value)
        assert not preview.started and answer['state']=='waiting-initialization', answer
        value = publish(plan, raw, root, cut(4), previous=value)
        probe = client.probe(value, root)
        assert probe['supported'] and not probe['complete'], probe
        forget_closed_readers(client, value)
        preview.publish(value)
        reader = preview.reader_id
        assert reader
        def wait(predicate, label, timeout=30):
            deadline = time.monotonic()+timeout
            while time.monotonic()<deadline:
                state = preview.heartbeat()
                assert state.get('ok') and not state.get('error') and state['state']!='error', state
                assert state['readerId']==reader and state['mediaMode']==mode, state
                if predicate(state):
                    report['phases'].append(dict(phase=label, status=state))
                    return state
                time.sleep(.1)
            raise AssertionError(('Growing TS playback timeout', label, state))
        def advance(seconds=None):
            nonlocal value
            value = publish(plan, raw, root, len(raw) if seconds is None else cut(seconds), previous=value, complete=seconds is None)
            return preview.publish(value)
        wait(lambda s:s['state']=='buffering' and s['availableSeconds']<6, 'partial-prefix-buffers')
        advance(12)
        wait(lambda s:s['state']=='playing' and s['positionSeconds']>=3, 'decoded-before-complete')
        pause()
        state = wait(lambda s:s['state']=='paused', 'pause')
        position = state['positionSeconds']
        advance(14)
        time.sleep(.4)
        assert abs(preview.heartbeat()['positionSeconds']-position)<.3
        seek(2)
        wait(lambda s:s['state']=='paused' and 1.8<=s['positionSeconds']<3, 'backward-seek-while-paused')
        pause()
        wait(lambda s:s['state']=='playing' and s['positionSeconds']>=4, 'resume-after-seek')
        # Classic keeps about one second of decoder reserve. A closed-GOP
        # prefix can end just below 13 seconds; assert distance from the actual
        # safe tail instead of requiring an absolute 12-second position.
        wait(lambda s:s['state']=='buffering' and s['positionSeconds']>=10
             and 0<=s['availableSeconds']-s['positionSeconds']<=1.2
             and not s['complete'], 'partial-pes-gop-tail-waits-not-eof')
        assert (root/'mux.part').stat().st_size<len(raw)
        advance()
        seek(17)
        wait(lambda s:s['state'] in ('playing','finished') and s['positionSeconds']>=16.7, 'forward-seek-to-published-tail')
        wait(lambda s:s['state']=='finished' and s.get('released'), 'complete-and-natural-eof')
        assert preview.close(timeout=10)['released']
        forget_closed_readers(client, value)
        report.update(result='PASS', readerIdentityPreserved=True, playingBeforeComplete=True, pausedSeek=True,
                      tailWaitsForPublication=True, forwardSeek=True, released=True, partialPacketStartWaits=True)
        return report
    finally:
        if not preview.closed:
            preview.close(timeout=10)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--player',required=True)
    parser.add_argument('--profile')
    parser.add_argument('--fixtures')
    parser.add_argument('--work',required=True)
    parser.add_argument('--media-profile',default='main-bframes')
    args=parser.parse_args()
    sequence=[0]
    def command(action,value=None):
        sequence[0]+=1
        path=Path(args.work)/'control.json'
        temp=path.with_suffix('.tmp')
        temp.write_text(json.dumps(dict(sequence=sequence[0],action=action,value=value)),encoding='utf-8')
        deadline=time.monotonic()+1
        while True:
            try:
                temp.replace(path)
                break
            except PermissionError:
                if time.monotonic()>=deadline:
                    raise
                time.sleep(.01)
    print(json.dumps(exercise(ProgressiveClient(args.player,args.profile),args.fixtures,args.work,
                             lambda:command('pause'),lambda t:command('seek',t),args.media_profile)))
