"""Real owned Classic seekbar messages and GDI pixels, with isolated profiles."""
import configparser
import copy
import ctypes
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time
from progressive_continuous import ContinuousRemux,AAC_MODE
from progressive_continuous_test_media import source_fixture,source_snapshot,source_cuts
from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_player_crash_recovery import wait,sha

class SeekBar:
    def __init__(self,peer,dark):
        self.peer=peer;self.dark=dark;self.user=ctypes.WinDLL('user32',use_last_error=True);self.gdi=ctypes.WinDLL('gdi32',use_last_error=True)
        u=self.user;g=self.gdi;callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
        u.EnumWindows.argtypes=[callback,w.LPARAM];u.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
        u.GetDlgItem.argtypes=[w.HWND,ctypes.c_int];u.GetDlgItem.restype=w.HWND;u.ShowWindow.argtypes=[w.HWND,ctypes.c_int]
        u.GetClientRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)];u.GetDpiForWindow.argtypes=[w.HWND];u.GetDpiForWindow.restype=w.UINT
        u.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,ctypes.POINTER(ctypes.c_size_t)]
        u.GetParent.argtypes=[w.HWND];u.GetParent.restype=w.HWND;u.GetWindowLongW.argtypes=[w.HWND,ctypes.c_int];u.GetWindowLongW.restype=w.LONG
        u.IsIconic.argtypes=[w.HWND];u.IsWindowVisible.argtypes=[w.HWND];u.PrintWindow.argtypes=[w.HWND,w.HDC,w.UINT];u.GetDC.argtypes=[w.HWND];u.GetDC.restype=w.HDC;u.ReleaseDC.argtypes=[w.HWND,w.HDC]
        g.CreateCompatibleDC.argtypes=[w.HDC];g.CreateCompatibleDC.restype=w.HDC;g.CreateCompatibleBitmap.argtypes=[w.HDC,ctypes.c_int,ctypes.c_int];g.CreateCompatibleBitmap.restype=w.HBITMAP
        g.SelectObject.argtypes=[w.HDC,w.HANDLE];g.SelectObject.restype=w.HANDLE;g.DeleteObject.argtypes=[w.HANDLE];g.DeleteDC.argtypes=[w.HDC]
        g.GetDIBits.argtypes=[w.HDC,w.HBITMAP,w.UINT,w.UINT,ctypes.c_void_p,ctypes.c_void_p,w.UINT]
        self.frame=None
        @callback
        def visit(hwnd,_):
            pid=w.DWORD();u.GetWindowThreadProcessId(hwnd,ctypes.byref(pid))
            if pid.value==peer.process.pid and u.GetDlgItem(hwnd,10017):self.frame=hwnd
            return True
        u.EnumWindows(visit,0);assert self.frame,'Owned Classic frame/seekbar not found'
        self.hwnd=u.GetDlgItem(self.frame,10017);self.owned();u.ShowWindow(self.frame,4);u.ShowWindow(self.hwnd,8)
    def owned(self):
        assert self.peer.process.poll() is None
        pid=w.DWORD();self.user.GetWindowThreadProcessId(self.hwnd,ctypes.byref(pid));assert pid.value==self.peer.process.pid
    def geometry(self):
        self.owned();rect=w.RECT();assert self.user.GetClientRect(self.hwnd,ctypes.byref(rect));width=rect.right;height=rect.bottom;assert 50<width<4000 and 0<height<200,(width,height)
        dpi=self.user.GetDpiForWindow(self.hwnd) or 96
        scale=lambda n:max(1,int(n*dpi/96+.5))
        left=0 if self.dark else scale(8);right=width if self.dark else width-scale(8)
        top=0 if self.dark else scale(7)+2;bottom=height if self.dark else height-(scale(5)+1)
        assert right>left and bottom>top,(width,height,left,right,top,bottom)
        return width,height,left,right,top,bottom
    def point(self,seconds,duration):
        _,_,left,right,top,bottom=self.geometry();return round(left+seconds/duration*(right-left)),(top+bottom)//2
    def send(self,msg,point,buttons=0):
        self.owned();out=ctypes.c_size_t();x,y=point
        assert self.user.SendMessageTimeoutW(self.hwnd,msg,buttons,(x&65535)|((y&65535)<<16),2,3000,ctypes.byref(out)),ctypes.get_last_error()
    def refresh_position(self):
        self.owned();out=ctypes.c_size_t()
        assert self.user.SendMessageTimeoutW(self.frame,0x113,1,0,2,3000,ctypes.byref(out)),'Owned position timer failed'
    def click(self,seconds,duration,right=False):
        point=self.point(seconds,duration);self.send(0x204 if right else 0x201,point,2 if right else 1);self.send(0x205 if right else 0x202,point)
        return dict(x=point[0],y=point[1],requestedSeconds=seconds)
    def drag(self,start,end,duration):
        self.send(0x201,self.point(start,duration),1);self.send(0x200,self.point(end,duration),1);self.send(0x202,self.point(end,duration))
    def image(self,path=None):
        width,height,*_=self.geometry();u=self.user;g=self.gdi;was_iconic=bool(u.IsIconic(self.frame));u.ShowWindow(self.frame,4);iconic=bool(u.IsIconic(self.frame));u.ShowWindow(self.hwnd,8);screen=u.GetDC(self.hwnd);assert screen;dc=g.CreateCompatibleDC(screen);bitmap=g.CreateCompatibleBitmap(screen,width,height);assert dc and bitmap
        old=g.SelectObject(dc,bitmap)
        class Header(ctypes.Structure):
            _fields_=[('size',w.DWORD),('width',w.LONG),('height',w.LONG),('planes',w.WORD),('bits',w.WORD),('compression',w.DWORD),('bytes',w.DWORD),('xppm',w.LONG),('yppm',w.LONG),('used',w.DWORD),('important',w.DWORD)]
        try:
            assert u.PrintWindow(self.hwnd,dc,3),'PrintWindow full-content failed'
            g.SelectObject(dc,old);old=None;pixels=ctypes.create_string_buffer(width*height*4);header=Header(ctypes.sizeof(Header),width,-height,1,32,0,len(pixels),0,0,0,0)
            assert g.GetDIBits(dc,bitmap,0,height,pixels,ctypes.byref(header),0)==height
            raw=pixels.raw;rgb=bytearray()
            for i in range(0,len(raw),4):rgb.extend((raw[i+2],raw[i+1],raw[i]))
            if path is not None:path.write_bytes(struct.pack('<2sIHHI',b'BM',54+len(raw),0,0,54)+bytes(header)+raw)
            color=(214,165,72) if self.dark else (146,100,20);count=sum(tuple(rgb[i:i+3])==color for i in range(0,len(rgb),3))
            return dict(rgbSha256=hashlib.sha256(rgb).hexdigest(),pendingColor=list(color),pendingPixels=count,channelCenter=list(rgb[(((self.geometry()[4]+self.geometry()[5])//2)*width+width//2)*3:(((self.geometry()[4]+self.geometry()[5])//2)*width+width//2)*3+3]),width=width,height=height,iconicBefore=was_iconic,iconicAfter=iconic,visible=bool(u.IsWindowVisible(self.hwnd)))
        finally:
            if old:g.SelectObject(dc,old)
            g.DeleteObject(bitmap);g.DeleteDC(dc);u.ReleaseDC(self.hwnd,screen)

def exercise(peers,root,report,dark):
    peer=peers[0];ui=SeekBar(peer,dark);rows=[];artifacts=Path(report)/'artifacts';artifacts.mkdir(parents=True,exist_ok=True)
    original=source_fixture(root,'reference',AAC_MODE);raw=original.read_bytes();prefix=source_cuts(original,AAC_MODE)[12]
    for ending in ('publish','stop','replace','failure'):
        folder=root/ending;folder.mkdir();path=folder/'input.part';path.write_bytes(raw);model=source_snapshot(path,raw,prefix=prefix,mode=AAC_MODE);path.write_bytes(raw)
        sdk=ContinuousRemux(peer.client,str(folder),mode=AAC_MODE);sdk.prepare(model,'clip');regular=None
        try:
            wait(lambda:sdk.status()['playbackReady'],'timeline ready',20);sdk.play(title='Timeline '+ending)
            wait(lambda:(lambda s:s if s['state']=='playing' and s['positionSeconds']>.15 else None)(sdk.playback_status()),'timeline playing',20)
            sdk.control('pause');held=wait(lambda:(lambda s:s if s['state']=='paused' else None)(sdk.playback_status()),'timeline paused',5);time.sleep(.2)
            duration=held['durationSeconds'];ui.refresh_position();before=ui.image(artifacts/(ending+'-before.bmp'));assert before['pendingPixels']==0,before
            selection=ui.click(20.5,duration)
            queued=wait(lambda:(lambda s:s if s['pendingSeekSeconds'] is not None else None)(sdk.playback_status()),'mouse queued',5)
            assert abs(queued['pendingSeekSeconds']-20.5)<=duration/before['width']*2 and queued['remainingGrowingSeeks']==16 and abs(queued['positionSeconds']-held['positionSeconds'])<.15,queued
            (folder/'ui-selection.json').write_text(json.dumps(dict(queued=queued,geometry=ui.geometry(),before=before,selection=selection),indent=2))
            marked=wait(lambda:(lambda image:image if image['pendingPixels']>0 else None)(ui.image(artifacts/(ending+'-current.bmp'))),'GDI pending marker',3)
            ui.image(artifacts/(ending+'-pending.bmp'))
            ui.drag(queued['pendingSeekSeconds'],21,duration)
            superseded=wait(lambda:(lambda s:s if s['pendingSeekSeconds'] is not None and abs(s['pendingSeekSeconds']-21)<.12 else None)(sdk.playback_status()),'mouse latest target',5)
            assert int(superseded['seekRevision'])>int(queued['seekRevision']) and superseded['remainingGrowingSeeks']==16,superseded
            ui.click(superseded['pendingSeekSeconds'],duration,right=True)
            cancelled=wait(lambda:(lambda s:s if s['pendingSeekSeconds'] is None else None)(sdk.playback_status()),'right click cancels marker',5)
            restored=wait(lambda:(lambda image:image if image['pendingPixels']==0 else None)(ui.image(artifacts/(ending+'-restored.bmp'))),'marker cleared',3)
            assert cancelled['seekState']=='cancelled' and restored['rgbSha256']==before['rgbSha256'],(cancelled,before,restored)
            ui.click(20.5,duration);pending=wait(lambda:(lambda s:s if s['pendingSeekSeconds'] is not None else None)(sdk.playback_status()),'mouse requeue',5)
            target=pending['pendingSeekSeconds']
            if ending=='publish':
                new=copy.deepcopy(model);new['revision']='2';new['resources'][0]['ranges']=[dict(offset='0',length=str(len(raw)))];sdk.publish(protect_snapshot(new,folder,previous=model))
                terminal=wait(lambda:(lambda s:s if s['seekState']=='applied' and s['state']=='paused' and abs(s['positionSeconds']-target)<.12 else None)(sdk.playback_status()),'mouse target applied',20)
                assert terminal['remainingGrowingSeeks']==15 and terminal['playbackId']==held['playbackId'] and not terminal['outputComplete'],terminal
            elif ending=='stop':terminal=sdk.control('stop')
            elif ending=='failure':
                new=copy.deepcopy(model);new['revision']='2';new['tracks'][0]['state']='failed';sdk.publish(protect_snapshot(new,folder,previous=model))
                terminal=wait(lambda:(lambda s:s if s['state']=='error' and s['pendingSeekSeconds'] is None else None)(sdk.playback_status()),'failure clears marker',10)
            else:
                source=source_fixture(folder,'replacement-source');replacement=folder/'replacement.mp4'
                subprocess.run(['ffmpeg','-v','error','-y','-i',str(source),'-c','copy','-movflags','+frag_keyframe+empty_moov+default_base_moof',str(replacement)],check=True,capture_output=True)
                regular=snapshot(replacement,replacement.read_bytes(),complete=True);regular['timeline']['duration']='240000000';regular['tracks'][0].update(containerHint='fmp4',initializationReady=True,codecHint='h264')
                peer.client.request(dict(version=1,action='start',session=regular,sessionDirectory=str(folder),replace=True))
                wait(lambda:(lambda s:s if s.get('active') and s['state']=='playing' and s['positionSeconds']>.15 else None)(peer.client.request(dict(version=1,action='heartbeat',sessionId=regular['sessionId']))),'ordinary replacement',20)
                terminal=wait(lambda:(lambda s:s if s['state']=='closed' and s['pendingSeekSeconds'] is None else None)(sdk.playback_status()),'old marker fenced',5)
            clear=wait(lambda:(lambda image:image if image['pendingPixels']==0 else None)(ui.image()),'terminal marker cleared',5);ui.image(artifacts/(ending+'-cleared.bmp'))
            if regular is not None:
                peer.client.request(dict(version=1,action='cancel',sessionId=regular['sessionId']))
                receipts=wait(lambda:(lambda rs:rs if rs and all(r['released'] and r['filesClosed'] for r in rs) else None)(peer.client.readers(regular['sessionId'],1)),'replacement files closed',2)
                for receipt in receipts:assert peer.client.forget_reader(receipt['readerId'])['forgotten']
                regular=None
            receipt=sdk.close(timeout=2);assert receipt['released'] and receipt['sourceFilesClosed'],receipt
            forgotten=sdk.forget(timeout=2);assert forgotten['forgotten'] and sha(path)==sha(original),forgotten
            rows.append(dict(profile=ending,result='PASS',theme='dark' if dark else 'light',selection=selection,queued=queued,superseded=superseded,cancelled=cancelled,terminal=terminal,before=before,marked=marked,restored=restored,cleared=clear,release=receipt,forgotten=forgotten))
        finally:
            if regular is not None:peer.client.request(dict(version=1,action='cancel',sessionId=regular['sessionId']))
            if not sdk.forgotten:sdk.close(timeout=5);sdk.forget(timeout=5)
    return dict(result='PASS',count=len(rows),rows=rows,realSeekbarMessages=True,ownedWindowPid=peer.process.pid,privateWindowShownWithoutActivation=True,gdiPixelsVerified=True)
