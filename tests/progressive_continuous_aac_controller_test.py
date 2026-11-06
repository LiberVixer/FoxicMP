"""Public continuous AAC admission, pending PCM-plan readiness and retirement."""
import argparse
import copy
from fractions import Fraction
import json
from pathlib import Path
import tempfile
import time
import traceback
import uuid
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_mux_ts_conformance import Rpc
from progressive_stream_aac_window_test import PROFILES
from progressive_stream_aac_aligned_test import fixture
from progressive_stream_remux_worker_test import probe
from progressive_stream_reorder_test import sha

MODE='continuous-h264-aac-window-v1'


def av_snapshot(path, raw, prefix, complete=False):
    model=snapshot(path,raw,complete=complete,prefix=prefix)
    model['timeline']['duration']='62000000'
    model['tracks'][0]['codecHint']='h264'
    audio=copy.deepcopy(model['tracks'][0]);audio.update(id='sound',kind='audio',codecHint='aac-lc')
    model['tracks'].append(audio)
    return model


def run(runner,worker,report):
    report.mkdir(parents=True,exist_ok=False);rows=[];exits=[]
    try:
        with tempfile.TemporaryDirectory(prefix='foxicmp-aac-controller-') as tmp:
            root=Path(tmp)
            for label,rate,channels,fps,bframes,first,n in PROFILES:
                folder=root/label;folder.mkdir();samples=n if n is not None else rate*6+137
                original,_=fixture(folder,'original',rate,channels,fps,bframes,first,False,seconds=6.137,samples=samples)
                raw=original.read_bytes();before=sha(original);packets=probe(original)['packets']
                prefix=int(next(p['pos'] for p in packets if p['codec_type']=='video' and Fraction(p['pts_time'])>=3))+13
                path=folder/'input.part';model=av_snapshot(path,raw,4);path.write_bytes(raw)
                rpc=Rpc([str(runner),str(folder/'registry'),str(folder/'cache'),str(worker)])
                def cmd(action,**fields):
                    result=rpc(action,version=1,**fields);assert result['ok'],result;return result
                def rejected(action,code,**fields):
                    result=rpc(action,version=1,**fields);assert not result['ok'] and result['code']==code,result
                request=dict(requestId=uuid.uuid4().hex,sessionDirectory=str(folder),session=model,resourceId='clip',mode=MODE)
                job=cmd('continuousRemuxStart',**request)
                def status():return cmd('continuousRemuxStatus',jobId=job['jobId'])
                identity={k:job[k] for k in ('jobId','sessionId','generation','writerInstanceId')}
                def wait(check):
                    until=time.monotonic()+20
                    while True:
                        s=status()
                        if check(s):return s
                        assert time.monotonic()<until,s
                        assert not s['errorCode'],s
                        time.sleep(.02)
                def publish(count,complete=False,failed=None):
                    nonlocal model
                    old=model;model=copy.deepcopy(old);model['revision']=str(int(old['revision'])+1)
                    state='complete' if complete else 'downloading'
                    model['writer']['state']=model['resources'][0]['state']=state
                    for t in model['tracks']:t['state']='failed' if t['kind']==failed else state
                    model['resources'][0]['ranges']=[dict(offset='0',length=str(count))]
                    model=protect_snapshot(model,folder,previous=old)
                    return cmd('continuousRemuxUpdate',jobId=job['jobId'],session=model)
                try:
                    assert job['mode']==MODE and job['audio'] and not job['audioWindowReady']
                    assert cmd('continuousRemuxStart',**request)['jobId']==job['jobId']
                    rejected('continuousRemuxStart','continuous-conflict',**dict(request,mode='continuous-h264-reordered-video-v1'))
                    pending=wait(lambda s:s['producerCode']=='waiting')
                    assert not pending['playbackReady'] and pending['maximumReadEnd']=='4' and not pending['audioWindowReady'],pending
                    pid=pending['workerId'];publish(prefix)
                    ready=wait(lambda s:s['playbackReady'])
                    assert ready['mode']==MODE and ready['audioWindowReady'] and ready['audioSampleRate']==rate and ready['audioChannels']==channels and ready['audioPresentationSamples']==str(samples),ready
                    assert ready['workerId']==pid and not ready['outputComplete']
                    publish(len(raw),True);complete=wait(lambda s:s['outputComplete'])
                    assert complete['workerId']==pid and complete['playbackReady'] and complete['sourceFilesClosed'],complete
                    cmd('continuousRemuxForget',**identity);final=wait(lambda s:s['forgotten'])
                    assert final['released'] and final['sourceFilesClosed'] and not final['playbackReady'] and not final['audioWindowReady']
                    assert sha(path)==sha(original)==before
                    rows.append(dict(profile=label,result='PASS',pending=pending,ready=ready,complete=complete,final=final))
                finally:
                    rpc.close();exits.append(dict(profile=label,exitCode=rpc.p.returncode))
                # Reuse a fresh controller to prove rejection precedes worker spawn.
                rpc=Rpc([str(runner),str(folder/'negative-registry'),str(folder/'negative-cache'),str(worker)])
                try:
                    for name,change in [('missing-audio',lambda s:s['tracks'].pop()),('optional-audio',lambda s:s['tracks'][1].update(required=False)),('wrong-codec',lambda s:s['tracks'][1].update(codecHint='opus')),('duplicate-video',lambda s:s['tracks'][1].update(kind='video',codecHint='h264'))]:
                        bad=copy.deepcopy(request);bad['requestId']=uuid.uuid4().hex;change(bad['session'])
                        rejected('continuousRemuxStart','unsupported-stream-source',**bad)
                    assert not (folder/'negative-cache').exists()
                    rows.append(dict(profile=label+'-invalid-layouts',result='PASS',rejections=4,noCacheCreated=True))
                finally:rpc.close();exits.append(dict(profile=label+'-invalid-layouts',exitCode=rpc.p.returncode))
            # Cancellation and failure before the AAC decoder can exist must not
            # dereference a missing decoder or publish readiness afterwards.
            for phase in ['cancel-before-init','audio-failed','video-failed']:
                folder=root/phase;folder.mkdir();path=folder/'input.part';model=av_snapshot(path,raw,4);path.write_bytes(raw)
                rpc=Rpc([str(runner),str(folder/'registry'),str(folder/'cache'),str(worker)])
                try:
                    request=dict(requestId=uuid.uuid4().hex,sessionDirectory=str(folder),session=model,resourceId='clip',mode=MODE)
                    job=cmd('continuousRemuxStart',**request);identity={k:job[k] for k in ('jobId','sessionId','generation','writerInstanceId')}
                    wait(lambda s:s['producerCode']=='waiting')
                    if phase=='cancel-before-init':cmd('continuousRemuxCancel',**identity)
                    else:publish(4,failed=phase.split('-')[0])
                    end=time.monotonic()+10
                    while True:
                        final=status()
                        if final['released']:break
                        assert time.monotonic()<end,final
                        time.sleep(.02)
                    assert final['sourceFilesClosed'] and not final['playbackReady'] and not final['audioWindowReady'],final
                    if phase!='cancel-before-init':assert final['errorCode']=='writer-failed',final
                    cmd('continuousRemuxForget',**identity)
                    end=time.monotonic()+5
                    while not status()['forgotten']:assert time.monotonic()<end;time.sleep(.02)
                    rows.append(dict(profile=phase,result='PASS',final=final))
                finally:rpc.close();exits.append(dict(profile=phase,exitCode=rpc.p.returncode))
        result=dict(result='PASS',count=len(rows),rows=rows,exactDriverExits=exits,testSourceSha256=sha(__file__),runnerSha256=sha(runner),workerSha256=sha(worker),temporaryWorkRemoved=not root.exists())
        (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
    except BaseException:
        (report/'failure.json').write_text(json.dumps(dict(rows=rows,exactDriverExits=exits,error=traceback.format_exc()),indent=2)+'\n');raise


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',type=Path,required=True);parser.add_argument('--worker',type=Path,required=True);parser.add_argument('--report-directory',type=Path,required=True);args=parser.parse_args()
    result=run(args.runner.resolve(),args.worker.resolve(),args.report_directory.resolve());print(json.dumps(dict(result=result['result'],count=result['count'])))
