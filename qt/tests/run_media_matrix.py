#!/usr/bin/env python3
"""Run the explicit Player compatibility suite on an existing display/audio session."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--reports', type=Path, required=True)
    args = parser.parse_args()
    args.reports.mkdir(parents=True, exist_ok=True)
    outcome = []
    for mode in ('software', 'opengl'):
        env = os.environ.copy()
        env.update(FOXICMP_MATRIX_DIR=str(args.corpus.resolve()),
                   FOXICMP_MATRIX_REPORT=str((args.reports / (mode + '.json')).resolve()),
                   FOXICMP_TEST_OUTPUT=mode, LC_NUMERIC='C')
        with (args.reports / (mode + '.log')).open('w') as log:
            try:
                result = subprocess.run([str(args.executable.resolve())], env=env,
                                        stdout=log, stderr=subprocess.STDOUT, timeout=300)
                code = result.returncode
            except subprocess.TimeoutExpired:
                code = 124
        outcome.append(dict(mode=mode, exitCode=code))
        print(mode, code, flush=True)
    (args.reports / 'run.json').write_text(json.dumps(outcome, indent=2) + '\n')
    return int(any(r['exitCode'] != 0 for r in outcome))


if __name__ == '__main__':
    raise SystemExit(main())
