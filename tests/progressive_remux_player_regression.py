"""Actual private players: bounded copy-remux and explicit RAM clip regression.

This is not a continuous remux soak or a whole-source playback claim.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile
import traceback
from progressive_player_crash_recovery import Peer,sha
from progressive_writer_failures import save
from progressive_backend_remux_conformance import run as preparation
from progressive_preview_conformance import exercise as playback
from progressive_process_metrics import sample


def run(args):
    report=Path(args.report_directory).resolve();report.mkdir(parents=True,exist_ok=True)
    peers=[];result=None;registration=None
    if args.edition=='classic':
        from windows.progressive_family_test import registration_snapshot
        registration=registration_snapshot();assert len(args.player)==2
    else:assert len(args.player)==1
    with tempfile.TemporaryDirectory(prefix='foxicmp-remux-regression-') as directory:
        root=Path(directory)
        try:
            for i in range(2):
                logs=root/('peer-%d'%i);logs.mkdir();player=args.player[i] if args.edition=='classic' else args.player[0]
                peer=Peer(args.edition,player,Path(player).parent if args.edition=='classic' else root/('profile-%d'%i),logs,args.reader_runner)
                peers.append(peer);peer.launch()
            before=[sample(p.process.pid) for p in peers]
            prepared=preparation(client=peers[0].client)
            played=playback(peers[1].client)
            assert prepared['result']==played['result']=='PASS' and played['continuousPlayback'] is False and played['sourceEofDistinct']
            after=[sample(p.process.pid) for p in peers]
            result=dict(result='PASS',boundedRemuxOnly=True,continuousPlayback=False,preparation=prepared,playback=played,beforeMetrics=before,afterMetrics=after)
        finally:
            errors=[];removed=[]
            for peer in peers:
                try:
                    if peer.process is not None and peer.process.poll() is None:peer.kill('cleanup-owned-idle-process' if result is not None else 'cleanup-after-test-failure')
                except Exception:errors.append(traceback.format_exc())
            for peer in peers:
                try:removed.extend(peer.cleanup())
                except Exception:errors.append(traceback.format_exc())
            for i,peer in enumerate(peers):
                dst=report/('peer-%d'%i);dst.mkdir(exist_ok=True)
                for p in peer.directory.glob('*.log'):shutil.copy2(p,dst/p.name)
            save(report/'cleanup.json',dict(runtimePathsRemoved=removed,processHistory=[p.history for p in peers],allOwnedProcessesExited=all(p.process.poll() is not None for p in peers)))
            if errors:(report/'cleanup-error.txt').write_text('\n'.join(errors),encoding='utf-8')
            assert not errors,errors
            if registration is not None:assert registration_snapshot()==registration,'Installed shell registration changed'
    result.update(edition=args.edition,processHistory=[p.history for p in peers],temporaryWorkRemoved=not root.exists(),playerSha256=[sha(p.player) for p in peers],readerRunnerSha256=sha(args.reader_runner))
    save(report/'result.json',result);return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--player',action='append',required=True);p.add_argument('--reader-runner',required=True);p.add_argument('--report-directory',required=True);args=p.parse_args()
    try:print(json.dumps(run(args)))
    except Exception:
        Path(args.report_directory).mkdir(parents=True,exist_ok=True);(Path(args.report_directory)/'error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
