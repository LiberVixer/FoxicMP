// Not part of normal builds. No file I/O or remote injection.
// Enable only in a private test process with FOXICMP_EVENT_AUDIT=1.
#include "progressive_event_audit.h"
#include <winternl.h>
#include <tlhelp32.h>
#include <intrin.h>
#include "../../src/ExtLib/Detours/src/detours.h"

extern "C" __declspec(dllexport) EventAuditData FoxicMPEventAuditData = {
    0x31415645, 1, sizeof(EventAuditData), sizeof(EventAuditEntry), EventAuditSlots,
    sizeof(EventAuditModule), EventAuditModules
};
extern "C" __declspec(dllexport) EventAuditStacks FoxicMPEventAuditStacks = {
    0x31535645,1,sizeof(EventAuditStacks),sizeof(EventAuditStack),8
};
extern "C" __declspec(dllexport) EventAuditHooks FoxicMPEventAuditHooks = {
    0x31485645,1,sizeof(EventAuditHooks),sizeof(EventAuditHook),32
};
namespace {
SRWLOCK lock = SRWLOCK_INIT;
__declspec(thread) unsigned depth = 0;
__declspec(thread) void* origin = nullptr;
__declspec(thread) unsigned originKind = 0;
using NtCreate = NTSTATUS (NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, int, BOOLEAN);
using NtOpen = NTSTATUS (NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
using NtDuplicate = NTSTATUS (NTAPI*)(HANDLE, HANDLE, HANDLE, PHANDLE, ACCESS_MASK, ULONG, ULONG);
using NtCloseFn = NTSTATUS (NTAPI*)(HANDLE);
NtCreate createNative = nullptr;
NtOpen openNative = nullptr;
NtDuplicate duplicateNative = nullptr;
NtCloseFn closeNative = nullptr;
auto createW = &CreateEventW;
auto createA = &CreateEventA;
auto createExW = &CreateEventExW;
auto createExA = &CreateEventExA;
auto openW = &OpenEventW;
auto openA = &OpenEventA;
auto duplicate = &DuplicateHandle;
auto setHook = &SetWinEventHook;
auto unhook = &UnhookWinEvent;
struct Origin {
    explicit Origin(void* caller, unsigned kind) {
        if (!depth++) { origin=caller; originKind=kind; }
    }
    ~Origin() { if (!--depth) { origin=nullptr; originKind=0; } }
};
bool active() { return FoxicMPEventAuditData.enabled != 0; }
size_t index(HANDLE h) { return (uintptr_t(h)>>2)&(EventAuditSlots-1); }
void record(HANDLE h, void* caller, unsigned kind) {
    if (!h || h==INVALID_HANDLE_VALUE) return;
    const DWORD error=GetLastError();
    AcquireSRWLockExclusive(&lock);
    auto& data=FoxicMPEventAuditData;
    auto& entry=data.entries[index(h)];
    if (entry.handle && entry.handle!=uint64_t(uintptr_t(h))) InterlockedIncrement64(&data.collisions);
    entry={uint64_t(uintptr_t(h)),uint64_t(uintptr_t(caller)),uint64_t(InterlockedIncrement64(&data.recorded)),
           GetCurrentThreadId(),kind,uint32_t(data.moduleUsed),0};
    ReleaseSRWLockExclusive(&lock);
    SetLastError(error);
}
EventAuditEntry remove(HANDLE h) {
    const DWORD error=GetLastError();
    AcquireSRWLockExclusive(&lock);
    auto& entry=FoxicMPEventAuditData.entries[index(h)];
    EventAuditEntry old{};
    if (entry.handle==uint64_t(uintptr_t(h))) { old=entry; entry={}; InterlockedIncrement64(&FoxicMPEventAuditData.closed); }
    ReleaseSRWLockExclusive(&lock);
    SetLastError(error);
    return old;
}
void restore(const EventAuditEntry& old) {
    if (!old.handle) return;
    const DWORD error=GetLastError();
    AcquireSRWLockExclusive(&lock);
    auto& entry=FoxicMPEventAuditData.entries[index(HANDLE(uintptr_t(old.handle)))];
    if (!entry.handle) entry=old;
    ReleaseSRWLockExclusive(&lock);
    SetLastError(error);
}
bool current(HANDLE process) {
    if (process==GetCurrentProcess()) return true;
    const DWORD error=GetLastError();
    const bool result=process && GetProcessId(process)==GetCurrentProcessId();
    SetLastError(error);
    return result;
}
NTSTATUS NTAPI created(PHANDLE out,ACCESS_MASK access,POBJECT_ATTRIBUTES attributes,int type,BOOLEAN initial) {
    const auto result=createNative(out,access,attributes,type,initial);
    if (active() && result>=0 && out) record(*out,originKind==1?origin:_ReturnAddress(),1);
    return result;
}
NTSTATUS NTAPI opened(PHANDLE out,ACCESS_MASK access,POBJECT_ATTRIBUTES attributes) {
    const auto result=openNative(out,access,attributes);
    if (active() && result>=0 && out) record(*out,originKind==1?origin:_ReturnAddress(),2);
    return result;
}
NTSTATUS NTAPI closed(HANDLE h) {
    if (!active()) return closeNative(h);
    const auto old=remove(h); // Remove before the OS can reuse this numeric handle.
    const auto result=closeNative(h);
    if (result<0) restore(old);
    return result;
}
NTSTATUS NTAPI duplicated(HANDLE sourceProcess,HANDLE source,HANDLE targetProcess,PHANDLE target,
                         ACCESS_MASK access,ULONG attributes,ULONG options) {
    if (!active()) return duplicateNative(sourceProcess,source,targetProcess,target,access,attributes,options);
    if ((options&DUPLICATE_CLOSE_SOURCE) && current(sourceProcess)) remove(source);
    const auto result=duplicateNative(sourceProcess,source,targetProcess,target,access,attributes,options);
    const DWORD error=GetLastError();
    if (result>=0 && target) {
        if (current(targetProcess)) record(*target,originKind==2?origin:_ReturnAddress(),3);
        else if (!targetProcess || GetProcessId(targetProcess)==0) InterlockedIncrement64(&FoxicMPEventAuditData.unidentifiedDuplicates);
    }
    SetLastError(error);
    return result;
}
HANDLE WINAPI madeW(LPSECURITY_ATTRIBUTES a,BOOL manual,BOOL initial,LPCWSTR name) {
    Origin o(_ReturnAddress(),1);
    HANDLE h=createW(a,manual,initial,name);
    const DWORD error=GetLastError();
    auto& stacks=FoxicMPEventAuditStacks;
    if(h && stacks.enabled && name && wcsstr(name,L"DwmDxBltEvent_")) {
        const LONG slot=InterlockedIncrement(&stacks.used)-1;
        if(slot>=0 && slot<LONG(stacks.capacity)) {
            auto& row=stacks.rows[slot];
            row.handle=uint64_t(uintptr_t(h));row.thread=GetCurrentThreadId();
            row.count=CaptureStackBackTrace(0,_countof(row.frames),reinterpret_cast<PVOID*>(row.frames),nullptr);
            row.moduleSequence=FoxicMPEventAuditData.moduleUsed;
            InterlockedExchange(&row.ready,1);
        }
    }
    SetLastError(error);
    return h;
}
HANDLE WINAPI madeA(LPSECURITY_ATTRIBUTES a,BOOL manual,BOOL initial,LPCSTR name) { Origin o(_ReturnAddress(),1); return createA(a,manual,initial,name); }
HANDLE WINAPI madeExW(LPSECURITY_ATTRIBUTES a,LPCWSTR name,DWORD flags,DWORD access) { Origin o(_ReturnAddress(),1); return createExW(a,name,flags,access); }
HANDLE WINAPI madeExA(LPSECURITY_ATTRIBUTES a,LPCSTR name,DWORD flags,DWORD access) { Origin o(_ReturnAddress(),1); return createExA(a,name,flags,access); }
HANDLE WINAPI foundW(DWORD access,BOOL inherit,LPCWSTR name) { Origin o(_ReturnAddress(),1); return openW(access,inherit,name); }
HANDLE WINAPI foundA(DWORD access,BOOL inherit,LPCSTR name) { Origin o(_ReturnAddress(),1); return openA(access,inherit,name); }
BOOL WINAPI copied(HANDLE sp,HANDLE sh,HANDLE tp,LPHANDLE th,DWORD access,BOOL inherit,DWORD options) { Origin o(_ReturnAddress(),2); return duplicate(sp,sh,tp,th,access,inherit,options); }
void hookRecord(HWINEVENTHOOK hook,void* caller,WINEVENTPROC callback,DWORD low,DWORD high,
                DWORD process,DWORD thread,DWORD flags,unsigned kind,BOOL result) {
    if(!active())return;
    const LONG index=InterlockedIncrement(&FoxicMPEventAuditHooks.used)-1;
    if(index<0 || index>=LONG(FoxicMPEventAuditHooks.capacity))return;
    auto& row=FoxicMPEventAuditHooks.rows[index];
    FILETIME created{},exited{},kernel{},user{};
    GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user);
    row={uint64_t(uintptr_t(hook)),uint64_t(uintptr_t(caller)),uint64_t(uintptr_t(callback)),
        uint64_t(created.dwLowDateTime)|(uint64_t(created.dwHighDateTime)<<32),
        GetCurrentThreadId(),low,high,process,thread,flags,kind,uint32_t(result),0,0};
    InterlockedExchange(&row.ready,1);
}
HWINEVENTHOOK WINAPI hooked(DWORD low,DWORD high,HMODULE module,WINEVENTPROC callback,
                           DWORD process,DWORD thread,DWORD flags) {
    auto result=setHook(low,high,module,callback,process,thread,flags);
    const DWORD error=GetLastError();
    hookRecord(result,_ReturnAddress(),callback,low,high,process,thread,flags,1,result!=nullptr);
    SetLastError(error);return result;
}
BOOL WINAPI unhooked(HWINEVENTHOOK hook) {
    const auto result=unhook(hook);const DWORD error=GetLastError();
    hookRecord(hook,_ReturnAddress(),nullptr,0,0,0,0,0,2,result);
    SetLastError(error);return result;
}

void module(uint64_t base,uint64_t size,const wchar_t* name,size_t chars,unsigned reason) {
    auto& data=FoxicMPEventAuditData;
    const auto sequence=InterlockedIncrement(&data.moduleUsed);
    if (sequence>EventAuditModules) { InterlockedIncrement64(&data.moduleOverflow); return; }
    auto& row=data.modules[sequence-1];
    row.base=base;row.size=size;row.sequence=sequence;row.reason=reason;
    size_t n=0;while(n<chars && n+1<_countof(row.name)) {row.name[n]=name[n];++n;}
    row.name[n]=0;InterlockedExchange(reinterpret_cast<volatile LONG*>(&row.ready),1);
}
struct DllInfo { ULONG flags; PCUNICODE_STRING fullName,baseName; PVOID base; ULONG size; };
union DllData { DllInfo loaded,unloaded; };
void CALLBACK notification(ULONG reason,const DllData* data,PVOID) {
    if (reason!=1 && reason!=2) return;
    const auto& d=data->loaded;
    if (d.baseName) module(uintptr_t(d.base),d.size,d.baseName->Buffer,d.baseName->Length/sizeof(wchar_t),reason);
}
struct Init {
    Init() {
        wchar_t enabled[2]{};
        if(GetEnvironmentVariableW(L"FOXICMP_EVENT_AUDIT",enabled,2)!=1 || enabled[0]!=L'1')return;
        auto ntdll=GetModuleHandleW(L"ntdll.dll");
        createNative=reinterpret_cast<NtCreate>(GetProcAddress(ntdll,"NtCreateEvent"));
        openNative=reinterpret_cast<NtOpen>(GetProcAddress(ntdll,"NtOpenEvent"));
        duplicateNative=reinterpret_cast<NtDuplicate>(GetProcAddress(ntdll,"NtDuplicateObject"));
        closeNative=reinterpret_cast<NtCloseFn>(GetProcAddress(ntdll,"NtClose"));
        if(!createNative || !openNative || !duplicateNative || !closeNative){FoxicMPEventAuditData.error=ERROR_PROC_NOT_FOUND;return;}
        LONG error=DetourTransactionBegin();
        if(!error)error=DetourUpdateThread(GetCurrentThread());
        const auto attach=[&](auto& from,auto to){if(!error)error=DetourAttach(reinterpret_cast<PVOID*>(&from),reinterpret_cast<PVOID>(to));};
        attach(createNative,created);attach(openNative,opened);attach(closeNative,closed);attach(duplicateNative,duplicated);
        attach(createW,madeW);attach(createA,madeA);attach(createExW,madeExW);attach(createExA,madeExA);
        attach(openW,foundW);attach(openA,foundA);attach(duplicate,copied);
        attach(setHook,hooked);attach(unhook,unhooked);
        if(error){DetourTransactionAbort();FoxicMPEventAuditData.error=error;return;}
        error=DetourTransactionCommit();FoxicMPEventAuditData.error=error;if(error)return;
        using Register=NTSTATUS(NTAPI*)(ULONG,decltype(&notification),PVOID,PVOID*);
        const auto subscribe=reinterpret_cast<Register>(GetProcAddress(ntdll,"LdrRegisterDllNotification"));
        static PVOID cookie=nullptr;
        if(!subscribe || subscribe(0,notification,nullptr,&cookie)<0){FoxicMPEventAuditData.error=ERROR_NOT_SUPPORTED;return;}
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
        if(snapshot!=INVALID_HANDLE_VALUE){MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);for(BOOL more=Module32FirstW(snapshot,&entry);more;more=Module32NextW(snapshot,&entry))module(uintptr_t(entry.modBaseAddr),entry.modBaseSize,entry.szModule,wcslen(entry.szModule),3);CloseHandle(snapshot);}
        InterlockedExchange(&FoxicMPEventAuditData.enabled,1);
        wchar_t stacks[2]{};
        if(GetEnvironmentVariableW(L"FOXICMP_EVENT_AUDIT_STACKS",stacks,2)==1 && stacks[0]==L'1')
            InterlockedExchange(&FoxicMPEventAuditStacks.enabled,1);
    }
} initialize;
}
