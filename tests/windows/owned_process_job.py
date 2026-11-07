"""Private test-driver job: killing the driver also terminates its descendants.

Call finish only after every owned child has exited. On a hard kill/exception,
Windows closes the non-inherited job handle and applies KILL_ON_JOB_CLOSE.
"""
import ctypes as C

class OwnedProcessJob:
 def __init__(self):
  from ctypes import wintypes as W
  class Basic(C.Structure):_fields_=[('procTime',C.c_int64),('jobTime',C.c_int64),('flags',W.DWORD),('minWs',C.c_size_t),('maxWs',C.c_size_t),('active',W.DWORD),('affinity',C.c_size_t),('priority',W.DWORD),('sched',W.DWORD)]
  class IO(C.Structure):_fields_=[(s,C.c_uint64) for s in ['readOps','writeOps','otherOps','readBytes','writeBytes','otherBytes']]
  class Extended(C.Structure):_fields_=[('basic',Basic),('io',IO),('procMem',C.c_size_t),('jobMem',C.c_size_t),('peakProc',C.c_size_t),('peakJob',C.c_size_t)]
  self.k=C.WinDLL('kernel32',use_last_error=True);self.limits=Extended();self.limits.basic.flags=0x2000
  for name,restype,argtypes in [('CreateJobObjectW',W.HANDLE,[C.c_void_p,W.LPCWSTR]),('SetInformationJobObject',W.BOOL,[W.HANDLE,C.c_int,C.c_void_p,W.DWORD]),('GetCurrentProcess',W.HANDLE,[]),('AssignProcessToJobObject',W.BOOL,[W.HANDLE,W.HANDLE]),('CloseHandle',W.BOOL,[W.HANDLE])]:
   f=getattr(self.k,name);f.restype=restype;f.argtypes=argtypes
  self.h=self.k.CreateJobObjectW(None,None);assert self.h
  assert self.k.SetInformationJobObject(self.h,9,C.byref(self.limits),C.sizeof(self.limits)),C.WinError(C.get_last_error())
  assert self.k.AssignProcessToJobObject(self.h,self.k.GetCurrentProcess()),C.WinError(C.get_last_error())
 def finish(self):
  self.limits.basic.flags=0
  assert self.k.SetInformationJobObject(self.h,9,C.byref(self.limits),C.sizeof(self.limits))
  self.k.CloseHandle(self.h)
