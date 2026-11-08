// Long-TEMP adapter used by Classic's existing process-local Detours hooks.
// GPL-3.0-or-later. No environment, filesystem or system policy mutations.
#pragma once
#include "progressivetemppath_win.h"
#include <algorithm>
namespace foxicmp { namespace progressive {
using TemporaryPathFunction=DWORD(WINAPI*)(DWORD,LPWSTR);
inline DWORD LongTemporaryPath(DWORD capacity,LPWSTR output,TemporaryPathFunction original) noexcept {
    const auto savedError=GetLastError();
    try {
        const auto directory=WindowsTemporaryDirectory();
        // Keep native behavior for the usual paths, including empty/relative
        // environment fallback, short-name expansion and buffer error details.
        if(directory.size()<MAX_PATH)return original(capacity,output);
        if(capacity && !output){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
        const auto required=DWORD(directory.size()+1);
        if(capacity<required){
            if(capacity)output[0]=L'\0';
            SetLastError(savedError);return required;
        }
        std::copy(directory.begin(),directory.end(),output);output[directory.size()]=L'\0';
        SetLastError(savedError);return DWORD(directory.size());
    }catch(...){
        if(capacity && output)output[0]=L'\0';
        SetLastError(ERROR_BAD_PATHNAME);return 0;
    }
}
}} // namespace foxicmp::progressive
