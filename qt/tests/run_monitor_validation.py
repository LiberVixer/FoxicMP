#!/usr/bin/env python3
"""X11 monitor/HiDPI trial. Resolution changes require a separate nested display."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--reports', type=Path, required=True)
    parser.add_argument('--nested-display', help='Dedicated Xephyr DISPLAY; its resolution WILL change')
    parser.add_argument('--require-gl', action='store_true', help='Fail rather than accept the documented software fallback')
    args = parser.parse_args()
    env = os.environ.copy()
    if not env.get('DISPLAY') or env.get('XDG_SESSION_TYPE') == 'wayland':
        parser.error('An X11 session is required; Wayland is excluded')
    if args.nested_display == env['DISPLAY']:
        parser.error('The nested display must differ from the working desktop')
    args.reports.mkdir(parents=True, exist_ok=True)
    executable = str((args.build / 'foxicmp-monitor-tests').resolve())
    env.update(QT_QPA_PLATFORM='xcb', FOXICMP_MATRIX_DIR=str(args.corpus.resolve()), LC_ALL='C.UTF-8', LC_NUMERIC='C')
    env.pop('LIBGL_ALWAYS_SOFTWARE', None)
    env.pop('FOXICMP_MONITOR_ISOLATED', None)
    env.pop('FOXICMP_MONITOR_REQUIRE_GL', None)
    if args.require_gl:
        env['FOXICMP_MONITOR_REQUIRE_GL'] = '1'
    inventory = subprocess.run(['xrandr', '--query'], env=env, capture_output=True, text=True, timeout=15)
    report = {'display': env['DISPLAY'], 'waylandTested': False,
              'xrandr': inventory.stdout, 'runs': []}
    if shutil.which('glxinfo'):
        gl = subprocess.run(['glxinfo', '-B'], env=env, capture_output=True, text=True, timeout=15)
        report['glxinfo'] = {'exitCode': gl.returncode, 'stdout': gl.stdout, 'stderr': gl.stderr}
    sink = 'foxicmp_monitor_' + str(os.getpid())
    module = subprocess.check_output(['pactl', 'load-module', 'module-null-sink', 'sink_name=' + sink], text=True).strip()
    env['PULSE_SINK'] = sink
    def run(name, test_env, functions):
        test_env['FOXICMP_MONITOR_REPORT'] = str((args.reports / (name + '.json')).resolve())
        with (args.reports / (name + '.log')).open('w') as out:
            try:
                result = subprocess.run([executable] + functions, env=test_env, stdout=out, stderr=subprocess.STDOUT, timeout=150)
                code = result.returncode
            except subprocess.TimeoutExpired:
                code = 124
        report['runs'].append({'name': name, 'exitCode': code})
        print(name, code, flush=True)
    try:
        for output in ['software', 'opengl']:
            for scale in ['1', '1.5', '2']:
                trial = env.copy()
                trial.update(FOXICMP_TEST_OUTPUT=output, QT_SCALE_FACTOR=scale)
                run(output + '-' + scale, trial, ['monitorsFullscreenAndRestore'])
        if args.nested_display:
            trial = env.copy()
            trial.update(DISPLAY=args.nested_display, FOXICMP_MONITOR_ISOLATED='1',
                         FOXICMP_TEST_OUTPUT='software', QT_SCALE_FACTOR='1')
            run('isolated-resolution', trial, ['resolutionShrink'])
    finally:
        subprocess.run(['pactl', 'unload-module', module], check=True)
    after = subprocess.run(['xrandr', '--query'], env=env, capture_output=True, text=True, timeout=15)
    report['nativeLayoutUnchanged'] = after.returncode == 0 and after.stdout == inventory.stdout
    report['passed'] = report['nativeLayoutUnchanged'] and all(r['exitCode'] == 0 for r in report['runs'])
    (args.reports / 'run.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    return int(not report['passed'])


if __name__ == '__main__':
    raise SystemExit(main())
