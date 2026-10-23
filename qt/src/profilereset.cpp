#include "profilereset.h"
#include "profile.h"
#include "i18n.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QScopeGuard>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

ProfileLease::ProfileLease(const QString &directory) {
  const QFileInfo info(directory);
  const auto canonical = info.canonicalFilePath();
  const auto hash = QCryptographicHash::hash(
      (canonical.isEmpty() ? info.absoluteFilePath() : canonical).toUtf8(),
      QCryptographicHash::Sha256).toHex();
  const auto runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (runtime.isEmpty() || !QDir(runtime).exists())
    return;
  const auto path = QDir(runtime).filePath("FoxicMPQt-profile-" + QString::fromLatin1(hash) + ".lease");
  fd = ::open(QFile::encodeName(path).constData(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd >= 0 && flock(fd, LOCK_SH | LOCK_NB) != 0) {
    ::close(fd);
    fd = -1;
  }
}
ProfileLease::~ProfileLease() { if (fd >= 0) ::close(fd); }
bool ProfileLease::exclusive() { return fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0; }
void ProfileLease::shared() { if (fd >= 0) flock(fd, LOCK_SH); }

namespace ProfileReset {
static bool replaceSettings(QSettings &settings, const QString &directory,
                            bool keepLibrary, QString *backup, QString *error) {
  if (backup) backup->clear();
  const auto fail = [&](const QString &message) {
    if (error) *error = message;
    return false;
  };
  const auto path = settings.fileName();
  QFileInfo info(path);
  // Check before QSettings::sync: following a FIFO or symlink could block or
  // write outside the profile before we have validated the target.
  if (info.isSymLink() || (info.exists() && (!info.isFile() || info.size() > 8 * 1024 * 1024)))
    return fail(uiTr("Не удалось прочитать настройки для сброса."));
  settings.sync();
  info.refresh();
  if (settings.status() != QSettings::NoError || info.isSymLink() ||
      !info.isFile() || info.size() > 8 * 1024 * 1024)
    return fail(uiTr("Не удалось прочитать настройки для сброса."));
  QFile original(path);
  if (!original.open(QIODevice::ReadOnly))
    return fail(uiTr("Не удалось прочитать настройки для сброса."));
  const auto bytes = original.read(8 * 1024 * 1024 + 1);
  if (bytes.size() > 8 * 1024 * 1024 || original.error() != QFileDevice::NoError)
    return fail(uiTr("Не удалось прочитать настройки для сброса."));
  original.close();
  QTemporaryDir temporary;
  if (!temporary.isValid())
    return fail(uiTr("Не удалось подготовить настройки по умолчанию."));
  QByteArray defaults;
  {
    Profile clean(temporary.path());
    if (!clean.lease.valid())
      return fail(uiTr("Не удалось подготовить настройки по умолчанию."));
    // Keep machine integration undo evidence, but never repair it automatically.
    for (const auto &key : settings.allKeys()) {
      const bool library = key.startsWith("positions/") || key.startsWith("favorites/") ||
          key == "history/files" || key == "files/playlist" || key == "files/currentIndex" ||
          key == "playlist/tabsV1";
      if (key.startsWith("integration/") || (keepLibrary && library))
        clean.settings.setValue(key, settings.value(key));
    }
    clean.settings.setValue("integration/checkOnStartup", false);
    clean.settings.sync();
    QFile file(clean.settings.fileName());
    if (clean.settings.status() != QSettings::NoError || !file.open(QIODevice::ReadOnly))
      return fail(uiTr("Не удалось подготовить настройки по умолчанию."));
    defaults = file.readAll();
    if (file.error() != QFileDevice::NoError)
      return fail(uiTr("Не удалось подготовить настройки по умолчанию."));
  }
  const auto folder = QDir(directory).filePath("Backups");
  if (QFileInfo(folder).isSymLink() || !QDir().mkpath(folder))
    return fail(uiTr("Не удалось сохранить резервную копию. Сброс отменён."));
  const auto saved = QDir(folder).filePath("before-reset-" +
      QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + "-" +
      QUuid::createUuid().toString(QUuid::WithoutBraces) + ".ini");
  const auto write = [](const QString &name, const QByteArray &data) {
    QSaveFile file(name);
    return file.open(QIODevice::WriteOnly) &&
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) &&
        file.write(data) == data.size() && file.commit();
  };
  if (!write(saved, bytes))
    return fail(uiTr("Не удалось сохранить резервную копию. Сброс отменён."));
  if (backup) *backup = saved;
  if (!write(path, defaults))
    return fail(uiTr("Не удалось заменить настройки. Резервная копия: %1").arg(saved));
  settings.sync(); // no pending writes; reload the atomically replaced INI
  return true;
}
bool apply(Profile &profile, bool keepLibrary, QString *backup, QString *error) {
  if (backup) backup->clear();
  if (!profile.lease.exclusive()) {
    profile.lease.shared();
    if (error) *error = uiTr("Сброс недоступен: закройте остальные окна этого профиля и повторите.");
    return false;
  }
  const auto unlock = qScopeGuard([&] { profile.lease.shared(); });
  return replaceSettings(profile.settings, profile.directory, keepLibrary, backup, error);
}
bool applyDirectory(const QString &directory, bool keepLibrary, QString *backup, QString *error) {
  if (backup) backup->clear();
  const QFileInfo ini(QDir(directory).filePath("FoxicMPQt.ini"));
  if (!QFileInfo(directory).isDir() || ini.isSymLink() || !ini.isFile()) {
    if (error) *error = uiTr("Для сброса нужен существующий профиль с обычным файлом FoxicMPQt.ini.");
    return false;
  }
  ProfileLease lease(directory);
  if (!lease.exclusive()) {
    lease.shared();
    if (error) *error = uiTr("Сброс недоступен: закройте остальные окна этого профиля и повторите.");
    return false;
  }
  QSettings settings(ini.absoluteFilePath(), QSettings::IniFormat);
  return replaceSettings(settings, directory, keepLibrary, backup, error);
}
} // namespace ProfileReset
