"""Private continuous-remux cache: fragment atomicity, SHA, reader groups, quotas."""
import argparse,copy,hashlib,json,os,subprocess,tempfile,time,uuid
from pathlib import Path
from progressive_mux_ts_conformance import Rpc
from progressive_backend_demux_conformance import snapshot
from progressive_backend_remux_conformance import verify_artifact
from progressive_stream_remux_worker_test import fixture
from progressive_integrity import protect_snapshot
SOURCE_SHA256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()


def run(runner,worker,reader_runner,report):
    rows=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-stream-cache-') as temp:
        root=Path(temp);original=fixture(root,'source');raw=original.read_bytes()
        fragmented=root/'fragmented.mp4'
        subprocess.run(['ffmpeg','-v','error','-y','-i',str(original),'-c','copy','-movflags','empty_moov+frag_keyframe+default_base_moof+negative_cts_offsets',str(fragmented)],check=True,capture_output=True)
        payload=fragmented.read_bytes();atoms=[];at=0
        while at<len(payload):
            n=int.from_bytes(payload[at:at+4],'big');assert n>=8;atoms.append((at,n,payload[at+4:at+8]));at+=n
        assert at==len(payload);init=next(at+n for at,n,t in atoms if t==b'moov');first_mdat=next(at+n for at,n,t in atoms if t==b'mdat')
        registry=root/'registry';rpc=Rpc([str(runner),str(registry),str(worker)]);ids=[]
        def command(action,**fields):
            v=rpc(action,**fields);assert v['ok'],v;return v
        def status():return command('status')
        def wait(predicate,timeout=15):
            until=time.monotonic()+timeout
            while time.monotonic()<until:
                v=status()
                if predicate(v):return v
                time.sleep(.02)
            raise AssertionError(('cache timeout',v))
        def new_store(name,limit=None):
            fields=dict(root=str(root/name),sessionId=uuid.uuid4().hex)
            if limit is not None:fields['limit']=str(limit)
            command('store',**fields);return Path(status()['path'])
        def append(data,at=None):
            if at is None:at=status()['written']
            return rpc('append',offset=str(at),hex=data.hex())
        def push(data):
            for at in range(0,len(data),65536):
                v=append(data[at:at+65536]);assert v['ok'],v
        def add_reader(name):
            rid=command('reader',name=name)['readerId'];ids.append(rid);return rid
        def close_reader(name):command('close',name=name);command('dropReader',name=name)
        def receipts():
            for rid in ids:
                v=command('readerStatus',version=1,readerId=rid)['reader'];assert v['released'] and v['reason']=='files-closed',v
                command('readerForget',version=1,readerId=rid)
            ids.clear()
        def retire():
            v=command('retire');assert v['removed'],status();s=status();assert s['removed'] and not Path(s['path']).exists() and s['budgetBytes']==0,s
            assert not any(s[k] for k in ('videoIndexedEnd100ns','audioIndexedEnd100ns','indexedEnd100ns','audioIndexedComplete')),s
            command('drop');receipts();return s
        def close_producer(error='',data=None,original_readers=None):
            return command('producerClosed',error=error,**({'sha256':hashlib.sha256(data).hexdigest()} if data is not None else {}),**({'originalReaders':original_readers} if original_readers is not None else {}))
        def read_all(name,expected=None):
            data=bytearray();at=0
            while True:
                v=command('read',name=name,offset=str(at),length='65536')
                if v['code']=='eof':break
                assert v['code']=='data' and v['bytes']>0,v;data+=bytes.fromhex(v['hex']);at+=v['bytes']
            if expected is not None:assert data==expected
            return bytes(data)
        try:
            path=new_store('atomic');a=add_reader('a');b=add_reader('b')
            assert not rpc('readerForget',version=1,readerId=a)['ok']
            command('beginRead',name='a',offset='0',length='8',wait=5000)
            cuts={1,7,8,len(payload)}
            for at,n,t in atoms[:8]:cuts.update([at+1,at+7,at+n-1,at+n])
            cuts=sorted(x for x in cuts if 0<x<=len(payload));sent=0;observations=[]
            for cut in cuts:
                while sent<cut:
                    end=min(cut,sent+65536);v=append(payload[sent:end],sent);assert v['ok'],v;sent=end
                s=status();visible=0
                if sent>=init:
                    visible=init
                    for at,n,t in atoms:
                        if t==b'mdat' and at+n<=sent:visible=at+n
                assert s['visible']==visible and not s['complete'],(sent,s,visible)
                assert s['videoIndexedEnd100ns']==s['indexedEnd100ns']==s['samples']*400000,s
                assert not s['audioIndexedEnd100ns'] and not s['audioIndexedComplete'],s
                v=command('read',name='b',offset=str(visible),length='1');assert v['code']=='waiting-range',v
                observations.append(dict(written=sent,visible=visible,indexedEnd100ns=s['indexedEnd100ns']))
            until=time.monotonic()+2
            while True:
                v=command('readResult')
                if not v.get('pending'):break
                assert time.monotonic()<until;time.sleep(.01)
            assert v['code']=='data' and bytes.fromhex(v['hex'])==payload[:8],v
            close_producer(data=payload);s=status();assert s['complete'] and s['visible']==len(payload) and s['samples']==600,s
            assert read_all('a',payload)==read_all('b',payload)
            assert not command('retire')['removed'] and path.exists();close_reader('a');assert path.exists() and not status()['removed'];close_reader('b');s=retire()
            rows.append(dict(profile='atomic-fragments-two-readers',result='PASS',observations=observations,samples=600,receiptCount=2,final=s))
            path=new_store('decoder-registry-guard');push(payload[:first_mdat])
            for mode,code in [('release','data'),('timeout','reader_registry_busy'),('cancel','cancelled')]:
                result=command('decoderGuard',mode=mode);assert result['code']==code,result
                assert (.2 if mode=='timeout' else .04)<=result['seconds']<1,result
                assert status()['activeReaders']==0 and not status()['failure'],status()
                if result['readerId']:
                    ids.append(result['readerId']);receipts()
                rows.append(dict(profile='decoder-guard-'+mode,result='PASS',observation=result))
            close_producer(error='cancelled');retire()
            # Concrete cross-process output lease, even under another session id.
            path=new_store('external-output');push(payload[:init]);close_producer(error='cancelled')
            peer=subprocess.Popen([str(reader_runner),'--hold',str(registry),uuid.uuid4().hex,str(path)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            rid=json.loads(peer.stdout.readline())['readerId'];ids.append(rid)
            try:
                assert not command('retire')['removed'] and path.exists()
                peer.stdin.write('close\n');peer.stdin.flush();assert json.loads(peer.stdout.readline())['ok']
                s=retire();rows.append(dict(profile='external-output-physical-binding',result='PASS',final=s))
            finally:
                peer.stdin.write('quit\n');peer.stdin.flush();peer.stdin.close();peer.wait(timeout=5);assert peer.returncode==0,peer.stderr.read();peer.stdout.close();peer.stderr.close()
            # The original receipt is part of the retirement group as well.
            path=new_store('original-group');push(payload[:init]);input_path=root/'held-original.bin';input_path.write_bytes(b'original')
            peer=subprocess.Popen([str(reader_runner),'--hold',str(registry),uuid.uuid4().hex,str(input_path)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            rid=json.loads(peer.stdout.readline())['readerId'];ids.append(rid)
            try:
                close_producer(error='cancelled',original_readers=[rid]);assert not command('retire')['removed'] and path.exists()
                peer.stdin.write('close\n');peer.stdin.flush();assert json.loads(peer.stdout.readline())['ok'];s=retire();assert input_path.read_bytes()==b'original'
                rows.append(dict(profile='original-receipt-gates-retirement',result='PASS',final=s))
            finally:
                peer.stdin.write('quit\n');peer.stdin.flush();peer.stdin.close();peer.wait(timeout=5);assert peer.returncode==0,peer.stderr.read();peer.stdout.close();peer.stderr.close()
            for name,code in [('truncated-box','cache-incomplete-output'),('rewrite','cache-integrity-mismatch'),('truncate','cache-size-changed'),('producer-error','writer-failed'),('injected-disk-full','cache-disk-full')]:
                path=new_store(name);push(payload[:first_mdat-1] if name=='truncated-box' else payload[:first_mdat]);add_reader('r')
                if name=='rewrite':
                    with path.open('r+b') as f:f.write(b'X');f.flush();os.fsync(f.fileno())
                    assert command('read',name='r',offset='0',length='1')['code']==code
                    close_producer(data=payload[:first_mdat])
                elif name=='truncate':
                    with path.open('r+b') as f:f.truncate(0)
                    assert command('read',name='r',offset='0',length='1')['code']==code;close_producer(error='writer-failed')
                else:close_producer(error='cache-disk-full' if name=='injected-disk-full' else 'writer-failed' if name=='producer-error' else '',data=payload[:first_mdat-1])
                assert status()['failure']==code and not status()['complete'];assert command('read',name='r',offset=str(status()['visible']),length='1')['code']==code
                failed=status();assert not any(failed[k] for k in ('videoIndexedEnd100ns','audioIndexedEnd100ns','indexedEnd100ns','audioIndexedComplete')),failed
                assert not command('retire')['removed'];close_reader('r');s=retire();rows.append(dict(profile=name,result='PASS',expectedCode=code,final=s))
            malformed=[('zero-size',b'\0\0\0\0ftyp'),('oversize',(0xffffffff).to_bytes(4,'big')+b'ftyp'),('unknown-atom',(8).to_bytes(4,'big')+b'junk'),('orphan-mdat',(8).to_bytes(4,'big')+b'mdat'),('moov-without-ftyp',(8).to_bytes(4,'big')+b'moov')]
            for name,data in malformed:
                path=new_store(name);v=append(data);assert not v['ok'] and v['code']=='cache-invalid-fragment',v;close_producer(error='cancelled');s=retire();rows.append(dict(profile=name,result='PASS',expectedCode='cache-invalid-fragment',final=s))
            for name,field,number,code in [('sample-count',8,50001,'cache-sample-limit'),('sample-outside-mdat',12,0x7fffffff,'cache-invalid-fragment')]:
                path=new_store(name);bad=bytearray(payload[:first_mdat]);trun=bad.index(b'trun');bad[trun+field:trun+field+4]=number.to_bytes(4,'big')
                failed=None
                for at in range(0,len(bad),65536):
                    v=append(bad[at:at+65536]);
                    if not v['ok']:failed=v;break
                assert failed and failed['code']==code,failed;close_producer(error='cancelled');s=retire();rows.append(dict(profile=name,result='PASS',expectedCode=code,final=s))
            for name,data,offset,limit,code in [('wrong-offset',payload[:8],1,None,'cache-invalid-append'),('empty',b'',0,None,'cache-invalid-append'),('output-limit',payload[:16],0,10,'cache-output-limit')]:
                path=new_store(name,limit);v=append(data,offset);assert not v['ok'] and v['code']==code,v;close_producer(error='cancelled');s=retire();rows.append(dict(profile=name,result='PASS',expectedCode=code,final=s))
            path=new_store('chunk-limit');assert append((1048576).to_bytes(4,'big')+b'ftyp')['ok'];v=rpc('repeatAppend',count=8192);assert not v['ok'] and v['code']=='cache-chunk-limit',v;assert status()['written']==8199
            close_producer(error='cancelled');s=retire();rows.append(dict(profile='chunk-limit',result='PASS',expectedCode='cache-chunk-limit',final=s))
            command('budget',bytes=str(init*2-1));path=new_store('budget-a');push(payload[:init]);command('holdStore',name='a')
            second=new_store('budget-b');v=append(payload[:init]);assert not v['ok'] and v['code']=='cache-budget-exceeded',v;close_producer(error='cancelled');assert command('retire')['removed'];command('drop')
            command('selectStore',name='a');close_producer(error='cancelled');s=retire();command('dropHeld',name='a');rows.append(dict(profile='shared-budget-two-stores',result='PASS',final=s));command('budget',bytes=str(64*1024*1024))
            path=new_store('path-poison');push(payload[:init]);close_producer(error='cancelled');moved=path.with_name('moved.fmp4');path.rename(moved);path.write_bytes(b'foreign-private-file')
            assert not command('retire')['removed'] and path.read_bytes()==b'foreign-private-file' and moved.exists();failure=status()['cleanupCode'];assert failure=='cache-path-replaced',failure
            path.unlink();moved.rename(path);s=retire();rows.append(dict(profile='replacement-is-not-deleted',result='PASS',final=s))
            path=new_store('owner-drop');push(payload[:first_mdat]);add_reader('last');close_producer(error='cancelled');command('drop')
            assert path.exists();close_reader('last');assert not path.exists() and command('budgetStatus')['bytes']==0;receipts()
            rows.append(dict(profile='owner-drop-retains-local-reader',result='PASS',cacheRemovedAfterLastClose=True))
            path=new_store('reader-limit');reader_names=['limit-'+str(i) for i in range(128)]
            for name in reader_names:add_reader(name)
            failed=rpc('reader',name='limit-129');assert not failed['ok'] and failed['code']=='cache-reader-limit',failed
            close_producer(error='cancelled');assert not command('retire')['removed']
            for name in reader_names:close_reader(name)
            s=retire();rows.append(dict(profile='reader-limit-128',result='PASS',readerReceipts=128,final=s))
            # Actual managed worker -> store -> independent readers during growth.
            folder=root/'growing-source';folder.mkdir();path=folder/'input.part';value=snapshot(path,raw,complete=False,prefix=4)
            command('start',directory=str(folder),root=str(root/'managed-cache'),session=value);add_reader('one');add_reader('two')
            s=wait(lambda s:s['sourceCode']=='waiting');pid=s['workerId'];assert not s['complete']
            for n in (len(raw)//3,len(raw)*2//3,len(raw)):
                with path.open('r+b') as f:f.write(raw[:n]);f.flush();os.fsync(f.fileno())
                previous=copy.deepcopy(value);value['revision']=str(int(value['revision'])+1);value['resources'][0]['ranges']=[dict(offset='0',length=str(n))];value=protect_snapshot(value,folder,previous=previous);command('publish',session=value)
                s=wait(lambda s:s['sourceCode']=='waiting' and s['fragments']>0 and (s['samples']>=int(n/len(raw)*500) or n==len(raw)))
                assert s['workerId']==pid and not s['complete'] and not s['producerClosed'],s
                assert command('read',name='two',offset=str(s['visible']),length='1')['code']=='waiting-range'
            # Closing one reader never removes the other reader or cache.
            close_reader('one');assert status()['activeReaders']==1
            previous=copy.deepcopy(value);value['revision']=str(int(value['revision'])+1);value['writer']['state']='complete';value['resources'][0]['state']='complete';value['tracks'][0]['state']='complete';command('publish',session=value)
            s=wait(lambda s:s.get('done'));assert s['complete'] and s['originalsClosed'] and s['sourceCode']=='data' and s['samples']==600,s
            source_ids=s['sourceReaderIds'];assert source_ids
            for rid in source_ids:
                receipt=command('readerStatus',version=1,readerId=rid)['reader'];assert receipt['released'] and receipt['reason']=='files-closed';ids.append(rid)
            copied=root/'decoded-copy.mp4';copied.write_bytes(read_all('two'));assert hashlib.sha256(copied.read_bytes()).hexdigest()==json.loads(s['sourceResult'])['sha256']
            verified=verify_artifact(original,copied,50000,True);assert not command('retire')['removed'];close_reader('two');final=retire();rows.append(dict(profile='managed-worker-cache-readers',result='PASS',source=s,final=final,**verified))
            assert path.read_bytes()==raw
            for mode,expected in [('worker-crash','backend-worker-exited'),('cache-corruption','cache-integrity-mismatch'),('writer-failed','writer-failed'),('cache-quota','cache-output-limit')]:
                value=snapshot(path,raw,complete=False,prefix=len(raw)//3)
                command('start',directory=str(folder),root=str(root/('managed-'+mode)),session=value,**({'limit':'10'} if mode=='cache-quota' else {}))
                # Even an early quota failure must close its original source.
                if mode!='cache-quota':
                    add_reader('fault');s=wait(lambda s:s['sourceCode']=='waiting' and s['fragments']>0)
                    command('beginRead',name='fault',offset=str(s['visible']),length='1',wait=5000)
                    if mode=='worker-crash':
                        if os.name=='nt':subprocess.run(['taskkill','/PID',str(s['workerId']),'/F'],check=True,capture_output=True)
                        else:os.kill(s['workerId'],9)
                    elif mode=='cache-corruption':
                        with Path(s['path']).open('r+b') as f:f.write(b'X');f.flush();os.fsync(f.fileno())
                        assert command('read',name='fault',offset='0',length='1')['code']==expected
                    else:
                        value['revision']='2';value['writer']['state']='failed';value['resources'][0]['state']='failed';value['tracks'][0]['state']='failed';command('publish',session=value)
                s=wait(lambda s:s.get('done'));assert s['failure']==expected and not s['complete'] and s['originalsClosed'],s
                for rid in s['sourceReaderIds']:ids.append(rid)
                if mode!='cache-quota':
                    until=time.monotonic()+2
                    while True:
                        v=command('readResult')
                        if not v.get('pending'):break
                        assert time.monotonic()<until;time.sleep(.01)
                    assert v['code']==expected,v;close_reader('fault')
                source_state=s;final=retire();rows.append(dict(profile='managed-'+mode,result='PASS',source=source_state,final=final))
            # Retired sessions remain owned while a separate process holds output.
            value=snapshot(path,raw,complete=False,prefix=len(raw)//3);command('start',directory=str(folder),root=str(root/'deferred-owner'),session=value)
            s=wait(lambda s:s['sourceCode']=='waiting' and s['fragments']>0);cache_path=Path(s['path']);source_ids=s['sourceReaderIds']
            peer=subprocess.Popen([str(reader_runner),'--hold',str(registry),uuid.uuid4().hex,str(cache_path)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            rid=json.loads(peer.stdout.readline())['readerId'];ids.append(rid)
            try:
                command('cancel');s=wait(lambda s:s.get('done'));ids+=s['sourceReaderIds'];command('drop')
                pending=command('collect');assert pending['count']==1 and pending['removed']==0 and pending['bytes']>0 and cache_path.exists(),pending
                peer.stdin.write('close\n');peer.stdin.flush();assert json.loads(peer.stdout.readline())['ok']
                cleared=command('collect');assert cleared['removed']==1 and cleared['count']==0 and cleared['bytes']==0 and not cache_path.exists(),cleared;receipts()
                rows.append(dict(profile='retired-session-external-reader-owner-queue',result='PASS',pending=pending,cleared=cleared))
            finally:
                peer.stdin.write('quit\n');peer.stdin.flush();peer.stdin.close();peer.wait(timeout=5);assert peer.returncode==0,peer.stderr.read();peer.stdout.close();peer.stderr.close()
            # Zero-byte stores are bounded as well as total media bytes.
            for i in range(16):
                command('ownerStore',root=str(root/'owner-cap'),sessionId=uuid.uuid4().hex);command('holdStore',name='owner-'+str(i));command('drop')
            denied=rpc('ownerStore',root=str(root/'owner-cap'),sessionId=uuid.uuid4().hex);assert not denied['ok'] and denied['code']=='cache-session-limit',denied
            assert command('collect')['count']==16
            for i in range(16):
                command('selectStore',name='owner-'+str(i));close_producer(error='cancelled');assert command('ownerRetire')['removed'];command('drop');command('dropHeld',name='owner-'+str(i))
            cleared=command('collect');assert cleared['count']==0 and cleared['bytes']==0
            rows.append(dict(profile='owner-session-limit-16',result='PASS',cleared=cleared))
            # Reader wait is woken by actual producer cancellation, no false EOF.
            value=snapshot(path,raw,complete=False,prefix=4);command('start',directory=str(folder),root=str(root/'cancel-cache'),session=value);add_reader('waiter');command('beginRead',name='waiter',offset='0',length='1',wait=5000)
            began=time.monotonic();command('cancel');s=wait(lambda s:s.get('done'));assert s['failure']=='cancelled' and not s['complete'] and time.monotonic()-began<2,s
            until=time.monotonic()+2
            while True:
                v=command('readResult')
                if not v.get('pending'):break
                assert time.monotonic()<until;time.sleep(.01)
            assert v['code']=='cancelled',v;close_reader('waiter');s=retire();rows.append(dict(profile='cancel-wakes-decoder-wait',result='PASS',elapsedSeconds=time.monotonic()-began,final=s))
        finally:rpc.close()
    result=dict(result='PASS',count=len(rows),rows=rows,cacheOnly=True,publicPlayerCapability=False,temporaryWorkRemoved=not root.exists(),runnerSha256=hashlib.sha256(runner.read_bytes()).hexdigest(),workerSha256=hashlib.sha256(worker.read_bytes()).hexdigest(),testSourceSha256=SOURCE_SHA256)
    report.mkdir(parents=True,exist_ok=True);(report/'result.json').write_text(json.dumps(result,indent=2));return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);p.add_argument('--worker',type=Path,required=True);p.add_argument('--reader-runner',type=Path,required=True);p.add_argument('--report-directory',type=Path,required=True);a=p.parse_args();print(json.dumps(run(a.runner.resolve(),a.worker.resolve(),a.reader_runner.resolve(),a.report_directory.resolve())))
