"""Shared SHA-256 I/O contract: corrupt bytes must never reach a reader."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt/tools'))
from progressive_integrity import protect_snapshot
from progressive_resource_conformance import Fixture,snapshot


def run(runner):
    checks=[]
    def case(name,action):
        with tempfile.TemporaryDirectory(prefix='foxicmp-integrity-') as folder:
            f=Fixture(runner,Path(folder))
            try:action(f);checks.append(name)
            finally:f.close()
    def sha(f):
        for size in (0,1,3,55,56,63,64,65,127,128,129,65537,1000000):
            text=('abc123Z\x00'*((size+7)//8))[:size]
            for step in (1,7,63,64,65,65536):
                result=f.rpc('sha256',text=text,step=step)
                assert result['sha256']==hashlib.sha256(text.encode()).hexdigest(),(size,step,result)
    case('sha256-padding-stream-boundaries-and-million-byte-reference',sha)
    def protected(f):return protect_snapshot(snapshot(ranges=((0,16),)),f.data,block_bytes=8)
    def first(f):
        value=protected(f)
        with f.media.open('r+b') as stream:stream.seek(7);stream.write(b'X')
        f.open(value);result=f.read(0,1)
        assert result['code']=='integrity-mismatch' and result['bytesRead']==0 and not result['hex'],result
    case('corrupt-unread-byte-rejected-before-first-block-byte-is-returned',first)
    def valid(f):
        value=protected(f);f.open(value)
        assert f.read(6,8)['hex']==b'6789abcd'.hex()
        assert f.read(0,16)['hex']==b'0123456789abcdef'.hex()
    case('verified-read-crosses-published-block-boundary',valid)
    def large(f):
        data=bytes(range(256))*1024;f.media.write_bytes(data)
        value=protect_snapshot(snapshot(ranges=((0,len(data)),),length=len(data)),f.data)
        f.open(value);assert f.read(65530,20)['hex']==data[65530:65550].hex()
        # Corrupt a different cached 64 KiB chunk, outside the last read.
        with f.media.open('r+b') as stream:stream.seek(200000);stream.write(b'X')
        for _ in range(6):
            result=f.read(100000,1)
            if result['code']=='integrity-mismatch':break
            assert result['code']=='data',result
        else:raise AssertionError('Bounded audit missed a cached chunk')
        assert result['bytesRead']==0 and not result['hex']
    case('cached-chunk-boundary-and-round-robin-audit-of-consumed-bytes',large)
    def append(f):
        value=protect_snapshot(snapshot(),f.data,block_bytes=3);f.open(value);assert f.read(0,4)['hex']==b'0123'.hex()
        raw=copy.deepcopy(value);raw['revision']='2';raw['resources'][0]['ranges']=[dict(offset='0',length='12')]
        next_value=protect_snapshot(raw,f.data,previous=value,block_bytes=5)
        assert next_value['resources'][0]['integrity']['blocks'][:2]==value['resources'][0]['integrity']['blocks']
        assert f.rpc('publish',session=next_value)['ok'];assert f.read(3,9)['hex']==b'3456789ab'.hex()
        before=copy.deepcopy(next_value);before['revision']='3';before['resources'][0]['integrity']['blocks'][0]['sha256']='f'*64
        assert not f.rpc('publish',session=before)['ok'];assert f.read(0,4)['code']=='data'
    case('new-hashes-append-old-digests-immutable-invalid-update-is-atomic',append)
    def sparse(f):
        value=protect_snapshot(snapshot(ranges=((0,4),(8,4))),f.data,block_bytes=3);f.open(value)
        assert f.read(3,10)['hex']==b'3'.hex();assert f.read(4,1)['code']=='waiting-range';assert f.read(8,4)['hex']==b'89ab'.hex()
        raw=copy.deepcopy(value);raw['revision']='2';raw['resources'][0]['ranges']=[dict(offset='0',length='12')]
        next_value=protect_snapshot(raw,f.data,previous=value,block_bytes=3)
        assert f.rpc('publish',session=next_value)['ok'];assert f.read(0,12)['hex']==b'0123456789ab'.hex()
    case('sparse-integrity-holes-can-be-confirmed-later',sparse)
    def late(f):
        value=snapshot(ranges=());f.media.unlink();value=protect_snapshot(value,f.data);f.open(value)
        assert f.read(0,4)['code']=='waiting-resource'
        f.media.write_bytes(b'0123456789abcdef');raw=copy.deepcopy(value);raw['revision']='2';raw['resources'][0]['ranges']=[dict(offset='0',length='16')]
        assert f.rpc('publish',session=protect_snapshot(raw,f.data,previous=value))['ok'];assert f.read(0,4)['code']=='data'
    case('empty-protected-late-resource-does-not-require-a-file',late)
    def srt(f,isolate):
        value=snapshot();sub=f.data/'sub.part';sub.write_bytes(b'ABCD')
        value['resources'].append(dict(id='sub',name=sub.name,length='4',ranges=[dict(offset='0',length='4')],state='complete'))
        value['tracks'].append(dict(id='subtitle',kind='subtitle',required=False,codecHint='srt',containerHint='srt',state='complete',initializationReady=True,source=dict(resourceId='sub')))
        value=protect_snapshot(value,f.data);assert f.rpc('open',directory=str(f.data),session=value,isolateOptionalSrt=isolate)['ok']
        sub.write_bytes(b'XBCD');result=f.rpc('read',resourceId='sub',offset='0',length='4')
        assert result['code']=='integrity-mismatch' and result['bytesRead']==0,result
        assert f.read(0,4)['code']==('data' if isolate else 'integrity-mismatch')
    case('optional-srt-integrity-error-isolated-only-with-explicit-policy',lambda f:srt(f,True))
    case('required-default-policy-propagates-integrity-failure',lambda f:srt(f,False))
    def limits(f):
        value=snapshot();original=copy.deepcopy(value);protect_snapshot(value,f.data);assert value==original
        for size in (0,-1,True,4194305):
            try:protect_snapshot(value,f.data,block_bytes=size)
            except ValueError:pass
            else:raise AssertionError('Invalid block size accepted')
        raw=copy.deepcopy(value);raw['resources'][0]['ranges']=[dict(offset='0',length='17')];raw['resources'][0]['length']=None
        try:protect_snapshot(raw,f.data)
        except ValueError:pass
        else:raise AssertionError('Unwritten confirmed bytes accepted')
    case('writer-helper-keeps-input-and-rejects-invalid-limits-and-short-files',limits)
    return dict(passed=len(checks),checks=checks,exitCode=0)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True);args=parser.parse_args()
    print(json.dumps(run(args.runner)))
