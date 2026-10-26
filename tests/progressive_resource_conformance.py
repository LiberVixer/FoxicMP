"""Native v2 range I/O: actual bytes, waits, publication, identity and leases."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def snapshot(name='media.part',ranges=((0,4),),length=16):
    return dict(schemaVersion=2,sessionId='a'*32,generation='1',revision='1',
        writer=dict(instanceId='b'*32,state='downloading'),
        timeline=dict(timebase=dict(numerator=1,denominator=1000),live=False,duration='1000',timestampOffset='0'),
        resources=[dict(id='media',name=name,length=None if length is None else str(length),ranges=[dict(offset=str(a),length=str(b)) for a,b in ranges],state='downloading')],
        tracks=[dict(id='video',kind='video',required=True,codecHint='',containerHint='',state='downloading',initializationReady=True,source=dict(resourceId='media'))])


class Fixture:
    def __init__(self,runner,base):
        self.base=base;self.data=base/'Данные';self.data.mkdir();self.media=self.data/'media.part';self.media.write_bytes(b'0123456789abcdef')
        self.process=subprocess.Popen([runner,'--registry',str(base/'registry')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
    def rpc(self,action,**fields):
        self.process.stdin.write(json.dumps(dict(action=action,**fields))+'\n');self.process.stdin.flush()
        line=self.process.stdout.readline();assert line,self.process.stderr.read()
        return json.loads(line)
    def open(self,value):
        answer=self.rpc('open',directory=str(self.data),session=value);assert answer['ok'],answer
    def read(self,offset,count,**fields):return self.rpc('read',resourceId='media',offset=str(offset),length=str(count),**fields)
    def result(self):
        deadline=time.monotonic()+6
        while time.monotonic()<deadline:
            value=self.rpc('result')
            if not value.get('pending'):return value
            time.sleep(.01)
        raise AssertionError('Read did not finish')
    def close(self):
        self.process.stdin.close()
        try:self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:self.process.kill();self.process.wait();raise
        error=self.process.stderr.read();self.process.stdout.close();self.process.stderr.close();assert self.process.returncode==0,error


def run(runner):
    names=[]
    def case(name,function):
        with tempfile.TemporaryDirectory(prefix='foxicmp-resource-') as folder:
            fixture=Fixture(runner,Path(folder))
            try:function(fixture);names.append(name)
            finally:fixture.close()
    def holes(f):
        f.open(snapshot(ranges=((0,4),(8,4))))
        assert f.read(0,16)==dict(ok=True,code='data',bytesRead=4,hex=b'0123'.hex())
        hole=f.read(4,4);assert hole['code']=='waiting-range' and hole['bytesRead']==0 and hole['hex']==''
        assert f.read(8,8)['hex']==b'89ab'.hex()
        assert f.read(12,4)['code']=='waiting-range'
    case('short-reads-stop-at-unconfirmed-holes',holes)
    def adjacent(f):
        f.open(snapshot(ranges=((0,4),(4,4))));assert f.read(2,8)['hex']==b'234567'.hex()
    case('adjacent-confirmed-ranges-merge',adjacent)
    def wake(f):
        value=snapshot();f.open(value)
        f.rpc('beginRead',resourceId='media',offset='4',length='4',waitMilliseconds=5000)
        assert f.rpc('result').get('pending')
        next_value=copy.deepcopy(value);next_value['revision']='2';next_value['resources'][0]['ranges']=[dict(offset='0',length='8')]
        assert f.rpc('publish',session=next_value)['ok'];assert f.result()['hex']==b'4567'.hex()
    case('publication-wakes-a-blocked-reader',wake)
    def timeout(f):
        f.open(snapshot());start=time.monotonic();result=f.read(4,4,waitMilliseconds=80)
        assert result['code']=='waiting-range' and result['bytesRead']==0 and time.monotonic()-start<1
    case('timeout-is-waiting-never-eof',timeout)
    def cancellation(f):
        f.open(snapshot());f.rpc('beginRead',resourceId='media',offset='4',length='4',waitMilliseconds=5000)
        start=time.monotonic();f.rpc('cancel');assert f.result()['code']=='cancelled' and time.monotonic()-start<.75
    case('cancel-interrupts-range-wait',cancellation)
    def paused(f):
        value=snapshot();value['writer']['state']='paused';value['resources'][0]['state']='paused';f.open(value)
        assert f.read(4,4)['code']=='waiting-range'
    case('paused-writer-is-not-eof',paused)
    def unknown(f):
        f.open(snapshot(length=None));assert f.read(16,4)['code']=='waiting-range'
    case('unknown-length-is-not-eof',unknown)
    def complete(f):
        value=snapshot();f.open(value);done=copy.deepcopy(value);done['revision']='2';done['writer']['state']='complete';done['resources'][0].update(state='complete',ranges=[dict(offset='0',length='16')]);done['tracks'][0]['state']='complete'
        assert f.rpc('publish',session=done)['ok'];assert f.read(14,4)['hex']==b'ef'.hex();assert f.read(16,4)['code']=='eof';assert f.read(17,1)['code']=='out-of-range'
    case('only-complete-resource-has-eof',complete)
    def missing(f):
        f.media.unlink();value=snapshot(ranges=());f.open(value);assert f.read(0,4)['code']=='waiting-resource'
        f.media.write_bytes(b'0123456789abcdef');value['revision']='2';value['resources'][0]['ranges']=[dict(offset='0',length='4')]
        assert f.rpc('publish',session=value)['ok'];assert f.read(0,4)['hex']==b'0123'.hex()
    case('late-resource-appears-and-binds',missing)
    def absent_complete(f):
        f.media.unlink();value=snapshot(ranges=((0,16),));value['resources'][0]['state']='complete';value['tracks'][0]['state']='complete';value['writer']['state']='complete';f.open(value)
        assert f.read(0,4)['code']=='missing-resource'
    case('missing-complete-resource-is-error-not-eof',absent_complete)
    def rename(f):
        value=snapshot();f.open(value);assert f.read(0,4)['hex']==b'0123'.hex();final=f.media.with_suffix('.done');f.media.rename(final)
        f.media.write_bytes(b'XXXXXXXXXXXXXXXX');value['revision']='2';value['resources'][0]['ranges']=[dict(offset='0',length='8')]
        assert f.rpc('publish',session=value)['ok'];assert f.read(4,4)['hex']==b'4567'.hex()
    case('rename-and-path-replacement-keep-pinned-file',rename)
    def truncate(f):
        f.open(snapshot());assert f.read(0,4)['code']=='data';f.media.write_bytes(b'0');assert f.read(0,4)['code']=='source-truncated'
    case('truncation-invalidates-confirmed-bytes',truncate)
    def rewrite(f):
        f.open(snapshot());assert f.read(0,4)['code']=='data'
        with f.media.open('r+b') as stream:stream.write(b'XXXX')
        assert f.read(0,4)['code']=='source-rewritten'
    case('rewrite-of-last-read-window-detected',rewrite)
    def invalid_update(f):
        value=snapshot();f.open(value);bad=copy.deepcopy(value);bad['revision']='2';bad['generation']='2'
        assert f.rpc('publish',session=bad)['code']=='generation_changed'
        assert f.read(0,4)['code']=='data';assert f.rpc('publish',session=value)['code']=='stale_revision';assert f.read(0,4)['code']=='data'
    case('invalid-publication-preserves-current-source',invalid_update)
    def writer_failure(f):
        value=snapshot();f.open(value);value['revision']='2';value['writer']['state']='failed';assert f.rpc('publish',session=value)['ok'];assert f.read(0,4)['code']=='writer-failed'
    case('writer-failure-is-not-successful-eof',writer_failure)
    def big(f):
        f.open(snapshot(length=2**53+100));assert f.read(2**53+1,1)['code']=='waiting-range'
        assert f.read(2**63-1,1)['code']=='invalid-read';assert f.read(0,65537)['ok'] is False
    case('exact-large-offsets-and-overflow-bounds',big)
    def sparse(f):
        target=f.data/'large.part';offset=2**32+17
        assert f.rpc('sparseFixture',path=str(target),offset=str(offset))['ok']
        f.open(snapshot(name=target.name,ranges=((offset,1),),length=offset+1));assert f.read(offset,1)['hex']==b'Z'.hex();assert f.read(offset-1,1)['code']=='waiting-range'
    case('native-sparse-read-above-four-gib',sparse)
    def retired(f):
        value=snapshot();value['resources'][0]['retired']=True;f.open(value);assert f.read(0,4)['code']=='resource-retired'
    case('new-reader-rejects-retired-resource',retired)
    def lease(f):
        f.open(snapshot());f.read(0,4);reader=f.rpc('bindings')['readerIds'][0]
        assert not f.rpc('readerStatus',version=1,readerId=reader)['reader']['released']
        f.rpc('retain');f.rpc('release');assert not f.rpc('released')['released']
        f.rpc('dropRetained');assert f.rpc('released')['released'];assert f.rpc('readerStatus',version=1,readerId=reader)['reader']['reason']=='files-closed'
    case('lease-lives-until-last-source-owner',lease)
    def add_resource(f):
        value=snapshot();f.open(value);f.read(0,4);second=f.data/'audio.part';second.write_bytes(b'AAAA')
        value['revision']='2';value['resources'].append(dict(id='second',name=second.name,length='4',ranges=[dict(offset='0',length='4')],state='downloading'))
        assert f.rpc('publish',session=value)['ok'];assert f.rpc('read',resourceId='second',offset='0',length='4')['hex']==b'AAAA'.hex();assert len(f.rpc('bindings')['readerIds'])==2
    case('new-resource-gets-independent-real-file-lease',add_resource)
    def alias(f):
        target=f.data/'alias.part';os.link(f.media,target);f.open(snapshot());assert f.read(0,4)['code']=='unsafe-resource'
    case('hardlink-resource-is-rejected',alias)
    if os.name!='nt':
        def symlink(f):
            target=f.data/'alias.part';target.symlink_to(f.media);f.open(snapshot(name=target.name));assert f.read(0,4)['code']=='unsafe-resource'
        case('symbolic-resource-is-rejected',symlink)
        def directory_link(f):
            link=f.base/'link';link.symlink_to(f.data,target_is_directory=True);reply=f.rpc('open',directory=str(link),session=snapshot());assert not reply['ok'] and reply['code']=='unsafe-resource'
        case('symbolic-session-directory-is-rejected',directory_link)
    def asynchronous(f):
        from progressive_resource_api_conformance import exercise
        from progressive_client import ApiError
        class Client:
            def request(self,message,timeout=5):
                if message['action']=='capabilities':return dict(resourceIoProbeVersion=1,resourceIoPlaybackSupported=False,
                    actions=['resourceProbe','resourceProbeUpdate','resourceProbeStatus','resourceProbeCancel','resourceProbeForget'])
                packet=dict(message);action=packet.pop('action');reply=f.rpc(action,**packet)
                if reply.get('ok') is not True:raise ApiError(reply)
                return reply
            def reader_status(self,reader):return self.request(dict(version=1,action='readerStatus',readerId=reader))['reader']
        result=exercise(Client(),f.base/'async-probe');assert result['passed']==11
    case('asynchronous-probe-worker-public-contract',asynchronous)
    return dict(passed=len(names),checks=names,exitCode=0)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True);args=parser.parse_args();print(json.dumps(run(args.runner)))
