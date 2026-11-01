"""Tail-index MP4 actual playback contract for all three editions."""
import argparse,json,time
from pathlib import Path
from progressive_indexed_mp4_playback import exercise as indexed_exercise
from progressive_client import ProgressiveClient

def exercise(client,fixtures,work,pause,seek,profile='main-bframes'):
    return indexed_exercise(client,fixtures,work,pause,seek,profile,tail_mode=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--player',required=True)
    parser.add_argument('--profile')
    parser.add_argument('--fixtures')
    parser.add_argument('--work',required=True)
    parser.add_argument('--media-profile',default='main-bframes')
    args=parser.parse_args()
    sequence=[0]
    def command(action,value=None):
        sequence[0]+=1
        path=Path(args.work)/'control.json'
        temp=path.with_suffix('.tmp')
        temp.write_text(json.dumps(dict(sequence=sequence[0],action=action,value=value)),encoding='utf-8')
        deadline=time.monotonic()+1
        while True:
            try:
                temp.replace(path)
                break
            except PermissionError:
                if time.monotonic()>=deadline:
                    raise
                time.sleep(.01)
    print(json.dumps(exercise(ProgressiveClient(args.player,args.profile),args.fixtures,args.work,
                             lambda:command('pause'),lambda t:command('seek',t),args.media_profile)))
