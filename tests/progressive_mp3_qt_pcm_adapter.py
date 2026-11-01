"""Real Qt Player PCM adapter: exact windows, user filters and replacement."""
import argparse
import hashlib
import json
import platform
import subprocess
import tempfile
from pathlib import Path

from progressive_mp3_gapless_reference import decode, project, window
from progressive_mp3_indexed_fixture import PROFILES, fixture


def run(runner, player_runner):
    checks, reports = [], []
    for profile in [p for p in PROFILES if '-vbri-' not in p]:
        with tempfile.TemporaryDirectory(prefix='foxicmp-qt-pcm-adapter-') as temp:
            root = Path(temp)
            model, raw, original, probe = fixture(root, profile, 4)
            projected = root / 'projected.mp4'
            projected.write_bytes(project(runner, root, model, raw))
            rate = int(probe['streams'][0]['sample_rate'])
            channels = int(probe['streams'][0]['channels'])
            first, end = window(probe['_index']['frames'] * 1152,
                                probe['_index']['delay'], probe['_index']['padding'], 529)
            native_pcm = decode(original)
            captures = []

            def capture(source, label, target=None, mode=None):
                output = root / (label + '.pcm')
                args = [player_runner, str(source), str(output), str(rate), str(channels)]
                if target is not None:
                    absolute = first + target
                    start_frame = max(0, (absolute - rate // 4) // 1152)
                    args += [str(absolute), str(end), format(start_frame * 1152 / rate, '.17g')]
                if mode:
                    args.append(mode)
                process = subprocess.run(args, check=False, capture_output=True, timeout=110)
                assert process.returncode == 0, (profile, label, process.returncode,
                                                 process.stdout.decode(errors='replace'),
                                                 process.stderr.decode(errors='replace'))
                report = json.loads(process.stdout.decode('utf-8'))
                assert report['result'] == 'PASS' and not report['audioDeviceOpened']
                pcm = output.read_bytes()
                captures.append(dict(scenario=label, samples=len(pcm) // (channels * 2),
                                     pcmSha256=hashlib.sha256(pcm).hexdigest(), player=report))
                return pcm

            assert capture(original, 'native') == native_pcm
            checks.append(profile + '-real-player-native-pcm-exact')
            for target in (0, 1, rate * 2 + 1, end - first - 1):
                pcm = capture(projected, 'window-' + str(target), target)
                assert pcm == native_pcm[target * channels * 2:], (profile, target)
                checks.append(profile + '-real-player-window-seek-' + str(target) + '-exact')
            gain_native = capture(original, 'native-gain', mode='volume=0.5')
            gain_trimmed = capture(projected, 'trimmed-gain', 0, 'volume=0.5')
            assert gain_native == gain_trimmed and gain_native != native_pcm
            checks.append(profile + '-user-volume-filter-preserved-before-output-format')
            # The last of three captures is an ordinary untrimmed reload. This
            # detects trim leaking into either the first or second replacement.
            replaced = capture(projected, 'replacement', 0, 'replace')
            assert replaced == decode(projected), profile
            checks.append(profile + '-two-replacements-clear-trim-and-preserve-managed-filters')
            after_stop = capture(projected, 'stop-replace', 0, 'stop-replace')
            assert after_stop == decode(projected), profile
            checks.append(profile + '-stop-and-raw-loadfile-clear-trim')
            coded_gain = capture(projected, 'coded-gain', mode='volume=0.5')
            changed = capture(projected, 'replace-gain', 0, 'replace-gain')
            assert changed == coded_gain and changed != replaced, profile
            checks.append(profile + '-changed-user-graph-survives-two-replacements')
            assert original.read_bytes() == raw
            reports.append(dict(profile=profile, fixtureSha256=hashlib.sha256(raw).hexdigest(),
                                sampleRate=rate, channels=channels, captures=captures))
    return dict(result='PASS', count=len(checks), checks=checks, profiles=reports,
                platform=platform.system(), realPlayerAdapter=True,
                progressiveControllerIntegrated=False, classicAdapterImplemented=False,
                gaplessCapabilityAdvertised=False, originalFilesModified=False,
                scope='Qt Player PCM adapter only; complete confirmed source, no gapless API/controller')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True)
    parser.add_argument('--player-runner', required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = run(args.runner, args.player_runner)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))
