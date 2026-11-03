"""Completed local video: normal Classic graph Stop/Play without progressive API."""
import argparse,configparser,ctypes,hashlib,json,subprocess,sys,time
from ctypes import wintypes as w
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from progressive_process_metrics import sample
from progressive_family_test import registration_snapshot

def run(player,media):
 player=Path(player).resolve();media=Path(media).resolve();
 raw=player.with_suffix('.ini').read_bytes();settings=configparser.ConfigParser();settings.read_string(raw.decode('utf-16') if raw.startswith((b'\xff\xfe',b'\xfe\xff')) else raw.decode('utf-8-sig'));assert settings.get('Settings','ContextMenuPrompted')=='1' and settings.get('Settings','ContextMenuEnabled')=='0','Use an isolated test copy with shell registration disabled'
 before=registration_snapshot();digest=hashlib.sha256(media.read_bytes()).hexdigest();u=ctypes.WinDLL('user32',use_last_error=True);k=ctypes.WinDLL('kernel32',use_last_error=True);callback=ctypes.WINFUNCTYPE(ctypes.c_ssize_t,w.HWND,w.UINT,w.WPARAM,w.LPARAM);owner=[None];position=[None];messages=[0];process=None
 class Copy(ctypes.Structure):_fields_=[('dwData',ctypes.c_size_t),('cbData',w.DWORD),('lpData',ctypes.c_void_p)]
 class Class(ctypes.Structure):_fields_=[('style',w.UINT),('proc',callback),('extra',ctypes.c_int),('windowExtra',ctypes.c_int),('instance',w.HINSTANCE),('icon',w.HICON),('cursor',w.HANDLE),('brush',w.HBRUSH),('menu',w.LPCWSTR),('name',w.LPCWSTR)]
 u.DefWindowProcW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.DefWindowProcW.restype=ctypes.c_ssize_t;u.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)];u.GetWindowThreadProcessId.restype=w.DWORD
 @callback
 def receiver(hwnd,message,wp,lp):
  if message==0x4a and process is not None:
   pid=w.DWORD();u.GetWindowThreadProcessId(w.HWND(wp),ctypes.byref(pid))
   if pid.value!=process.pid:return 0
   c=ctypes.cast(lp,ctypes.POINTER(Copy)).contents;owner[0]=wp
   if c.dwData==0x50000007:
    try:position[0]=float(ctypes.wstring_at(c.lpData).replace(',','.'));messages[0]+=1
    except ValueError:pass
   return 1
  return u.DefWindowProcW(hwnd,message,wp,lp)
 k.GetModuleHandleW.argtypes=[w.LPCWSTR];k.GetModuleHandleW.restype=w.HMODULE;instance=k.GetModuleHandleW(None);name='FoxicMPPrivateNormalLifecycle'
 cls=Class(0,receiver,0,0,instance,None,None,None,None,name);u.RegisterClassW.argtypes=[ctypes.POINTER(Class)];assert u.RegisterClassW(ctypes.byref(cls))
 u.CreateWindowExW.argtypes=[w.DWORD,w.LPCWSTR,w.LPCWSTR,w.DWORD,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,w.HWND,w.HMENU,w.HINSTANCE,ctypes.c_void_p];u.CreateWindowExW.restype=w.HWND;window=u.CreateWindowExW(0,name,name,0,0,0,0,0,None,None,instance,None);assert window
 u.PeekMessageW.argtypes=[ctypes.POINTER(w.MSG),w.HWND,w.UINT,w.UINT,w.UINT];u.DispatchMessageW.argtypes=[ctypes.POINTER(w.MSG)];u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,ctypes.POINTER(ctypes.c_size_t)];u.SendMessageTimeoutW.restype=ctypes.c_ssize_t
 def pump():
  msg=w.MSG()
  while u.PeekMessageW(ctypes.byref(msg),None,0,0,1):u.DispatchMessageW(ctypes.byref(msg))
 def command(code):
  text=ctypes.create_unicode_buffer('');data=Copy(code,ctypes.sizeof(text),ctypes.cast(text,ctypes.c_void_p));result=ctypes.c_size_t();assert u.SendMessageTimeoutW(owner[0],0x4a,window,ctypes.addressof(data),2,1000,ctypes.byref(result))
 def wait(predicate,seconds=20):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   pump();assert process.poll() is None,process.returncode
   if owner[0]:command(0xa0003004);pump()
   if predicate():return
   time.sleep(.02)
  raise AssertionError(('Normal Classic deadline',position[0],messages[0]))
 try:
  process=subprocess.Popen([str(player),'/new','/nofocus','/minimized','/slave',str(window),'/play',str(media)],cwd=player.parent)
  wait(lambda:position[0] is not None and position[0]>.25);rows=[]
  for cycle in range(5):
   rows.append(dict(cycle=cycle,positionSeconds=position[0],process=sample(process.pid)));command(0xa0000001);wait(lambda:position[0] is not None and position[0]<.1,5);command(0xa0000004);wait(lambda:position[0] is not None and position[0]>.25,10)
  assert max(r['process']['handles'] for r in rows[-2:])<=max(r['process']['handles'] for r in rows[:2])+8
  return dict(result='PASS',normalFile=True,progressiveApiUsed=False,stopPlayCycles=5,rows=rows,sourceSha256=digest)
 finally:
  if process is not None and process.poll() is None:
   if owner[0]:u.PostMessageW(owner[0],0x10,0,0)
   end=time.monotonic()+10
   while process.poll() is None and time.monotonic()<end:pump();time.sleep(.02)
   if process.poll() is None:process.kill();process.wait();raise AssertionError('Normal test player did not close')
   assert process.returncode==0,process.returncode
  u.DestroyWindow.argtypes=[w.HWND];u.DestroyWindow(window)
  assert registration_snapshot()==before,'Installed shell registration changed'
  assert hashlib.sha256(media.read_bytes()).hexdigest()==digest
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--media',required=True);a=p.parse_args();print(json.dumps(run(a.player,a.media)))
