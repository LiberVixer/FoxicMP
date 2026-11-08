// Diagnostic stacks of a directly owned Classic test child. GPL-3.0-or-later.
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "ExtLib/rapidjson/include/rapidjson/document.h"
#include "ExtLib/rapidjson/include/rapidjson/stringbuffer.h"
#include "ExtLib/rapidjson/include/rapidjson/writer.h"

struct Handle {
    HANDLE h;
    explicit Handle(HANDLE value):h(value){}
    ~Handle(){if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    Handle(const Handle&)=delete;
};
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(std::string(text)+": "+std::to_string(GetLastError()));}
static std::string utf8(const std::wstring& s){return std::filesystem::path(s).u8string();}
static DWORD parent(DWORD pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    check(snapshot.h!=INVALID_HANDLE_VALUE,"Process snapshot");PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    for(BOOL ok=Process32FirstW(snapshot.h,&entry);ok;ok=Process32NextW(snapshot.h,&entry))if(entry.th32ProcessID==pid)return entry.th32ParentProcessID;
    throw std::runtime_error("Process not found");
}
struct Symbols {
    HMODULE dll=nullptr;HANDLE process;bool ready=false;DWORD loadError=0;
    decltype(&StackWalk64) walk=nullptr;
    decltype(&SymInitialize) initialize=nullptr;
    decltype(&SymCleanup) cleanup=nullptr;
    decltype(&SymSetOptions) options=nullptr;
    decltype(&SymFunctionTableAccess64) table=nullptr;
    decltype(&SymGetModuleBase64) base=nullptr;
    decltype(&SymFromAddr) name=nullptr;
    Symbols(HANDLE h,const std::string& path):process(h) {
        wchar_t directory[32768];const auto n=GetSystemDirectoryW(directory,32768);check(n && n<32768,"System directory");
        const auto file=std::wstring(directory,n)+L"\\dbghelp.dll";
        dll=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);loadError=dll?0:GetLastError();if(!dll)return;
#define LOAD(member,api) member=reinterpret_cast<decltype(member)>(GetProcAddress(dll,#api))
        LOAD(walk,StackWalk64);LOAD(initialize,SymInitialize);LOAD(cleanup,SymCleanup);LOAD(options,SymSetOptions);
        LOAD(table,SymFunctionTableAccess64);LOAD(base,SymGetModuleBase64);LOAD(name,SymFromAddr);
#undef LOAD
        if(!walk || !initialize || !cleanup || !options || !table || !base || !name)return;
        options(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS|SYMOPT_IGNORE_NT_SYMPATH);
        // Explicit local-only search path; no symbol server or desktop settings.
        ready=initialize(process,path.c_str(),TRUE)!=FALSE;
    }
    ~Symbols(){if(ready)cleanup(process);if(dll)FreeLibrary(dll);}
};
struct Suspended {
    HANDLE thread;bool held=false;DWORD previous=0;
    explicit Suspended(HANDLE h):thread(h){previous=SuspendThread(h);check(previous!=DWORD(-1),"Suspend owned thread");held=true;}
    void resume(){if(held){check(ResumeThread(thread)!=DWORD(-1),"Resume owned thread");held=false;}}
    ~Suspended(){if(held)ResumeThread(thread);}
};
static rapidjson::Document capture(DWORD pid,const std::wstring& expected,const std::string& symbols,bool self=false) {
    using namespace rapidjson;
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid));check(process.h!=nullptr,"Open owned process");
    check(WaitForSingleObject(process.h,0)==WAIT_TIMEOUT,"Owned process exited");
    wchar_t image[32768];DWORD size=32768;check(QueryFullProcessImageNameW(process.h,0,image,&size)!=FALSE,"Query owned image");
    check(!_wcsicmp(std::wstring(image,size).c_str(),expected.c_str()),"Image ownership mismatch");
    if(!self){
        check(parent(pid)==parent(GetCurrentProcessId()),"Target is not a sibling child of this test driver");
        check(expected.find(L"foxicmp-classic-runtime-")!=std::wstring::npos,"Target is not a private Classic test copy");
    }
    FILETIME birth{},exit{},kernel{},user{};check(GetProcessTimes(process.h,&birth,&exit,&kernel,&user)!=FALSE,"Process identity");
    Document result;result.SetObject();auto& a=result.GetAllocator();
    result.AddMember("pid",uint32_t(pid),a);result.AddMember("birth",(uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime,a);
    const auto image8=utf8(std::wstring(image,size));result.AddMember("image",Value(image8.c_str(),a),a);
    Symbols dbg(process.h,symbols);result.AddMember("symbolHandlerReady",dbg.ready,a);result.AddMember("dbghelpLoadError",uint32_t(dbg.loadError),a);
    Value modules(kArrayType);
    Handle moduleSnapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
    check(moduleSnapshot.h!=INVALID_HANDLE_VALUE,"Module snapshot");MODULEENTRY32W m{};m.dwSize=sizeof(m);
    for(BOOL ok=Module32FirstW(moduleSnapshot.h,&m);ok;ok=Module32NextW(moduleSnapshot.h,&m)){
        Value row(kObjectType);const auto path=utf8(m.szExePath);row.AddMember("path",Value(path.c_str(),a),a);
        row.AddMember("base",uint64_t(reinterpret_cast<uintptr_t>(m.modBaseAddr)),a);row.AddMember("size",uint32_t(m.modBaseSize),a);modules.PushBack(row,a);
    }
    result.AddMember("modules",modules,a);
    Value threads(kArrayType);Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0));check(snapshot.h!=INVALID_HANDLE_VALUE,"Thread snapshot");
    THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
    for(BOOL ok=Thread32First(snapshot.h,&entry);ok;ok=Thread32Next(snapshot.h,&entry)){
        if(entry.th32OwnerProcessID!=pid || (self && entry.th32ThreadID==GetCurrentThreadId()))continue;
        Value row(kObjectType);row.AddMember("tid",uint32_t(entry.th32ThreadID),a);
        try {
            Handle thread(OpenThread(THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID));check(thread.h!=nullptr,"Open owned thread");
            check(GetProcessIdOfThread(thread.h)==pid && WaitForSingleObject(process.h,0)==WAIT_TIMEOUT,"Thread ownership mismatch");
            std::vector<uint64_t> addresses;const auto began=GetTickCount64();
            {
                Suspended stop(thread.h);row.AddMember("previousSuspendCount",uint32_t(stop.previous),a);
                CONTEXT context{};context.ContextFlags=CONTEXT_FULL;check(GetThreadContext(thread.h,&context)!=FALSE,"Get owned context");
                addresses.push_back(context.Rip);
                if(dbg.ready){
                    STACKFRAME64 frame{};frame.AddrPC={context.Rip,0,AddrModeFlat};frame.AddrStack={context.Rsp,0,AddrModeFlat};frame.AddrFrame={context.Rbp,0,AddrModeFlat};
                    for(unsigned i=0;i<47 && GetTickCount64()-began<3000;++i){
                        if(!dbg.walk(IMAGE_FILE_MACHINE_AMD64,process.h,thread.h,&frame,&context,nullptr,dbg.table,dbg.base,nullptr) || !frame.AddrPC.Offset)break;
                        if(frame.AddrPC.Offset!=addresses.back())addresses.push_back(frame.AddrPC.Offset);
                    }
                }
                stop.resume();
            }
            row.AddMember("suspendedMilliseconds",uint64_t(GetTickCount64()-began),a);
            // Resolve names only after resuming the thread.
            Value frames(kArrayType);
            for(const auto address:addresses){
                Value frame(kObjectType);frame.AddMember("pc",address,a);
                if(dbg.ready){
                    alignas(SYMBOL_INFO) char bytes[sizeof(SYMBOL_INFO)+2048]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(bytes);
                    symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=2047;DWORD64 displacement=0;
                    if(dbg.name(process.h,address,&displacement,symbol)){frame.AddMember("symbol",Value(symbol->Name,a),a);frame.AddMember("displacement",uint64_t(displacement),a);}
                }
                frames.PushBack(frame,a);
            }
            row.AddMember("frames",frames,a);
        }catch(const std::exception& error){row.AddMember("error",Value(error.what(),a),a);}
        threads.PushBack(row,a);
    }
    result.AddMember("threads",threads,a);return result;
}
int wmain(int argc,wchar_t** argv) {
    try {
        rapidjson::Document result;
        if(argc==2 && std::wstring(argv[1])==L"--self-test"){
            Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));check(event.h!=nullptr,"Fixture event");
            std::atomic<DWORD> workerId{0};
            std::thread worker([&]{workerId.store(GetCurrentThreadId());WaitForSingleObject(event.h,INFINITE);});
            try{
                wchar_t image[32768];DWORD size=32768;check(QueryFullProcessImageNameW(GetCurrentProcess(),0,image,&size)!=FALSE,"Fixture image");
                const auto deadline=GetTickCount64()+3000;
                while(!workerId.load() && GetTickCount64()<deadline)Sleep(1);
                check(workerId.load()!=0,"Fixture thread started");
                result=capture(GetCurrentProcessId(),std::wstring(image,size),".",true);
            }catch(...){SetEvent(event.h);worker.join();throw;}
            SetEvent(event.h);worker.join();check(result["symbolHandlerReady"].GetBool(),"Fixture symbols");
            bool captured=false;
            for(const auto& thread:result["threads"].GetArray()){
                if(thread["tid"].GetUint()==workerId.load())
                    captured=!thread.HasMember("error") && thread["previousSuspendCount"].GetUint()==0 && thread["frames"].Size()>=3;
            }
            check(captured,"Fixture waiting thread stack and suspension count");
            result.AddMember("fixtureWorkerJoined",true,result.GetAllocator());
        }else{
            check(argc==4,"Expected PID EXACT_PRIVATE_EXE LOCAL_SYMBOL_DIRECTORY");
            const auto pid=std::stoul(argv[1]);check(pid>0 && pid<=MAXDWORD,"Invalid PID");
            result=capture(DWORD(pid),argv[2],utf8(argv[3]));
        }
        rapidjson::StringBuffer output;rapidjson::Writer<rapidjson::StringBuffer> writer(output);result.Accept(writer);std::cout<<output.GetString()<<std::endl;return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
