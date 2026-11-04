"""Real libmpv decode over verified growing cache, plus Classic fragment-index view.
Private engine fixtures only: not installed Player/API or a DirectShow graph test.
"""
import argparse,copy,hashlib,json,os,subprocess,tempfile,time,uuid
from fractions import Fraction
from pathlib import Path
from progressive_mux_ts_conformance import Rpc
from progressive_backend_demux_conformance import snapshot
from progressive_stream_remux_worker_test import fixture,probe
from progressive_integrity import protect_snapshot
SOURCE_SHA256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(runner,worker,report):
    rows=[];exits=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-stream-decoder-') as tmp:
        root=Path(tmp);original=fixture(root,'source');raw=original.read_bytes();original_sha=sha(original)
        fragmented=root/'fragmented.mp4'
        subprocess.run(['ffmpeg','-v','error','-y','-i',str(original),'-c','copy','-movflags','empty_moov+frag_keyframe+default_base_moof+negative_cts_offsets',str(fragmented)],check=True,capture_output=True)
        payload=fragmented.read_bytes();at=0;cuts=[]
        while at<len(payload):
            n=int.from_bytes(payload[at:at+4],'big');kind=payload[at+4:at+8];assert n>=8
            if kind==b'moov':init=at+n
            if kind==b'mdat':cuts.append(at+n)
            at+=n
        assert at==len(payload) and len(cuts)>3
        reference=root/'reference';reference.mkdir()
        def case(name,fn):
            folder=root/name;folder.mkdir();rpc=Rpc([str(runner),str(folder/'registry'),str(worker)])
            def cmd(action,**fields):
                v=rpc(action,**fields);assert v['ok'],(action,v);return v
            def wait(action,predicate,timeout=15):
                until=time.monotonic()+timeout
                while time.monotonic()<until:
                    v=cmd(action)
                    if predicate(v):return v
                    time.sleep(.02)
                raise AssertionError((name,action,'timeout',v))
            def push(data):
                for at in range(0,len(data),65536):cmd('append',hex=data[at:at+65536].hex())
            def new():cmd('store',root=str(folder/'cache'),sessionId=uuid.uuid4().hex)
            def finish(error=''):cmd('finish',error=error,**({'sha256':hashlib.sha256(payload).hexdigest()} if not error else {}))
            def receipts(state):
                ids=state.get('sourceReaderIds',[])+state['readerIds']
                for rid in ids:
                    v=cmd('readerStatus',version=1,readerId=rid)['reader'];assert v['released'] and v['reason']=='files-closed',v
                    cmd('readerForget',version=1,readerId=rid)
                return len(ids)
            try:
                detail=fn(folder,cmd,wait,push,new,finish,receipts)
                rows.append(dict(profile=name,result='PASS',**(detail or {})))
            finally:
                rpc.close();exits.append(dict(profile=name,pid=rpc.p.pid,exitCode=rpc.p.returncode))
        def golden(folder,c,w,p,n,f,r):
            c('mpv',images=str(reference),file=str(original));state=w('mpvStatus',lambda s:s['ended']);assert state['reason']==0 and state['error']==0,state
            c('mpvDestroy');assert len(list(reference.glob('*.png')))==600,len(list(reference.iterdir()))
            return dict(frames=600,decoder=state)
        case('completed-source-libmpv-reference',golden)
        reference_hashes=[sha(p) for p in sorted(reference.glob('*.png'))]
        def blocked(folder,c,w,p,n,f,r):
            n();rid=c('cursor')['readerId'];assert c('size')['size']==-1;c('beginRead',length=8)
            w('cursorStatus',lambda s:s['waiting']);assert c('readResult')['pending'];assert c('cursorStatus')['eofs']==0
            began=time.monotonic();c('interrupt');v=w('readResult',lambda s:not s.get('pending'));elapsed=time.monotonic()-began
            assert v['bytes']==-1 and elapsed<.5,(v,elapsed);c('closeCursor');f('cancelled');state=c('status');assert c('retire')['removed'];assert r(state)==1
            return dict(interruptSeconds=elapsed,readerId=rid,read=v)
        case('interrupt-blocked-read-without-false-eof',blocked)
        def source_cancel(folder,c,w,p,n,f,r):
            n();c('cursor');c('beginRead',length=1);w('cursorStatus',lambda s:s['waiting']);c('cancelSource');v=w('readResult',lambda s:not s.get('pending'));assert v['bytes']==-1,v
            c('closeCursor');f('cancelled');state=c('status');assert c('retire')['removed'];r(state)
        case('source-cancel-wakes-blocked-cursor',source_cancel)
        def cursor_bytes(folder,c,w,p,n,f,r):
            n();p(payload);c('cursor');assert c('size')['size']==-1
            assert c('seek',offset=-1)['offset']==-1 and c('seek',offset=len(payload)+1)['offset']==-1
            # mfra is not visible until producer confirms completion.
            assert c('seek',offset=len(payload))['offset']==-1
            c('beginRead',length=2**40);v=w('readResult',lambda s:not s.get('pending'));assert v['bytes']==65536 and v['sha256']==hashlib.sha256(payload[:65536]).hexdigest(),v
            f();assert c('size')['size']==len(payload);assert c('seek',offset=len(payload))['offset']==len(payload)
            c('beginRead',length=1);v=w('readResult',lambda s:not s.get('pending'));assert v['bytes']==0 and c('cursorStatus')['eofs']==1,v
            c('closeCursor');state=c('status');assert c('retire')['removed'];r(state)
            return dict(boundedRead=65536,finalSize=len(payload),confirmedEof=True)
        case('bounded-read-seek-size-confirmed-eof',cursor_bytes)
        for error in ('writer-failed','cancelled','backend-worker-exited'):
            def fault(folder,c,w,p,n,f,r,error=error):
                n();c('cursor');c('beginRead',length=1);w('cursorStatus',lambda s:s['waiting']);f(error);v=w('readResult',lambda s:not s.get('pending'));assert v['bytes']==-1 and v['code']==error,v
                assert c('cursorStatus')['eofs']==0;c('closeCursor');state=c('status');assert c('retire')['removed'];r(state);return dict(read=v)
            case('cursor-propagates-'+error,fault)
        def fragment_view(folder,c,w,p,n,f,r):
            n();c('fragmentOpen');p(payload[:init]);v=c('fragments');assert v['initialized'] and not v['samples'],v
            p(payload[init:cuts[0]-1]);assert not c('fragments')['samples']
            p(payload[cuts[0]-1:cuts[0]]);assert len(c('fragments')['samples'])==25
            p(payload[cuts[0]:]);f();samples=c('fragments')['samples'];expected=probe(fragmented);packets=expected['packets'];tb=Fraction(expected['streams'][0]['time_base']);assert len(samples)==len(packets)==600
            for x,y in zip(samples,packets):
                assert x['sha256']==y['data_hash'].split(':')[1].lower(),(x,y)
                for left,right in [('pts','pts'),('dts','dts'),('end',None)]:
                    actual=Fraction(x[left],10000000);want=(int(y['pts'])+int(y['duration']))*tb if right is None else int(y[right])*tb
                    assert abs(actual-want)<=Fraction(1,10000000),(x,y)
                assert x['sync']==('K' in y['flags'])
            c('fragmentClose');state=c('status');assert c('retire')['removed'];r(state);return dict(samples=600,exactPacketSha256=True,clockTolerance100ns=1)
        case('classic-fragment-view-packets-clocks-keyframes',fragment_view)
        def mpv_wait_stop(folder,c,w,p,n,f,r):
            n();c('mpv',images='');time.sleep(.15);assert not c('mpvStatus')['ended']
            v=c('mpvDestroy');assert v['seconds']<2,v;state=c('status');assert state['activeReaders']==0,state
            f('cancelled');assert c('retire')['removed'];r(state);return v
        case('libmpv-destroy-during-empty-output-wait',mpv_wait_stop)
        def mpv_growing(folder,c,w,p,n,f,r):
            n();images=folder/'images';images.mkdir();p(payload[:cuts[3]]);c('mpv',images=str(images));state=w('mpvStatus',lambda s:s['loaded']);assert state['codec'],state
            until=time.monotonic()+5
            while len(list(images.glob('*.png')))<50:
                assert time.monotonic()<until;time.sleep(.02)
            time.sleep(.2);state=c('mpvStatus');before=len(list(images.glob('*.png')));assert not state['ended'] and 0<before<600,state
            c('mpvCommand',args=['set','pause','yes']);p(payload[cuts[3]:cuts[9]]);time.sleep(.15);paused=len(list(images.glob('*.png')));time.sleep(.15);assert len(list(images.glob('*.png')))==paused
            c('mpvCommand',args=['set','pause','no']);p(payload[cuts[9]:]);time.sleep(.2);assert not c('mpvStatus')['ended']
            f();state=w('mpvStatus',lambda s:s['ended']);assert state['reason']==0 and state['error']==0,state;c('mpvDestroy')
            hashes=[sha(path) for path in sorted(images.glob('*.png'))];assert hashes==reference_hashes,(len(hashes),len(reference_hashes))
            state=c('status');assert state['activeReaders']==0 and c('retire')['removed'];count=r(state)
            return dict(frames=600,decodedPngSequenceEqualsCompletedSource=True,framesBeforeComplete=before,manualPausePreserved=True,receiptCount=count)
        case('libmpv-growing-fragments-pause-resume-exact-frames',mpv_growing)
        def managed(folder,c,w,p,n,f,r):
            path=folder/'input.part';value=snapshot(path,raw,complete=False,prefix=4);c('start',directory=str(folder),root=str(folder/'cache'),session=value)
            images=folder/'images';images.mkdir();c('mpv',images=str(images));s=w('status',lambda s:s['sourceCode']=='waiting');pid=s['workerId']
            def publish(count,complete=False):
                nonlocal value
                previous=copy.deepcopy(value)
                with path.open('ab') as file:file.write(raw[file.tell():count]);file.flush();os.fsync(file.fileno())
                value=copy.deepcopy(value);value['revision']=str(int(value['revision'])+1);state='complete' if complete else 'downloading';value['writer']['state']=state;value['resources'][0].update(state=state,ranges=[dict(offset='0',length=str(count))]);value['tracks'][0]['state']=state;value=protect_snapshot(value,folder,previous=previous);c('publish',session=value)
            publish(len(raw)//3);s=w('status',lambda s:s['sourceCode']=='waiting' and s['visible']>init);assert s['workerId']==pid
            w('mpvStatus',lambda s:s['loaded']);until=time.monotonic()+5
            while not list(images.glob('*.png')):
                assert time.monotonic()<until;time.sleep(.02)
            assert not c('mpvStatus')['ended'];before=len(list(images.glob('*.png')))
            c('fragmentOpen');partial=c('fragments')['samples'];assert 0<len(partial)<600
            publish(len(raw)*2//3);w('status',lambda s:s['sourceCode']=='waiting' and s['visible']>cuts[8]);publish(len(raw));time.sleep(.2);assert not c('mpvStatus')['ended']
            publish(len(raw),True);s=w('status',lambda s:s['done']);assert s['workerId']==pid and s['complete'] and s['sourceCode']=='data',s
            end=w('mpvStatus',lambda s:s['ended']);assert end['reason']==0 and end['error']==0,end;c('mpvDestroy')
            hashes=[sha(path) for path in sorted(images.glob('*.png'))];assert hashes==reference_hashes,(len(hashes),len(reference_hashes));assert len(c('fragments')['samples'])==600
            assert not c('retire')['removed'];c('fragmentClose');state=c('status');assert c('retire')['removed'];count=r(state);c('drop');assert c('collect')['bytes']==0;assert sha(path)==original_sha
            return dict(singleWorkerPid=pid,frames=600,decodedPngSequenceEqualsCompletedSource=True,framesBeforeComplete=before,partialClassicSamples=len(partial),receiptCount=count,originalUnchanged=True)
        case('managed-resource-remux-libmpv-and-classic-view',managed)
        assert sha(original)==original_sha
    result=dict(result='PASS',count=len(rows),rows=rows,exactDriverExits=exits,temporaryWorkRemoved=not root.exists(),runnerSha256=sha(runner),workerSha256=sha(worker),testSourceSha256=SOURCE_SHA256,realLibmpvDecode=True,classicFragmentParserOnly=True,installedDirectShowGraphTested=False,publicPlayerCapability=False)
    report.mkdir(parents=True,exist_ok=True);(report/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(result='PASS',count=len(rows))))
if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('--runner',type=Path,required=True);a.add_argument('--worker',type=Path,required=True);a.add_argument('--report-directory',type=Path,required=True);v=a.parse_args();run(v.runner.resolve(),v.worker.resolve(),v.report_directory.resolve())
