"""Private AVC/AAC parent/cache: physical growth, exact two-track index, leases."""
import argparse
import copy
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import traceback

from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_mux_ts_conformance import Rpc
from progressive_stream_aac_aligned_test import fixture
from progressive_stream_aac_window_test import PROFILES, MODE, packets, reference
from progressive_stream_remux_worker_test import probe
from progressive_stream_reorder_test import sha


def fragment_track_counts(data):
    def boxes(start,end):
        at=start
        while at<end:
            n=int.from_bytes(data[at:at+4],'big');assert n>=8 and at+n<=end
            yield at,n,data[at+4:at+8];at+=n
        assert at==end
    return [sum(kind==b'traf' for _,_,kind in boxes(at+8,at+n))
            for at,n,kind in boxes(0,len(data)) if kind==b'moof']


def run(runner, worker, report):
    report.mkdir(parents=True, exist_ok=False)
    rows=[];exits=[]
    inputs=dict(testSourceSha256=sha(__file__),runnerSha256=sha(runner),workerSha256=sha(worker))
    try:
        with tempfile.TemporaryDirectory(prefix='foxicmp-managed-aac-') as temp:
            root=Path(temp);control=None;aligned_control=None
            for label,rate,channels,fps,bframes,first,n in PROFILES:
                folder=root/label;folder.mkdir();samples=n if n is not None else rate*4+137
                original,_=fixture(folder,'reference',rate,channels,fps,bframes,first,False,seconds=4.137,samples=samples)
                raw=original.read_bytes();before=sha(original);packet_data=probe(original)['packets']
                video=[p for p in packet_data if p['codec_type']=='video']
                cuts=[int(next(p['pos'] for p in video if Fraction(p['pts_time'])>=s))+13 for s in (1,2,3,4)]
                path=folder/'input.part';model=snapshot(path,raw,complete=False,prefix=4)
                model['timeline']['duration']='41370000'
                audio=copy.deepcopy(model['tracks'][0]);audio.update(id='sound',kind='audio',codecHint='aac-lc')
                model['tracks'][0]['codecHint']='h264';model['tracks'].append(audio)
                rpc=Rpc([str(runner),str(folder/'registry'),str(worker)]);current=4;pid=None;publications=[];proof=[]
                def cmd(action,**fields):
                    value=rpc(action,**fields);assert value['ok'],(action,value);return value
                def wait(fn,label,timeout=20):
                    end=time.monotonic()+timeout
                    while time.monotonic()<end:
                        value=fn()
                        if value:return value
                        time.sleep(.02)
                    raise AssertionError(label)
                def status():
                    s=cmd('status');assert not s['failure'],s
                    assert int(s['maximumReadEnd'])<=current,s
                    if pid:assert s['workerId']==pid,s
                    return s
                def publish(count,complete=False,gap=False):
                    nonlocal current,model
                    assert path.stat().st_size==current
                    with path.open('ab') as out:
                        assert out.write(raw[current:count])==count-current;out.flush();os.fsync(out.fileno())
                    if gap:
                        end=time.monotonic()+.15
                        while time.monotonic()<end:status();time.sleep(.02)
                    previous=model;model=copy.deepcopy(previous);model['revision']=str(int(model['revision'])+1)
                    state='complete' if complete else 'downloading'
                    model['writer']['state']=model['resources'][0]['state']=state
                    for t in model['tracks']:t['state']=state
                    model['resources'][0]['ranges']=[dict(offset='0',length=str(count))]
                    model=protect_snapshot(model,folder,previous=previous);current=count
                    cmd('publish',session=model);publications.append(dict(bytes=count,complete=complete))
                def index(kind):
                    actual=cmd('avIndex',kind=kind,name='held')['samples']
                    wanted=[p for p in packet_data if p['codec_type']==('audio' if kind else 'video')]
                    assert len(actual)<=len(wanted)
                    base=Fraction(1,rate) if kind else Fraction(next(s['time_base'] for s in probe(original)['streams'] if s['codec_type']=='video'))
                    for x,y in zip(actual,wanted):
                        assert x['sha256']==y['data_hash'].split(':')[1].lower()
                        for field,key in [('pts','pts'),('dts','dts')]:assert abs(x[field]-int(int(y[key])*base*10000000))<=1,(x,y)
                        duration=int(y['duration'])
                        assert abs(x['end']-int((int(y['pts'])+duration)*base*10000000))<=1,(x,y)
                    return actual
                try:
                    cmd('start',directory=str(folder),root=str(folder/'cache'),session=model,aacWindow=True)
                    initial=wait(lambda:(lambda s:s if s['sourceCode']=='waiting' else None)(status()),'initial wait')
                    pid=initial['workerId'];assert pid>0 and initial['visible']==0 and not initial['audioWindow']
                    cmd('reader',name='held');assert not cmd('read',name='held',offset='0',length='8')['bytes']
                    for cut in cuts:
                        publish(cut,gap=True)
                        wait(lambda:status()['sourceCode']=='waiting' and status()['maximumReadEnd']==current,'confirmed prefix wait')
                        index(0);index(1)
                    ready=status();assert ready['visible']>0 and ready['fragments']>0 and ready['audioWindow'] and not ready['complete']
                    plan=json.loads(ready['audioWindow']);assert int(plan['timestampEnd'])==samples and plan['trackId']==(1 if first else 2)
                    # PCM consumers are not wired to continuous AV yet: reject
                    # accidental use through the existing video-only decoder API.
                    admission=rpc('decoderAdmission');assert not admission['ok'] and admission['code']=='invalid_request',admission
                    publish(len(raw));wait(lambda:status()['sourceCode']=='waiting','all bytes without complete')
                    tail=status();assert not tail['complete'] and not tail['done']
                    at=tail['visible'];assert cmd('read',name='held',offset=str(at),length='1')['code']=='waiting-range'
                    publish(len(raw),True)
                    final=wait(lambda:(lambda s:s if s['done'] else None)(status()),'producer complete')
                    assert final['complete'] and final['sourceCode']=='data' and final['sourceFilesClosed'],final
                    result=json.loads(final['sourceResult']);assert result['mode']==MODE and result['aacPcmWindow']==plan
                    indexes=[index(0),index(1)];assert [len(x) for x in indexes]==[result['videoPackets'],result['audioPackets']]
                    cache=Path(final['path']);original_packets,output_packets=packets(original),packets(cache)
                    assert original_packets['video']==output_packets['video']
                    assert [p[:3] for p in original_packets['audio']]==[p[:3] for p in output_packets['audio']]
                    # Independently decode the cache with the accepted timestamp
                    # bound; this is a reference check, not real Player A/V playback.
                    expected,_=reference(original,rate,channels,samples)
                    decoded=subprocess.run(['ffmpeg','-v','error','-i',str(cache),'-map','0:a','-af','atrim=end_sample='+str(samples),'-f','s16le','-'],capture_output=True)
                    assert decoded.returncode==0 and decoded.stdout==expected,(decoded.returncode,decoded.stderr)
                    cache_hash=sha(cache);assert cache_hash==result['sha256']
                    counts=fragment_track_counts(cache.read_bytes());assert counts and all(n in (1,2) for n in counts)
                    strict=rpc('strictMux',path=str(cache))
                    assert strict['ok']==all(n==2 for n in counts),(counts,strict)
                    if control is None:control=dict(output=cache.read_bytes(),window=plan,source=original,raw=raw)
                    if label=='aligned-control':aligned_control=dict(output=cache.read_bytes(),window=plan)
                    # Independent output lease retains the cache after source close.
                    assert not cmd('retire')['removed'] and cache.exists()
                    cmd('close',name='held');wait(lambda:cmd('retire')['removed'],'last reader retirement',2)
                    assert not cache.exists()
                    reader_ids=set(final['sourceReaderIds'])
                    reader_ids.update(r['readerId'] for r in cmd('readers',version=1,sessionId=model['sessionId'],generation='1')['readers'])
                    for rid in sorted(reader_ids):
                        receipt=cmd('readerStatus',version=1,readerId=rid)['reader']
                        assert receipt['released'] and receipt['filesClosed'];proof.append(receipt)
                    for receipt in proof:cmd('readerForget',version=1,readerId=receipt['readerId'])
                    cmd('dropReader',name='held');cmd('drop');assert cmd('collect')['bytes']==0
                    assert sha(path)==sha(original)==before
                    rows.append(dict(profile=label,result='PASS',window=plan,sampleCounts=[len(x) for x in indexes],
                                     firstReady=ready,final=final,publications=publications,receipts=proof,
                                     exactPacketPayloadClocks=True,exactPcmReference=True,physicalAppendOnly=True,
                                     fragmentTrackCounts=counts,previousMuxProfileStillRequiresBothTracks=True,
                                     unpublishedTailNotRead=True,singleWorkerPid=pid,cacheSha256=cache_hash,
                                     continuousDecoderAdmissionRejected=True))
                finally:
                    rpc.close();exits.append(dict(profile=label,exitCode=rpc.p.returncode))
                (report/'progress.json').write_text(json.dumps(dict(completed=[r['profile'] for r in rows])))
            # Cache boundary rejects inconsistent or unbound media, and keeps
            # the strict two-traf requirement for the older shared MP4 profile.
            for fault in ('no-window','wrong-rate','wrong-channels','wrong-track','wrong-skip','missing-last-packet','duplicate-window','late-window','bad-end-padding','unknown-field'):
                folder=root/fault;folder.mkdir();rpc=Rpc([str(runner),str(folder/'registry'),str(worker)])
                def command(action,**fields):
                    answer=rpc(action,**fields);assert answer['ok'],(action,answer);return answer
                try:
                    import uuid
                    command('store',root=str(folder/'cache'),sessionId=uuid.uuid4().hex)
                    selected=aligned_control if fault=='missing-last-packet' else control
                    plan=copy.deepcopy(selected['window'])
                    if fault=='wrong-rate':plan['sampleRate']=44100
                    if fault=='wrong-channels':plan['channels']=2
                    if fault=='wrong-track':plan['trackId']=1
                    if fault=='wrong-skip':
                        plan['rawBegin']='0';plan['rawEnd']=plan['timestampEnd'];plan['encodedEnd']=str((int(plan['rawEnd'])+1023)//1024*1024);plan['endPadding']=str(int(plan['encodedEnd'])-int(plan['rawEnd']))
                    if fault=='missing-last-packet':
                        for key in ('timestampEnd','rawEnd','encodedEnd'):plan[key]=str(int(plan[key])+1024)
                    if fault=='bad-end-padding':plan['endPadding']=str(int(plan['endPadding'])+1)
                    if fault=='unknown-field':plan['unverified']=True
                    if fault=='late-window':command('append',offset='0',hex=control['output'][:8].hex())
                    rejected=None
                    if fault!='no-window':
                        answer=rpc('audioWindow',window=plan)
                        if fault in ('late-window','bad-end-padding','unknown-field'):
                            assert not answer['ok'],answer;rejected=answer['code']
                        else:assert answer['ok'],answer
                    if fault=='duplicate-window':
                        answer=rpc('audioWindow',window=plan);assert not answer['ok'],answer;rejected=answer['code']
                    if rejected is None:
                        data=selected['output']
                        for offset in range(0,len(data),65536):
                            answer=rpc('append',offset=str(offset),hex=data[offset:offset+65536].hex())
                            if not answer['ok']:rejected=answer['code'];break
                        if fault=='missing-last-packet':assert rejected is None,('Expected final packet-count rejection',rejected)
                        if rejected is None:
                            command('producerClosed',error='',sha256=hashlib.sha256(data).hexdigest())
                            rejected=command('status')['failure']
                        assert rejected,(fault,'invalid AV output accepted')
                    s=command('status');assert not s['complete']
                    command('producerClosed',error='cancelled');assert command('retire')['removed']
                    command('drop');assert command('budgetStatus')['bytes']==0
                    rows.append(dict(profile=fault,result='PASS',rejection=rejected,statusBeforeCleanup=s))
                finally:rpc.close();exits.append(dict(profile=fault,exitCode=rpc.p.returncode))
            for after_window in (False,True):
                label='cancel-after-window' if after_window else 'cancel-before-window'
                folder=root/label;folder.mkdir();raw=control['raw'];path=folder/'input.part'
                video=[p for p in probe(control['source'])['packets'] if p['codec_type']=='video']
                prefix=int(next(p['pos'] for p in video if Fraction(p['pts_time'])>=3))+13 if after_window else 4
                model=snapshot(path,raw,complete=False,prefix=prefix);model['timeline']['duration']='41370000'
                audio=copy.deepcopy(model['tracks'][0]);audio.update(id='sound',kind='audio',codecHint='aac-lc');model['tracks'].append(audio)
                rpc=Rpc([str(runner),str(folder/'registry'),str(worker)])
                def command(action,**fields):
                    answer=rpc(action,**fields);assert answer['ok'],(action,answer);return answer
                try:
                    command('start',directory=str(folder),root=str(folder/'cache'),session=model,aacWindow=True)
                    before=wait(lambda:(lambda s:s if s['sourceCode']=='waiting' and (not after_window or s['visible']) else None)(command('status')),label+' wait')
                    assert bool(before['audioWindow'])==after_window
                    rid=command('reader',name='held')['readerId'];began=time.monotonic();command('cancel')
                    final=wait(lambda:(lambda s:s if s['done'] else None)(command('status')),label+' release',2)
                    elapsed=time.monotonic()-began
                    assert final['sourceFilesClosed'] and not final['complete'] and final['sourceCode']=='cancelled'
                    assert not command('retire')['removed'];command('close',name='held');assert command('retire')['removed']
                    for reader_id in final['sourceReaderIds']+[rid]:
                        receipt=command('readerStatus',version=1,readerId=reader_id)['reader'];assert receipt['released'] and receipt['filesClosed']
                        command('readerForget',version=1,readerId=reader_id)
                    command('dropReader',name='held');command('drop');assert command('collect')['bytes']==0
                    assert path.read_bytes()==raw[:prefix]
                    rows.append(dict(profile=label,result='PASS',seconds=elapsed,before=before,final=final,lastReaderCleanup=True))
                finally:rpc.close();exits.append(dict(profile=label,exitCode=rpc.p.returncode))
            for fault in ('early-write','wrong-digest','missing-field','duplicate-window'):
                folder=root/('protocol-'+fault);folder.mkdir();path=folder/'input.part'
                model=snapshot(path,control['raw']);model['timeline']['duration']='41370000'
                audio=copy.deepcopy(model['tracks'][0]);audio.update(id='sound',kind='audio',codecHint='aac-lc');model['tracks'].append(audio)
                variable='FOXICMP_MANAGED_AAC_TEST_CHILD';previous=os.environ.get(variable)
                try:
                    os.environ[variable]=fault
                    rpc=Rpc([str(runner),str(folder/'registry'),str(runner)])
                finally:
                    if previous is None:os.environ.pop(variable,None)
                    else:os.environ[variable]=previous
                try:
                    answer=rpc('start',directory=str(folder),root=str(folder/'cache'),session=model,aacWindow=True);assert answer['ok'],answer
                    final=wait(lambda:(lambda s:s if s['done'] else None)(rpc('status')),'protocol rejection '+fault)
                    assert final['sourceCode'] in ('backend-protocol-error','invalid_snapshot') and final['written']==0 and not final['complete'] and final['sourceFilesClosed'],final
                    assert rpc('retire')['removed'];assert rpc('drop')['ok'];assert rpc('collect')['bytes']==0
                    rows.append(dict(profile='protocol-'+fault,result='PASS',final=final,mediaBytesAccepted=0))
                finally:rpc.close();exits.append(dict(profile='protocol-'+fault,exitCode=rpc.p.returncode))
            assert sha(__file__)==inputs['testSourceSha256']
        result=dict(result='PASS',**inputs,count=len(rows),rows=rows,exactDriverExits=exits,temporaryWorkRemoved=not root.exists(),
                    publicAudioCapability=False,actualContinuousAvPlayerTested=False,physicalAudioHardwareOpened=False)
        (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');return result
    except BaseException:
        (report/'failure.json').write_text(json.dumps(dict(rows=rows,exactDriverExits=exits,error=traceback.format_exc()),indent=2)+'\n');raise


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);p.add_argument('--worker',type=Path,required=True);p.add_argument('--report-directory',type=Path,required=True);a=p.parse_args()
    r=run(a.runner.resolve(),a.worker.resolve(),a.report_directory.resolve());print(json.dumps(dict(result=r['result'],count=r['count'])))
