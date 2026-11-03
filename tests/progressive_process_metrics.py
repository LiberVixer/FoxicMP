"""Read-only process measurements for isolated test processes (standard library)."""
import ctypes,os
from pathlib import Path

def sample(pid):
 if os.name!='nt':
  v={a:b.strip().split()[0] for a,b in (s.split(':',1) for s in (Path('/proc')/str(pid)/'status').read_text().splitlines() if ':' in s and s.split(':',1)[0] in {'VmRSS','RssAnon','Threads'})}
  fields=(Path('/proc')/str(pid)/'stat').read_text().rsplit(')',1)[1].split();cpu=(int(fields[11])+int(fields[12]))/os.sysconf('SC_CLK_TCK')
  return dict(cpuSeconds=cpu,rssBytes=int(v['VmRSS'])*1024,privateBytes=int(v['RssAnon'])*1024,handles=len(list((Path('/proc')/str(pid)/'fd').iterdir())),threads=int(v['Threads']),memorySource='proc-status-VmRSS-RssAnon')
 from ctypes import wintypes
 class Memory(ctypes.Structure):
  _fields_=[('cb',wintypes.DWORD),('PageFaultCount',wintypes.DWORD)]+[(n,ctypes.c_size_t) for n in ('PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage')]
 k=ctypes.WinDLL('kernel32',use_last_error=True);k.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];k.OpenProcess.restype=wintypes.HANDLE;k.CloseHandle.argtypes=[wintypes.HANDLE];k.CloseHandle.restype=wintypes.BOOL
 handle=k.OpenProcess(0x410,False,pid)
 if not handle:raise ctypes.WinError(ctypes.get_last_error())
 try:
  k.K32GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(Memory),wintypes.DWORD];k.K32GetProcessMemoryInfo.restype=wintypes.BOOL;k.GetProcessHandleCount.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.DWORD)];k.GetProcessHandleCount.restype=wintypes.BOOL
  mem=Memory();mem.cb=ctypes.sizeof(mem);count=wintypes.DWORD()
  if not k.K32GetProcessMemoryInfo(handle,ctypes.byref(mem),mem.cb) or not k.GetProcessHandleCount(handle,ctypes.byref(count)):raise ctypes.WinError(ctypes.get_last_error())
  start,end,kernel,user=(wintypes.FILETIME() for _ in range(4));k.GetProcessTimes.argtypes=[wintypes.HANDLE]+[ctypes.POINTER(wintypes.FILETIME)]*4;k.GetProcessTimes.restype=wintypes.BOOL
  if not k.GetProcessTimes(handle,ctypes.byref(start),ctypes.byref(end),ctypes.byref(kernel),ctypes.byref(user)):raise ctypes.WinError(ctypes.get_last_error())
  ticks=lambda t:(int(t.dwHighDateTime)<<32)|int(t.dwLowDateTime)
  return dict(cpuSeconds=(ticks(kernel)+ticks(user))/10000000,threads=windows_threads(k,pid),rssBytes=mem.WorkingSetSize,privateBytes=mem.PrivateUsage,handles=count.value,memorySource='GetProcessMemoryInfo-WorkingSetSize-PrivateUsage')
 finally:k.CloseHandle(handle)

def windows_threads(kernel,pid):
 from ctypes import wintypes
 class Thread(ctypes.Structure):
  _fields_=[('dwSize',wintypes.DWORD),('cntUsage',wintypes.DWORD),('th32ThreadID',wintypes.DWORD),('th32OwnerProcessID',wintypes.DWORD),('tpBasePri',wintypes.LONG),('tpDeltaPri',wintypes.LONG),('dwFlags',wintypes.DWORD)]
 kernel.CreateToolhelp32Snapshot.argtypes=[wintypes.DWORD,wintypes.DWORD];kernel.CreateToolhelp32Snapshot.restype=wintypes.HANDLE
 snapshot=kernel.CreateToolhelp32Snapshot(4,0)
 if snapshot==ctypes.c_void_p(-1).value:raise ctypes.WinError(ctypes.get_last_error())
 try:
  for name in ('Thread32First','Thread32Next'):
   fn=getattr(kernel,name);fn.argtypes=[wintypes.HANDLE,ctypes.POINTER(Thread)];fn.restype=wintypes.BOOL
  entry=Thread();entry.dwSize=ctypes.sizeof(entry);count=0;more=kernel.Thread32First(snapshot,ctypes.byref(entry))
  if not more:raise ctypes.WinError(ctypes.get_last_error())
  while more:
   if entry.th32OwnerProcessID==pid:count+=1
   more=kernel.Thread32Next(snapshot,ctypes.byref(entry))
  if ctypes.get_last_error()!=18:raise ctypes.WinError(ctypes.get_last_error())
  return count
 finally:kernel.CloseHandle(snapshot)

def windows_handle_types(pid,detail=False):
 """Diagnostic only: classify duplicated handles of an isolated test PID."""
 from ctypes import wintypes
 from collections import Counter
 k=ctypes.WinDLL('kernel32',use_last_error=True);nt=ctypes.WinDLL('ntdll')
 k.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];k.OpenProcess.restype=wintypes.HANDLE;k.CloseHandle.argtypes=[wintypes.HANDLE]
 process=k.OpenProcess(0x450,False,pid)
 if not process:raise ctypes.WinError(ctypes.get_last_error())
 try:
  nt.NtQueryInformationProcess.argtypes=[wintypes.HANDLE,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.POINTER(ctypes.c_ulong)];nt.NtQueryInformationProcess.restype=ctypes.c_long
  capacity=65536
  while True:
   data=ctypes.create_string_buffer(capacity);needed=ctypes.c_ulong();status=nt.NtQueryInformationProcess(process,51,data,capacity,ctypes.byref(needed))
   if status>=0:break
   if status not in (-1073741820,-1073741789) or capacity>=16*1024**2:raise RuntimeError(('NtQueryInformationProcess diagnostic',status,needed.value))
   capacity=max(capacity*2,needed.value)
  class Entry(ctypes.Structure):
   _fields_=[('handle',wintypes.HANDLE),('handleCount',ctypes.c_size_t),('pointerCount',ctypes.c_size_t),('access',ctypes.c_ulong),('type',ctypes.c_ulong),('attributes',ctypes.c_ulong),('reserved',ctypes.c_ulong)]
  class String(ctypes.Structure):_fields_=[('length',ctypes.c_ushort),('maximum',ctypes.c_ushort),('buffer',ctypes.c_void_p)]
  k.GetCurrentProcess.restype=wintypes.HANDLE;k.DuplicateHandle.argtypes=[wintypes.HANDLE,wintypes.HANDLE,wintypes.HANDLE,ctypes.POINTER(wintypes.HANDLE),wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];k.DuplicateHandle.restype=wintypes.BOOL
  nt.NtQueryObject.argtypes=[wintypes.HANDLE,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.POINTER(ctypes.c_ulong)];nt.NtQueryObject.restype=ctypes.c_long
  counts=Counter();details=[];entries=int.from_bytes(data.raw[:ctypes.sizeof(ctypes.c_size_t)],'little');assert 16+entries*ctypes.sizeof(Entry)<=capacity
  for i in range(entries):
   entry=Entry.from_buffer(data,16+i*ctypes.sizeof(Entry));handle=wintypes.HANDLE()
   if not k.DuplicateHandle(process,entry.handle,k.GetCurrentProcess(),ctypes.byref(handle),0,False,2):continue
   try:
    out=ctypes.create_string_buffer(4096);needed=ctypes.c_ulong()
    status=nt.NtQueryObject(handle,2,out,len(out),ctypes.byref(needed))
    if status>=0:
     name=String.from_buffer(out);kind=ctypes.wstring_at(name.buffer,name.length//2);counts[kind]+=1;details.append(dict(handle=int(entry.handle),type=kind))
   finally:k.CloseHandle(handle)
  return (dict(counts),details) if detail else dict(counts)
 finally:k.CloseHandle(process)
