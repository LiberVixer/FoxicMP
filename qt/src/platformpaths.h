#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace PlatformPaths {
inline QString runtimeDirectory() {
#ifdef Q_OS_WIN
  const auto path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("FoxicMPq");
  return QDir().mkpath(path) ? path : QString();
#else
  return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
#endif
}
inline QString identity(const QString &directory) {
  const QFileInfo info(directory);
  auto path = info.canonicalFilePath();
  if (path.isEmpty()) path = info.absoluteFilePath();
#ifdef Q_OS_WIN
  path = QDir::cleanPath(path).toCaseFolded();
#endif
  return path;
}
inline bool moveNoReplace(const QString &source, const QString &destination) {
#ifdef Q_OS_WIN
  return MoveFileExW(reinterpret_cast<const wchar_t *>(source.utf16()),
                     reinterpret_cast<const wchar_t *>(destination.utf16()), MOVEFILE_WRITE_THROUGH);
#else
  return ::syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(source).constData(),
                   AT_FDCWD, QFile::encodeName(destination).constData(), RENAME_NOREPLACE) == 0;
#endif
}
}
