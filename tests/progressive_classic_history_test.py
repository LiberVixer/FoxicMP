"""Check that ordinary Classic file history still resumes and saves positions.

Private player copies only; their shell prompt is disabled before first launch.
This is a normal schema-2 fMP4 source, not a continuous projection alias.
"""
import argparse
import configparser
import importlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace

sys.dont_write_bytecode = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--player', type=Path, required=True)
    parser.add_argument('--reader-runner', type=Path, required=True)
    parser.add_argument('--sdk-directory', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('This regression requires Windows Classic')
    sdk = args.sdk_directory.resolve()
    sys.path.insert(0, str(sdk))
    names = ('progressive_schema', 'progressive_client', 'progressive_integrity',
             'progressive_resources', 'progressive_sdk', 'progressive_v2',
             'progressive_continuous', 'progressive_windows', 'progressive_preview',
             'progressive_writer')
    modules = {name: importlib.import_module(name) for name in names}
    assert all(Path(module.__file__).resolve().parent == sdk for module in modules.values())
    from progressive_continuous_api_test import run
    from progressive_player_crash_recovery import sha, wait
    from progressive_backend_demux_conformance import snapshot
    from windows.progressive_family_test import post_command

    def exercise(peers, root):
        source = root/'ordinary.mp4'
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
                        'testsrc2=size=160x90:rate=25:duration=60', '-c:v', 'libx264',
                        '-threads', '2', '-bf', '0', '-g', '25', '-movflags',
                        '+frag_keyframe+empty_moov+default_base_moof', str(source)],
                       check=True, capture_output=True)
        raw = source.read_bytes()
        digest = sha(source)
        history = Path(peers[0].player).parent/'history.mpc_lst'
        history.write_text('; FoxicMP History File 0.1\n\n[001]\nPath='+str(source)+
                           '\nTitle=Ordinary file\nPosition=00:00:37\n', encoding='utf-8-sig')
        client = peers[0].client
        rows = []
        for attempt in range(2):
            model = snapshot(source, raw, complete=True)
            model['timeline']['duration'] = '600000000'
            model['tracks'][0].update(containerHint='fmp4', codecHint='h264', initializationReady=True)
            active = False
            try:
                client.request(dict(version=1, action='start', session=model, sessionDirectory=str(root), replace=True))
                active = True
                def status():
                    return client.request(dict(version=1, action='heartbeat', sessionId=model['sessionId']))
                def resumed():
                    value = status()
                    assert value['state'] not in ('error', 'closed', 'finished'), value
                    return value if value.get('active') and value['state']=='playing' and value['positionSeconds']>36 else None
                initial = wait(resumed, 'ordinary saved position restored', 10)
                assert 36 < initial['positionSeconds'] < 40, initial
                post_command(peers[0].process.pid, 889)
                paused = wait(lambda: (lambda s: s if s['state']=='paused' else None)(status()), 'ordinary pause')
                client.request(dict(version=1, action='cancel', sessionId=model['sessionId']))
                active = False
                receipts = wait(lambda: (lambda rs: rs if rs and all(r['released'] and r['filesClosed'] for r in rs) else None)(client.readers(model['sessionId'], 1)), 'ordinary files closed', 2)
                for receipt in receipts:
                    assert client.forget_reader(receipt['readerId'])['forgotten']
                text = history.read_text(encoding='utf-8-sig')
                assert 'Path='+str(source)+'\n' in text and 'Position=00:00:37\n' in text, text
                rows.append(dict(attempt=attempt+1, result='PASS', restored=initial, paused=paused,
                                 receipts=receipts, history=text))
            finally:
                if active:
                    client.request(dict(version=1, action='cancel', sessionId=model['sessionId']))
        assert sha(source)==digest
        return dict(result='PASS', count=len(rows), rows=rows, ordinaryHistoryPreserved=True,
                    sourceSha256=digest, continuousHistoryTest='aac-profile-resilience')

    with tempfile.TemporaryDirectory(prefix='foxicmp-ordinary-history-') as name:
        players = []
        for index in range(2):
            dest = Path(name)/str(index)
            shutil.copytree(args.player.resolve().parent, dest)
            player = dest/args.player.name
            ini = player.with_suffix('.ini')
            raw = ini.read_bytes()
            settings = configparser.ConfigParser()
            settings.optionxform = str
            settings.read_string(raw.decode('utf-16') if raw.startswith(b'\xff\xfe') else raw.decode('utf-8-sig'))
            for key, value in dict(ContextMenuPrompted='1', ContextMenuEnabled='0', KeepHistory='1', RememberFilePos='1').items():
                settings.set('Settings', key, value)
            settings.set('Audio', 'AudioRenderer', 'Null Audio Renderer (Uncompressed)')
            with ini.open('w', encoding='utf-16') as file:
                settings.write(file, space_around_delimiters=False)
            players.append(str(player))
        result = run(SimpleNamespace(edition='classic', player=players, reader_runner=str(args.reader_runner),
                                     report_directory=str(args.report_directory)), exercise_case=exercise, source_file=__file__)
    result.update(privatePeerCopiesRemoved=True, sdkModuleSha256={name: sha(module.__file__) for name, module in modules.items()})
    (args.report_directory/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(result=result['result'], count=result['count'])))


if __name__ == '__main__':
    main()
