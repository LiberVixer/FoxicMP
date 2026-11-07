#include "progressivefile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include "../../shared/progressivelocalpath_win.h"
#include <aclapi.h>
#include <vector>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace ProgressiveFile {
bool absolute(const QString &path) {
  if (!QFileInfo(path).isAbsolute()) return false;
#ifdef Q_OS_WIN
  const auto native = QDir::toNativeSeparators(QDir::cleanPath(path));
  return native.size() > 3 && native[1] == ':' && native[2] == '\\' &&
         !native.mid(2).contains(':') && GetDriveTypeW(reinterpret_cast<LPCWSTR>(native.left(3).utf16())) == DRIVE_FIXED;
#else
  return path.startsWith('/');
#endif
}
Info inspect(intptr_t file, uint64_t available) {
#ifdef Q_OS_WIN
  const HANDLE handle = reinterpret_cast<HANDLE>(file);
  BY_HANDLE_FILE_INFORMATION info{};
  if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || GetFileType(handle) != FILE_TYPE_DISK)
    throw std::runtime_error("Track is not a regular local file");
  const uint64_t size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
  if (size < available) throw std::runtime_error("Confirmed bytes are missing or the track was truncated");
  return {info.dwVolumeSerialNumber, (uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow, size};
#else
  struct stat info{};
  if (fstat(int(file), &info) || !S_ISREG(info.st_mode) || info.st_uid != getuid() ||
      info.st_size < 0 || uint64_t(info.st_size) < available)
    throw std::runtime_error("Track is not an owned regular file or confirmed bytes are missing");
  return {uint64_t(info.st_dev), uint64_t(info.st_ino), uint64_t(info.st_size)};
#endif
}
intptr_t open(const QString &path) {
#ifdef Q_OS_WIN
  if (!absolute(path)) throw std::runtime_error("Track must be on a local fixed drive");
  HANDLE handle = foxicmp::progressive::local_path_detail::Open(path.toStdWString());
  try {
    inspect(reinterpret_cast<intptr_t>(handle), 0);
    PSID owner = nullptr; PSECURITY_DESCRIPTOR security = nullptr;
    const DWORD result = GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &security);
    HANDLE token = nullptr; DWORD count = 0; bool owned = false;
    if (result == ERROR_SUCCESS && OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
      GetTokenInformation(token, TokenUser, nullptr, 0, &count);
      std::vector<BYTE> bytes(count);
      if (count && GetTokenInformation(token, TokenUser, bytes.data(), count, &count))
        owned = EqualSid(owner, reinterpret_cast<TOKEN_USER *>(bytes.data())->User.Sid);
      // Windows can assign the token's default owner (e.g. Administrators)
      // instead of its account SID when that account creates a file.
      count = 0; GetTokenInformation(token, TokenOwner, nullptr, 0, &count);
      bytes.resize(count);
      if (count && GetTokenInformation(token, TokenOwner, bytes.data(), count, &count))
        owned = owned || EqualSid(owner, reinterpret_cast<TOKEN_OWNER *>(bytes.data())->Owner);
    }
    if (token) CloseHandle(token);
    if (security) LocalFree(security);
    if (!owned) throw std::runtime_error("Track must belong to the current user");
  } catch (...) { CloseHandle(handle); throw; }
  return reinterpret_cast<intptr_t>(handle);
#else
  // O_NOFOLLOW on the final open alone still follows directory symlinks.
  // Walk from a pinned root, checking every component without a path-length
  // buffer or a check/open race against a replaced parent directory.
  const auto parts = QFile::encodeName(
      QDir::cleanPath(QFileInfo(path).absoluteFilePath())).split('/');
  int parent = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (parent < 0) return -1;
  for (int i = 1; i + 1 < parts.size(); ++i) {
    const int next = openat(parent, parts[i].constData(),
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    ::close(parent);
    if (next < 0) return -1;
    parent = next;
  }
  const int file = openat(parent, parts.last().constData(),
      O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  ::close(parent);
  return file;
#endif
}
int64_t read(intptr_t file, uint64_t offset, void *data, size_t count) {
  if (offset > uint64_t(INT64_MAX) || count > uint64_t(INT64_MAX) - offset) return -1;
#ifdef Q_OS_WIN
  // The source mutex serializes stream reads and index reads on each pinned handle.
  LARGE_INTEGER position{}; position.QuadPart = int64_t(offset);
  DWORD received = 0;
  if (count > MAXDWORD || !SetFilePointerEx(reinterpret_cast<HANDLE>(file), position, nullptr, FILE_BEGIN) ||
      !ReadFile(reinterpret_cast<HANDLE>(file), data, DWORD(count), &received, nullptr)) return -1;
  return received;
#else
  return pread(int(file), data, count, off_t(offset));
#endif
}
void close(intptr_t file) {
#ifdef Q_OS_WIN
  CloseHandle(reinterpret_cast<HANDLE>(file));
#else
  ::close(int(file));
#endif
}
}
