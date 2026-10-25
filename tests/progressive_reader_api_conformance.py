"""Tracked v1 media lifecycle through the public client, without Harvester."""
import copy
from pathlib import Path
import time
import uuid
import argparse
import json
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qt'/'tools'))
from progressive_client import ProgressiveClient
from progressive_v1_conformance import fragment_prefix, reject


def exercise(client, fixtures, work):
    work=Path(work).resolve();work.mkdir(parents=True)
    snapshot=dict(schemaVersion=1,sessionId=uuid.uuid4().hex,generation=1,revision=1,
                  state='downloading',duration100ns=200000000,tracks={})
    for kind,name in [('video','video.mp4'),('audio','audio.m4a')]:
        data=(Path(fixtures)/'progressive'/name).read_bytes();count=fragment_prefix(data,5)
        path=work/(kind+'.part');path.write_bytes(data[:count])
        snapshot['tracks'][kind]=dict(path=path.name,container='mp4' if kind=='video' else 'm4a',fragmented=True,
            initializationReady=True,availablePrefixBytes=count,expectedTotalBytes=None,state='downloading')
    ids=[]
    def closed(reader):
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            status=client.reader_status(reader)
            if status['released']:return status
            time.sleep(.05)
        raise AssertionError(('Reader still holds files',reader,status))
    try:
        first=client.start(snapshot,session_directory=work,activate=False,track_reader=True);one=first['readerId'];ids.append(one)
        status=client.reader_status(one);assert not status['released'] and status['reason']=='lease-held'
        assert {r['id'] for r in status['resources']}=={'video','audio'}
        assert all(int(r['fileId'])>0 for r in status['resources'])
        again=client.start(snapshot,session_directory=work,activate=False,track_reader=True);assert again['readerId']==one
        assert len(client.readers(snapshot['sessionId'],1))==1
        reject(client,dict(version=1,action='readerForget',readerId=one),'reader_in_use')
        next_snapshot=copy.deepcopy(snapshot);next_snapshot['generation']=2
        reject(client,dict(version=1,action='start',session=next_snapshot,sessionDirectory=str(work),trackReader=True),'session_busy')
        second=client.start(next_snapshot,session_directory=work,activate=False,replace=True,track_reader=True);two=second['readerId'];ids.append(two);assert one!=two
        closed(one)
        assert client.release_reader(one)['released']
        assert client.command('status',next_snapshot['sessionId'])['active']
        for action,fields in [('heartbeat',{}),('status',{}),('error',dict(message='Old writer error')),
                              ('release',{}),('cancel',{}),('update',dict(session=snapshot))]:
            reject(client,dict(version=1,action=action,sessionId=snapshot['sessionId'],readerId=one,**fields),'reader_mismatch')
        assert client.command('status',next_snapshot['sessionId'])['active']
        assert not client.reader_status(two)['released']
        assert len(client.readers(snapshot['sessionId'],1))==1 and len(client.readers(snapshot['sessionId'],2))==1
        assert client.release_reader(two,timeout=10)['released'];closed(two)
        for reader in ids:
            assert client.forget_reader(reader)['forgotten']
            reject(client,dict(version=1,action='readerStatus',readerId=reader),'unknown_reader')
        ids.clear()
        return dict(result='PASS',trackedReaders=2,boundMediaFilesPerReader=2,
                    repeatedStartIdempotent=True,oldReleasePreservesReplacement=True)
    finally:
        for reader in ids:
            try:
                if client.release_reader(reader,timeout=10)['released']:client.forget_reader(reader)
            except (OSError,RuntimeError):pass


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--player',required=True);parser.add_argument('--profile');parser.add_argument('--fixtures',required=True);parser.add_argument('--work',required=True);parser.add_argument('--expected-pid',type=int)
    args=parser.parse_args();client=ProgressiveClient(args.player,args.profile)
    if args.expected_pid is not None:assert client.request(dict(version=1,action='capabilities'))['pid']==args.expected_pid
    print(json.dumps(exercise(client,args.fixtures,args.work)))
