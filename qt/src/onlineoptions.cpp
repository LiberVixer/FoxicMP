#include "onlineoptions.h"
#include "i18n.h"
#include "mediasource.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrlQuery>
#include <algorithm>

namespace {
QString safeText(QString value, const QString &fallback = {}) {
  value = value.trimmed();
  if (value.contains('\n') || value.contains('\r') || value.contains(','))
    return fallback;
  return value;
}

QString filters(QSettings &settings, bool audio, bool relaxedHdr = false) {
  QString result;
  auto add = [&](const QString &value) { result += '[' + value + ']'; };
  if (!audio) {
    const int height = settings.value("online/maxHeight", 1080).toInt();
    const int fps = settings.value("online/maxFps", 60).toInt();
    const int bitrate = settings.value("online/maxVideoBitrate", 0).toInt();
    if (height > 0)
      add(QString("height<=%1").arg(std::clamp(height, 144, 4320)));
    if (fps > 0)
      add(QString("fps<=%1").arg(std::clamp(fps, 24, 120)));
    if (bitrate > 0)
      add(QString("vbr<=%1").arg(std::clamp(bitrate, 100, 100000)));
    const QString codec = settings.value("online/videoCodec", "any").toString();
    if (codec == "h264")
      add("vcodec^=avc");
    else if (codec == "vp9")
      add("vcodec^=vp");
    else if (codec == "av1")
      add("vcodec^=av01");
    const QString hdr = settings.value("online/hdr", "allow").toString();
    if (!relaxedHdr && hdr == "avoid")
      add("dynamic_range=SDR");
    else if (!relaxedHdr && hdr == "prefer")
      add("dynamic_range!=SDR");
  } else {
    const QString codec = settings.value("online/audioCodec", "any").toString();
    if (codec == "aac")
      add("acodec^=mp4a");
    else if (codec == "opus")
      add("acodec=opus");
    const QString language =
        safeText(settings.value("online/audioLanguage").toString());
    if (QRegularExpression("^[A-Za-z0-9_-]{1,16}$").match(language).hasMatch())
      add("language^=" + language);
  }
  return result;
}
} // namespace

QString OnlineOptions::defaultUserAgent() {
  const auto version = QCoreApplication::applicationVersion();
  return version.isEmpty() ? QStringLiteral("FoxicMPQt") : "FoxicMPQt/" + version;
}

QString OnlineOptions::resolveYtDlp(QSettings &settings) {
  QString configured = settings.value("online/ytdlpPath").toString().trimmed();
  if (!configured.isEmpty()) {
    QFileInfo file(configured);
    return file.isFile() && file.isExecutable() ? file.absoluteFilePath()
                                                : QString();
  }
  return QStandardPaths::findExecutable("yt-dlp");
}

QString OnlineOptions::formatSelector(QSettings &settings) {
  const QString video = filters(settings, false);
  const QString audio = filters(settings, true);
  QStringList choices;
  choices << "bv*" + video + "+ba" + audio;
  if (settings.value("online/hdr", "allow").toString() != "allow")
    choices << "bv*" + filters(settings, false, true) + "+ba" + audio;
  choices << "b" + video;
  return choices.join('/');
}

QMap<QString, QString> OnlineOptions::mpvOptions(QSettings &settings) {
  QMap<QString, QString> result;
  const int timeout =
      std::clamp(settings.value("online/networkTimeout", 30).toInt(), 5, 300);
  result["network-timeout"] = QString::number(timeout);
  const QString userAgent = safeText(
      settings.value("online/userAgent", defaultUserAgent()).toString(),
      defaultUserAgent());
  result["user-agent"] = userAgent;
  const QString executable = resolveYtDlp(settings);
  if (!settings.value("online/enabled", true).toBool() ||
      executable.isEmpty()) {
    result["ytdl"] = "no";
    return result;
  }
  result["ytdl"] = "yes";
  result["ytdl-format"] = formatSelector(settings);
  result["script-opts"] = "ytdl_hook-ytdl_path=" + executable;
  result["ytdl-raw-options"] =
      QString("ignore-config=,socket-timeout=%1,user-agent=%2,extractor-"
              "retries=3,fragment-retries=3")
          .arg(timeout)
          .arg(userAgent);
  return result;
}

QStringList OnlineOptions::playlistArguments(QSettings &settings,
                                             const QString &url) {
  const int timeout =
      std::clamp(settings.value("online/networkTimeout", 30).toInt(), 5, 300);
  const QString userAgent = safeText(
      settings.value("online/userAgent", defaultUserAgent()).toString(),
      defaultUserAgent());
  return {"--ignore-config",
          "--flat-playlist",
          "--dump-single-json",
          "--playlist-end",
          "500",
          "--socket-timeout",
          QString::number(timeout),
          "--user-agent",
          userAgent,
          "--",
          url};
}

QString OnlineOptions::playbackUrl(QSettings &settings, const QString &source,
                                   QString *error) {
  const QUrl input(source);
  const QString scheme = input.scheme().toLower();
  if (scheme != "acestream" && scheme != "magnet" && scheme != "torrserver")
    return source;
  auto fail = [&](const QString &message) {
    if (error)
      *error = message;
    return QString();
  };
  QString endpoint;
  QString link;
  QString index;
  if (scheme == "acestream") {
    endpoint = settings
                   .value("online/aceStreamEndpoint",
                          "http://127.0.0.1:6878/ace/getstream")
                   .toString();
    link = input.host() + input.path();
    link.remove('/');
    if (!QRegularExpression("^[A-Fa-f0-9]{40}$").match(link).hasMatch())
      return fail(uiTr("Неверный идентификатор Ace Stream"));
  } else {
    endpoint =
        settings
            .value("online/torrServerEndpoint", "http://127.0.0.1:8090/stream")
            .toString();
    if (scheme == "magnet")
      link = source;
    else {
      link = input.host() + input.path();
      if (link.startsWith('/'))
        link.remove(0, 1);
      QUrlQuery original(input);
      index = original.queryItemValue("index");
    }
    if (link.isEmpty())
      return fail(uiTr("В адресе TorrServer не указан magnet или hash"));
  }
  QUrl result(endpoint, QUrl::StrictMode);
  if (!result.isValid() ||
      (result.scheme() != "http" && result.scheme() != "https") ||
      result.host().isEmpty())
    return fail(uiTr("Неверный адрес локального потокового сервера"));
  QUrlQuery query(result);
  if (scheme == "acestream")
    query.addQueryItem("id", link);
  else {
    query.addQueryItem("link", link);
    if (!index.isEmpty())
      query.addQueryItem("index", index);
    query.addQueryItem("play", "1");
  }
  result.setQuery(query);
  return result.toString(QUrl::FullyEncoded);
}

bool OnlineOptions::parsePlaylist(const QByteArray &json,
                                  QList<OnlineEntry> *entries, QString *error) {
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    *error = uiTr("yt-dlp вернул неверные данные плейлиста");
    return false;
  }
  const QJsonArray values = document.object().value("entries").toArray();
  QList<OnlineEntry> result;
  for (const QJsonValue &value : values) {
    if (!value.isObject())
      continue;
    const QJsonObject object = value.toObject();
    QString url = object.value("webpage_url").toString();
    if (url.isEmpty())
      url = object.value("original_url").toString();
    if (url.isEmpty())
      url = object.value("url").toString();
    if (!url.contains("://") && object.value("ie_key").toString().startsWith(
                                    "Youtube", Qt::CaseInsensitive))
      url = "https://www.youtube.com/watch?v=" + url;
    QString sourceError;
    url = MediaSource::normalize(url, &sourceError);
    if (url.isEmpty() || !MediaSource::isNetwork(url))
      continue;
    result.append({url, object.value("title").toString().trimmed()});
    if (result.size() >= 500)
      break;
  }
  if (result.isEmpty()) {
    *error = uiTr("Онлайн-плейлист не содержит доступных записей");
    return false;
  }
  *entries = result;
  return true;
}
