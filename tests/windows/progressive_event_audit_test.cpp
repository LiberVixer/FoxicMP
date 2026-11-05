// Native self-test; no player/registry/desktop settings are accessed.
#include "progressive_event_audit.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
#include <winternl.h>

bool enabled;
void CALLBACK fixtureHook(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD) {}
void tracked(HANDLE h,bool present=true) {
    if(enabled){
        const auto& e=FoxicMPEventAuditData.entries[(uintptr_t(h)>>2)&(EventAuditSlots-1)];
        if((e.handle==uint64_t(uintptr_t(h)))!=present)
            fprintf(stderr,"Tracking mismatch: h=%p present=%d slot=%llx kind=%u recorded=%lld closed=%lld\n",h,present,e.handle,e.kind,FoxicMPEventAuditData.recorded,FoxicMPEventAuditData.closed);
        assert((e.handle==uint64_t(uintptr_t(h)))==present);
    }
}
int wmain(int argc,wchar_t** argv) {
    enabled=argc>=2 && argv[1][0]==L'1';
    assert(bool(FoxicMPEventAuditData.enabled)==enabled && !FoxicMPEventAuditData.error);
    DWORD errors[8]{};
    HANDLE handles[4];
    SetLastError(0x511);handles[0]=CreateEventW(nullptr,FALSE,FALSE,nullptr);errors[0]=GetLastError();
    SetLastError(0x512);handles[1]=CreateEventA(nullptr,TRUE,FALSE,nullptr);errors[1]=GetLastError();
    SetLastError(0x513);handles[2]=CreateEventExW(nullptr,nullptr,CREATE_EVENT_MANUAL_RESET,EVENT_ALL_ACCESS);errors[2]=GetLastError();
    SetLastError(0x514);handles[3]=CreateEventExA(nullptr,nullptr,0,EVENT_ALL_ACCESS);errors[3]=GetLastError();
    for(auto h:handles){assert(h);tracked(h);assert(WaitForSingleObject(h,0)==WAIT_TIMEOUT);assert(SetEvent(h));assert(WaitForSingleObject(h,0)==WAIT_OBJECT_0);assert(CloseHandle(h));tracked(h,false);}
    wchar_t name[96];swprintf_s(name,L"Local\\FoxicMP-event-audit-fixture-%lu",GetCurrentProcessId());
    HANDLE named=CreateEventW(nullptr,TRUE,FALSE,name);assert(named);tracked(named);
    HANDLE again=CreateEventW(nullptr,TRUE,FALSE,name);errors[4]=GetLastError();assert(again && errors[4]==ERROR_ALREADY_EXISTS);tracked(again);
    HANDLE opened=OpenEventW(EVENT_ALL_ACCESS,FALSE,name);assert(opened);tracked(opened);
    char narrow[96];sprintf_s(narrow,"Local\\FoxicMP-event-audit-fixture-%lu",GetCurrentProcessId());
    HANDLE openedA=OpenEventA(EVENT_ALL_ACCESS,FALSE,narrow);assert(openedA);tracked(openedA);
    const auto oldSequence=FoxicMPEventAuditData.entries[(uintptr_t(named)>>2)&(EventAuditSlots-1)].sequence;
    HANDLE copied=nullptr;SetLastError(0x611);
    assert(DuplicateHandle(GetCurrentProcess(),named,GetCurrentProcess(),&copied,0,FALSE,DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE));
    errors[5]=GetLastError();
    // DUPLICATE_CLOSE_SOURCE may reuse the source's numeric value immediately.
    if(named!=copied)tracked(named,false);
    tracked(copied);
    if(enabled)assert(FoxicMPEventAuditData.entries[(uintptr_t(copied)>>2)&(EventAuditSlots-1)].sequence>oldSequence);
    assert(CloseHandle(copied));tracked(copied,false);
    assert(CloseHandle(again));assert(CloseHandle(opened));assert(CloseHandle(openedA));
    using NtCreate=NTSTATUS(NTAPI*)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,int,BOOLEAN);
    auto native=reinterpret_cast<NtCreate>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtCreateEvent"));
    HANDLE direct=nullptr;SetLastError(0x711);assert(native(&direct,EVENT_ALL_ACCESS,nullptr,1,FALSE)>=0);errors[6]=GetLastError();tracked(direct);assert(CloseHandle(direct));tracked(direct,false);
    HANDLE limited=OpenProcess(PROCESS_DUP_HANDLE,FALSE,GetCurrentProcessId());assert(limited);
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);HANDLE unclassified=nullptr;SetLastError(0x811);
    assert(DuplicateHandle(GetCurrentProcess(),event,limited,&unclassified,0,FALSE,DUPLICATE_SAME_ACCESS));errors[7]=GetLastError();
    assert(CloseHandle(unclassified));assert(CloseHandle(event));assert(CloseHandle(limited));
    std::vector<std::thread> workers;
    for(unsigned worker=0;worker<4;++worker)workers.emplace_back([]{
        for(unsigned round=0;round<64;++round){
            HANDLE h=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(h);tracked(h);
            HANDLE copy=nullptr;assert(DuplicateHandle(GetCurrentProcess(),h,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS));tracked(copy);
            assert(SetEvent(h));assert(WaitForSingleObject(copy,0)==WAIT_OBJECT_0);
            assert(CloseHandle(copy));assert(CloseHandle(h));
        }
    });
    for(auto& worker:workers)worker.join();
    auto hook=SetWinEventHook(EVENT_OBJECT_DESTROY,EVENT_OBJECT_DESTROY,nullptr,fixtureHook,
        GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);assert(hook);
    assert(UnhookWinEvent(hook));
    if(enabled) {
        assert(FoxicMPEventAuditHooks.used==2);
        assert(FoxicMPEventAuditHooks.rows[0].hook==uint64_t(uintptr_t(hook)));
        assert(FoxicMPEventAuditHooks.rows[0].thread==GetCurrentThreadId());
        assert(FoxicMPEventAuditHooks.rows[0].threadCreated && FoxicMPEventAuditHooks.rows[0].kind==1);
        assert(FoxicMPEventAuditHooks.rows[1].kind==2 && FoxicMPEventAuditHooks.rows[1].result);
    }
    if(FoxicMPEventAuditStacks.enabled) {
        swprintf_s(name,L"Local\\DwmDxBltEvent_FoxicMP-fixture-%lu",GetCurrentProcessId());
        HANDLE stackEvent=CreateEventW(nullptr,FALSE,FALSE,name);assert(stackEvent);
        assert(FoxicMPEventAuditStacks.used==1 && FoxicMPEventAuditStacks.rows[0].ready);
        assert(FoxicMPEventAuditStacks.rows[0].handle==uint64_t(uintptr_t(stackEvent)));
        assert(FoxicMPEventAuditStacks.rows[0].count>1 && FoxicMPEventAuditStacks.rows[0].count<=24);
        assert(CloseHandle(stackEvent));
    }
    if(enabled){assert(FoxicMPEventAuditData.recorded>=523);assert(FoxicMPEventAuditData.moduleUsed>0);assert(!FoxicMPEventAuditData.collisions && !FoxicMPEventAuditData.moduleOverflow);}
    HANDLE held=argc>2?CreateEventW(nullptr,FALSE,FALSE,nullptr):nullptr;
    printf("{\"result\":\"PASS\",\"enabled\":%s,\"lastErrors\":[",enabled?"true":"false");
    for(unsigned i=0;i<8;++i)printf("%s%lu",i?",":"",errors[i]);
    printf("],\"recorded\":%lld,\"closed\":%lld,\"held\":%llu}\n",FoxicMPEventAuditData.recorded,FoxicMPEventAuditData.closed,uint64_t(uintptr_t(held)));
    fflush(stdout);
    if(argc>2)getchar();
    if(held)assert(CloseHandle(held));
}
