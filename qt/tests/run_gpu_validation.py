#!/usr/bin/env python3
"""Explicit X11 OpenGL/decoder trial; never treat a renderer name as proof of hardware decoding."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def inventory(env):
    results = {}
    for name, args in {
        'pci': ['lspci', '-nnk'],
        'virtualization': ['systemd-detect-virt'],
        'glxinfo': ['glxinfo', '-B'],
        'vaapi': ['vainfo', '--display', 'drm', '--device', '/dev/dri/renderD128'],
    }.items():
        if not shutil.which(args[0]):
            results[name] = {'available': False}
            continue
        try:
            run = subprocess.run(args, env=env, capture_output=True, text=True, timeout=15)
            results[name] = {'exitCode': run.returncode, 'stdout': run.stdout, 'stderr': run.stderr}
        except subprocess.TimeoutExpired:
            results[name] = {'timeout': True}
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--reports', type=Path, required=True)
    parser.add_argument('--real-media', type=Path)
    args = parser.parse_args()
    env = os.environ.copy()
    if env.get('XDG_SESSION_TYPE', '').lower() == 'wayland' or env.get('QT_QPA_PLATFORM', '').startswith('wayland'):
        parser.error('Wayland is excluded from this validation; use an X11 session')
    if not env.get('DISPLAY'):
        parser.error('A working X11 DISPLAY is required')
    args.reports.mkdir(parents=True, exist_ok=True)
    if not (args.corpus / 'manifest.json').is_file():
        parser.error('Generate the corpus with generate_media_matrix.py first')
    env.pop('LIBGL_ALWAYS_SOFTWARE', None)
    env.update(QT_QPA_PLATFORM='xcb', LC_NUMERIC='C',
               FOXICMP_MATRIX_DIR=str(args.corpus.resolve()),
               FOXICMP_GPU_REQUIRE_GL='1', FOXICMP_TEST_OUTPUT='opengl',
               FOXICMP_GPU_REPORT=str((args.reports / 'player.json').resolve()),
               FOXICMP_MATRIX_REPORT=str((args.reports / 'native-matrix.json').resolve()))
    if args.real_media:
        env['FOXICMP_REAL_MEDIA'] = str(args.real_media.resolve())
    else:
        env.pop('FOXICMP_REAL_MEDIA', None)
    report = {'schema': 1, 'display': env['DISPLAY'], 'platform': 'xcb',
              'waylandTested': False, 'inventory': inventory(env), 'runs': []}
    for executable, logname in [('foxicmp-gpu-tests', 'player.log'),
                                ('foxicmp-media-matrix-tests', 'native-matrix.log')]:
        path = (args.build / executable).resolve()
        if not path.is_file():
            report['runs'].append({'executable': executable, 'missing': True})
            continue
        with (args.reports / logname).open('w') as log:
            try:
                result = subprocess.run([str(path)], env=env, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=240)
                code = result.returncode
            except subprocess.TimeoutExpired:
                code = 124
        report['runs'].append({'executable': executable, 'exitCode': code})
        print(executable, code, flush=True)
    report['passed'] = all(r.get('exitCode') == 0 for r in report['runs'])
    # Actual hwdec-current for every codec is stored in player.json. Neither
    # a successful VA-API query nor OpenGL by itself proves hardware decoding.
    (args.reports / 'run.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    return int(not report['passed'])


if __name__ == '__main__':
    raise SystemExit(main())
