"""Classic continuous API plus normal window close and same-profile recovery."""
import argparse
import json
from pathlib import Path
import time
import traceback
import uuid
import progressive_continuous_playback_test as playback
from progressive_continuous_api_test import run
from progressive_player_crash_recovery import wait,sha
from progressive_stream_remux_worker_test import fixture
from progressive_backend_demux_conformance import snapshot


def exercise(peers,root):
    result=playback.exercise(peers,root);peer=peers[0];client=peer.client
    directory=root/'normal-window-close';directory.mkdir();source=fixture(directory,'reference');raw=source.read_bytes();path=directory/'input.part'
    model=snapshot(path,raw,complete=False,prefix=len(raw)*2//3);model['timeline']['duration']='240000000';path.write_bytes(raw);before=sha(path)
    def command(action,**fields):return client.request(dict(version=1,action=action,**fields))
    def start(value):return command('continuousRemuxStart',requestId=uuid.uuid4().hex,sessionDirectory=str(directory),session=value,resourceId='clip')
    job=start(model)
    def prepared():
        s=command('continuousRemuxStatus',jobId=job['jobId']);assert not s['errorCode'],s
        return s if s['playbackReady'] else None
    wait(prepared,'close-test preparation')
    fields={key:job[key] for key in ('jobId','sessionId','generation','writerInstanceId')}
    opened=command('continuousRemuxPlay',requestId=uuid.uuid4().hex,**fields)
    def playing():
        s=command('continuousPlaybackStatus',playbackId=opened['playbackId']);assert s['state'] not in ('error','closed'),s
        return s if s['state']=='playing' and s['positionSeconds']>.2 and s['decoderReaders']>0 else None
    actual=wait(playing,'close-test actual playback')
    from windows.progressive_family_test import post_command
    began=time.monotonic();post_command(peer.process.pid,0);peer.process.wait(timeout=8);elapsed=time.monotonic()-began
    assert peer.process.returncode==0,peer.process.returncode
    peer.history[-1].update(reason='normal-window-close',exitCode=0,terminationSeconds=elapsed)
    closed=peer.native('readers',sessionId=model['sessionId'],generation='1')['readers']
    assert len(closed)>=2 and all(r['released'] and r['reason'] in ('files-closed','process-exited','pid-reused') for r in closed),closed
    cache=Path(str(peer.registry)+'.continuous-cache');remaining=[p.name for p in cache.glob('remux-*')]
    # Restart the same private copy/profile. Cache recovery uses retained reader
    # receipts/OS process identity before the test acknowledges those receipts.
    peer.launch();model2=snapshot(directory/'restart.part',raw,complete=False,prefix=4);model2['timeline']['duration']='240000000';job=start(model2)
    def waiting():
        s=command('continuousRemuxStatus',jobId=job['jobId']);assert not s['errorCode'],s
        return s if s['producerCode']=='waiting' else None
    recovered=wait(waiting,'same-profile cache recovery')
    command('continuousRemuxForget',**{key:job[key] for key in ('jobId','sessionId','generation','writerInstanceId')})
    final=wait(lambda:(lambda s:s if s['forgotten'] else None)(command('continuousRemuxStatus',jobId=job['jobId'])),'restarted source release')
    assert final['released'] and not list(cache.glob('remux-*')),final
    for receipt in closed:peer.native('readerForget',readerId=receipt['readerId'])
    assert sha(path)==before
    result['rows'].append(dict(profile='normal-window-close-and-same-profile-recovery',result='PASS',actualPlayback=actual,normalExitCode=0,exitSeconds=elapsed,closedReceipts=closed,cacheDirectoriesAfterExit=remaining,restarted=recovered,final=final,originalUnchanged=True))
    result.update(count=len(result['rows']),normalWindowClose=True,sameProfileRecovery=True,basePlaybackTestSha256=sha(playback.__file__))
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--edition',choices=('classic',),required=True);p.add_argument('--player',action='append',required=True);p.add_argument('--reader-runner',required=True);p.add_argument('--report-directory',required=True);args=p.parse_args()
    try:
        result=run(args,exercise,__file__);print(json.dumps(dict(result=result['result'],count=result['count'])))
    except Exception:
        Path(args.report_directory).mkdir(parents=True,exist_ok=True);(Path(args.report_directory)/'error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
