#include "profile.h"
#include "devicesource.h"
#include "mediasource.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <cmath>
static QString key(const QString &file) {
  return "positions/" +
         QString::fromLatin1(
             QCryptographicHash::hash(file.toUtf8(), QCryptographicHash::Sha256)
                 .toHex());
}
Profile::Profile(const QString &dir)
    : lease(dir), settings(QDir(dir).filePath("FoxicMPQt.ini"), QSettings::IniFormat),
      directory(dir) {
  if (!lease.valid())
    return;
#ifdef Q_OS_WIN
  // A saved profile always wins, including older profiles without a version.
  // Keep the captured preferences in core so they also work before any UI exists.
  if (settings.allKeys().isEmpty()) {
    Q_INIT_RESOURCE(profiledefaults);
    const QSettings defaults(":/windows-defaults.ini", QSettings::IniFormat);
    for (const auto &name : defaults.allKeys())
      settings.setValue(name, defaults.value(name));
  }
#endif
  if (!settings.contains("profile/version")) {
    settings.setValue("profile/version", 1);
    const QVariantMap defaults{{"audio/volume", 75}, {"audio/mute", false},
        {"playback/rememberPosition", true}, {"playback/hardwareDecoding", true},
        {"integration/checkOnStartup", false}, {"ui/osd", false}};
    for (auto it = defaults.begin(); it != defaults.end(); ++it)
      if (!settings.contains(it.key()))
        settings.setValue(it.key(), it.value());
  }
}
bool Profile::writable() const {
  QTemporaryFile f(QDir(directory).filePath(".foxicmp-write-XXXXXX"));
  return f.open();
}
void Profile::savePosition(const QString &file, double pos, double duration) {
  const auto path = MediaSource::normalize(file);
  if (path.isEmpty() || !std::isfinite(pos) || !std::isfinite(duration) ||
      !DeviceSource::rememberPosition(path) ||
      forgottenPositions.contains(path) ||
      !settings.value("playback/rememberPosition", true).toBool())
    return;
  observedSources.insert(path);
  settings.setValue(
      key(path), duration > 0 && pos >= duration - 2 ? 0 : std::max(0.0, pos));
}
double Profile::position(const QString &file) const {
  const auto path = MediaSource::normalize(file);
  if (path.isEmpty() || !DeviceSource::rememberPosition(path) ||
      !settings.value("playback/rememberPosition", true).toBool())
    return 0;
  const double value = settings.value(key(path), 0).toDouble();
  return std::isfinite(value) && value >= 0 ? value : 0;
}
int Profile::historyLimit() const {
  return std::clamp(settings.value("history/limit", 50).toInt(), 1, 10000);
}
int Profile::historyMenuLimit() const {
  return std::clamp(settings.value("history/menuLimit", 50).toInt(), 0, 100);
}
QStringList Profile::recentFiles() const {
  QStringList result;
  QSet<QString> seen;
  for (const auto &file : settings.value("history/files").toStringList()) {
    const auto path = MediaSource::normalize(file);
    if (!path.isEmpty() && !seen.contains(path)) {
      result.append(path);
      seen.insert(path);
    }
    if (result.size() >= historyLimit())
      break;
  }
  return result;
}
void Profile::recordRecent(const QString &file) {
  const auto path = MediaSource::normalize(file);
  if (path.isEmpty())
    return;
  // An explicit new playback can resume recording after selective deletion.
  forgottenPositions.remove(path);
  observedSources.insert(path);
  if (!settings.value("history/enabled", true).toBool())
    return;
  auto files = recentFiles();
  files.removeAll(path);
  files.prepend(path);
  while (files.size() > historyLimit())
    files.removeLast();
  settings.setValue("history/files", files);
}
void Profile::setHistoryLimits(int stored, int menu) {
  settings.setValue("history/limit", std::clamp(stored, 1, 10000));
  settings.setValue("history/menuLimit", std::clamp(menu, 0, 100));
  settings.setValue("history/files", recentFiles());
}
void Profile::removeRecent(const QStringList &sources) {
  auto files = recentFiles();
  for (const auto &source : sources) {
    const auto path = MediaSource::normalize(source);
    if (path.isEmpty())
      continue;
    files.removeAll(path);
    settings.remove(key(path));
    forgottenPositions.insert(path);
  }
  settings.setValue("history/files", files);
}
void Profile::clearHistory() {
  for (const auto &file : recentFiles())
    forgottenPositions.insert(file);
  forgottenPositions.unite(observedSources);
  settings.remove("history/files");
  settings.remove("positions");
}
bool Profile::sourceMissing(const QString &source) {
  DeviceSource::Request request;
  if (DeviceSource::decode(source, &request)) {
    // Do not probe live hardware or infer absence from an unmounted disc.
    return (request.kind == "dvd" || request.kind == "bluray") &&
           !request.path.startsWith("/dev/") &&
           !QFileInfo::exists(request.path);
  }
  return !MediaSource::isUrl(source) && !QFileInfo::exists(source);
}
bool Profile::canUsePortablePath(const QString &source) {
  DeviceSource::Request request;
  if (DeviceSource::decode(source, &request))
    return (request.kind == "dvd" || request.kind == "bluray") &&
           !request.path.startsWith("/dev/");
  return !source.isEmpty() && !MediaSource::isUrl(source);
}
QString Profile::favoriteCategory(const QString &source) {
  DeviceSource::Request request;
  if (!DeviceSource::decode(source, &request))
    return "files";
  return request.kind == "dvd" || request.kind == "bluray" ||
                 request.kind == "cdda"
             ? "discs"
             : "devices";
}
bool Profile::canRememberFavoritePosition(const QString &source) {
  return !DeviceSource::isDevice(source) || canUsePortablePath(source);
}
namespace {
QString favoritePath(const QString &source, const QString &directory,
                     bool store) {
  // Device decode requires an absolute path, so resolve its path parameter
  // first.
  if (DeviceSource::isDevice(source)) {
    QUrl url(source);
    QUrlQuery query(url);
    const auto path = query.queryItemValue("path", QUrl::FullyDecoded);
    if (path.isEmpty())
      return {};
    const auto converted = store ? QDir(directory).relativeFilePath(path)
                                 : QDir(directory).absoluteFilePath(path);
    query.removeAllQueryItems("path");
    query.addQueryItem("path", converted);
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
  }
  return store ? QDir(directory).relativeFilePath(source)
               : QDir(directory).absoluteFilePath(source);
}
} // namespace
QList<Favorite> Profile::favorites() const {
  QList<Favorite> result;
  auto entries =
      QJsonDocument::fromJson(settings.value("favorites/entries").toByteArray())
          .array();
  for (auto value : entries) {
    auto o = value.toObject();
    const bool portable = o.value("portable").toBool(false);
    QString source = o.value("file").toString();
    if (portable && !MediaSource::isNetwork(source))
      source = favoritePath(source, directory, false);
    source = MediaSource::normalize(source);
    Favorite f{o.value("id").toString(),
               o.value("title").toString(),
               source,
               o.value("position").toDouble(),
               o.value("rememberPosition").toBool(true),
               portable};
    if (!f.id.isEmpty() && !f.file.isEmpty() && std::isfinite(f.position) &&
        f.position >= 0) {
      f.portable = portable && canUsePortablePath(f.file);
      if (!canRememberFavoritePosition(f.file))
        f.rememberPosition = false;
      if (f.title.trimmed().isEmpty())
        f.title = MediaSource::displayName(f.file);
      result.append(f);
    }
  }
  return result;
}
void Profile::storeFavorites(const QList<Favorite> &entries) {
  QJsonArray array;
  for (const auto &f : entries) {
    const bool portable = f.portable && canUsePortablePath(f.file);
    array.append(QJsonObject{
        {"id", f.id},
        {"title", f.title},
        {"file", portable ? favoritePath(f.file, directory, true) : f.file},
        {"position", f.position},
        {"rememberPosition", f.rememberPosition},
        {"portable", portable}});
  }
  settings.setValue("favorites/entries",
                    QJsonDocument(array).toJson(QJsonDocument::Compact));
}
QString Profile::addFavorite(const QString &title, const QString &file,
                             double position, bool remember, bool portable) {
  if (file.isEmpty() || !std::isfinite(position) || position < 0)
    return {};
  Favorite favorite{QUuid::createUuid().toString(QUuid::WithoutBraces),
                    title.trimmed(),
                    MediaSource::normalize(file),
                    position,
                    remember,
                    portable};
  if (favorite.file.isEmpty())
    return {};
  favorite.portable = portable && canUsePortablePath(favorite.file);
  favorite.rememberPosition =
      remember && canRememberFavoritePosition(favorite.file);
  if (favorite.title.isEmpty())
    favorite.title = MediaSource::displayName(favorite.file);
  auto entries = favorites();
  entries.append(favorite);
  storeFavorites(entries);
  return favorite.id;
}
void Profile::renameFavorite(const QString &id, const QString &title) {
  if (title.trimmed().isEmpty())
    return;
  auto entries = favorites();
  for (auto &entry : entries)
    if (entry.id == id)
      entry.title = title.trimmed();
  storeFavorites(entries);
}
void Profile::removeFavorite(const QString &id) {
  auto entries = favorites();
  for (int i = entries.size() - 1; i >= 0; --i)
    if (entries[i].id == id)
      entries.removeAt(i);
  storeFavorites(entries);
}

bool Profile::updateFavorite(const QString &id, const QString &title,
                             double position, bool remember, bool portable) {
  if (title.trimmed().isEmpty() || !std::isfinite(position) || position < 0)
    return false;
  auto entries = favorites();
  for (auto &entry : entries)
    if (entry.id == id) {
      entry.title = title.trimmed();
      entry.position = position;
      entry.rememberPosition =
          remember && canRememberFavoritePosition(entry.file);
      entry.portable = portable && canUsePortablePath(entry.file);
      storeFavorites(entries);
      return true;
    }
  return false;
}
bool Profile::reorderFavorites(const QStringList &ids) {
  const auto entries = favorites();
  if (ids.size() != entries.size() ||
      QSet<QString>(ids.begin(), ids.end()).size() != ids.size())
    return false;
  QMap<QString, Favorite> byId;
  for (const auto &entry : entries)
    byId.insert(entry.id, entry);
  QList<Favorite> reordered;
  for (const auto &id : ids) {
    if (!byId.contains(id))
      return false;
    reordered.append(byId.value(id));
  }
  storeFavorites(reordered);
  return true;
}
