"""Ordinary schema-2 and continuous playback from long local source roots.

Same published snapshots on the three editions. No downloader, OS policy or
desktop changes; short default cache/registry paths are reported separately.
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
    parser.add_argument('--route', choices=('schema2', 'continuous'), required=True)
    parser.add_argument('--player', type=Path, required=True)
    parser.add_argument('--reader-runner', type=Path, required=True)
    parser.add_argument('--sdk-directory', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    args.report_directory.mkdir(parents=True, exist_ok=False)
    sdk = args.sdk_directory.resolve()
    sys.path.insert(0, str(sdk))
    names = ('progressive_schema', 'progressive_client', 'progressive_integrity',
             'progressive_resources', 'progressive_sdk', 'progressive_v2',
             'progressive_continuous', 'progressive_windows', 'progressive_preview',
             'progressive_writer')
    modules = {name: importlib.import_module(name) for name in names}
    assert all(Path(m.__file__).resolve().parent == sdk for m in modules.values())
    from progressive_continuous_api_test import run
    from progressive_player_crash_recovery import sha, wait
    from progressive_continuous_test_media import source_fixture, source_snapshot, source_cuts
    from progressive_continuous_heartbeat_conformance import WorkerExit
    from progressive_v1_conformance import fragment_prefix
    from progressive_v2 import V2PreviewSession
    from progressive_continuous import ContinuousRemux
    from progressive_integrity import protect_snapshot
    rows = []

    def exercise(peers, root):
        client = peers[0].client
        if args.route=='continuous':
            reference = source_fixture(root, 'reference')
            raw = reference.read_bytes()
            cut = source_cuts(reference)[12]
        else:
            data = [(args.fixtures/'progressive'/name).read_bytes()
                    for name in ('video.mp4', 'audio.m4a')]
        for length in (220, 320, 1100):
            folder = root/('source-'+str(length))
            while len(str(folder)) < length:
                component = ('Видео с пробелами '+'x'*35)[:max(1, length-len(str(folder))-1)]
                folder /= component[:-1]+'x' if component.endswith(' ') else component
            folder.mkdir(parents=True)
            if args.route=='schema2':
                paths = [folder/'видео.part', folder/'звук.part']
                model = dict(schemaVersion=2, sessionId=uuid.uuid4().hex, generation='1', revision='1',
                    writer=dict(instanceId=uuid.uuid4().hex, state='downloading'),
                    timeline=dict(timebase=dict(numerator=1, denominator=10000000), live=False,
                                  duration='200000000', timestampOffset='0'), resources=[], tracks=[])
                for i, kind in enumerate(('video', 'audio')):
                    size = fragment_prefix(data[i], 12)
                    paths[i].write_bytes(data[i][:size])
                    model['resources'].append(dict(id=kind, name=paths[i].name, length=None,
                        ranges=[dict(offset='0', length=str(size))], state='downloading'))
                    model['tracks'].append(dict(id=kind, kind=kind, required=True,
                        codecHint='aac-lc' if i else 'h264', containerHint='fmp4',
                        initializationReady=True, state='downloading', source=dict(resourceId=kind)))
                model = protect_snapshot(model, folder)
                preview = V2PreviewSession(client, str(folder))
                active = False
                try:
                    preview.publish(model)
                    active = True
                    def playing():
                        s = preview.heartbeat()
                        assert s['state'] not in ('error', 'closed', 'finished'), s
                        return s if s['state']=='playing' and s['positionSeconds']>.2 else None
                    initial = wait(playing, 'schema2 long path actual playback', 15)
                    assert not initial['complete'] and all(int(initial['decoderIoBytes'][kind])>0 for kind in ('video','audio'))
                    previous = model
                    model = copy.deepcopy(model)
                    for i in range(2):
                        with paths[i].open('ab') as stream:
                            stream.write(data[i][stream.tell():])
                        model['resources'][i].update(length=str(len(data[i])), state='complete',
                            ranges=[dict(offset='0', length=str(len(data[i])))])
                        model['tracks'][i]['state']='complete'
                    model['revision']='2';model['writer']['state']='complete'
                    model = protect_snapshot(model, folder, previous=previous)
                    completed = preview.publish(model)
                    assert completed['complete'], completed
                    # SDK close may forget its group receipt. Snapshot the
                    # other source leases only after positive close proof.
                    proof = preview.close(timeout=2)
                    active = False
                    assert proof['released'] and proof['reader']['filesClosed'], proof
                    remaining = client.readers(model['sessionId'], 1)
                    receipts = [proof['reader']] + remaining
                    assert all(r['released'] and r['filesClosed'] for r in receipts), receipts
                    for r in remaining:
                        assert client.forget_reader(r['readerId'])['forgotten']
                    assert all(p.read_bytes()==d for p,d in zip(paths,data))
                    row = dict(playing=initial, completed=completed, release=proof, receipts=receipts,
                               sourceSha256=[sha(p) for p in paths])
                finally:
                    if active:
                        preview.close(timeout=2)
            else:
                paths = [folder/'видео.part']
                model = source_snapshot(paths[0], raw, complete=False, prefix=cut)
                remux = ContinuousRemux(client, str(folder))
                watch = None
                try:
                    remux.prepare(model, 'clip')
                    def ready():
                        s = remux.status()
                        assert not s['errorCode'], s
                        if s['playbackReady']: return s
                        remux.heartbeat()
                    ready_state = wait(ready, 'continuous long path ready', 20)
                    watch = WorkerExit(ready_state['workerId'])
                    remux.play(title='Long local source')
                    def playing():
                        s = remux.playback_status()
                        assert s['state'] not in ('error','closed','finished'), s
                        return s if s['state']=='playing' and s['positionSeconds']>.2 and s['decoderReaders'] else None
                    initial = wait(playing, 'continuous long path actual playback', 15)
                    with paths[0].open('ab') as stream:
                        stream.write(raw[stream.tell():])
                    previous = model;model = copy.deepcopy(model)
                    model['revision']='2';model['writer']['state']='complete'
                    model['resources'][0].update(state='complete', ranges=[dict(offset='0',length=str(len(raw)))])
                    model['tracks'][0]['state']='complete'
                    model = protect_snapshot(model, folder, previous=previous)
                    remux.publish(model)
                    def complete():
                        s = remux.status()
                        assert not s['errorCode'], s
                        return s if s['outputComplete'] else None
                    completed = wait(complete, 'continuous long path complete', 20)
                    assert completed['workerId']==ready_state['workerId']
                    remux.control('stop')
                    proof = remux.close(timeout=2)
                    assert proof['released'] and proof['sourceFilesClosed'], proof
                    assert watch.exited(), 'Continuous worker still owns files'
                    receipts = client.readers(model['sessionId'], 1)
                    assert receipts and all(r['released'] and r['filesClosed'] for r in receipts), receipts
                    forgotten = remux.forget(timeout=2)
                    assert forgotten['forgotten'] and not client.readers(model['sessionId'], 1)
                    assert not list(Path(str(peers[0].registry)+'.continuous-cache').glob('remux-*'))
                    assert paths[0].read_bytes()==raw
                    row = dict(ready=ready_state, playing=initial, completed=completed, release=proof,
                        receipts=receipts, forgotten=forgotten, workerExited=True, sourceSha256=[sha(paths[0])])
                finally:
                    if not remux.forgotten:
                        proof = remux.close(timeout=2)
                        if proof.get('released') and proof.get('sourceFilesClosed'):
                            remux.forget(timeout=2)
                    if watch:watch.close()
            for p in paths:p.unlink()
            rows.append(dict(result='PASS', route=args.route, directoryLength=len(str(folder)),
                maximumFileLength=max(len(str(p)) for p in paths), **row))
            (args.report_directory/'progress.json').write_text(json.dumps(dict(count=len(rows),rows=rows),indent=2)+'\n')
        return dict(result='PASS',count=len(rows),rows=rows,longSourceRoots=True,
                    longCacheRootTested=False,privateRegistryPaths=[str(p.registry) for p in peers])

    with tempfile.TemporaryDirectory(prefix='foxicmp-v2-long-path-player-') as name:
        players = [str(args.player.resolve())]
        if args.edition=='classic':
            assert os.name=='nt';players=[]
            for i in range(2):
                dest=Path(name)/str(i);shutil.copytree(args.player.resolve().parent,dest)
                player=dest/args.player.name
                player.with_suffix('.ini').write_text('[Settings]\nContextMenuPrompted=1\nContextMenuEnabled=0\n'
                    'PauseMinimizedVideo=0\nCheckForUpdates=0\nKeepHistory=0\n'
                    '[Audio]\nAudioRenderer=Null Audio Renderer (Uncompressed)\n',encoding='utf-16')
                players.append(str(player))
        try:
            result=run(SimpleNamespace(edition=args.edition,player=players,reader_runner=str(args.reader_runner),
                report_directory=str(args.report_directory)),exercise_case=exercise,source_file=__file__)
        except BaseException:
            (args.report_directory/'error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
    result.update(privateCopiesRemoved=True,sdkModuleSha256={n:sha(m.__file__) for n,m in modules.items()})
    (args.report_directory/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(result='PASS',count=result['count'],route=args.route)))


if __name__=='__main__':main()
