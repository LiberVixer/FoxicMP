#include "framepreview.h"
#include "i18n.h"
#include "mediasource.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <cmath>
namespace {
bool http(const QString &source) {
  const auto url = QUrl(source);
  return url.isValid() && (url.scheme() == "http" || url.scheme() == "https") &&
         !url.host().isEmpty();
}
bool direct(const QString &source) {
  const auto suffix = QFileInfo(QUrl(source).path()).suffix().toLower();
  return QStringList{"mp4", "mkv", "webm", "mov", "m3u8",
                     "mpd", "ts",  "avi",  "m4v"}
      .contains(suffix);
}
} // namespace
FramePreview::FramePreview(QObject *parent) : QObject(parent) {
  debounce.setSingleShot(true);
  debounce.setInterval(180);
  deadline.setSingleShot(true);
  connect(&debounce, &QTimer::timeout, this, &FramePreview::start);
  connect(&deadline, &QTimer::timeout, this,
          [this] { fail(uiTr("Превью не готово: время ожидания истекло")); });
}
FramePreview::~FramePreview() {
  cancel();
  // Normal hover cancellation stays asynchronous. At owner destruction reap
  // killed workers before QObject destroys its QProcess children.
  for (auto *job : findChildren<QProcess *>())
    if (job->state() != QProcess::NotRunning) {
      job->kill();
      job->waitForFinished(1000);
    }
}
bool FramePreview::available() const {
  return !QStandardPaths::findExecutable("ffmpeg").isEmpty();
}
void FramePreview::configure(const QSize &wanted, bool network,
                             const QString &tool, const QString &agent,
                             int timeout) {
  const QSize bounded(std::clamp(wanted.width(), 80, 960),
                      std::clamp(wanted.height(), 45, 540));
  timeout = std::clamp(timeout, 1000, 20000);
  const QString cleanAgent = agent.size() > 2048 || agent.contains('\r') ||
                                     agent.contains('\n') ||
                                     agent.contains(QChar(0))
                                 ? QString()
                                 : agent;
  if (size == bounded && online == network && extractor == tool &&
      userAgent == cleanAgent && timeoutMs == timeout)
    return;
  cancel();
  cache.clear();
  resolvedSource.clear();
  resolvedUrl.clear();
  headers.clear();
  size = bounded;
  online = network;
  extractor = tool;
  userAgent = cleanAgent;
  timeoutMs = timeout;
}
void FramePreview::setFilters(const QString &value) {
  if (filters == value)
    return;
  cancel();
  filters = value;
  cache.clear();
}
void FramePreview::setVideoStream(int index) {
  index = std::clamp(index, -1, 1024);
  if (index == videoStream)
    return;
  cancel();
  videoStream = index;
  cache.clear();
}
void FramePreview::cancel() {
  ++generation;
  debounce.stop();
  deadline.stop();
  resolving = false;
  if (process) {
    auto *old = process;
    process = nullptr;
    old->disconnect(this);
    old->kill();
    connect(old, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), old,
            &QObject::deleteLater);
    if (old->state() == QProcess::NotRunning)
      old->deleteLater();
  }
}
void FramePreview::fail(const QString &message) {
  cancel();
  emit unavailable(message);
}
void FramePreview::request(const QString &file, double seconds) {
  const bool network = MediaSource::isUrl(file);
  const QFileInfo info(file);
  if (!std::isfinite(seconds) || seconds < 0 ||
      (network ? !online || !http(file) : !info.isFile())) {
    fail(uiTr("Превью недоступно"));
    return;
  }
  const double target = std::floor(seconds * 10) / 10;
  const QString source = network ? file : info.absoluteFilePath();
  const QString key =
      source + "|" +
      (network ? QString()
               : QString::number(info.size()) + "|" +
                     QString::number(info.lastModified().toMSecsSinceEpoch())) +
      "|" + QString::number(target, 'f', 1) + "|" + filters;
  if (key == pendingKey && (debounce.isActive() || process))
    return;
  // Finish one extractor request while the cursor moves on the same source;
  // decode only the most recently requested time afterwards.
  if (resolving && pendingFile == source) {
    pendingTime = target;
    pendingKey = key;
    return;
  }
  cancel();
  pendingFile = source;
  pendingTime = target;
  pendingKey = key;
  if (auto *image = cache.object(key)) {
    emit ready(*image, target);
    return;
  }
  if (!available()) {
    emit unavailable(uiTr("Для превью установите ffmpeg"));
    return;
  }
  debounce.start();
}
void FramePreview::start() {
  const bool network = http(pendingFile);
  if (network && resolvedSource == pendingFile &&
      QDateTime::currentMSecsSinceEpoch() - resolvedAt < 5 * 60 * 1000 &&
      http(resolvedUrl)) {
    decode();
    return;
  }
  if (network && !direct(pendingFile) && !extractor.isEmpty()) {
    resolve();
    return;
  }
  resolvedSource = pendingFile;
  resolvedUrl = pendingFile;
  headers.clear();
  resolvedAt = QDateTime::currentMSecsSinceEpoch();
  decode();
}
void FramePreview::resolve() {
  auto *job = new QProcess(this);
  process = job;
  resolving = true;
  const auto token = generation;
  const auto source = pendingFile;
  connect(job, &QProcess::readyReadStandardOutput, this, [this, job] {
    if (job->bytesAvailable() > 2 * 1024 * 1024)
      fail(uiTr("Слишком большой ответ для превью"));
  });
  connect(job, &QProcess::errorOccurred, this,
          [this, job, token](QProcess::ProcessError) {
            if (token == generation && process == job)
              fail(uiTr("Не удалось запустить получение онлайн-превью"));
          });
  connect(job, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, job, token, source](int code, QProcess::ExitStatus status) {
            if (token != generation || process != job) {
              job->deleteLater();
              return;
            }
            deadline.stop();
            process = nullptr;
            resolving = false;
            const auto data = job->readAllStandardOutput();
            job->deleteLater();
            const auto object = QJsonDocument::fromJson(data).object();
            const auto url = object.value("url").toString();
            if (code != 0 || status != QProcess::NormalExit || !http(url) ||
                data.size() > 2 * 1024 * 1024) {
              emit unavailable(uiTr("Не удалось получить адрес онлайн-превью"));
              return;
            }
            resolvedSource = source;
            resolvedUrl = url;
            headers.clear();
            resolvedAt = QDateTime::currentMSecsSinceEpoch();
            const auto values = object.value("http_headers").toObject();
            for (const auto &name : {"User-Agent", "Referer", "Origin"}) {
              const auto value = values.value(name).toString();
              if (!value.isEmpty() && value.size() < 2048 &&
                  !value.contains('\r') && !value.contains('\n') &&
                  !value.contains(QChar(0)))
                headers += QString(name) + ": " + value + "\r\n";
            }
            decode();
          });
  const auto limit =
      QString("[height<=%1]").arg(std::clamp(size.height(), 240, 540));
  const auto format =
      "bestvideo" + limit + "[vcodec^=avc][protocol^=http]/best" + limit +
      "[vcodec^=avc]/bestvideo" + limit + "[protocol^=http]/best" + limit +
      "[protocol^=http]/bestvideo" + limit + "/best" + limit;
  QStringList args{"--ignore-config",    "--no-playlist",
                   "--no-warnings",      "--skip-download",
                   "--dump-single-json", "--no-progress",
                   "--socket-timeout",   "5",
                   "--format",           format};
  if (!userAgent.isEmpty())
    args << "--user-agent" << userAgent;
  args << "--" << pendingFile;
  job->start(extractor, args);
  deadline.start(timeoutMs);
}
void FramePreview::decode() {
  auto *job = new QProcess(this);
  process = job;
  const auto token = generation;
  const auto key = pendingKey;
  const double seconds = pendingTime;
  connect(job, &QProcess::readyReadStandardOutput, this, [this, job] {
    if (job->bytesAvailable() > 8 * 1024 * 1024)
      fail(uiTr("Слишком большое изображение превью"));
  });
  connect(job, &QProcess::errorOccurred, this,
          [this, job, token](QProcess::ProcessError) {
            if (token == generation && process == job)
              fail(uiTr("Не удалось запустить превью"));
          });
  connect(
      job, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
      [this, job, token, key, seconds](int code, QProcess::ExitStatus status) {
        if (token != generation || process != job) {
          job->deleteLater();
          return;
        }
        deadline.stop();
        process = nullptr;
        QImage image;
        image.loadFromData(job->readAllStandardOutput(), "PNG");
        job->deleteLater();
        if (code != 0 || status != QProcess::NormalExit || image.isNull() ||
            image.width() > 960 || image.height() > 960) {
          emit unavailable(uiTr("В этом месте превью недоступно"));
          return;
        }
        cache.insert(key, new QImage(image),
                     int(std::max<qint64>(1, image.sizeInBytes() / 1024)));
        emit ready(image, seconds);
      });
  const bool network = http(pendingFile);
  QStringList args{"-hide_banner",
                   "-loglevel",
                   "error",
                   "-nostdin",
                   "-threads",
                   "1",
                   "-protocol_whitelist",
                   network ? "http,https,tcp,tls,crypto,httpproxy"
                           : "file,pipe"};
  if (network) {
    args << "-rw_timeout" << "5000000" << "-user_agent"
         << (userAgent.isEmpty() ? "FoxicMPQt/0.1.21" : userAgent);
    if (!headers.isEmpty())
      args << "-headers" << headers;
    args << "-probesize" << "2097152" << "-analyzeduration" << "2000000";
  }
  args << "-ss" << QString::number(seconds, 'f', 1) << "-i"
       << (network ? resolvedUrl : pendingFile);
  if (videoStream >= 0)
    args << "-map" << QString("0:%1").arg(videoStream);
  args << "-an" << "-sn" << "-dn" << "-vf";
  const auto scale = QString("scale=%1:%2:force_original_aspect_ratio=decrease")
                         .arg(size.width())
                         .arg(size.height());
  args << (filters.isEmpty() ? scale : filters + "," + scale) << "-frames:v"
       << "1" << "-c:v" << "png" << "-threads" << "1" << "-f" << "image2pipe"
       << "pipe:1";
  job->start(QStandardPaths::findExecutable("ffmpeg"), args);
  deadline.start(network ? timeoutMs : 3500);
}
