"""Local, stream-copy HLS preview. FFmpeg receives cached bytes via stdin only."""
from __future__ import annotations
import os
import subprocess
import struct
import threading
import time
from pathlib import Path
from scripts.progressive_session import Session,read_json


class HlsPreview:
    def __init__(self, root, duration, ffmpeg, provider_command, report, stopping):
        self.root=Path(root)
        self.names=[self.root/'preview-video.mp4',self.root/'preview-audio.m4a']
        self.session=Session(self.root,[p.name for p in self.names],[None,None],int(duration*10000000))
        self.session.document["timestampOffset100ns"]=10000000
        self.session.publish()
        self.report,self.stopping=report,stopping
        self.condition=threading.Condition()
        self.source=None;self.prefix=0;self.sent=0;self.input_complete=False
        self.halt=threading.Event();self.error=None;self.last_report=0;self.confirmed=[0,0]
        self.log=(self.root/'preview-ffmpeg.log').open('wb')
        command=[ffmpeg,'-hide_banner','-loglevel','error','-nostdin','-y',
                 '-probesize','1048576','-analyzeduration','2000000','-copyts','-start_at_zero','-i','pipe:0']
        # AAC from MPEG-TS gains AudioSpecificConfig only after aac_adtstoasc sees its first packet.
        # Delay the initial moov until then; an empty early header cannot initialize the decoder.
        for selector,path in [('v:0',self.names[0]),('a:0',self.names[1])]:
            command+=['-map',selector,'-c','copy','-movflags','+empty_moov+delay_moov+frag_keyframe+default_base_moof+skip_trailer+frag_discont',
                      '-use_editlist','0','-output_ts_offset','1','-avoid_negative_ts','disabled','-frag_duration','2000000','-flush_packets','1','-f','mp4',str(path)]
            if selector=='a:0':command[-1:-1]=['-bsf:a','aac_adtstoasc']
        self.process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=self.log,
                                      creationflags=0x08000000 if os.name=='nt' else 0)
        try:
            self.provider=subprocess.Popen(provider_command(self.session.path,os.getpid()),stdin=subprocess.DEVNULL,
                                           stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,close_fds=True,
                                           creationflags=0x08000000 if os.name=='nt' else 0)
            deadline=time.monotonic()+5
            while True:
                try:
                    if read_json(self.session.path.parent/'provider.json').get('ready'):break
                except (OSError,ValueError):pass
                if self.provider.poll() is not None or time.monotonic()>=deadline:raise OSError('HLS session provider did not start')
                time.sleep(.05)
        except BaseException:
            self.process.terminate();self.process.wait(5);self.log.close();self.session.state('failed');raise
        self.feeder=threading.Thread(target=self._feed,daemon=True)
        self.monitor=threading.Thread(target=self._monitor,daemon=True)
        self.feeder.start();self.monitor.start()

    def feed(self,path,prefix):
        with self.condition:
            if self.source is not None and self.source!=Path(path):raise ValueError('HLS cache path changed')
            if prefix<self.prefix:raise ValueError('HLS written prefix regressed')
            self.source=Path(path);self.prefix=prefix;self.condition.notify_all()

    def _feed(self):
        try:
            while not self.halt.is_set():
                with self.condition:
                    if self.sent>=self.prefix:
                        if self.input_complete:break
                        self.condition.wait(.1);continue
                    path,prefix=self.source,self.prefix
                # No long-lived source handle: ordinary yt-dlp postprocessing may replace its file.
                with path.open('rb',buffering=0) as source:
                    source.seek(self.sent);data=source.read(min(65536,prefix-self.sent))
                if not data:raise ValueError('Published HLS bytes disappeared')
                self.process.stdin.write(data);self.process.stdin.flush();self.sent+=len(data)
            self.process.stdin.close()
        except (OSError,ValueError) as error:
            self.error=error

    def _complete_prefix(self,index,physical):
        # FFmpeg backpatches moof/mdat headers while a fragment is being emitted.
        # Publish only closed fragments, never the changing file-size prefix.
        position=self.confirmed[index]
        with self.names[index].open('rb',buffering=0) as stream:
            def box(at):
                if at+8>physical:return None
                stream.seek(at);raw=stream.read(8)
                if len(raw)!=8:return None
                size,kind=struct.unpack('>I4s',raw);header=8
                if size==1:
                    if at+16>physical:return None
                    raw=stream.read(8)
                    if len(raw)!=8:return None
                    size=struct.unpack('>Q',raw)[0];header=16
                if size<header or size>physical-at:return None
                return size,kind
            while position<physical:
                current=box(position)
                if not current:break
                size,kind=current
                if kind==b'moof':
                    payload=box(position+size)
                    if not payload or payload[1]!=b'mdat':break
                    position+=size+payload[0]
                elif kind==b'mdat':break
                else:position+=size
        self.confirmed[index]=position
        return position

    def _publish(self,finished=False):
        for i,path in enumerate(self.names):
            if path.exists():
                physical=path.stat().st_size
                prefix=self._complete_prefix(i,physical)
                if finished and prefix!=physical:raise ValueError("Unclosed final HLS preview fragment")
                if finished and prefix<=0:raise ValueError('Empty HLS preview track')
                self.session.progress(i,prefix,prefix if finished else None,finished)
            elif finished:raise ValueError('Missing HLS preview track')
        if time.monotonic()-self.last_report>=.5 or finished:
            ready=self.session.ready()
            self.report(self.session.path,ready,'' if ready else 'Ожидание локальных HLS-фрагментов')
            self.last_report=time.monotonic()

    def _monitor(self):
        try:
            while not self.halt.wait(.1):
                if self.stopping():break
                self._publish()
                if self.process.poll() is not None:
                    if not self.input_complete or self.process.returncode:raise ValueError('Local HLS stream copy failed')
                    break
        except (OSError,ValueError) as error:self.error=error

    def finish(self):
        with self.condition:self.input_complete=True;self.condition.notify_all()
        self.feeder.join(30)
        if self.feeder.is_alive():self.error=TimeoutError('Local HLS writer did not finish')
        try:
            if self.process.wait(30):self.error=ValueError('Local HLS stream copy failed')
        except subprocess.TimeoutExpired:self.error=TimeoutError('Local HLS stream copy timed out')
        self.halt.set();self.monitor.join(3)
        if self.error:
            self.abort();return False
        try:
            self._publish(True);self.session.state('merging')
        except (OSError,ValueError) as error:
            self.error=error;self.abort();return False
        self.log.close();return True

    def abort(self,cancelled=False):
        self.halt.set()
        with self.condition:self.condition.notify_all()
        if self.process.poll() is None:self.process.terminate()
        try:self.process.wait(5)
        except subprocess.TimeoutExpired:self.process.kill();self.process.wait()
        self.feeder.join(3);self.monitor.join(3)
        self.log.close()
        if self.session.document['state'] not in {'failed','cancelled','complete'}:
            self.session.state('cancelled' if cancelled else 'failed')
        self.report(self.session.path,False,'Предварительный просмотр HLS недоступен; обычное скачивание продолжается')
