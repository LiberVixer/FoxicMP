#include "profilelaunch.h"
#include "i18n.h"
#include "integration.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include "platformpaths.h"
#ifdef Q_OS_WIN
#include <shobjidl.h>
#include <future>
#endif

namespace {
bool fail(QString *error, const QString &text) {
  if (error) *error = text;
  return false;
}
bool valid(const QString &path) {
  if (path.isEmpty() || path.size() > 8192) return false;
  for (const auto c : path)
    if (c.unicode() < 32 || c.unicode() == 127) return false;
  return true;
}
bool directoryValid(const QString &path) {
  const QFileInfo dir(path), ini(QDir(path).filePath("FoxicMPQt.ini"));
  return dir.isAbsolute() && dir.isDir() && !ini.isSymLink() &&
      (!ini.exists() || (ini.isFile() && ini.isReadable()));
}
}
QString ProfileLaunch::configPath(const QString &program) {
  return QDir(program).filePath("FoxicMPQt.launch.json");
}
bool ProfileLaunch::resolve(const QString &program, QString *directory, QString *error) {
  *directory = QFileInfo(program).absoluteFilePath();
  const auto path = configPath(program);
  const QFileInfo info(path);
  if (!info.exists() && !info.isSymLink()) return true;
  QFile file(path);
  if (info.isSymLink() || !info.isFile() || info.size() > 16384 || !file.open(QIODevice::ReadOnly))
    return fail(error, uiTr("Не удалось прочитать выбор профиля. Запустите с --portable или --profile."));
  const auto bytes = file.read(16385);
  const auto document = QJsonDocument::fromJson(bytes);
  const auto object = document.object();
  if (file.error() != QFileDevice::NoError || bytes.size() > 16384 || !document.isObject() ||
      object.size() != 2 || object.value("version") != QJsonValue(1) ||
      !object.value("directory").isString() || !valid(object.value("directory").toString()))
    return fail(error, uiTr("Неверный файл выбора профиля. Запустите с --portable или --profile."));
  const auto selected = QDir(program).absoluteFilePath(object.value("directory").toString());
  if (!directoryValid(selected))
    return fail(error, uiTr("Выбранный профиль недоступен: %1. Используйте --portable или --profile.").arg(selected));
  *directory = QDir::cleanPath(selected);
  return true;
}
bool ProfileLaunch::select(const QString &program, const QString &directory, QString *error) {
  if (!QFileInfo(program).isAbsolute() || !QFileInfo(program).isDir() ||
      !valid(directory) || !directoryValid(directory))
    return fail(error, uiTr("Выберите существующую папку профиля с доступным INI или пустую папку."));
  QTemporaryFile probe(QDir(directory).filePath(".foxicmp-launch-XXXXXX"));
  if (!probe.open()) return fail(error, uiTr("Папка профиля недоступна для записи."));
  const auto path = configPath(program);
  const QFileInfo info(path);
  if (info.isSymLink() || (info.exists() && !info.isFile()))
    return fail(error, uiTr("Не удалось сохранить выбор профиля рядом с программой."));
  const auto canonical = QFileInfo(directory).canonicalFilePath();
  const auto base = QFileInfo(program).canonicalFilePath();
  const auto relative = QDir(base).relativeFilePath(canonical);
  if (canonical.isEmpty() || base.isEmpty())
    return fail(error, uiTr("Не удалось сохранить выбор профиля рядом с программой."));
  const auto stored = relative != ".." && !relative.startsWith("../") ? relative : canonical;
  const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"directory", stored}}).toJson();
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      file.write(bytes) != bytes.size() || !file.commit())
    return fail(error, uiTr("Не удалось сохранить выбор профиля рядом с программой."));
  return true;
}
bool ProfileLaunch::launcher(const QString &executable, const QString &directory,
                             const QString &desktopFile, QString *error) {
  const QFileInfo output(desktopFile), exe(executable);
#ifdef Q_OS_WIN
  if (!exe.isFile() || !directoryValid(directory) || !valid(desktopFile) ||
      !output.isAbsolute() || !desktopFile.endsWith(".lnk", Qt::CaseInsensitive) ||
      !QFileInfo(output.absolutePath()).isDir() || output.exists() || output.isSymLink())
    return fail(error, uiTr("Ярлык Windows не создан: выберите новое имя .lnk и проверьте пути."));
  QTemporaryDir temp(QDir(output.absolutePath()).filePath(".foxicmp-launcher-XXXXXX"));
  if (!temp.isValid()) return fail(error, uiTr("Не удалось записать ярлык."));
  const auto temporaryShortcut = temp.filePath("shortcut.lnk");
  auto path = QDir::toNativeSeparators(directory);
  int trailing = 0;
  for (int i = path.size() - 1; i >= 0 && path[i] == '\\'; --i) ++trailing;
  const auto arguments = "--profile \"" + path + QString(trailing, '\\') + '\"';
  const auto nativeExe = QDir::toNativeSeparators(executable);
  const auto nativeFile = QDir::toNativeSeparators(temporaryShortcut);
  const auto working = QDir::toNativeSeparators(exe.absolutePath());
  HRESULT result = E_FAIL;
  try {
    // The GUI thread may already cache WinRT factories before opening a native
    // window. A temporary COM apartment there invalidates those factories on
    // CoUninitialize. Keep ShellLink's apartment entirely on its own thread.
    result = std::async(std::launch::async, [nativeExe, nativeFile, working, arguments] {
      const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
      if (FAILED(com)) return com;
      IShellLinkW *link = nullptr;
      IPersistFile *persist = nullptr;
      auto status = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
          IID_IShellLinkW, reinterpret_cast<void **>(&link));
      if (SUCCEEDED(status)) status = link->SetPath(reinterpret_cast<LPCWSTR>(nativeExe.utf16()));
      if (SUCCEEDED(status)) status = link->SetArguments(reinterpret_cast<LPCWSTR>(arguments.utf16()));
      if (SUCCEEDED(status)) status = link->SetWorkingDirectory(reinterpret_cast<LPCWSTR>(working.utf16()));
      if (SUCCEEDED(status)) status = link->SetIconLocation(reinterpret_cast<LPCWSTR>(nativeExe.utf16()), 0);
      if (SUCCEEDED(status)) status = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persist));
      if (SUCCEEDED(status)) status = persist->Save(reinterpret_cast<LPCWSTR>(nativeFile.utf16()), TRUE);
      if (persist) persist->Release();
      if (link) link->Release();
      CoUninitialize();
      return status;
    }).get();
  } catch (const std::exception &) {
    return fail(error, uiTr("Не удалось записать ярлык."));
  }
  if (FAILED(result)) return fail(error, uiTr("Не удалось записать ярлык."));
  if (!PlatformPaths::moveNoReplace(temporaryShortcut, desktopFile))
    return fail(error, uiTr("Не удалось опубликовать ярлык без замены существующего файла."));
  return true;
#else
  const auto bytes = Integration::desktopText(executable, directory).toUtf8();
  if (bytes.isEmpty() || !exe.isFile() || !exe.isExecutable() ||
      !directoryValid(directory) || !valid(desktopFile) || !output.isAbsolute() ||
      !desktopFile.endsWith(".desktop", Qt::CaseInsensitive) ||
      !QFileInfo(output.absolutePath()).isDir() || output.exists() || output.isSymLink())
    return fail(error, uiTr("Ярлык не создан: проверьте пути и выберите новое имя .desktop."));
  QTemporaryFile file(QDir(output.absolutePath()).filePath(".foxicmp-launcher-XXXXXX"));
  if (!file.open() || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner) ||
      file.write(bytes) != bytes.size() || !file.flush())
    return fail(error, uiTr("Не удалось записать ярлык."));
  file.close();
  if (!PlatformPaths::moveNoReplace(file.fileName(), desktopFile))
    return fail(error, uiTr("Не удалось опубликовать ярлык без замены существующего файла."));
  return true;
#endif
}
