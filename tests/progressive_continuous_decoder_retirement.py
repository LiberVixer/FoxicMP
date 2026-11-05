"""Hold native decoder callbacks after producer failure; independent readers survive.

No GUI, installed profile or Harvester. A shortened deadline is private fixture
configuration only; public 30-second heartbeat conformance remains separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import time
import traceback
import uuid

from progressive_mux_ts_conformance import Rpc
from progressive_backend_demux_conformance import snapshot
from progressive_stream_remux_worker_test import fixture, probe


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args):
    report=Path(args.report_directory).resolve();report.mkdir(parents=True,exist_ok=False)
    rows=[];exits=[]
    (report/'inputs.json').write_text(json.dumps(dict(runnerSha256=digest(args.runner),workerSha256=digest(args.worker),testSha256=digest(__file__),privateDeadlineMilliseconds=1000,publicHeartbeatAcceptance=False),indent=2)+'\n')
    with tempfile.TemporaryDirectory(prefix='foxicmp-decoder-retirement-') as name:
        root=Path(name);media=fixture(root,'reference');raw=media.read_bytes();media_sha=digest(media)
        cut=int(next(p['pos'] for p in probe(media)['packets'] if float(p['pts_time'])>=12))+13
        for mode in ('held-callback','independent-reader','waiting-read','completed'):
            folder=root/mode;folder.mkdir();source=folder/'input.part'
            model=snapshot(source,raw,complete=mode=='completed',prefix=len(raw) if mode=='completed' else cut)
            model['timeline']['duration']='240000000';source.write_bytes(raw);original=digest(source)
            rpc=Rpc([args.runner,str(folder/'registry'),str(folder/'cache'),args.worker]);job=None;cursor=None;held=False;trace=[];observations=[]
            def command(action,**fields):
                value=rpc(action,**fields);assert value.get('ok'),(action,value);return value
            def receipt(identifier):
                value=rpc('readerStatus',version=1,readerId=identifier)
                if not value.get('ok') and value.get('code')=='reader_registry_busy':return None
                assert value.get('ok'),value
                return value['reader']
            def status():
                value=command('continuousRemuxStatus',version=1,jobId=job['jobId']);observations.append(value);return value
            def fenced(action):
                return command(action,version=1,**{k:job[k] for k in ('jobId','sessionId','generation','writerInstanceId')})
            def wait(check,label,seconds=5):
                end=time.monotonic()+seconds
                while time.monotonic()<end:
                    value=check()
                    if value:return value
                    time.sleep(.01)
                raise AssertionError(label)
            try:
                command('configure',heartbeat=1000)
                job=command('continuousRemuxStart',version=1,requestId=uuid.uuid4().hex,sessionDirectory=str(folder),resourceId='clip',session=model)
                def ready():
                    value=status();assert not value['errorCode'],value
                    if value['playbackReady'] and (mode=='completed' or value['producerCode']=='waiting'):return value
                    if not value['outputComplete']:fenced('continuousRemuxHeartbeat')
                wait(ready,'Decoder not ready',15)
                cursor=command('decoderCursor',jobId=job['jobId'],name='decoder')['readerId']
                assert command('cursorRead',name='decoder')['result']==1
                if mode=='independent-reader':
                    independent=command('reader',jobId=job['jobId'],name='independent')['readerId'];held=True
                if mode=='waiting-read':
                    command('cursorWait',name='decoder',offset=status()['visibleBytes'])
                    wait(lambda:command('cursorStatus',name='decoder')['waiting'],'Read did not block')
                if mode=='completed':
                    wait(lambda:status()['outputComplete'],'Completion not confirmed')
                    time.sleep(1.1);value=status();assert value['outputComplete'] and not value['errorCode']
                    held_receipt=wait(lambda:receipt(cursor),'Completed receipt');assert not held_receipt['released']
                    assert command('cursorRead',name='decoder')['result']==1
                    command('cursorClose',name='decoder');cursor=None
                    fenced('continuousRemuxCancel')
                    final=wait(lambda:(lambda s:s if s['released'] else None)(status()),'Completed cleanup')
                    rows.append(dict(profile=mode,result='PASS',completedReaderRetained=True,final=final))
                else:
                    fenced('continuousRemuxHeartbeat')
                    terminal=wait(lambda:(lambda s:s if s['errorCode'] else None)(status()),'Heartbeat error not observed')
                    assert terminal['errorCode']=='writer-heartbeat-timeout',terminal
                    began=time.monotonic()
                    if mode=='waiting-read':
                        ended=wait(lambda:(lambda r:r if r['ready'] else None)(command('cursorPoll',name='decoder')),'Read not interrupted')
                        assert ended['result']==-1,ended
                    def closed():
                        current=status();trace.append(dict(secondsAfterError=time.monotonic()-began,producer=current))
                        # Raw independent ownership keeps the store pinned.
                        if mode=='independent-reader':
                            closed_receipt=receipt(cursor)
                            return current if closed_receipt and closed_receipt['released'] and closed_receipt['filesClosed'] else None
                        return current if current['released'] and current['sourceFilesClosed'] else None
                    final=wait(closed,'Decoder callback lifetime retained cache beyond 2 seconds',2)
                    elapsed=time.monotonic()-began
                    closed_receipt=wait(lambda:receipt(cursor),'Closed receipt');assert closed_receipt['released'] and closed_receipt['filesClosed'] and closed_receipt['reason']=='files-closed'
                    assert command('cursorStatus',name='decoder')['eofCount']==0
                    assert command('cursorRead',name='decoder')['result']==-1
                    if held:
                        other=wait(lambda:receipt(independent),'Independent receipt')
                        assert not other['released'] and not other['filesClosed'] and not final['released']
                        command('close',name='independent');held=False
                        wait(lambda:status()['released'],'Independent reader cleanup')
                    rows.append(dict(profile=mode,result='PASS',automaticCloseSeconds=elapsed,cursorStillOwned=True,decoderReceipt=closed_receipt,independentReaderPreserved=mode=='independent-reader',terminal=terminal,final=final))
                assert digest(source)==original
                (report/'progress.json').write_text(json.dumps(dict(completed=len(rows),rows=rows),indent=2)+'\n')
            except BaseException:
                (report/'failure.json').write_text(json.dumps(dict(profile=mode,error=traceback.format_exc(),observations=observations,releaseTrace=trace),indent=2)+'\n')
                raise
            finally:
                if cursor:command('cursorClose',name='decoder')
                if held:command('close',name='independent')
                if job:
                    fenced('continuousRemuxCancel');wait(lambda:status()['released'],'Owned failure cleanup')
                    fenced('continuousRemuxForget');wait(lambda:status()['forgotten'],'Owned receipt cleanup')
                rpc.close();exits.append(rpc.p.returncode)
        result=dict(result='PASS',count=len(rows),rows=rows,sourceUnchanged=digest(media)==media_sha,actualFixtureExitCodes=exits)
        (report/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True);parser.add_argument('--worker',required=True);parser.add_argument('--report-directory',required=True)
    run(parser.parse_args())
