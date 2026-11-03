"""Two isolated Classic processes sharing media; never use installed copies."""
import argparse
import configparser
import json
from pathlib import Path
import subprocess
import tempfile
import shutil
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from progressive_multi_reader_playback import exercise
from progressive_family_test import post_command, registration_snapshot
from progressive_client import ProgressiveClient


def run(players, work):
    assert len(players)==2 and players[0]!=players[1]
    for player in players:
        raw=Path(player).with_suffix('.ini').read_bytes();settings=configparser.ConfigParser()
        settings.read_string(raw.decode('utf-16') if raw.startswith(b'\xff\xfe') else raw.decode('utf-8-sig'))
        assert settings.get('Settings','ContextMenuPrompted')=='1' and settings.get('Settings','ContextMenuEnabled')=='0'
    before=registration_snapshot();processes=[];clients=[];result=None;codes=[]
    try:
        for player in players:
            client=ProgressiveClient(player);process=subprocess.Popen([player,'/new','/nofocus','/minimized'],cwd=Path(player).parent)
            processes.append(process);clients.append(client);deadline=time.monotonic()+20
            while True:
                try:assert client.request(dict(version=1,action='capabilities'))['pid']==process.pid;break
                except (OSError,RuntimeError):
                    assert process.poll() is None and time.monotonic()<deadline;time.sleep(.05)
        def control(peer,action):
            if action=='stop':post_command(processes[peer].pid,890)
            elif action=='settle':assert clients[peer].request(dict(version=1,action='capabilities'))['pid']==processes[peer].pid;time.sleep(.15)
            else:raise AssertionError(action)
        registries=[str(Path(tempfile.gettempdir())/(c.endpoint.rsplit('\\',1)[-1]+'.readers')) for c in clients]
        from progressive_resource_api_conformance import exercise as resource_regression
        regression=resource_regression(clients[0],Path(work)/'resource-regression')
        result=exercise(clients,work,control,registries=registries);assert result['separateProcesses']
        result['resourceIoRegression']=regression
    finally:
        for process in processes:
            if process.poll() is None:
                post_command(process.pid,0)
                try:process.wait(timeout=15)
                except subprocess.TimeoutExpired:process.kill();process.wait();raise
            codes.append(process.returncode)
        assert registration_snapshot()==before,'Installed shell registration changed'
        assert all(code==0 for code in codes),codes
    # Only our random staged installations generated these endpoint directories.
    registries=[]
    for client in clients:
        registry=Path(tempfile.gettempdir())/(client.endpoint.rsplit('\\',1)[-1]+'.readers')
        assert registry.name.startswith('FoxicMPClassic-') and not list(registry.glob('*.lease'))
        if registry.exists():shutil.rmtree(registry)
        registries.append(str(registry))
    result['processExitCodes']=codes;result['shellEntriesUnchanged']=len(before);result['privateRegistriesRemoved']=registries;return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--player',action='append',required=True);p.add_argument('--work',required=True);a=p.parse_args()
    print(json.dumps(run(a.player,a.work)))
