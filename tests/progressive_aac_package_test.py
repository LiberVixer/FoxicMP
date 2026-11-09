"""Exercise deployed ordinary players using their bundled SDK, isolated profiles."""
import argparse
import configparser
import hashlib
import importlib
import json
from pathlib import Path
import shutil
import sys
import os
sys.dont_write_bytecode=True
os.environ["PYTHONDONTWRITEBYTECODE"]="1"
import tempfile
import traceback
from types import SimpleNamespace


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--edition',choices=('qt','classic'),required=True)
    parser.add_argument('--player',type=Path,required=True)
    parser.add_argument('--reader-runner',type=Path,required=True)
    parser.add_argument('--sdk-directory',type=Path,required=True)
    parser.add_argument('--scenario',choices=('initial-position','initial-position-video','initial-position-reordered','aac','legacy','reordered','aac-waiting','aac-worker-crash','aac-races','aac-shutdown','aac-lifecycle','aac-growing','aac-heartbeat','aac-heartbeat-ordering','aac-heartbeat-idle-stability','aac-profile-resilience'),required=True)
    parser.add_argument('--expected-audio-output',choices=('null','pulse','wasapi'),default='null')
    parser.add_argument('--classic-null-video',action='store_true',help='Private profile: require decoded video without a desktop renderer')
    parser.add_argument('--report-directory',type=Path,required=True)
    args=parser.parse_args();sdk=args.sdk_directory.resolve()
    if args.classic_null_video and args.edition!='classic':parser.error('--classic-null-video requires Classic')
    if args.scenario=='aac-heartbeat-idle-stability' and (args.edition!='classic' or os.name!='nt'):
        parser.error('aac-heartbeat-idle-stability requires Windows Classic')
    sys.path.insert(0,str(sdk))
    names=('progressive_schema','progressive_client','progressive_integrity','progressive_resources','progressive_sdk','progressive_v2','progressive_continuous','progressive_windows','progressive_preview','progressive_writer')
    modules={name:importlib.import_module(name) for name in names}
    assert all(Path(m.__file__).resolve().parent==sdk for m in modules.values())
    from progressive_continuous_api_test import run
    from progressive_player_crash_recovery import sha
    if args.scenario=='initial-position':
        import progressive_initial_position_api_test as case
        exercise=lambda peers,root:case.exercise(peers,root,args.expected_audio_output)
    elif args.scenario in ('initial-position-video','initial-position-reordered'):
        import progressive_continuous_sdk_conformance as case
        mode=modules['progressive_continuous'].REORDERED_MODE if args.scenario.endswith('reordered') else modules['progressive_continuous'].MODE
        exercise=lambda peers,root:case.exercise(peers,root,mode,initial_position=True)
    elif args.scenario=='aac':
        import progressive_continuous_aac_api_test as case
        exercise=lambda peers,root:case.exercise(peers,root,args.expected_audio_output)
    elif args.scenario in ('legacy','reordered'):
        import progressive_continuous_sdk_conformance as case
        mode=modules['progressive_continuous'].REORDERED_MODE if args.scenario=='reordered' else modules['progressive_continuous'].MODE
        exercise=lambda peers,root:case.exercise(peers,root,mode)
    else:
        cases={'aac-waiting':'progressive_continuous_waiting_conformance',
               'aac-worker-crash':'progressive_continuous_worker_crash_conformance',
               'aac-races':'progressive_continuous_command_races',
               'aac-shutdown':'progressive_continuous_shutdown_conformance',
               'aac-lifecycle':'progressive_continuous_lifecycle_stress',
               'aac-growing':'progressive_continuous_growing_stress',
               'aac-heartbeat':'progressive_continuous_aac_heartbeat_stress',
               'aac-heartbeat-ordering':'progressive_continuous_heartbeat_ordering',
               'aac-heartbeat-idle-stability':'progressive_continuous_aac_heartbeat_stability',
               'aac-profile-resilience':'progressive_continuous_aac_profiles'}
        case=importlib.import_module(cases[args.scenario])
        mode=modules['progressive_continuous'].AAC_MODE
        def exercise(peers,root):
            options={'media_mode':mode} if args.scenario=='aac-races' else {'mode':mode}
            if args.scenario=='aac-shutdown':options['require_clean']=True
            if args.scenario=='aac-lifecycle':options.update(cycles=100,repeats=0)
            result=case.exercise(peers,root,args.report_directory,**options)
            from progressive_continuous_test_media import __file__ as media_source
            result.update(publicMode=mode,mediaHelperSha256=sha(media_source))
            result.setdefault('fixtureProfile','h264-b3-30000/1001-aac-lc-48000-stereo')
            return result
    with tempfile.TemporaryDirectory(prefix='foxicmp-package-peers-') as name:
        root=Path(name);players=[str(args.player.resolve())]
        if args.edition=='classic':
            players=[]
            for i in range(2):
                dest=root/str(i);shutil.copytree(args.player.resolve().parent,dest)
                player=dest/args.player.name;ini=player.with_suffix('.ini')
                raw=ini.read_bytes();settings=configparser.ConfigParser();settings.optionxform=str
                settings.read_string(raw.decode('utf-16') if raw.startswith(b'\xff\xfe') else raw.decode('utf-8-sig'))
                if not settings.has_section('Settings'):settings.add_section('Settings')
                settings.set('Settings','ContextMenuPrompted','1');settings.set('Settings','ContextMenuEnabled','0')
                if args.scenario=='aac-profile-resilience':
                    settings.set('Settings','KeepHistory','1');settings.set('Settings','RememberFilePos','1')
                    # Reproduce older profiles that retained the shared alias.
                    (dest/'history.mpc_lst').write_text('; FoxicMP History File 0.1\n\n[001]\nPath=continuous-video.mp4\nTitle=Existing continuous alias\nPosition=00:00:37\n',encoding='utf-8-sig')
                if not settings.has_section('Audio'):settings.add_section('Audio')
                settings.set('Audio','AudioRenderer','Null Audio Renderer (Uncompressed)')
                if args.classic_null_video:
                    if not settings.has_section('Video'):settings.add_section('Video')
                    settings.set('Video','VideoRenderer','11')
                with ini.open('w',encoding='utf-16') as file:settings.write(file,space_around_delimiters=False)
                players.append(str(player))
        def checked(peers,work):
            try:
                return exercise(peers,work)
            except BaseException:
                args.report_directory.mkdir(parents=True,exist_ok=True)
                (args.report_directory/'error.txt').write_text(traceback.format_exc(),encoding='utf-8')
                try:
                    shutil.copytree(work,args.report_directory/'failure-artifacts')
                except Exception:
                    (args.report_directory/'failure-artifacts-error.txt').write_text(traceback.format_exc(),encoding='utf-8')
                raise
        result=run(SimpleNamespace(edition=args.edition,player=players,reader_runner=str(args.reader_runner),report_directory=str(args.report_directory)),exercise_case=checked,source_file=case.__file__)
    result.update(ordinaryPackagedMain=True,packagedMainAcceptance=True,privatePeerCopiesRemoved=not root.exists(),sdkDirectory=str(sdk),sdkModuleSha256={name:sha(module.__file__) for name,module in modules.items()},driverSha256=sha(__file__),scenario=args.scenario)
    result['classicPrivateNullVideo']=args.classic_null_video
    (args.report_directory/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(result=result['result'],count=result['count'])))


if __name__=='__main__':main()
