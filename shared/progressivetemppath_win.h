// Progressive runtime directory selection on Windows. GPL-3.0-or-later.
#pragma once
#include <windows.h>
#include <stdexcept>
#include <initializer_list>
#include <utility>
#include <string>

namespace foxicmp { namespace progressive {
// GetTempPathW can block on long TMP in a longPathAware process on affected
// Windows hosts. Read its documented environment precedence directly, then
// normalize dynamically. Registry I/O still enforces owner/ACL/reparse guards.
inline std::wstring WindowsTemporaryDirectory() {
    constexpr DWORD limit=32768;
    std::wstring selected;
    for(const auto name:{L"TMP",L"TEMP",L"USERPROFILE"}) {
        for(unsigned attempt=0;attempt<4;++attempt) {
            SetLastError(ERROR_SUCCESS);
            const auto capacity=GetEnvironmentVariableW(name,nullptr,0);
            if(!capacity) {
                if(GetLastError()!=ERROR_SUCCESS && GetLastError()!=ERROR_ENVVAR_NOT_FOUND)
                    throw std::runtime_error("Cannot read temporary-directory environment");
                break;
            }
            if(capacity>limit)throw std::runtime_error("Temporary directory exceeds path limit");
            std::wstring buffer(capacity,L'\0');
            const auto count=GetEnvironmentVariableW(name,buffer.data(),capacity);
            if(count>=capacity)continue; // Environment changed; retry within a bound.
            if(!count) {
                if(GetLastError()!=ERROR_SUCCESS && GetLastError()!=ERROR_ENVVAR_NOT_FOUND)
                    throw std::runtime_error("Cannot read temporary-directory environment");
                break;
            }
            buffer.resize(count);selected=std::move(buffer);break;
        }
        if(!selected.empty())break;
        // A racing environment must not silently select a different directory.
        if(GetEnvironmentVariableW(name,nullptr,0)>1)throw std::runtime_error("Temporary-directory environment is unstable");
    }
    if(selected.empty()) {
        std::wstring buffer(limit,L'\0');
        const auto count=GetWindowsDirectoryW(buffer.data(),limit);
        if(!count || count>=limit)throw std::runtime_error("Cannot obtain Windows directory");
        buffer.resize(count);selected=std::move(buffer);
    }
    const auto capacity=GetFullPathNameW(selected.c_str(),0,nullptr,nullptr);
    if(!capacity || capacity>limit)throw std::runtime_error("Invalid temporary-directory path length");
    std::wstring full(capacity,L'\0');
    const auto count=GetFullPathNameW(selected.c_str(),capacity,full.data(),nullptr);
    if(!count || count>=capacity)throw std::runtime_error("Cannot normalize temporary directory");
    full.resize(count);
    if(full.back()!=L'\\')full+=L'\\';
    if(full.size()>=limit)throw std::runtime_error("Temporary directory exceeds path limit");
    return full;
}
}} // namespace foxicmp::progressive
