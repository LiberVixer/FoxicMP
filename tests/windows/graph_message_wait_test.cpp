#include "../../shared/graphmessagewait.h"
#include <atomic>
#include <thread>
#include <iostream>
static std::atomic<unsigned> sent{0},posted{0};
static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_APP+1){++sent;return 73;}
    if(message==WM_APP+2){++posted;return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
int main(){
    WNDCLASSW type{};type.lpfnWndProc=windowProc;type.lpszClassName=L"FoxicMPGraphWaitTest";type.hInstance=GetModuleHandleW(nullptr);RegisterClassW(&type);
    HWND window=CreateWindowW(type.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,type.hInstance,nullptr);
    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!window || !event)return 1;
    PostMessageW(window,WM_APP+2,0,0);std::atomic<bool> acknowledged{false};
    std::thread worker([&]{DWORD_PTR reply=0;acknowledged=SendMessageTimeoutW(window,WM_APP+1,0,0,SMTO_ABORTIFHUNG,2000,&reply)!=0 && reply==73;SetEvent(event);});
    const auto result=foxicmp::progressive::GraphMessageWait(event,3000);worker.join();
    bool ok=result==WAIT_OBJECT_0 && acknowledged && sent==1 && posted==0;
    MSG message{};if(PeekMessageW(&message,window,WM_APP+2,WM_APP+2,PM_REMOVE))DispatchMessageW(&message);
    ok=ok && posted==1;ResetEvent(event);ok=ok && foxicmp::progressive::GraphMessageWait(event,10)==WAIT_TIMEOUT;
    CloseHandle(event);DestroyWindow(window);UnregisterClassW(type.lpszClassName,type.hInstance);
    std::cout<<"{\"result\":\""<<(ok?"PASS":"FAIL")<<"\",\"sentHandshake\":"<<sent<<",\"postedCommandDeferred\":true,\"timeoutWasNotCompletion\":true}"<<std::endl;return ok?0:1;
}
