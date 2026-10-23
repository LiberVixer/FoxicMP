#include "mediasource.h"
#include "devicesource.h"
#include "i18n.h"
#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSet>
#include <QUrl>
#include <algorithm>

namespace {
const QSet<QString> networkSchemes{
    "http", "https", "rtsp", "rtmp",      "rtmps",      "udp",
    "tcp",  "ftp",   "srt",  "acestream", "torrserver", "magnet"};
const QSet<QString> mediaExtensions{
    "264",  "265",  "3g2",  "3gp",  "aac", "ac3",  "aiff", "alac", "ape",
    "asf",  "avi",  "divx", "dts",  "evo", "f4v",  "flac", "flv",  "h264",
    "h265", "hevc", "ivf",  "m1v",  "m2t", "m2ts", "m2v",  "m4a",  "m4v",
    "mka",  "mk3d", "mkv",  "mov",  "mp3", "mp4",  "mpg",  "mpeg", "mts",
    "mxf",  "nut",  "ogg",  "ogm",  "ogv", "opus", "rm",   "rmvb", "ssif",
    "ts",   "vob",  "wav",  "webm", "wma", "wmv",  "y4m"};
} // namespace

QString MediaSource::normalize(const QString &source, QString *error) {
  const QString input = source.trimmed();
  auto fail = [&](const QString &message) {
    if (error)
      *error = message;
    return QString();
  };
  if (input.isEmpty())
    return fail(uiTr("Источник не указан"));
  const QUrl url(input, QUrl::StrictMode);
  if (DeviceSource::isDevice(input)) {
    DeviceSource::Request request;
    return DeviceSource::decode(input, &request, error)
               ? DeviceSource::encode(request)
               : QString();
  }
  if (url.isLocalFile())
    return QDir::cleanPath(QFileInfo(url.toLocalFile()).absoluteFilePath());
  if (url.scheme().compare("magnet", Qt::CaseInsensitive) == 0 &&
      input.startsWith("magnet:?", Qt::CaseInsensitive) && url.isValid())
    return url.toString(QUrl::FullyEncoded);
  // A colon is legal in a Unix filename. Treat only URI-shaped input as a URL.
  if (input.contains("://")) {
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || !networkSchemes.contains(scheme))
      return fail(uiTr("Неподдерживаемый или неверный сетевой адрес"));
    if ((scheme == "http" || scheme == "https" || scheme == "ftp" ||
         scheme == "rtsp" || scheme == "rtmp" || scheme == "rtmps") &&
        url.host().isEmpty())
      return fail(uiTr("В сетевом адресе не указан сервер"));
    return url.toString(QUrl::FullyEncoded);
  }
  return QDir::cleanPath(QFileInfo(input).absoluteFilePath());
}

bool MediaSource::isNetwork(const QString &source) {
  if (!source.contains("://"))
    return source.startsWith("magnet:?", Qt::CaseInsensitive);
  return networkSchemes.contains(QUrl(source).scheme().toLower());
}

QString MediaSource::displayName(const QString &source) {
  if (DeviceSource::isDevice(source))
    return DeviceSource::label(source);
  if (!isNetwork(source))
    return QFileInfo(source).fileName();
  const QUrl url(source);
  if (url.scheme() == "magnet")
    return "Magnet";
  if (url.scheme() == "acestream")
    return "Ace Stream " + url.host();
  if (url.scheme() == "torrserver")
    return "TorrServer " + url.host();
  const QString file = QFileInfo(url.path()).fileName();
  return file.isEmpty() ? url.host() : QUrl::fromPercentEncoding(file.toUtf8());
}

bool MediaSource::isUrl(const QString &source) {
  return isNetwork(source) || DeviceSource::isDevice(source);
}

QStringList MediaSource::filesInDirectory(const QString &directory) {
  const QDir dir(directory);
  if (!dir.exists())
    return {};
  QStringList files;
  QMimeDatabase mime;
  for (const auto &file :
       dir.entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort)) {
    const QString type =
        mime.mimeTypeForFile(file, QMimeDatabase::MatchExtension).name();
    if (mediaExtensions.contains(file.suffix().toLower()) ||
        type.startsWith("video/") || type.startsWith("audio/"))
      files.append(file.absoluteFilePath());
  }
  QCollator collator;
  if (collator.locale().language() == QLocale::C)
    collator.setLocale(QLocale(QLocale::English));
  collator.setNumericMode(true);
  std::sort(files.begin(), files.end(),
            [&collator](const QString &a, const QString &b) {
              return collator.compare(QFileInfo(a).fileName(),
                                      QFileInfo(b).fileName()) < 0;
            });
  return files;
}
