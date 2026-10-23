#include "profilearchive.h"
#include "i18n.h"
#include "profile.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
constexpr qsizetype fileLimit = 8 * 1024 * 1024;
constexpr qsizetype totalLimit = 32 * 1024 * 1024;
constexpr qsizetype archiveLimit = 48 * 1024 * 1024;
bool fail(QString *error, const QString &message) {
  if (error)
    *error = message;
  return false;
}
QString digest(const QByteArray &bytes) {
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
bool safePath(const QString &path) {
  if (path.isEmpty() || path.size() > 512 || path.contains('\\') ||
      path.contains(QChar::Null) || QDir::isAbsolutePath(path) ||
      QDir::cleanPath(path) != path)
    return false;
  for (const auto &part : path.split('/'))
    if (part.isEmpty() || part == "." || part == "..")
      return false;
  return path == "FoxicMPQt.ini" || path.startsWith("Assets/") ||
         path.startsWith("Subtitles/Edits/");
}
bool write(const QString &path, const QByteArray &bytes) {
  QSaveFile file(path);
  return QDir().mkpath(QFileInfo(path).absolutePath()) &&
         file.open(QIODevice::WriteOnly) &&
         file.setPermissions(QFileDevice::ReadOwner |
                             QFileDevice::WriteOwner) &&
         file.write(bytes) == bytes.size() && file.commit();
}
bool readFile(const QString &path, QByteArray *bytes, QString *error) {
  QFile file(path);
  const QFileInfo info(path);
  if (info.isSymLink() || !info.isFile() || info.size() > fileLimit ||
      !file.open(QIODevice::ReadOnly))
    return fail(error, uiTr("Не удалось прочитать файл профиля: %1").arg(path));
  *bytes = file.read(fileLimit + 1);
  if (bytes->size() > fileLimit || file.error() != QFileDevice::NoError)
    return fail(error, uiTr("Ошибка чтения файла профиля: %1").arg(path));
  return true;
}
void localIntegration(QSettings &settings) {
  settings.remove("integration");
  settings.setValue("integration/prompted", true);
  settings.setValue("integration/enabled", false);
  settings.setValue("integration/checkOnStartup", false);
}
void editedTracks(QSettings &settings, const QString &directory,
                  bool importing) {
  auto document =
      QJsonDocument::fromJson(settings.value("playlist/tabsV1").toByteArray());
  if (!document.isObject() || !document.object().value("tabs").isArray())
    return;
  const auto map = [&](const QString &path) {
    if (importing)
      return safePath(path) && path.startsWith("Subtitles/Edits/")
                 ? QDir(directory).absoluteFilePath(path)
                 : path;
    if (!QDir::isAbsolutePath(path))
      return path;
    const auto relative = QDir(directory).relativeFilePath(path);
    return safePath(relative) && relative.startsWith("Subtitles/Edits/")
               ? relative
               : path;
  };
  auto root = document.object();
  auto tabs = root.value("tabs").toArray();
  bool changed = false;
  for (int tabIndex = 0; tabIndex < tabs.size(); ++tabIndex) {
    auto tab = tabs[tabIndex].toObject();
    if (!tab.value("entries").isArray())
      continue;
    auto entries = tab.value("entries").toArray();
    for (int row = 0; row < entries.size(); ++row) {
      auto entry = entries[row].toObject();
      if (!entry.value("metadata").isObject())
        continue;
      auto metadata = entry.value("metadata").toObject();
      if (metadata.value("subtitleFiles").isArray()) {
        auto files = metadata.value("subtitleFiles").toArray();
        for (int i = 0; i < files.size(); ++i) {
          if (!files[i].isString())
            continue;
          const auto original = files[i].toString(),
                     replacement = map(original);
          changed |= replacement != original;
          files[i] = replacement;
        }
        metadata["subtitleFiles"] = files;
      }
      if (metadata.value("selectedSubtitleFile").isString()) {
        const auto original = metadata.value("selectedSubtitleFile").toString();
        const auto replacement = map(original);
        changed |= replacement != original;
        metadata["selectedSubtitleFile"] = replacement;
      }
      entry["metadata"] = metadata;
      entries[row] = entry;
    }
    tab["entries"] = entries;
    tabs[tabIndex] = tab;
  }
  if (changed) {
    root["tabs"] = tabs;
    settings.setValue("playlist/tabsV1",
                      QJsonDocument(root).toJson(QJsonDocument::Compact));
  }
}
bool validate(const ProfileArchive::Bundle &bundle, QString *error) {
  if (bundle.files.isEmpty() || bundle.files.size() > 2048 ||
      !bundle.files.contains("FoxicMPQt.ini"))
    return fail(error, uiTr("Некорректный состав архива профиля."));
  qsizetype total = 0;
  for (auto it = bundle.files.begin(); it != bundle.files.end(); ++it) {
    total += it.value().size();
    if (!safePath(it.key()) || it.value().size() > fileLimit ||
        total > totalLimit)
      return fail(error,
                  uiTr("Недопустимый путь или превышен размер профиля."));
    auto parent = QFileInfo(it.key()).path();
    while (parent != ".") {
      if (bundle.files.contains(parent))
        return fail(error, uiTr("Конфликт путей в архиве профиля."));
      parent = QFileInfo(parent).path();
    }
  }
  QTemporaryDir temporary;
  const auto ini = temporary.filePath("FoxicMPQt.ini");
  if (!temporary.isValid() || !write(ini, bundle.files.value("FoxicMPQt.ini")))
    return fail(error, uiTr("Не удалось проверить настройки профиля."));
  QSettings settings(ini, QSettings::IniFormat);
  const auto keys = settings.allKeys();
  if (settings.status() != QSettings::NoError || keys.size() > 20000 ||
      settings.value("profile/version").toInt() != 1)
    return fail(
        error,
        uiTr("Настройки повреждены или версия профиля не поддерживается."));
  return true;
}
} // namespace

bool ProfileArchive::save(Profile &profile, const QString &archive,
                          QString *error) {
  if (error)
    error->clear();
  if (!archive.endsWith(".foxicmp-profile", Qt::CaseInsensitive))
    return fail(error, uiTr("Архив должен иметь расширение .foxicmp-profile."));
  if (QFileInfo(archive).isSymLink())
    return fail(error,
                uiTr("Файл архива не должен быть символической ссылкой."));
  profile.settings.sync();
  if (profile.settings.status() != QSettings::NoError)
    return fail(error, uiTr("Не удалось сохранить текущие настройки."));
  QTemporaryDir temporary;
  if (!temporary.isValid())
    return fail(error, uiTr("Не удалось создать временную папку."));
  Bundle bundle;
  const auto ini = temporary.filePath("FoxicMPQt.ini");
  {
    QSettings snapshot(ini, QSettings::IniFormat);
    for (const auto &key : profile.settings.allKeys())
      snapshot.setValue(key, profile.settings.value(key));
    localIntegration(snapshot);
    editedTracks(snapshot, profile.directory, false);
    // Copy selected resources, including external resources, under content
    // hashes.
    for (const auto &key : {"appearance/logoPath", "appearance/toolbarPath",
                            "appearance/languageFile", "video/iccProfile"}) {
      const auto value = snapshot.value(key).toString();
      if (value.isEmpty())
        continue;
      // The Logo page retains the previous external path when Internal is
      // selected. A missing inactive image must not block a profile backup.
      if (QString(key) == "appearance/logoPath" &&
          !snapshot.value("appearance/logoExternal", true).toBool() &&
          !QFileInfo::exists(QDir(profile.directory).absoluteFilePath(value)))
        continue;
      QByteArray bytes;
      if (!readFile(QDir(profile.directory).absoluteFilePath(value), &bytes,
                    error))
        return false;
      auto suffix = QFileInfo(value).suffix().toLower();
      if (suffix.size() > 12 ||
          suffix.contains(QRegularExpression("[^a-z0-9]")))
        suffix = "data";
      const auto path = "Assets/" + digest(bytes) + "." + suffix;
      bundle.files.insert(path, bytes);
      snapshot.setValue(key, path);
    }
    snapshot.sync();
    if (snapshot.status() != QSettings::NoError)
      return fail(error, uiTr("Не удалось сохранить снимок настроек."));
  }
  QByteArray iniBytes;
  if (!readFile(ini, &iniBytes, error))
    return false;
  bundle.files.insert("FoxicMPQt.ini", iniBytes);
  const auto edits = QDir(profile.directory).filePath("Subtitles/Edits");
  // Do not follow directory links, including a linked root of the edits tree.
  if (QFileInfo(QDir(profile.directory).filePath("Subtitles")).isSymLink() ||
      QFileInfo(edits).isSymLink())
    return fail(error,
                uiTr("Папка правок субтитров является символической ссылкой."));
  QDirIterator iterator(edits,
                        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot |
                            QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
  qsizetype total = 0;
  for (const auto &bytes : bundle.files)
    total += bytes.size();
  while (iterator.hasNext()) {
    iterator.next();
    const auto info = iterator.fileInfo();
    if (info.isSymLink())
      return fail(error, uiTr("В профиле найдена символическая ссылка: %1")
                             .arg(info.filePath()));
    if (info.isDir())
      continue;
    QByteArray bytes;
    if (!readFile(info.filePath(), &bytes, error))
      return false;
    total += bytes.size();
    if (total > totalLimit || bundle.files.size() >= 2048)
      return fail(error,
                  uiTr("Превышен размер профиля (32 МиБ или 2048 файлов)."));
    bundle.files.insert(
        QDir(profile.directory).relativeFilePath(info.filePath()), bytes);
  }
  if (!validate(bundle, error))
    return false;
  QJsonArray files;
  for (auto it = bundle.files.begin(); it != bundle.files.end(); ++it)
    files.append(
        QJsonObject{{"path", it.key()},
                    {"sha256", digest(it.value())},
                    {"data", QString::fromLatin1(it.value().toBase64())}});
  const auto bytes =
      QJsonDocument(
          QJsonObject{{"format", "FoxicMPQtProfile"},
                      {"version", 1},
                      {"createdUtc",
                       QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                      {"files", files}})
          .toJson(QJsonDocument::Compact);
  if (bytes.size() > archiveLimit)
    return fail(error, uiTr("Архив недоступен или превышает 48 МиБ."));
  if (!write(archive, bytes))
    return fail(error, uiTr("Не удалось записать архив профиля."));
  return true;
}

bool ProfileArchive::read(const QString &archive, Bundle *bundle,
                          QString *error) {
  if (error)
    error->clear();
  QFile file(archive);
  if (!QFileInfo(archive).isFile() || !file.open(QIODevice::ReadOnly) ||
      file.size() > archiveLimit)
    return fail(error, uiTr("Архив недоступен или превышает 48 МиБ."));
  const auto bytes = file.read(archiveLimit + 1);
  if (bytes.size() > archiveLimit || file.error() != QFileDevice::NoError)
    return fail(error, uiTr("Ошибка чтения архива профиля."));
  QJsonParseError parse;
  const auto document = QJsonDocument::fromJson(bytes, &parse);
  const auto object = document.object();
  if (parse.error != QJsonParseError::NoError ||
      object.value("format") != "FoxicMPQtProfile" ||
      object.value("version") != 1 || !object.value("files").isArray() ||
      object.value("files").toArray().size() > 2048)
    return fail(error, uiTr("Формат или версия архива не поддерживается."));
  Bundle result;
  qsizetype total = 0;
  for (const auto &entry : object.value("files").toArray()) {
    const auto item = entry.toObject();
    const auto path = item.value("path").toString();
    if (!safePath(path) || result.files.contains(path) ||
        !item.value("data").isString())
      return fail(error, uiTr("Недопустимый или повторяющийся путь в архиве."));
    const auto decoded =
        QByteArray::fromBase64Encoding(item.value("data").toString().toLatin1(),
                                       QByteArray::AbortOnBase64DecodingErrors);
    total += decoded.decoded.size();
    if (!decoded || decoded.decoded.size() > fileLimit || total > totalLimit ||
        digest(decoded.decoded) != item.value("sha256").toString())
      return fail(error,
                  uiTr("Нарушена целостность или превышен размер архива."));
    result.files.insert(path, decoded.decoded);
  }
  if (!validate(result, error))
    return false;
  *bundle = result;
  return true;
}

bool ProfileArchive::restore(const Bundle &bundle, const QString &newDirectory,
                             QString *error) {
  if (error)
    error->clear();
  if (!validate(bundle, error))
    return false;
  const QFileInfo target(
      QDir::cleanPath(QFileInfo(newDirectory).absoluteFilePath()));
  const auto parent = QFileInfo(target.absolutePath()).canonicalFilePath();
  if (parent.isEmpty() || target.exists() || target.isSymLink() ||
      target.fileName().isEmpty())
    return fail(error, uiTr("Выберите новую папку в существующем каталоге."));
  QTemporaryDir temporary(QDir(parent).filePath(".foxicmp-import-XXXXXX"));
  if (!temporary.isValid())
    return fail(error, uiTr("Не удалось создать временную папку."));
  for (auto it = bundle.files.begin(); it != bundle.files.end(); ++it)
    if (!write(temporary.filePath(it.key()), it.value()))
      return fail(error, uiTr("Не удалось записать восстановленный профиль."));
  const auto destination = QDir(parent).filePath(target.fileName());
  {
    QSettings settings(temporary.filePath("FoxicMPQt.ini"),
                       QSettings::IniFormat);
    localIntegration(settings);
    editedTracks(settings, destination, true);
    settings.sync();
    if (settings.status() != QSettings::NoError)
      return fail(error,
                  uiTr("Не удалось сохранить восстановленные настройки."));
  }
  if (::syscall(SYS_renameat2, AT_FDCWD,
                QFile::encodeName(temporary.path()).constData(), AT_FDCWD,
                QFile::encodeName(destination).constData(),
                RENAME_NOREPLACE) != 0)
    return fail(error, uiTr("Не удалось создать папку профиля: путь уже занят "
                            "или недоступен."));
  temporary.setAutoRemove(false);
  return true;
}

QString ProfileArchive::backup(Profile &profile, QString *error) {
  const auto directory = QDir(profile.directory).filePath("Backups");
  if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory)) {
    fail(error, uiTr("Не удалось создать папку Backups в профиле."));
    return {};
  }
  const auto path = QDir(directory).filePath(
      "FoxicMPQt-" +
      QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + "-" +
      QUuid::createUuid().toString(QUuid::Id128) + ".foxicmp-profile");
  return save(profile, path, error) ? path : QString();
}
