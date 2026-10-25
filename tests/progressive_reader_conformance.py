"""Real cross-process leases, locked files, abrupt exit and conservative recovery."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def run(runner):
    processes=[];checks=[]
    with tempfile.TemporaryDirectory(prefix='foxicmp-reader-runtime-') as folder:
        base=Path(folder);root=base/'readers';media=base/'media.bin';media.write_bytes(b'original-media')
        session='a'*32
        def query(action,**fields):
            result=subprocess.run([runner,'--request',str(root)],input=json.dumps(dict(version=1,action=action,**fields))+'\n',capture_output=True,text=True,timeout=10)
            assert result.returncode==0,result.stderr
            return json.loads(result.stdout)
        def hold():
            child=subprocess.Popen([runner,'--hold',str(root),session,str(media)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            processes.append(child);line=child.stdout.readline()
            assert line,child.stderr.read()
            return child,json.loads(line)['readerId']
        def signal(child,command):
            child.stdin.write(command+'\n');child.stdin.flush()
            if command!='quit':assert json.loads(child.stdout.readline())['ok']
        def receipt(reader):return query('readerStatus',readerId=reader)['reader']
        try:
            one,id1=hold();two,id2=hold()
            assert id1!=id2 and not receipt(id1)['released'] and not receipt(id2)['released'];checks.append('two-independent-processes')
            assert query('readerForget',readerId=id1)['code']=='reader_in_use';checks.append('live-reader-cannot-be-forgotten')
            signal(one,'abandon');assert receipt(id1)['reason']=='indeterminate' and not receipt(id1)['released'];checks.append('unlocked-alive-is-not-release')
            one.kill();one.wait(timeout=10)
            assert receipt(id1)['released'] and receipt(id1)['reason']=='process-exited';checks.append('abrupt-process-exit-recovered')
            assert not receipt(id2)['released'];checks.append('one-dead-reader-does-not-release-another')
            signal(two,'close');assert two.poll() is None and receipt(id2)['released'] and receipt(id2)['reason']=='files-closed';checks.append('closed-handles-release-while-process-lives')
            assert len(query('readers',sessionId=session,generation='1')['readers'])==2;checks.append('fresh-process-discovers-persisted-receipts')
            record=root/(id1+'.lease');saved=record.read_bytes();record.write_text('{broken')
            assert query('readerStatus',readerId=id1)['ok'] is False;record.write_bytes(saved);checks.append('corrupt-receipt-is-not-release')
            assert query('readerStatus',readerId='../escape')['ok'] is False;checks.append('path-escape-rejected')
            if os.name!='nt':
                alias=root/('f'*32+'.lease');alias.symlink_to(media)
                assert query('readerStatus',readerId='f'*32)['ok'] is False;alias.unlink()
                root.chmod(0o755);assert query('readerStatus',readerId=id1)['ok'] is False;root.chmod(0o700);checks.append('private-directory-and-no-symlinks')
            extra=[]
            for _ in range(17):
                child,reader=hold();extra.append(reader);signal(child,'close');signal(child,'quit');child.wait(timeout=10)
            page=query('readers',sessionId=session,generation='1');assert len(page['readers'])==16 and page['hasMore']
            next_page=query('readers',sessionId=session,generation='1',afterReaderId=page['nextReaderId']);assert len(next_page['readers'])==3 and not next_page['hasMore'];checks.append('bounded-pagination')
            assert query('readers',sessionId='b'*32,generation='1')['readers']==[];checks.append('session-isolation')
            for reader in [id1,id2]+extra:assert query('readerForget',readerId=reader)['forgotten']
            assert query('readerStatus',readerId=id1)['code']=='unknown_reader';checks.append('forgotten-is-unknown')
            assert media.read_bytes()==b'original-media';checks.append('media-never-modified-or-deleted')
        finally:
            for child in processes:
                if child.poll() is None:child.kill()
                child.wait(timeout=10)
                for stream in (child.stdin,child.stdout,child.stderr):stream.close()
    return dict(passed=len(checks),checks=checks,exitCode=0)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True);args=parser.parse_args()
    print(json.dumps(run(args.runner)))
