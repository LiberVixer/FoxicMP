"""Ordinary Classic EXE: schema-1 indexed/fragmented files through public IPC."""
import argparse
import configparser
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'qt' / 'tools'))
from progressive_player_crash_recovery import Peer, sha
from progressive_v1_conformance import exercise_directory_contract
from progressive_family_test import registration_snapshot, post_command


def run(args):
    args.report_directory.mkdir(parents=True, exist_ok=True)
    registered = registration_snapshot()
    original_ini = args.player.with_suffix('.ini').read_bytes()
    with tempfile.TemporaryDirectory(prefix='foxicmp-raw-api-') as folder:
        root = Path(folder)
        stage = root / 'player'
        shutil.copytree(args.player.parent, stage)
        player = stage / args.player.name
        settings = configparser.ConfigParser()
        settings.optionxform = str
        raw = player.with_suffix('.ini').read_bytes()
        settings.read_string(raw.decode('utf-16') if raw.startswith(b'\xff\xfe') else raw.decode('utf-8-sig'))
        for section, values in {'Settings': {'ContextMenuPrompted': '1', 'ContextMenuEnabled': '0'},
                                'Audio': {'AudioRenderer': 'Null Audio Renderer (Uncompressed)'},
                                'Video': {'VideoRenderer': '11'}}.items():
            if not settings.has_section(section):
                settings.add_section(section)
            for key, value in values.items():
                settings.set(section, key, value)
        with player.with_suffix('.ini').open('w', encoding='utf-16') as file:
            settings.write(file, space_around_delimiters=False)
        fixtures = root / 'fixtures'
        for fragmented in (False, True):
            directory = fixtures / ('progressive' if fragmented else 'progressive-indexed')
            directory.mkdir(parents=True)
            for audio in (False, True):
                command = ['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i']
                command += ['sine=frequency=880:sample_rate=48000', '-t', '20', '-vn', '-c:a', 'aac'] if audio else ['testsrc2=size=320x180:rate=25', '-t', '20', '-an', '-c:v', 'libx264', '-threads', '2', '-g', '25', '-keyint_min', '25', '-sc_threshold', '0']
                command += ['-movflags', '+empty_moov+default_base_moof+frag_keyframe', '-frag_duration', '1000000'] if fragmented else ['-movflags', '+faststart']
                path = directory / ('audio.m4a' if audio else 'video.mp4')
                value = subprocess.run(command + [str(path)], capture_output=True, timeout=40)
                assert value.returncode == 0, (value.returncode, value.stderr.decode(errors='replace'))
        before = {str(p.relative_to(fixtures)): sha(p) for p in fixtures.rglob('*') if p.is_file()}
        peer = Peer('classic', player, root / 'unused-profile', root, args.reader_runner)
        try:
            peer.launch()
            rows = exercise_directory_contract(peer.client, fixtures, root / 'work')
            assert len(rows) == 2 and {row['fragmented'] for row in rows} == {False, True}
        finally:
            if peer.process is not None and peer.process.poll() is None:
                post_command(peer.process.pid, 0)
                try:
                    peer.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    peer.process.kill()
                    peer.process.wait()
                    raise
            assert peer.process is not None and peer.process.returncode == 0
            peer.history[-1].update(reason='clean-exit', exitCode=peer.process.returncode)
            removed = peer.cleanup()
        assert {str(p.relative_to(fixtures)): sha(p) for p in fixtures.rglob('*') if p.is_file()} == before
        assert args.player.with_suffix('.ini').read_bytes() == original_ini
        assert registration_snapshot() == registered
        result = dict(result='PASS', count=len(rows), rows=rows, actualOrdinaryExe=True,
            publicSchema=1, actualRawFileGraph=True, audioOutput='null',
            videoOutput='Null Video Renderer (Uncompressed)', processHistory=peer.history,
            playerSha256=sha(player), sourceSha256=sha(__file__),
            contractSha256=sha(Path(__file__).resolve().parents[1] / 'progressive_v1_conformance.py'),
            originalStageProfileUnchanged=True, hkcuUnchanged=True, fixtureSha256=before,
            runtimePathsRemoved=removed)
    result['temporaryWorkRemoved'] = not root.exists()
    (args.report_directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(result='PASS', count=len(rows))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--player', type=Path, required=True)
    parser.add_argument('--reader-runner', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    args = parser.parse_args()
    args.player = args.player.resolve()
    run(args)
