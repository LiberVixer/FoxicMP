// Preserve graph ownership while allowing STA sent-message handshakes.
#pragma once
#ifdef _WIN32
#include <windows.h>
namespace foxicmp { namespace progressive {
inline DWORD GraphMessageWait(HANDLE event,DWORD timeout=INFINITE) {
  const auto started=GetTickCount64();
  for(;;) {
    const auto elapsed=GetTickCount64()-started;
    const auto remaining=timeout==INFINITE?INFINITE:elapsed>=timeout?0:DWORD(timeout-elapsed);
    const DWORD result=MsgWaitForMultipleObjects(1,&event,FALSE,remaining,QS_SENDMESSAGE);
    if(result!=WAIT_OBJECT_0+1)return result;
    // Peek dispatches pending sent messages. Never remove posted commands,
    // timers, opens or quit here: they could reenter a graph being destroyed.
    MSG message{};PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);
  }
}
}}
#endif
