#include "integration.h"
#include "i18n.h"
#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSet>
#include <QTimer>
#include <windows.h>
#include <shlobj.h>

namespace {
bool failure(QString *error, LONG code) {
  if (error) *error = uiTr("Ошибка интеграции Windows: %1").arg(code);
  return false;
}
bool put(const QString &path, const QString &name, const QString &value, QString *error) {
  HKEY key = nullptr;
  auto result = RegCreateKeyExW(HKEY_CURRENT_USER,
      reinterpret_cast<LPCWSTR>(path.utf16()), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
  if (result != ERROR_SUCCESS) return failure(error, result);
  result = RegSetValueExW(key, name.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(name.utf16()),
      0, REG_SZ, reinterpret_cast<const BYTE *>(value.utf16()), (value.size() + 1) * sizeof(wchar_t));
  RegCloseKey(key);
  return result == ERROR_SUCCESS || failure(error, result);
}
bool pathsValid(const QString &exe, const QString &profile, QString *error) {
  for (const auto &path : {exe, profile}) {
    if (path.isEmpty() && path == profile) continue;
    if (!QFileInfo(path).isAbsolute() || path.contains('"') || path.contains(QChar::Null) ||
        path.contains('\n') || path.contains('\r')) return failure(error, ERROR_INVALID_NAME);
  }
  return QFileInfo(exe).isFile() || failure(error, ERROR_FILE_NOT_FOUND);
}
QString quoted(QString path) {
  // A directory ending with a backslash needs it doubled before the closing quote.
  path = QDir::toNativeSeparators(path);
  int trailing = 0;
  for (int i = path.size() - 1; i >= 0 && path[i] == '\\'; --i) ++trailing;
  return '"' + path + QString(trailing, '\\') + '"';
}
QString command(const QString &exe, const QString &profile) {
  return quoted(exe) + (profile.isEmpty() ? " --portable" : " --profile " + quoted(profile)) + " -- \"%1\"";
}
QStringList extensions() {
  QSet<QString> result;
  for (const auto &mime : QMimeDatabase().allMimeTypes()) {
    if (!(mime.name().startsWith("video/") || mime.name().startsWith("audio/") ||
          mime.name().contains("playlist") || mime.name().contains("mpegurl"))) continue;
    for (const auto &suffix : mime.suffixes())
      if (!suffix.isEmpty() && !suffix.contains('\\') && !suffix.contains('/')) result.insert('.' + suffix);
  }
  for (const auto &suffix : QString("mkv mk3d mp4 m4v avi webm mov mpg mpeg ts m2ts mts vob flv wmv ogv hevc h264 h265 m3u m3u8 pls cue").split(' '))
    result.insert('.' + suffix);
  return result.values();
}
constexpr auto appKey = "Software\\Classes\\Applications\\FoxicMPq.exe";
}
QString Integration::desktopText(const QString &, const QString &) { return {}; }
bool Integration::registerApplication(const QString &exe, const QString &profile, QString *error) {
  if (!pathsValid(exe, profile, error)) return false;
  if (!put(appKey, "FriendlyAppName", "FoxicMP Qt", error) ||
      !put(QString(appKey) + "\\DefaultIcon", {}, quoted(exe) + ",0", error) ||
      !put(QString(appKey) + "\\shell\\open\\command", {}, command(exe, profile), error)) return false;
  const QString prog = "Software\\Classes\\FoxicMPq.Media";
  if (!put(prog, {}, "FoxicMP Qt media", error) ||
      !put(prog + "\\DefaultIcon", {}, quoted(exe) + ",0", error) ||
      !put(prog + "\\shell\\open\\command", {}, command(exe, profile), error) ||
      !put("Software\\FoxicMPq\\Capabilities", "ApplicationName", "FoxicMP Qt", error) ||
      !put("Software\\FoxicMPq\\Capabilities", "ApplicationDescription", "FoxicMP Qt media player", error)) return false;
  for (const auto &extension : extensions()) {
    if (!put(QString(appKey) + "\\SupportedTypes", extension, {}, error) ||
        !put("Software\\FoxicMPq\\Capabilities\\FileAssociations", extension, "FoxicMPq.Media", error) ||
        !put("Software\\Classes\\" + extension + "\\OpenWithProgids", "FoxicMPq.Media", {}, error)) return false;
  }
  if (!put("Software\\RegisteredApplications", "FoxicMPq", "Software\\FoxicMPq\\Capabilities", error)) return false;
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return true;
}
bool Integration::install(const QString &exe, QString *error) { return installForProfile(exe, {}, error); }
bool Integration::installForProfile(const QString &exe, const QString &profile, QString *error) {
  if (!registerApplication(exe, profile, error)) return false;
  const QString key = "Software\\Classes\\SystemFileAssociations\\video\\shell\\FoxicMPq";
  if (!put(key, {}, "FoxicMP Qt", error) || !put(key, "Icon", quoted(exe) + ",0", error) ||
      !put(key + "\\command", {}, command(exe, profile), error)) return false;
  // Also cover video suffixes whose current ProgID omits PerceivedType=video.
  for (const auto &mime : QMimeDatabase().allMimeTypes()) {
    if (!mime.name().startsWith("video/")) continue;
    for (const auto &suffix : mime.suffixes()) {
      if (suffix.isEmpty() || suffix.contains('\\') || suffix.contains('/')) continue;
      const auto extKey = "Software\\Classes\\SystemFileAssociations\\." + suffix + "\\shell\\FoxicMPq";
      if (!put(extKey, {}, "FoxicMP Qt", error) || !put(extKey, "Icon", quoted(exe) + ",0", error) ||
          !put(extKey + "\\command", {}, command(exe, profile), error)) return false;
    }
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return true;
}
bool Integration::remove(QString *error) {
  QStringList keys{"Software\\Classes\\SystemFileAssociations\\video\\shell\\FoxicMPq"};
  for (const auto &extension : extensions()) keys << "Software\\Classes\\SystemFileAssociations\\" + extension + "\\shell\\FoxicMPq";
  for (const auto &key : keys) {
    const auto result = RegDeleteTreeW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(key.utf16()));
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) return failure(error, result);
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return true;
}
void Integration::refreshDatabase(QObject *owner, std::function<void(const QString &)> done) {
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  if (done) QTimer::singleShot(0, owner, [done] { done({}); });
}
