"""Actual ordinary Qt/Classic main: single-track M4A before writer completion."""
import argparse,json,shutil,sys,tempfile,time,traceback
from pathlib import Path
from progressive_player_crash_recovery import Peer,wait,sha
from progressive_v2 import V2PreviewSession
from progressive_m4a_fixture import publish
from progressive_tail_mp4_fixture import publish as publish_tail
from progressive_process_metrics import sample as metrics

def run(args):
    report=Path(args.report_directory).resolve();report.mkdir(parents=True,exist_ok=True);rows=[];peer=None;preview=None;registration=None
    (report/'error.txt').unlink(missing_ok=True)
    if args.edition=='classic':
        from windows.progressive_family_test import registration_snapshot
        registration=registration_snapshot()
    with tempfile.TemporaryDirectory(prefix='foxicmp-m4a-main-') as temp:
        root=Path(temp);logs=root/'logs';logs.mkdir()
        try:
            peer=Peer(args.edition,args.player,Path(args.player).parent if args.edition=='classic' else root/'profile',logs,args.reader_runner);peer.launch()
            cases=args.case or ['48000-1-2-head','44100-2-2-tail','48000-1-5-tail','44100-2-5-head','48000-2-29-tail']
            for name in cases:
                case=Path(args.corpus)/name;model=json.loads((case/'model.json').read_text());info=json.loads((case/'info.json').read_text());raw=(case/'original.m4a').read_bytes()
                folder=root/name;folder.mkdir();publisher=publish_tail if info['tailOffset'] else publish
                cut=info['ends'][12*info['rate']//info['frame']];value=publisher(model,raw,folder,cut)
                measurements=[];latencies=[];started=time.monotonic()
                preview=V2PreviewSession(peer.client,str(folder),title='Private M4A '+name);opened=preview.publish(value)
                def status():
                    began=time.monotonic();s=preview.heartbeat();latencies.append(time.monotonic()-began);measurements.append(metrics(peer.process.pid));(report/'last-status.json').write_text(json.dumps(s,indent=2));assert s['state'] not in ('error','cancelled'),s;return s
                running=wait(lambda:(lambda s:s if s['state']=='playing' and s['positionSeconds']>.2 and int(s['decoderIoBytes']['audio'])>0 else None)(status()),'actual M4A decoder',20)
                viewed=wait(lambda:(lambda s:s if s['positionSeconds']>=10.1 else None)(status()),'ten seconds before complete',20)
                assert not viewed['complete'] and viewed['m4aWindow']['exactTrimApplied'] and len(viewed['mediaTracks'])==1,viewed
                value=publisher(model,raw,folder,len(raw),previous=value);preview.publish(value)
                held=status();assert not held['complete'] and held['state']!='finished',held
                value=publisher(model,raw,folder,len(raw),previous=value,complete=True);preview.publish(value)
                finished=wait(lambda:(lambda s:s if s['state']=='finished' else None)(status()),'M4A natural EOF',15)
                before=time.monotonic();closed=preview.close(timeout=2);elapsed=time.monotonic()-before;preview=None
                assert closed['released'] and elapsed<2 and (folder/'audio.part').read_bytes()==raw,closed
                peaks={key:max(m[key] for m in measurements) for key in ('rssBytes','privateBytes','handles','threads')}
                assert peaks['privateBytes']<1024**3 and peaks['handles']<2048 and peaks['threads']<128 and max(latencies)<2,(peaks,max(latencies))
                rows.append(dict(profile=name,result='PASS',running=running,playedBeforeComplete=viewed,fullBytesBeforeComplete=held,finished=finished,release=closed,releaseSeconds=elapsed,peakResources=peaks,peakStatusLatencySeconds=max(latencies),cpuSeconds=measurements[-1]['cpuSeconds']-measurements[0]['cpuSeconds'],wallSeconds=time.monotonic()-started,sourceSha256=sha(folder/'audio.part')))
                (report/'progress.json').write_text(json.dumps(rows,indent=2))
        finally:
            if preview:
                try:preview.close(timeout=5)
                except Exception:(report/'release-error.txt').write_text(traceback.format_exc())
            if peer:
                if peer.process is not None and peer.process.poll() is None:peer.kill('cleanup-owned-idle-process')
                peer.cleanup()
                shutil.copytree(logs,report/'player-logs',dirs_exist_ok=True)
            if registration is not None:assert registration_snapshot()==registration,'Installed Classic registration changed'
    result=dict(result='PASS',edition=args.edition,profiles=len(rows),ordinaryMain=True,playerSha256=sha(args.player),processHistory=peer.history,rows=rows,installedSettingsUnchanged=True,temporaryWorkRemoved=not root.exists())
    (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--player',required=True);p.add_argument('--reader-runner',required=True);p.add_argument('--corpus',required=True);p.add_argument('--case',action='append');p.add_argument('--report-directory',required=True);args=p.parse_args()
    try:print(json.dumps(dict(result=run(args)['result'])))
    except BaseException:
        Path(args.report_directory).mkdir(parents=True,exist_ok=True);(Path(args.report_directory)/'error.txt').write_text(traceback.format_exc());raise
