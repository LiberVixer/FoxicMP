// Close a splitter worker's kernel reference before publishing its exit.
// CAMThread::Close may take it first; the exchange makes ownership exclusive.
#pragma once

inline void CloseSplitterWorkerHandleOnExit(HANDLE& threadHandle)
{
    HANDLE thread = (HANDLE)InterlockedExchangePointer(&threadHandle, nullptr);
    if (thread) CloseHandle(thread);
}
