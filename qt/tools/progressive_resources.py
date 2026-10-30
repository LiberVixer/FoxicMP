"""Explicit asynchronous v2 resource-I/O diagnostics. This never starts playback."""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
import time
import uuid
from progressive_client import ProgressiveClient, read_snapshot


class ResourceProbe:
    def __init__(self, client: ProgressiveClient):
        self.client=client;self.probe_id=None;self.identity=None;self.request_id=None;self.acknowledged=False
        caps=client.request(dict(version=1,action='capabilities'))
        self.segment_supported=caps.get('segmentProbeVersion')==1
        required={'resourceProbe','resourceProbeUpdate','resourceProbeStatus','resourceProbeCancel','resourceProbeForget'}
        if caps.get('resourceIoProbeVersion')!=1 or caps.get('resourceIoPlaybackSupported') is not False or not required<=set(caps.get('actions',[])):
            raise RuntimeError('Player lacks the resource-I/O diagnostic contract')

    def _check(self, reply):
        if reply.get('mode')!='resource-io-only' or reply.get('playbackSupported') is not False:
            raise RuntimeError('Invalid resource probe capability/reply')
        identifier=reply.get('probeId')
        if not isinstance(identifier,str) or len(identifier)!=32 or any(c not in '0123456789abcdef' for c in identifier):
            raise RuntimeError('Invalid resource probe identifier')
        if reply.get('requestId')!=self.request_id or (reply.get('sessionId'),reply.get('generation'))!=self.identity or (self.probe_id is not None and identifier!=self.probe_id):
            raise RuntimeError('Resource probe belongs to another request; retain files')
        self.probe_id=identifier
        return reply

    def start(self, snapshot: dict, directory: str, reads: list[dict], *, wait_milliseconds: int=0, segment: dict | None=None):
        if self.identity is not None and not self.acknowledged:
            raise RuntimeError('Acknowledge the previous resource probe first')
        if type(wait_milliseconds) is not int or not 0<=wait_milliseconds<=10000:
            raise ValueError('Resource probe wait must be 0..10000 milliseconds')
        if segment is not None:
            if not self.segment_supported:raise RuntimeError('Player lacks the segment staging/indexing diagnostic contract')
            if reads:raise ValueError('Segment probe cannot contain raw read queries')
        self.probe_id=None;self.acknowledged=False;self.identity=(snapshot['sessionId'],snapshot['generation'])
        self.request_id=uuid.uuid4().hex
        # Save identity BEFORE sending: a lost start reply is recovered through
        # read-only discovery, without cancelling an unrelated job or playback.
        request=dict(version=1,action='resourceProbe',session=copy.deepcopy(snapshot),
            sessionDirectory=str(Path(directory).resolve()),reads=copy.deepcopy(reads),waitMilliseconds=wait_milliseconds,requestId=self.request_id)
        if segment is not None:request['segment']=copy.deepcopy(segment)
        return self._check(self.client.request(request))

    def start_segment(self,snapshot: dict,directory: str,track_id: str,index: int,*,demux: bool=True,wait_milliseconds: int=0):
        if type(index) is not int or not 0<=index<=127 or type(demux) is not bool:
            raise ValueError('Segment index must be 0..127 and demux must be boolean')
        return self.start(snapshot,directory,[],segment=dict(trackId=track_id,index=str(index),demux=demux),wait_milliseconds=wait_milliseconds)

    def status(self, timeout: float=5):
        if self.identity is None:raise RuntimeError('No resource probe was requested')
        request=dict(version=1,action='resourceProbeStatus')
        if self.probe_id is not None:request['probeId']=self.probe_id
        return self._check(self.client.request(request,timeout=timeout))

    def publish(self, snapshot: dict):
        if self.probe_id is None:self.status()
        if (snapshot['sessionId'],snapshot['generation'])!=self.identity:
            raise ValueError('Resource probe generation changed')
        return self._check(self.client.request(dict(version=1,action='resourceProbeUpdate',probeId=self.probe_id,session=copy.deepcopy(snapshot))))

    def cancel(self):
        if self.probe_id is None:self.status()
        return self._check(self.client.request(dict(version=1,action='resourceProbeCancel',probeId=self.probe_id)))

    def wait(self, timeout: float=15):
        if not 0<=timeout<=60:raise ValueError('Probe wait timeout must be 0..60 seconds')
        deadline=time.monotonic()+timeout
        while True:
            reply=self.status(timeout=max(.001,min(5,deadline-time.monotonic())))
            if reply.get('state') in {'finished','cancelled','error','forgotten'} and reply.get('released') is True:return reply
            if time.monotonic()>=deadline:raise TimeoutError('Resource probe is still active; retain files')
            time.sleep(min(.05,max(0,deadline-time.monotonic())))

    def forget(self, timeout: float=5):
        if not 0<=timeout<=60:raise ValueError('Probe acknowledgement timeout must be 0..60 seconds')
        deadline=time.monotonic()+timeout
        if self.probe_id is None:self.status(timeout=max(.001,min(5,deadline-time.monotonic())))
        self._check(self.client.request(dict(version=1,action='resourceProbeForget',probeId=self.probe_id),timeout=max(.001,min(5,deadline-time.monotonic()))))
        reply=self.wait(max(0,deadline-time.monotonic()))
        if reply.get('state')!='forgotten':raise RuntimeError('Closed resource receipts were not acknowledged')
        self.acknowledged=True
        return reply


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--player',required=True);parser.add_argument('--profile');parser.add_argument('--directory',required=True)
    parser.add_argument('--snapshot',type=Path,required=True);parser.add_argument('--reads',type=Path,required=True,help='Bounded JSON object with a reads array');parser.add_argument('--wait-ms',type=int,default=0)
    args=parser.parse_args();probe=ResourceProbe(ProgressiveClient(args.player,args.profile))
    probe.start(read_snapshot(args.snapshot),args.directory,read_snapshot(args.reads)['reads'],wait_milliseconds=args.wait_ms)
    result=probe.wait();ack=probe.forget();print(json.dumps(dict(probe=result,acknowledged=ack['state']=='forgotten'),ensure_ascii=False))
