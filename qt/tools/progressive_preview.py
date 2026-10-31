"""Explicit playback of one retained remux clip; never whole-source streaming."""
from __future__ import annotations
import math
import uuid
import time


class RemuxPreview:
    def __init__(self,client):
        self.client=client;self.preview_id=None;self.request_id=None;self.probe_id=None
        caps=client.request(dict(version=1,action='capabilities'));cap=caps.get('preparedRemuxPreview',{})
        if not isinstance(cap,dict) or type(cap.get('version')) is not int or cap['version']!=1 or cap.get('mode')!='prepared-remux-clip' or cap.get('boundedClip') is not True or cap.get('continuousPlayback') is not False or cap.get('playbackSupported') is not True:
            raise RuntimeError('Player lacks prepared clip preview playback')
    def _check(self,reply):
        identifier=reply.get('previewId')
        if reply.get('requestId')!=self.request_id or reply.get('probeId')!=self.probe_id or reply.get('mode')!='prepared-remux-clip' or reply.get('boundedClip') is not True or reply.get('continuousPlayback') is not False or not isinstance(identifier,str) or len(identifier)!=32 or any(c not in '0123456789abcdef' for c in identifier) or (self.preview_id is not None and self.preview_id!=identifier):
            raise RuntimeError('Unexpected prepared clip owner/mode')
        self.preview_id=identifier;return reply
    def start(self,probe,*,replace=False,activate=False,title='FoxicMP — fragment'):
        if self.request_id is not None:raise RuntimeError('Use a new preview object for another clip')
        if type(replace) is not bool or type(activate) is not bool or not isinstance(title,str) or len(title.encode('utf-8'))>1024:raise ValueError('Invalid preview options')
        if probe.client is not self.client or not probe.remux_mode or probe.probe_id is None:raise ValueError('A prepared remux probe of the same client is required')
        before=probe.status()
        if before.get('state')!='finished' or before.get('media',{}).get('code')!='data' or before['media'].get('remux',{}).get('retained') is not True:raise RuntimeError('No retained clip is ready')
        self.request_id=uuid.uuid4().hex;self.probe_id=probe.probe_id
        return self._check(self.client.request(dict(version=1,action='previewRemux',probeId=probe.probe_id,requestId=self.request_id,replace=replace,activate=activate,title=title)))
    def _request(self,action,**fields):
        if self.preview_id is None:raise RuntimeError('No preview was started')
        return self._check(self.client.request(dict(version=1,action=action,previewId=self.preview_id,**fields)))
    def status(self):
        if self.preview_id is None:
            if self.request_id is None:raise RuntimeError('No preview was requested')
            return self._check(self.client.request(dict(version=1,action='previewStatus',requestId=self.request_id)))
        return self._request('previewStatus')
    def control(self,command,seconds=None):
        if command not in {'pause','play','seek'}:raise ValueError('Unknown preview control')
        fields=dict(command=command)
        if command=='seek':
            if type(seconds) not in (int,float) or not math.isfinite(seconds) or not 0<=seconds<=3600:raise ValueError('Invalid preview seek')
            fields['seconds']=seconds
        elif seconds is not None:raise ValueError('Unexpected seek time')
        return self._request('previewControl',**fields)
    def cancel(self):
        if self.preview_id is None:self.status()
        return self._request('previewCancel')
    def wait_released(self,timeout=10):
        if not 0<=timeout<=60:raise ValueError('Invalid preview release timeout')
        until=time.monotonic()+timeout
        while True:
            status=self.status()
            if status.get('released') is True:return status
            if time.monotonic()>=until:raise TimeoutError('Preview readers are still closing')
            time.sleep(.02)
