// Local DOS paths for the Windows legacy progressive adapters. GPL-3.0-or-later.
#pragma once
#include <windows.h>
#include <stdexcept>
#include <string>

namespace foxicmp { namespace progressive { namespace local_path_detail {
// Leave room for the extended prefix and terminator, independently of the
// process manifest / LongPathsEnabled policy. IPC continues to use DOS paths.
constexpr size_t MaxDosPath = 32760;
inline std::wstring Native(const std::wstring& canonical) { return L"\\\\?\\" + canonical; }
inline std::wstring Canonical(const std::wstring& path, bool inspect = true) {
    if (path.empty() || path.size() > MaxDosPath || path.find(L'\0') != std::wstring::npos ||
        path.find(L"://") != std::wstring::npos)
        throw std::runtime_error("Invalid local session path");
    const DWORD capacity = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!capacity || capacity > MaxDosPath + 1)
        throw std::runtime_error("Invalid local session path length");
    std::wstring full(capacity, L'\0');
    const DWORD count = GetFullPathNameW(path.c_str(), capacity, full.data(), nullptr);
    if (count < 3 || count >= capacity || full[1] != L':' || full[2] != L'\\' ||
        GetDriveTypeW(full.substr(0, 3).c_str()) != DRIVE_FIXED)
        throw std::runtime_error("Session must use a local fixed drive");
    full.resize(count);
    if (full.find(L':', 2) != std::wstring::npos)
        throw std::runtime_error("Alternate data streams are not allowed");
    for (size_t i = 3; i <= full.size(); ++i) {
        if (i != full.size() && full[i] != L'\\') continue;
        // Do not give trailing dots/spaces a new meaning by adding \\?\.
        if (i > 3 && (full[i-1] == L'.' || full[i-1] == L' '))
            throw std::runtime_error("Ambiguous local path component");
        if (!inspect) continue;
        const DWORD attributes = GetFileAttributesW(Native(full.substr(0, i)).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Missing file or reparse point in session path");
    }
    return full;
}
inline HANDLE Open(const std::wstring& path, bool asynchronous = false) {
    const auto full = Canonical(path);
    const auto native = Native(full);
    HANDLE file = CreateFileW(native.c_str(), GENERIC_READ | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | (asynchronous ? FILE_FLAG_OVERLAPPED : 0), nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open session file with read/write sharing");
    try {
        BY_HANDLE_FILE_INFORMATION info{};
        std::wstring final(MaxDosPath + 5, L'\0');
        const DWORD count = GetFinalPathNameByHandleW(file, final.data(), DWORD(final.size()),
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!GetFileInformationByHandle(file, &info) || GetFileType(file) != FILE_TYPE_DISK ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
            !count || count >= final.size() || _wcsicmp(final.c_str(), native.c_str()))
            throw std::runtime_error("Session path changed or is not a regular local file");
    } catch (...) { CloseHandle(file); throw; }
    return file;
}
}}} // namespace foxicmp::progressive::local_path_detail
