"""Explicit initial position over ordinary main IPC; old seek control unchanged."""
import argparse
from pathlib import Path
from progressive_continuous_aac_api_test import exercise as aac_exercise
from progressive_continuous_api_test import run

def exercise(peers,root,audio_output='null'):
    return aac_exercise(peers,root,audio_output,initial_position=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--player',action='append',required=True);p.add_argument('--reader-runner',required=True);p.add_argument('--report-directory',required=True);p.add_argument('--sdk-directory',type=Path);p.add_argument('--expected-audio-output',default='null');a=p.parse_args()
    r=run(a,lambda peers,root:exercise(peers,root,a.expected_audio_output),__file__)
    print(r['result'],r['count'])
