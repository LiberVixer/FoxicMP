"""Temporary process-local handle tracing; never changes registry or installed apps."""
import ctypes,json
from ctypes import wintypes as w
from progressive_process_metrics import windows_handle_types
class Trace:
 def __init__(self,pid):
  self.pid=pid;self.k=ctypes.WinDLL('kernel32',use_last_error=True);self.nt=ctypes.WinDLL('ntdll');self.k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];self.k.OpenProcess.restype=w.HANDLE;self.k.CloseHandle.argtypes=[w.HANDLE]
  self.process=self.k.OpenProcess(0x650,False,pid)
  if not self.process:raise ctypes.WinError(ctypes.get_last_error())
  self.nt.NtSetInformationProcess.argtypes=[w.HANDLE,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong];self.nt.NtSetInformationProcess.restype=ctypes.c_long
  options=(ctypes.c_ulong*2)(0,0x20000);status=self.nt.NtSetInformationProcess(self.process,32,options,ctypes.sizeof(options))
  if status<0:raise RuntimeError(('Enable private handle tracing',status))
  self.before=windows_handle_types(pid,True)
 def finish(self):
  after=windows_handle_types(self.pid,True);old={v['handle'] for v in self.before[1]};fresh=[v for v in after[1] if v['handle'] not in old and v['type'] in ('Thread','Event')][-12:]
  class Entry(ctypes.Structure):_fields_=[('handle',w.HANDLE),('pid',w.HANDLE),('tid',w.HANDLE),('type',ctypes.c_ulong),('stacks',ctypes.c_void_p*16)]
  self.nt.NtQueryInformationProcess.argtypes=[w.HANDLE,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.POINTER(ctypes.c_ulong)];self.nt.NtQueryInformationProcess.restype=ctypes.c_long
  modules=self.modules();out=[]
  for handle in fresh:
   data=ctypes.create_string_buffer(65536);ctypes.c_void_p.from_buffer(data).value=handle['handle'];needed=ctypes.c_ulong();status=self.nt.NtQueryInformationProcess(self.process,32,data,len(data),ctypes.byref(needed))
   if status<0:out.append(dict(**handle,error=status));continue
   total=ctypes.c_ulong.from_buffer(data,8).value;assert 16+total*ctypes.sizeof(Entry)<=len(data)
   traces=[]
   for i in range(total):
    e=Entry.from_buffer(data,16+i*ctypes.sizeof(Entry))
    if e.type!=1:continue
    stacks=[]
    for ip in e.stacks:
     if not ip:continue
     m=next((m for m in modules if m['base']<=ip<m['base']+m['size']),None);stacks.append(dict(address=hex(ip),module=m['name'] if m else '?',offset=hex(ip-m['base']) if m else None))
    traces.append(dict(type=e.type,tid=int(e.tid or 0),stacks=stacks))
   out.append(dict(**handle,traces=traces))
  self.nt.NtSetInformationProcess(self.process,32,None,0);self.k.CloseHandle(self.process)
  return dict(before=self.before[0],after=after[0],fresh=out,modules=modules)
 def modules(self):
  class Module(ctypes.Structure):_fields_=[('dwSize',w.DWORD),('id',w.DWORD),('pid',w.DWORD),('globalCount',w.DWORD),('processCount',w.DWORD),('base',ctypes.c_void_p),('size',w.DWORD),('hModule',w.HMODULE),('name',w.WCHAR*256),('path',w.WCHAR*260)]
  k=self.k;k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE;snap=k.CreateToolhelp32Snapshot(0x18,self.pid)
  if snap==ctypes.c_void_p(-1).value:raise ctypes.WinError(ctypes.get_last_error())
  try:
   for name in ('Module32FirstW','Module32NextW'):
    f=getattr(k,name);f.argtypes=[w.HANDLE,ctypes.POINTER(Module)];f.restype=w.BOOL
   e=Module();e.dwSize=ctypes.sizeof(e);more=k.Module32FirstW(snap,ctypes.byref(e));out=[]
   while more:out.append(dict(name=e.name,path=e.path,base=int(e.base),size=e.size));more=k.Module32NextW(snap,ctypes.byref(e))
   return out
  finally:k.CloseHandle(snap)
