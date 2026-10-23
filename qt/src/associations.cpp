#include "associations.h"
#include "i18n.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
namespace {
const QString desktopId = "foxicmp-qt.desktop";
const QString backupKey = "integration/defaultsBackupV1";
const QString defaultsGroup = "Default Applications",
              removedGroup = "Removed Associations";
constexpr int maxSize = 1024 * 1024;
bool failure(QString *error, const QString &message) {
  if (error)
    *error = message;
  return false;
}
class Document {
public:
  QByteArray original;
  QList<QByteArray> lines;
  QByteArray newline = "\n";
  QString path;
  bool exists = false;
  bool load(const QString &file, QString *error) {
    path = file;
    exists = QFileInfo::exists(file);
    if (exists) {
      QFile f(file);
      if (!f.open(QIODevice::ReadOnly))
        return failure(error, f.errorString());
      if (f.size() > maxSize)
        return failure(error, uiTr("Файл ассоциаций превышает 1 МиБ."));
      original = f.readAll();
      if (original.startsWith("\xef\xbb\xbf") || original.contains('\0') ||
          QString::fromUtf8(original).toUtf8() != original)
        return failure(
            error, uiTr("Файл ассоциаций имеет неподдерживаемую кодировку."));
    }
    newline = original.contains("\r\n") ? "\r\n" : "\n";
    lines = original.split('\n');
    // Fail closed on ambiguous repeated sections/keys rather than overwrite
    // unrelated associations through a lossy generic INI serializer.
    QSet<QByteArray> groups, keys;
    QByteArray group;
    for (const auto &line : lines) {
      const auto trimmed = line.trimmed();
      if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
        group = trimmed.mid(1, trimmed.size() - 2);
        if (groups.contains(group))
          return failure(error, uiTr("Повторяющиеся разделы в mimeapps.list: "
                                     "изменения отменены."));
        groups.insert(group);
      } else if (!trimmed.isEmpty() && !trimmed.startsWith('#') &&
                 trimmed.contains('=')) {
        const auto key =
            group + '\0' + trimmed.left(trimmed.indexOf('=')).trimmed();
        if (keys.contains(key))
          return failure(
              error,
              uiTr("Повторяющиеся ключи в mimeapps.list: изменения отменены."));
        keys.insert(key);
      }
    }
    return true;
  }
  int index(const QString &section, const QString &key) const {
    QByteArray group;
    for (int i = 0; i < lines.size(); ++i) {
      const auto text = lines[i].trimmed();
      if (text.startsWith('[') && text.endsWith(']'))
        group = text.mid(1, text.size() - 2);
      else if (group == section.toUtf8() && !text.startsWith('#') &&
               text.contains('=') &&
               text.left(text.indexOf('=')).trimmed() == key.toUtf8())
        return i;
    }
    return -1;
  }
  QJsonValue value(const QString &section, const QString &key) const {
    const int i = index(section, key);
    if (i < 0)
      return QJsonValue(QJsonValue::Null);
    auto raw = lines[i].mid(lines[i].indexOf('=') + 1);
    if (raw.endsWith('\r'))
      raw.chop(1);
    return QString::fromUtf8(raw);
  }
  void put(const QString &section, const QString &key,
           const QJsonValue &value) {
    const int i = index(section, key);
    if (value.isNull()) {
      if (i >= 0)
        lines.removeAt(i);
      return;
    }
    const auto suffix = newline == "\r\n" ? "\r" : "";
    if (i >= 0) {
      lines[i] = lines[i].left(lines[i].indexOf('=') + 1) +
                 value.toString().toUtf8() + suffix;
      return;
    }
    int at = -1;
    bool in = false;
    for (int n = 0; n < lines.size(); ++n) {
      const auto text = lines[n].trimmed();
      if (text.startsWith('[') && text.endsWith(']')) {
        if (in) {
          at = n;
          break;
        }
        if (text == '[' + section.toUtf8() + ']') {
          in = true;
          at = n + 1;
        }
      } else if (in)
        at = n + 1;
    }
    if (!in) {
      if (lines.last().isEmpty())
        lines.removeLast();
      lines << '[' + section.toUtf8() + ']' + suffix;
      at = lines.size();
    }
    lines.insert(at, key.toUtf8() + '=' + value.toString().toUtf8() + suffix);
    if (!lines.last().isEmpty())
      lines.append(QByteArray());
  }
  bool commit(QString *error) {
    // A lock coordinates FoxicMP writers; also detect edits by other programs
    // before committing. Third-party writers do not honor our advisory lock.
    QFile current(path);
    if (exists != QFileInfo::exists(path) ||
        (exists &&
         (!current.open(QIODevice::ReadOnly) || current.readAll() != original)))
      return failure(error, uiTr("Ассоциации изменены другой программой. "
                                 "Обновите список и повторите."));
    const auto bytes = lines.join('\n');
    if (bytes == original)
      return true;
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() ||
        !out.commit())
      return failure(error, out.errorString());
    return true;
  }
};
QStringList ids(const QJsonValue &value) {
  QStringList result;
  for (const auto &id : value.toString().split(';', Qt::SkipEmptyParts)) {
    const auto clean = id.trimmed();
    if (!clean.isEmpty() && !result.contains(clean))
      result << clean;
  }
  return result;
}
bool journal(QSettings &settings, const QString &file, QJsonObject *entries,
             QString *error) {
  const auto bytes = settings.value(backupKey).toByteArray();
  if (bytes.isEmpty())
    return true;
  const auto root = QJsonDocument::fromJson(bytes).object();
  if (bytes.size() > maxSize || root.value("version") != QJsonValue(1) ||
      !root.value("entries").isObject() ||
      root.value("file").toString() != file)
    return failure(error, uiTr("Журнал отката относится к другому файлу или "
                               "повреждён. Назначения не изменены."));
  *entries = root.value("entries").toObject();
  for (auto it = entries->begin(); it != entries->end(); ++it) {
    const auto item = it.value().toObject();
    const auto parts = it.key().split('\n');
    const auto known = Associations::mimeTypes();
    bool unsafe = parts.size() != 2 ||
                  (parts[0] != defaultsGroup && parts[0] != removedGroup) ||
                  !known.contains(parts.value(1));
    for (const auto &name : {"before", "expected"}) {
      const auto text = item.value(name).toString();
      unsafe |= text.contains('\n') || text.contains('\r') ||
                text.contains(QChar(0)) || text.size() > 65536;
    }

    if (unsafe || !it.value().isObject() || !item.contains("before") ||
        !item.contains("expected") ||
        (!item["before"].isString() && !item["before"].isNull()) ||
        (!item["expected"].isString() && !item["expected"].isNull()))
      return failure(error, uiTr("Журнал отката относится к другому файлу или "
                                 "повреждён. Назначения не изменены."));
  }
  return true;
}
bool saveJournal(QSettings &settings, const QString &file,
                 const QJsonObject &entries, QString *error) {
  if (entries.isEmpty())
    settings.remove(backupKey);
  else
    settings.setValue(
        backupKey,
        QJsonDocument(
            QJsonObject{{"version", 1}, {"file", file}, {"entries", entries}})
            .toJson(QJsonDocument::Compact));
  settings.sync();
  if (settings.status() != QSettings::NoError)
    return failure(error,
                   uiTr("Не удалось сохранить журнал отката в профиле."));
  return true;
}
QString identity(const QString &section, const QString &mime) {
  return section + '\n' + mime;
}
} // namespace
QList<Associations::Type> Associations::catalog() {
  static const auto types = [] {
    QMimeDatabase db;
    QMap<QString, Type> map;
    for (const auto &mime : db.allMimeTypes()) {
      const auto name = mime.name();
      if (!name.startsWith("video/") && !name.startsWith("audio/"))
        continue;
      map.insert(name, {name, name.startsWith("video/") ? "video" : "audio",
                        mime.globPatterns().join(' '), mime.comment()});
    }
    for (const auto &name :
         QStringList{"audio/x-mpegurl", "audio/mpegurl",
                     "application/vnd.apple.mpegurl", "application/x-mpegurl",
                     "audio/x-scpls", "video/x-ms-asf", "application/x-ms-asx",
                     "application/x-cue", "application/x-mpc-playlist"}) {
      auto mime = db.mimeTypeForName(name);
      if (!mime.isValid())
        continue;
      const auto canonical = mime.name();
      // ASF is also a media container, not exclusively a playlist.
      if (canonical == "video/x-ms-asf")
        continue;
      map.insert(canonical, {canonical, "playlist",
                             mime.globPatterns().join(' '), mime.comment()});
    }
    return map.values();
  }();
  return types;
}
QStringList Associations::mimeTypes() {
  QStringList result;
  for (const auto &t : catalog())
    result << t.mime;
  return result;
}
QString Associations::configFile() {
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::GenericConfigLocation))
      .filePath("mimeapps.list");
}
bool Associations::readDefaults(QMap<QString, QString> *values,
                                QString *error) {
  Document doc;
  if (!doc.load(configFile(), error))
    return false;
  values->clear();
  for (const auto &type : catalog())
    (*values)[type.mime] = ids(doc.value(defaultsGroup, type.mime)).value(0);
  return true;
}
bool Associations::assign(QSettings &profile, const QStringList &types,
                          QString *error) {
  const auto known = mimeTypes();
  if (types.isEmpty() || types.size() > 512)
    return failure(error, uiTr("Выберите типы медиафайлов."));
  for (const auto &type : types)
    if (!known.contains(type))
      return failure(error, uiTr("Недопустимый MIME-тип: %1").arg(type));
  const auto file = configFile();
  if (!QDir().mkpath(QFileInfo(file).absolutePath()))
    return failure(error, uiTr("Не удалось создать папку интеграции"));
  QLockFile lock(file + ".foxicmp.lock");
  if (!lock.tryLock(0))
    return failure(
        error, uiTr("Ассоциации уже изменяются другим экземпляром FoxicMP."));
  Document doc;
  if (!doc.load(file, error))
    return false;
  QJsonObject entries;
  if (!journal(profile, file, &entries, error))
    return false;
  const auto previous = entries;
  const auto change = [&](const QString &section, const QString &type,
                          const QJsonValue &value) {
    const auto before = doc.value(section, type);
    if (before == value)
      return;
    const auto key = identity(section, type);
    const auto old = entries.value(key).toObject();
    entries[key] = QJsonObject{{"before", old.value("expected") == before
                                              ? old.value("before")
                                              : before},
                               {"expected", value}};
    doc.put(section, type, value);
  };
  for (const auto &type : types) {
    auto list = ids(doc.value(defaultsGroup, type));
    list.removeAll(desktopId);
    list.prepend(desktopId);
    change(defaultsGroup, type, list.join(';') + ';');
    auto removed = ids(doc.value(removedGroup, type));
    if (removed.removeAll(desktopId))
      change(removedGroup, type,
             removed.isEmpty() ? QJsonValue(QJsonValue::Null)
                               : QJsonValue(removed.join(';') + ';'));
  }
  // Persist intent first: interruption before the file commit leaves an inert
  // journal; restore checks each expected value and never overwrites a
  // mismatch.
  if (!saveJournal(profile, file, entries, error))
    return false;
  if (!doc.commit(error)) {
    saveJournal(profile, file, previous, nullptr);
    return false;
  }
  return true;
}
bool Associations::restore(QSettings &profile, int *restored, int *skipped,
                           QString *error) {
  if (restored)
    *restored = 0;
  if (skipped)
    *skipped = 0;
  const auto known = mimeTypes();
  const auto file = configFile();
  QJsonObject entries;
  if (!journal(profile, file, &entries, error))
    return false;
  if (entries.isEmpty())
    return true;
  QLockFile lock(file + ".foxicmp.lock");
  if (!lock.tryLock(0))
    return failure(
        error, uiTr("Ассоциации уже изменяются другим экземпляром FoxicMP."));
  Document doc;
  if (!doc.load(file, error))
    return false;
  int changed = 0, ignored = 0;
  for (auto it = entries.begin(); it != entries.end(); ++it) {
    const auto parts = it.key().split('\n');
    if (parts.size() != 2 ||
        (parts[0] != defaultsGroup && parts[0] != removedGroup) ||
        !known.contains(parts[1]))
      return failure(error, uiTr("Журнал отката относится к другому файлу или "
                                 "повреждён. Назначения не изменены."));
    const auto record = it.value().toObject();
    if (doc.value(parts[0], parts[1]) == record["expected"]) {
      doc.put(parts[0], parts[1], record["before"]);
      ++changed;
    } else
      ++ignored;
  }
  if (!doc.commit(error))
    return false;
  if (!saveJournal(profile, file, {}, error))
    return false;
  if (restored)
    *restored = changed;
  if (skipped)
    *skipped = ignored;
  return true;
}
bool Associations::hasDesktopReferences(const QString &id) {
  // Conservatively retain the launcher if an existing association mentions it.
  // Do not break defaults made in a desktop-specific or distribution file.
  const auto desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP")
                            .toLower()
                            .split(':', Qt::SkipEmptyParts);
  QStringList names{"mimeapps.list", "defaults.list"};
  for (const auto &desktop : desktops)
    if (!desktop.contains('/') && !desktop.contains('\\'))
      names << desktop + "-mimeapps.list";
  QStringList dirs =
      QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
  for (const auto &dir :
       QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
    dirs << QDir(dir).filePath("applications");
  for (const auto &dir : dirs)
    for (const auto &name : names) {
      QFile file(QDir(dir).filePath(name));
      if (!file.exists())
        continue;
      if (!file.open(QIODevice::ReadOnly) || file.size() > maxSize)
        return true;
      if (file.readAll().contains(id.toUtf8()))
        return true;
    }
  return false;
}
