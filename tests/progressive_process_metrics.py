"""Read-only process measurements for isolated test processes (standard library)."""
import ctypes,os
from pathlib import Path

def sample(pid):
 if os.name!='nt':
  v={a:b.strip().split()[0] for a,b in (s.split(':',1) for s in (Path('/proc')/str(pid)/'status').read_text().splitlines() if ':' in s and s.split(':',1)[0] in {'VmRSS','RssAnon','Threads'})}
  return dict(rssBytes=int(v['VmRSS'])*1024,privateBytes=int(v['RssAnon'])*1024,handles=len(list((Path('/proc')/str(pid)/'fd').iterdir())),threads=int(v['Threads']),memorySource='proc-status-VmRSS-RssAnon')
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
  return dict(rssBytes=mem.WorkingSetSize,privateBytes=mem.PrivateUsage,handles=count.value,memorySource='GetProcessMemoryInfo-WorkingSetSize-PrivateUsage')
 finally:k.CloseHandle(handle)
