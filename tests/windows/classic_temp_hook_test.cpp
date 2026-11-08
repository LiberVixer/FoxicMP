// Exercise the actual Classic long-TEMP adapter and process-local Detours.
#include <windows.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "../../shared/progressivetemppathhook_win.h"
#include "ExtLib/Detours/src/detours.h"
static foxicmp::progressive::TemporaryPathFunction original=GetTempPathW;
static DWORD WINAPI adapter(DWORD capacity,LPWSTR buffer){return foxicmp::progressive::LongTemporaryPath(capacity,buffer,original);}
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int wmain(int argc,wchar_t** argv){
    try {
        check(argc==2,"Expected --short or --long");
        const bool longCase=std::wstring(argv[1])==L"--long";
        const auto expected=foxicmp::progressive::WindowsTemporaryDirectory();
        check((expected.size()>=MAX_PATH)==longCase,"Unexpected fixture TEMP length");
        std::vector<wchar_t> before(32768);
        DWORD nativeLength=0;
        if(!longCase){nativeLength=GetTempPathW(DWORD(before.size()),before.data());check(nativeLength>0 && nativeLength<before.size(),"Native baseline");}
        check(DetourTransactionBegin()==NO_ERROR,"Begin hook");
        check(DetourUpdateThread(GetCurrentThread())==NO_ERROR,"Update thread");
        check(DetourAttach(reinterpret_cast<PVOID*>(&original),adapter)==NO_ERROR,"Attach hook");
        check(DetourTransactionCommit()==NO_ERROR,"Commit hook");
        auto cleanup=[](){check(DetourTransactionBegin()==NO_ERROR,"Begin cleanup");check(DetourUpdateThread(GetCurrentThread())==NO_ERROR,"Update cleanup");check(DetourDetach(reinterpret_cast<PVOID*>(&original),adapter)==NO_ERROR,"Detach");check(DetourTransactionCommit()==NO_ERROR,"Commit cleanup");};
        try {
            std::vector<wchar_t> result(32768);const auto count=GetTempPathW(DWORD(result.size()),result.data());
            check(count && count<result.size(),"Hooked directory result");
            if(longCase){
                check(std::wstring(result.data(),count)==expected,"Hook changed exact root");
                check(GetTempPathW(0,nullptr)==expected.size()+1,"Required length query");
                wchar_t guard[3]={L'a',L'b',L'c'};
                check(GetTempPathW(1,guard)==expected.size()+1 && !guard[0] && guard[1]==L'b' && guard[2]==L'c',"Small buffer bounds");
                std::vector<wchar_t> exact(expected.size()+2,L'!');
                check(GetTempPathW(DWORD(expected.size()),exact.data())==expected.size()+1 && exact[expected.size()]==L'!',"Off-by-one capacity");
                check(GetTempPathW(DWORD(expected.size()+1),exact.data())==expected.size() && exact[expected.size()]==0 && exact.back()==L'!',"Exact capacity bounds");
                check(!GetTempPathW(1,nullptr) && GetLastError()==ERROR_INVALID_PARAMETER,"Null output");
                auto temp2=reinterpret_cast<foxicmp::progressive::TemporaryPathFunction>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetTempPath2W"));
                if(temp2){check(temp2(DWORD(result.size()),result.data())==expected.size() && std::wstring(result.data())==expected,"GetTempPath2 delegation");}
            }else{
                check(count==nativeLength && std::wstring(result.data())==std::wstring(before.data()),"Short TEMP changed");
                for(DWORD size:std::array<DWORD,4>{0,1,nativeLength,nativeLength+1}){
                    std::vector<wchar_t> a(32768,L'!'),b=a;
                    const auto x=GetTempPathW(size,a.data()),y=original(size,b.data());
                    check(x==y && a==b,"Short buffer native behavior changed");
                }
            }
            cleanup();
        }catch(...){cleanup();throw;}
        std::cout<<"{\"result\":\"PASS\",\"longCase\":"<<(longCase?"true":"false")<<",\"pathLength\":"<<expected.size()<<"}"<<std::endl;return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
