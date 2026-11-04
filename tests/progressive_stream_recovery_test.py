"""Cache restart recovery: real owner death, reader proofs, journal/path quarantine."""
import argparse,copy,hashlib,json,os,shutil,subprocess,tempfile,time,uuid
from pathlib import Path
from progressive_mux_ts_conformance import Rpc
from progressive_stream_remux_worker_test import fixture
from progressive_backend_demux_conformance import snapshot
SOURCE_SHA256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
FTYP=bytes.fromhex('000000186674797069736f360000020069736f366d703431')


def run(runner,worker,reader_runner,report):
    rows=[];exits=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-stream-recovery-') as temp:
        base=Path(temp);original=fixture(base,'source');raw=original.read_bytes();original_sha=hashlib.sha256(raw).hexdigest()
        def rpc(reg):return Rpc([str(runner),str(reg),str(worker)])
        def ask(peer,action,**fields):
            value=peer(action,**fields);assert value['ok'],value;return value
        def kill(peer):
            peer.p.kill();peer.p.wait(timeout=5);code=peer.p.returncode;assert code==(-9 if os.name!='nt' else 1),code
            exits.append(dict(kind='forced-owner-kill',exitCode=code));peer.p.stdin.close();peer.p.stdout.close();peer.p.stderr.close();return code
        def close(peer):peer.close();exits.append(dict(kind='fixture-driver',exitCode=peer.p.returncode))
        def recover(root,registry):
            peer=rpc(registry)
            try:return peer('recover',root=str(root))
            finally:close(peer)
        def make(name,*,managed=False):
            folder=base/name;folder.mkdir();root=folder/'cache';registry=folder/'receipts';peer=rpc(registry);session=uuid.uuid4().hex
            if managed:
                source=folder/'input.part';value=snapshot(source,raw,complete=False,prefix=len(raw)//3)
                ask(peer,'start',directory=str(folder),root=str(root),session=value);session=value['sessionId']
                until=time.monotonic()+10
                while True:
                    state=ask(peer,'status')
                    if state['sourceCode']=='waiting' and state['fragments']>0:break
                    assert time.monotonic()<until,state;time.sleep(.01)
            else:
                ask(peer,'store',root=str(root),sessionId=session);ask(peer,'append',offset='0',hex=FTYP.hex());state=ask(peer,'status')
            path=Path(state['path']);journal=path.with_name('journal.json');record=json.loads(journal.read_text());assert record['outputBirth'] and record['journalBirth'],record
            return dict(folder=folder,root=root,registry=registry,peer=peer,path=path,journal=journal,record=record,state=state,session=session)
        def private_file(case,path,text):
            helper=rpc(case['registry'])
            try:ask(helper,'privateFile',path=str(path),text=text)
            finally:close(helper)
        def signed_record(record):
            value=copy.deepcopy(record);value.pop('sha256',None);encoded=json.dumps(value,sort_keys=True,separators=(',',':')).encode();value['sha256']=hashlib.sha256(encoded).hexdigest();return json.dumps(value)
        def dead(name,**fields):
            case=make(name,**fields);case['ownerExitCode']=kill(case['peer']);return case
        def intact(case):return {str(p.relative_to(case['folder'])):hashlib.sha256(p.read_bytes()).hexdigest() for p in case['folder'].rglob('*') if p.is_file() and not p.is_symlink()}
        def rejected(case,expected=None):
            before=intact(case);value=recover(case['root'],case['registry']);assert value.get('ok') and not value['ready'] and value['removed']==0,value
            if expected is not None:assert value['entries'][0]['code'] in expected,value
            # A registry guard may be created, but cache bytes and user files stay intact.
            after=intact(case)
            for name,digest in before.items():assert after.get(name)==digest,(name,value)
            return value
        def removed(case):
            value=recover(case['root'],case['registry']);assert value['ok'] and value['ready'] and value['removed']==1 and not case['path'].parent.exists(),value;return value
        case=make('live-locked')
        try:
            value=rejected(case,{'cache-owner-active'});rows.append(dict(profile='live-owner-locked',result='PASS',recovery=value))
        finally:kill(case['peer'])
        removed(case)
        # An unlocked journal is not proof of process exit.
        case=make('live-unlocked');ask(case['peer'],'drop')
        try:value=rejected(case,{'cache-owner-alive'});rows.append(dict(profile='live-owner-unlocked',result='PASS',recovery=value))
        finally:kill(case['peer'])
        removed(case)
        case=dead('owner-crash');before=case['path'].read_bytes();value=removed(case);rows.append(dict(profile='real-owner-crash',result='PASS',ownerExitCode=case['ownerExitCode'],cacheBytes=len(before),recovery=value))
        # Actual managed FFmpeg/cache owner dies while waiting for more input.
        case=dead('managed-owner-crash',managed=True);value=removed(case);assert (case['folder']/'input.part').read_bytes()==raw[:len(raw)//3]
        rows.append(dict(profile='managed-owner-crash',result='PASS',ownerExitCode=case['ownerExitCode'],sourceReaderIds=case['state']['sourceReaderIds'],recovery=value))
        # An independent output reader survives owner death under a different session.
        case=make('external-output',managed=True)
        reader=subprocess.Popen([str(reader_runner),'--hold',str(case['registry']),uuid.uuid4().hex,str(case['path'])],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        reader_id=json.loads(reader.stdout.readline())['readerId'];kill(case['peer'])
        try:
            pending=rejected(case,{'cache-reader-active'});reader.stdin.write('close\n');reader.stdin.flush();assert json.loads(reader.stdout.readline())['ok'];value=removed(case)
            rows.append(dict(profile='external-output-reader-survives',result='PASS',readerId=reader_id,pending=pending,recovery=value))
        finally:
            reader.stdin.write('quit\n');reader.stdin.flush();reader.stdin.close();reader.wait(timeout=5);assert reader.returncode==0,reader.stderr.read();reader.stdout.close();reader.stderr.close();exits.append(dict(kind='external-reader',exitCode=reader.returncode))
        # Conservative source-session group, even when the output itself is unused.
        case=make('source-reader');source=case['folder']/'original.bin';source.write_bytes(b'ORIGINAL')
        reader=subprocess.Popen([str(reader_runner),'--hold',str(case['registry']),case['session'],str(source)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        reader_id=json.loads(reader.stdout.readline())['readerId'];kill(case['peer'])
        try:
            pending=rejected(case,{'cache-reader-active'});reader.stdin.write('close\n');reader.stdin.flush();assert json.loads(reader.stdout.readline())['ok'];value=removed(case);assert source.read_bytes()==b'ORIGINAL'
            rows.append(dict(profile='source-group-reader-survives',result='PASS',readerId=reader_id,pending=pending,recovery=value))
        finally:
            reader.stdin.write('quit\n');reader.stdin.flush();reader.stdin.close();reader.wait(timeout=5);assert reader.returncode==0,reader.stderr.read();reader.stdout.close();reader.stderr.close();exits.append(dict(kind='source-reader',exitCode=reader.returncode))
        case=dead('pid-reused');record=copy.deepcopy(case['record']);record['pid']=os.getpid();case['journal'].write_text(signed_record(record));value=removed(case)
        rows.append(dict(profile='pid-reused-birth-proof',result='PASS',recovery=value))
        case=make('forged-birth-live');record=copy.deepcopy(case['record']);record['birth']='not-the-owner-birth';case['journal'].write_text(signed_record(record))
        try:value=rejected(case,{'cache-owner-active'});rows.append(dict(profile='birth-change-never-bypasses-lock',result='PASS',recovery=value))
        finally:kill(case['peer'])
        case['journal'].write_text(json.dumps(case['record']));removed(case)
        for name,mutate,expected in [
            ('corrupt-json',lambda c:c['journal'].write_text('{broken'),None),
            ('oversized-json',lambda c:c['journal'].write_bytes(b'x'*16385),None),
            ('missing-journal',lambda c:c['journal'].unlink(),None),
            ('unexpected-entry',lambda c:(c['path'].parent/'foreign.txt').write_bytes(b'USER'),{'cache-unexpected-entry'}),
            ('bad-checksum',lambda c:c['journal'].write_text(json.dumps(c['record']|{'sessionId':uuid.uuid4().hex})),{'cache-journal-integrity'}),
            ('wrong-version',lambda c:c['journal'].write_text(signed_record(c['record']|{'version':2})),{'cache-journal-invalid'}),
            ('wrong-output-id',lambda c:c['journal'].write_text(signed_record(c['record']|{'outputFileId':'0'})),{'cache-output-replaced'}),
            ('wrong-output-birth',lambda c:c['journal'].write_text(signed_record(c['record']|{'outputBirth':'wrong'})),{'cache-output-replaced'}),
            ('wrong-folder-id',lambda c:c['journal'].write_text(signed_record(c['record']|{'directoryFileId':'0'})),{'cache-directory-replaced'}),
            ('wrong-registry-id',lambda c:c['journal'].write_text(signed_record(c['record']|{'registryFileId':'0'})),{'cache-registry-replaced'}),
            ('output-grew-beyond-cap',lambda c:c['path'].open('r+b').truncate(67108865),{'cache-output-size-changed'}),
        ]:
            case=dead(name);mutate(case);value=rejected(case,expected);rows.append(dict(profile=name,result='PASS',recovery=value))
        case=dead('replace-output');old=case['path'].read_bytes();case['path'].unlink();private_file(case,case['path'],'FOREIGN');value=rejected(case,{'cache-output-replaced'});assert case['path'].read_bytes()==b'FOREIGN';rows.append(dict(profile='replacement-output-kept',result='PASS',recovery=value))
        case=dead('replace-journal');saved=case['journal'].read_bytes();case['journal'].unlink();private_file(case,case['journal'],saved.decode());value=rejected(case,{'cache-journal-replaced'});rows.append(dict(profile='replacement-journal-kept',result='PASS',recovery=value))
        case=dead('metadata-only');case['path'].unlink();value=removed(case);assert value['entries'][0]['code']=='removed-metadata-only';rows.append(dict(profile='cleanup-interrupted-after-output-unlink',result='PASS',recovery=value))
        case=dead('registry-mismatch');other=case['folder']/'other-registry';guard=rpc(other);ask(guard,'holdGuard');ask(guard,'releaseGuard');close(guard)
        value=recover(case['root'],other);assert not value['ready'] and value['entries'][0]['code']=='cache-registry-replaced' and case['path'].exists();rows.append(dict(profile='other-registry-is-not-empty-proof',result='PASS',recovery=value))
        removed(case)
        case=dead('registry-busy');guard=rpc(case['registry']);ask(guard,'holdGuard')
        try:value=rejected(case,{'reader_registry_busy'});rows.append(dict(profile='registry-contention',result='PASS',recovery=value))
        finally:ask(guard,'releaseGuard');close(guard)
        removed(case)
        # A replacement empty registry must never authorize live-owner deletion.
        case=make('live-registry-replaced');external=None;reader_id=None
        try:
            if os.name!='nt':
                external=subprocess.Popen([str(reader_runner),'--hold',str(case['registry']),uuid.uuid4().hex,str(case['path'])],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                reader_id=json.loads(external.stdout.readline())['readerId']
            ask(case['peer'],'producerClosed',error='cancelled');old_registry=case['folder']/'old-receipts';case['registry'].rename(old_registry)
            helper=rpc(case['registry']);ask(helper,'holdGuard');ask(helper,'releaseGuard');close(helper)
            before=case['path'].read_bytes();denied=ask(case['peer'],'retire');state=ask(case['peer'],'status')
            assert not denied['removed'] and case['path'].exists() and state['cleanupCode']=='cache-registry-replaced',dict(retire=denied,state=state,externalReaderId=reader_id)
            assert case['path'].read_bytes()==before
            if external:
                external.stdin.write('close\n');external.stdin.flush();assert json.loads(external.stdout.readline())['ok']
            shutil.rmtree(case['registry']);old_registry.rename(case['registry']);assert ask(case['peer'],'retire')['removed'];ask(case['peer'],'drop')
            rows.append(dict(profile='live-cleanup-rejects-replaced-registry',result='PASS',denied=denied,cleanupCode=state['cleanupCode'],externalReaderId=reader_id))
        finally:
            close(case['peer'])
            if external:
                external.stdin.write('quit\n');external.stdin.flush();external.stdin.close();external.wait(timeout=5);assert external.returncode==0,external.stderr.read();external.stdout.close();external.stderr.close();exits.append(dict(kind='external-reader',exitCode=external.returncode))
        case=make('live-admission-registry');ask(case['peer'],'drop');pool=case['folder']/'pool-cache';ask(case['peer'],'ownerStore',root=str(pool),sessionId=uuid.uuid4().hex)
        old_registry=case['folder']/'old-pool-receipts';case['registry'].rename(old_registry);helper=rpc(case['registry']);ask(helper,'holdGuard');ask(helper,'releaseGuard');close(helper)
        try:
            denied=case['peer']('ownerStore',root=str(pool),sessionId=uuid.uuid4().hex);assert not denied['ok'] and denied['code']=='cache-registry-replaced',denied
            rows.append(dict(profile='live-admission-rejects-replaced-registry',result='PASS',denied=denied))
        finally:
            shutil.rmtree(case['registry']);old_registry.rename(case['registry']);ask(case['peer'],'producerClosed',error='cancelled');assert ask(case['peer'],'ownerRetire')['removed'];ask(case['peer'],'drop');close(case['peer'])
        # New service admission recovers old cache before allocating a fresh store.
        case=dead('auto-admission');peer=rpc(case['registry'])
        try:
            value=ask(peer,'ownerStore',root=str(case['root']),sessionId=uuid.uuid4().hex);assert not case['path'].exists();fresh=Path(ask(peer,'status')['path']);ask(peer,'producerClosed',error='cancelled');assert ask(peer,'ownerRetire')['removed'];ask(peer,'drop')
            rows.append(dict(profile='new-owner-recovers-before-create',result='PASS',oldCacheRemoved=True,newCacheRemoved=not fresh.exists()))
        finally:close(peer)
        case=make('admission-with-reader',managed=True)
        reader=subprocess.Popen([str(reader_runner),'--hold',str(case['registry']),uuid.uuid4().hex,str(case['path'])],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);reader_id=json.loads(reader.stdout.readline())['readerId'];kill(case['peer']);peer=rpc(case['registry'])
        try:
            denied=peer('ownerStore',root=str(case['root']),sessionId=uuid.uuid4().hex);assert not denied['ok'] and denied['code']=='cache-recovery-required',denied
            denied_other=peer('ownerStore',root=str(case['folder']/'new-root'),sessionId=uuid.uuid4().hex);assert not denied_other['ok'] and denied_other['code']=='cache-recovery-required',denied_other
            reader.stdin.write('close\n');reader.stdin.flush();assert json.loads(reader.stdout.readline())['ok'];ask(peer,'ownerStore',root=str(case['root']),sessionId=uuid.uuid4().hex);assert not case['path'].exists();ask(peer,'producerClosed',error='cancelled');assert ask(peer,'ownerRetire')['removed'];ask(peer,'drop')
            rows.append(dict(profile='pending-orphan-blocks-fresh-budget',result='PASS',readerId=reader_id,denied=denied,otherRootDenied=denied_other))
        finally:
            close(peer);reader.stdin.write('quit\n');reader.stdin.flush();reader.stdin.close();reader.wait(timeout=5);assert reader.returncode==0,reader.stderr.read();reader.stdout.close();reader.stderr.close();exits.append(dict(kind='external-reader',exitCode=reader.returncode))
        # Root service fence is independent of live per-store journals.
        case=make('service-owner');ask(case['peer'],'drop');ask(case['peer'],'ownerStore',root=str(case['folder']/'service-root'),sessionId=uuid.uuid4().hex);root=case['folder']/'service-root';peer=rpc(case['registry'])
        try:
            value=peer('ownerStore',root=str(root),sessionId=uuid.uuid4().hex);assert not value['ok'] and value['code']=='cache-owner-active',value;rows.append(dict(profile='single-service-owner-root',result='PASS',denied=value))
        finally:close(peer);kill(case['peer'])
        # This root contains valid tracked data from the killed owner.
        value=recover(root,case['registry']);assert value['ready'] and value['removed']==1,value
        if os.name!='nt':
            # Windows symlink creation requires extra privileges, never requested here.
            for target in ('output','journal'):
                case=dead('symlink-'+target);path=case['path'] if target=='output' else case['journal'];saved=path.read_bytes();foreign=case['folder']/('external-'+target);foreign.write_bytes(saved);foreign.chmod(0o600);path.unlink();path.symlink_to(foreign)
                value=rejected(case);assert foreign.read_bytes()==saved and path.is_symlink();rows.append(dict(profile='symlink-'+target,result='PASS',recovery=value))
        case=dead('journal-hardlink');linked=case['folder']/'journal-alias';os.link(case['journal'],linked);value=rejected(case);assert linked.exists();rows.append(dict(profile='hardlinked-journal',result='PASS',recovery=value))
        case=dead('scan-limit');
        for i in range(128):(case['root']/('remux-'+format(i,'032x'))).mkdir(mode=0o700)
        before=intact(case);value=recover(case['root'],case['registry']);assert not value['ok'] and value['code']=='cache-recovery-scan-limit',value
        for name,digest in before.items():assert intact(case)[name]==digest
        rows.append(dict(profile='bounded-recovery-scan',result='PASS',denied=value))
        assert hashlib.sha256(original.read_bytes()).hexdigest()==original_sha
    result=dict(result='PASS',count=len(rows),rows=rows,exactProcessExits=exits,internalRecoveryOnly=True,publicPlayerCapability=False,temporaryWorkRemoved=not base.exists(),runnerSha256=hashlib.sha256(runner.read_bytes()).hexdigest(),workerSha256=hashlib.sha256(worker.read_bytes()).hexdigest(),testSourceSha256=SOURCE_SHA256)
    report.mkdir(parents=True,exist_ok=True);(report/'result.json').write_text(json.dumps(result,indent=2));return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);p.add_argument('--worker',type=Path,required=True);p.add_argument('--reader-runner',type=Path,required=True);p.add_argument('--report-directory',type=Path,required=True);a=p.parse_args();print(json.dumps(run(a.runner.resolve(),a.worker.resolve(),a.reader_runner.resolve(),a.report_directory.resolve())))
