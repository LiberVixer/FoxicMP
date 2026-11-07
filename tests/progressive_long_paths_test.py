"""Growing local files on long Unicode paths, through ordinary Qt/Classic IPC.

Private profiles/copies only. No registry policy changes or downloader access.
The same v1 wire requests run on all three editions; no MAX_PATH bypass in IPC.
"""
import argparse
import copy
import importlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import traceback
from types import SimpleNamespace
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--edition', choices=('qt', 'classic'), required=True)
    parser.add_argument('--player', type=Path, required=True)
    parser.add_argument('--reader-runner', type=Path, required=True)
    parser.add_argument('--sdk-directory', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    sdk = args.sdk_directory.resolve()
    sys.path.insert(0, str(sdk))
    names = ('progressive_schema', 'progressive_client', 'progressive_integrity',
             'progressive_resources', 'progressive_sdk', 'progressive_v2',
             'progressive_continuous', 'progressive_windows', 'progressive_preview',
             'progressive_writer')
    modules = {name: importlib.import_module(name) for name in names}
    assert all(Path(m.__file__).resolve().parent == sdk for m in modules.values())
    from progressive_continuous_api_test import run
    from progressive_player_crash_recovery import sha, wait, ApiError
    args.report_directory.mkdir(parents=True, exist_ok=False)
    rows = []

    def save():
        (args.report_directory/'progress.json').write_text(
            json.dumps(dict(count=len(rows), rows=rows), ensure_ascii=False, indent=2)+'\n', encoding='utf-8')

    def exercise(peers, root):
        client = peers[0].client
        fixture = args.fixtures/'progressive-indexed'
        data = [(fixture/name).read_bytes() for name in ('video.mp4', 'audio.m4a')]
        for length in (220, 320, 1100):
            for absolute in (False, True):
                folder = root/('paths-%d-%s' % (length, absolute))
                while len(str(folder)) < length:
                    count = max(1, length-len(str(folder))-1)
                    component = ('Видео с пробелами ' + 'x'*35)[:count]
                    folder /= component[:-1]+'x' if component.endswith(' ') else component
                folder.mkdir(parents=True)
                paths = [folder/'видео.part', folder/'звук.part']
                model = dict(schemaVersion=1, sessionId=uuid.uuid4().hex,
                             generation=1, revision=1, state='downloading',
                             duration100ns=200000000, tracks={})
                for i, kind in enumerate(('video', 'audio')):
                    count = len(data[i])*4//5
                    paths[i].write_bytes(data[i][:count])
                    model['tracks'][kind] = dict(path=str(paths[i]) if absolute else paths[i].name,
                        container='m4a' if i else 'mp4', fragmented=False,
                        initializationReady=True, availablePrefixBytes=count,
                        expectedTotalBytes=len(data[i]), state='downloading')
                active = False
                try:
                    started = client.start(model, session_directory=str(folder),
                        track_reader=True, activate=False)
                    active = True
                    def playing():
                        s = client.command('heartbeat', model['sessionId'])
                        assert s['state'] not in ('error', 'finished'), s
                        return s if s['state']=='playing' and s['positionSeconds']>.2 else None
                    initial = wait(playing, 'long path actual growing playback', 15)
                    assert not initial['complete'], initial
                    for i, kind in enumerate(('video', 'audio')):
                        with paths[i].open('ab') as stream:
                            stream.write(data[i][stream.tell():])
                        model['tracks'][kind].update(availablePrefixBytes=len(data[i]), state='complete')
                    model.update(state='complete', revision=2)
                    completed = client.update(model, complete=True)
                    assert completed['complete'], completed
                    cancelled = client.command('cancel', model['sessionId'])
                    active = False
                    def closed():
                        rs = client.readers(model['sessionId'], 1)
                        return rs if rs and all(r['released'] and r['filesClosed'] for r in rs) else None
                    receipts = wait(closed, 'long path files closed', 2)
                    for r in receipts:
                        assert client.forget_reader(r['readerId'])['forgotten']
                    assert all(p.read_bytes()==d for p, d in zip(paths, data))
                    rows.append(dict(result='PASS', requestedDirectoryLength=length,
                        directoryLength=len(str(folder)), maximumFileLength=max(len(str(p)) for p in paths),
                        absoluteTrackPaths=absolute, started=started, playing=initial,
                        completed=completed, cancelled=cancelled, receipts=receipts,
                        sourceSha256=[sha(p) for p in paths]))
                    save()
                    # File removal happens only after positive filesClosed proof.
                    for p in paths:
                        p.unlink()
                finally:
                    if active:
                        client.command('cancel', model['sessionId'])
        # Existing safety boundaries still apply on a long root. A rejected
        # start must leave the endpoint available for the next valid session.
        folder /= 'negative'
        folder.mkdir()
        paths = [folder/'video.part', folder/'audio.part']
        for p, raw in zip(paths, data):
            p.write_bytes(raw)
        model = copy.deepcopy(model)
        model.update(state='complete', revision=1)
        for i, kind in enumerate(('video', 'audio')):
            model['tracks'][kind].update(path=str(paths[i]), availablePrefixBytes=len(data[i]))
        invalid = [('url', 'https://example.invalid/video.part'), ('nul', str(paths[0])+'\x00tail'),
                   ('over-wire-limit', str(folder/('x'*8193)))]
        if os.name=='nt':
            invalid += [('ads', str(paths[0])+':extra'), ('unc', r'\\127.0.0.1\share\video.part')]
        links = root/'links'
        links.mkdir()
        link = links/'video.part'
        linked_dir = links/'directory'
        # These objects belong to this test. A failure to create links is a
        # setup failure, never a silent pass of the reparse/symlink checks.
        link.symlink_to(paths[0])
        linked_dir.symlink_to(folder, target_is_directory=True)
        invalid += [('file-link', str(link)), ('directory-link', str(linked_dir/paths[0].name))]
        for label, path in invalid:
            bad = copy.deepcopy(model)
            bad['sessionId'] = uuid.uuid4().hex
            bad['tracks']['video']['path'] = path
            try:
                client.start(bad, session_directory=str(folder), activate=False)
            except ApiError as e:
                assert e.code=='source_not_ready', e.reply
                rows.append(dict(result='PASS', rejected=label, code=e.code))
                save()
            else:
                raise AssertionError(('Unsafe path accepted', label))
        link.unlink()
        linked_dir.unlink()
        model['sessionId'] = uuid.uuid4().hex
        client.start(model, session_directory=str(folder), activate=False, track_reader=True)
        try:
            restored = wait(playing, 'valid session after unsafe paths', 15)
        finally:
            client.command('cancel', model['sessionId'])
        receipts = wait(closed, 'post-rejection files closed', 2)
        for r in receipts:
            assert client.forget_reader(r['readerId'])['forgotten']
        rows.append(dict(result='PASS', validAfterRejections=restored, receipts=receipts))
        save()
        return dict(result='PASS', count=len(rows), rows=rows, scope='schema1-local-indexed-growing',
                    longPathsPolicyNotChanged=True, audioAudibilityChecked=False)

    with tempfile.TemporaryDirectory(prefix='foxicmp-long-path-player-') as name:
        players = [str(args.player.resolve())]
        if args.edition=='classic':
            assert os.name=='nt'
            players = []
            for i in range(2):
                dest = Path(name)/str(i)
                shutil.copytree(args.player.resolve().parent, dest)
                player = dest/args.player.name
                player.with_suffix('.ini').write_text(
                    '[Settings]\nContextMenuPrompted=1\nContextMenuEnabled=0\n'
                    'PauseMinimizedVideo=0\nCheckForUpdates=0\nKeepHistory=0\n'
                    '[Audio]\nAudioRenderer=Null Audio Renderer (Uncompressed)\n', encoding='utf-16')
                players.append(str(player))
        try:
            result = run(SimpleNamespace(edition=args.edition, player=players,
                reader_runner=str(args.reader_runner), report_directory=str(args.report_directory)),
                exercise_case=exercise, source_file=__file__)
        except BaseException:
            (args.report_directory/'error.txt').write_text(traceback.format_exc(), encoding='utf-8')
            raise
    result.update(privateCopiesRemoved=True, sdkModuleSha256={n:sha(m.__file__) for n,m in modules.items()})
    (args.report_directory/'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(result=result['result'], count=result['count'])))


if __name__=='__main__':
    main()
