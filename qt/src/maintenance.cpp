#include "maintenance.h"
#include "i18n.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QTimer>
#include <QVersionNumber>
#include <memory>
#include <cerrno>
#include <cstring>
#include <sys/resource.h>

Maintenance::Update Maintenance::releases(const QByteArray &bytes,
                                         const QString &current, bool previews) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  if (bytes.size() > 2 * 1024 * 1024 || error.error != QJsonParseError::NoError ||
      !document.isArray() || document.array().size() > 100)
    return {"error", {}, uiTr("Некорректный ответ сервера обновлений."), {}};
  QVersionNumber newest;
  QUrl page;
  QString found;
  const QRegularExpression assetPattern("^FoxicMPQt-(\\d+\\.\\d+\\.\\d+)-linux-x64\\.tar\\.gz$");
  for (const auto &entry : document.array()) {
    const auto release = entry.toObject();
    if (release.value("draft").toBool() ||
        (!previews && release.value("prerelease").toBool()))
      continue;
    const QUrl link(release.value("html_url").toString());
    if (link.scheme() != "https" || link.host() != "github.com" ||
        !link.userInfo().isEmpty() || link.hasQuery() || link.hasFragment() ||
        !link.path().startsWith("/LiberVixer/FoxicMP/releases/tag/") ||
        link.port(-1) != -1)
      continue;
    for (const auto &value : release.value("assets").toArray()) {
      const auto asset = value.toObject();
      const auto match = assetPattern.match(asset.value("name").toString());
      if (!match.hasMatch() || asset.value("state").toString() != "uploaded" ||
          asset.value("size").toDouble() <= 0)
        continue;
      const auto candidate = QVersionNumber::fromString(match.captured(1));
      if (newest.isNull() || candidate > newest) {
        newest = candidate;
        found = match.captured(1);
        page = link;
      }
    }
  }
  if (found.isEmpty())
    return {"unpublished", {}, {}, {}};
  return {newest > QVersionNumber::fromString(current) ? "available" : "current",
          found, {}, page};
}
int Maintenance::niceValue(const QString &mode) {
  return mode == "above" ? -5 : mode == "low" ? 5 : 0;
}
QVariantMap Maintenance::priority(const QString &mode, bool apply) {
  const int requested = niceValue(mode);
  QString error;
  // Linux nice is per thread. Include Qt threads already created by QApplication;
  // decoder/render workers born later inherit their creator's priority.
  if (apply) {
    const auto threads = QDir("/proc/self/task").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &thread : threads) {
      bool valid = false;
      const auto tid = thread.toUInt(&valid);
      if (valid && setpriority(PRIO_PROCESS, tid, requested) != 0 && errno != ESRCH && error.isEmpty())
        error = QString::fromLocal8Bit(std::strerror(errno));
    }
    if (threads.isEmpty() && setpriority(PRIO_PROCESS, 0, requested) != 0)
      error = QString::fromLocal8Bit(std::strerror(errno));
  }
  errno = 0;
  const int actual = getpriority(PRIO_PROCESS, 0);
  if (errno && error.isEmpty())
    error = QString::fromLocal8Bit(std::strerror(errno));
  return {{"requestedMode", mode}, {"requestedNice", requested},
          {"actualNice", actual}, {"applied", error.isEmpty() && requested == actual},
          {"error", error}};
}
QByteArray Maintenance::report(const QVariantMap &runtime,
                               const QStringList &messages, bool details) {
  QVariantMap data = runtime;
  data["schema"] = "foxicmp-qt-diagnostic-1";
  data["generatedUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  data["qtVersion"] = qVersion();
  data["system"] = QSysInfo::prettyProductName();
  data["kernel"] = QSysInfo::kernelVersion();
  data["architecture"] = QSysInfo::currentCpuArchitecture();
  data["detailsIncluded"] = details;
  data["recentMessageCount"] = messages.size();
  if (details)
    data["recentMessages"] = messages;
  else {
    auto stats = data.value("renderStatistics").toMap();
    stats.remove("vo-passes"); // Shader descriptions may contain local file names.
    data["renderStatistics"] = stats;
    data.remove("source");
    data.remove("profileDirectory");
    data.remove("audioDevices");
    data.remove("audioDevice");
    data.remove("lastError");
  }
  return QJsonDocument(QJsonObject::fromVariantMap(data)).toJson(QJsonDocument::Indented);
}
bool Maintenance::saveReport(const QString &path, const QByteArray &bytes, QString *error) {
  const QFileInfo target(path);
  if (target.isSymLink() || (target.exists() && !target.isFile())) {
    if (error) *error = uiTr("Отчёт нельзя записать в ссылку или специальный файл.");
    return false;
  }
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) *error = file.errorString();
    return false;
  }
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  if (file.write(bytes) != bytes.size() || !file.commit()) {
    if (error) *error = file.errorString();
    return false;
  }
  return true;
}
UpdateCheck::UpdateCheck(QObject *parent)
    : QObject(parent), network(new QNetworkAccessManager(this)) {}
bool UpdateCheck::start(const QString &current, bool previews) {
  return startAt(QUrl("https://api.github.com/repos/LiberVixer/FoxicMP/releases?per_page=100"), current, previews);
}
bool UpdateCheck::startAt(const QUrl &url, const QString &current, bool previews) {
  if (reply) return false;
  QNetworkRequest request(url);
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("User-Agent", "FoxicMPQt/" + current.toUtf8());
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(12000);
  reply = network->get(request);
  auto *active = reply;
  auto bytes = std::make_shared<QByteArray>();
  auto *timer = new QTimer(active);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, active, &QNetworkReply::abort);
  timer->start(15000);
  connect(active, &QNetworkReply::readyRead, active, [active, bytes] {
    if (active->isOpen()) bytes->append(active->readAll());
    if (bytes->size() > 2 * 1024 * 1024) active->abort();
  });
  connect(active, &QNetworkReply::finished, this, [=] {
    timer->stop();
    if (active->isOpen()) bytes->append(active->readAll());
    Maintenance::Update result;
    const int code = active->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (active->error() != QNetworkReply::NoError || code != 200)
      result = {"error", {}, uiTr("Проверка не выполнена: HTTP %1; %2").arg(code).arg(active->errorString()), {}};
    else
      result = Maintenance::releases(*bytes, current, previews);
    reply = nullptr;
    active->deleteLater();
    emit finished(result.status, result.version, result.page, result.error);
  });
  return true;
}
void UpdateCheck::cancel() { if (reply) reply->abort(); }
