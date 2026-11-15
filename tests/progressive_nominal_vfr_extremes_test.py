"""Ordinary-main boundary proof: nominal1ms/1s must not set the VFR grid.

Uses the existing isolated package/peer acceptance driver. Only this process's
fixture factory is replaced; no player, SDK or user profile is monkeypatched.
All usual playback/seek/PCM/source-integrity checks remain enabled.
"""
from pathlib import Path
import runpy
import sys

import progressive_webm_nominal_vfr_fixtures as fixtures
from progressive_webm_vfr_preflight import PROFILES


def main():
    original = fixtures.media
    fixtures.CASES = [('nominal-min-finite', PROFILES[0], False),
                      ('nominal-max-unknown', PROFILES[0], True)]
    def media(folder, case):
        hint = 1000000 if case[0].startswith('nominal-min') else 1000000000
        return original(folder, case, nominal_duration=hint)
    fixtures.media = media
    driver = Path(__file__).with_name('progressive_aac_package_test.py')
    sys.argv = [str(driver), '--scenario', 'webm-nominal-vfr', *sys.argv[1:]]
    runpy.run_path(str(driver), run_name='__main__')


if __name__ == '__main__':
    main()
