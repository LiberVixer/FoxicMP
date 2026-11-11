// Compile-only owned Classic main-window fixture. GPL-3.0-or-later.
#ifdef FOXICMP_PRIVATE_CLASSIC_SEEK_TEST
#pragma once
#include <windows.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <string>
namespace foxicmp { namespace progressive { namespace classic_seek_test {
struct State {HWND frame=nullptr;LONGLONG target=0;unsigned frameDepth=0,graphDepth=0;};
inline thread_local State state;
inline std::filesystem::path directory(){wchar_t path[32768];const DWORD n=GetEnvironmentVariableW(L"FOXICMP_PRIVATE_SEEK_DIRECTORY",path,32768);return n && n<32768?std::filesystem::path(path):std::filesystem::path();}
inline void Observe(const char* stage){
 if(!state.frameDepth)return;const auto root=directory();if(root.empty())return;
 APTTYPE apartment=APTTYPE_CURRENT;APTTYPEQUALIFIER qualifier=APTTYPEQUALIFIER_NONE;const auto hr=CoGetApartmentType(&apartment,&qualifier);
 std::ofstream out(root/L"trace.jsonl",std::ios::app);out<<"{\"stage\":\""<<stage<<"\",\"pid\":"<<GetCurrentProcessId()<<",\"tid\":"<<GetCurrentThreadId()<<",\"ownerTid\":"<<GetWindowThreadProcessId(state.frame,nullptr)<<",\"target100ns\":"<<state.target<<",\"frameDepth\":"<<state.frameDepth<<",\"graphDepth\":"<<state.graphDepth<<",\"apartmentHr\":"<<long(hr)<<",\"apartment\":"<<int(apartment)<<"}\n";
}
inline void Hit(const char* stage){
 if(!state.frameDepth)return;const auto root=directory();if(root.empty() || !std::filesystem::exists(root/L"arm"))return;
 std::ifstream arm(root/L"arm");std::string wanted;std::getline(arm,wanted);if(wanted!=stage)return;arm.close();std::error_code error;std::filesystem::remove(root/L"arm",error);if(error)return;
 Observe("barrier-enter");{std::ofstream ready(root/L"ready");ready<<stage;}
 const auto began=GetTickCount64();while(!std::filesystem::exists(root/L"release")){
  // Test-only nested pump: dispatch only this owned frame's real IPC request.
  // Peek also dispatches sent messages, allowing an explicit nested WM_TIMER.
  MSG message{};if(PeekMessageW(&message,state.frame,WM_APP+188,WM_APP+188,PM_REMOVE)){Observe("nested-ipc-dispatch");DispatchMessageW(&message);}else Sleep(1);
  if(GetTickCount64()-began>=15000){Observe("barrier-timeout");break;}
 }
 Observe("barrier-leave");
}
struct FrameScope {
 State saved;
 FrameScope(HWND frame,LONGLONG target):saved(state){state.frame=frame;state.target=target;++state.frameDepth;Observe("frame-enter");}
 ~FrameScope(){Observe("frame-leave");state=saved;}
};
struct GraphScope {
 bool active=false;
 GraphScope(){if(state.frameDepth){active=true;++state.graphDepth;Observe("graph-enter");Hit("graph-before");}}
 ~GraphScope(){if(active){Hit("graph-after");Observe("graph-leave");--state.graphDepth;}}
};
}}}
#endif
