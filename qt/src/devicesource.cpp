#include "devicesource.h"
#include "i18n.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QUrl>
#include <QUrlQuery>
#include <sys/stat.h>

namespace {
const QSet<QString> kinds{"dvd",  "bluray", "cdda",       "v4l2",      "pulse",
                          "alsa", "dvb",    "test-video", "test-audio"};
bool plain(const QString &value) {
  return value.size() <= 4096 && !value.contains(QChar(0)) &&
         !value.contains('\n') && !value.contains('\r');
}
bool deviceNode(const QString &path, bool block) {
  struct stat info{};
  const auto native = QFile::encodeName(path);
  return !stat(native.constData(), &info) &&
         (block ? S_ISBLK(info.st_mode) : S_ISCHR(info.st_mode));
}
} // namespace

bool DeviceSource::isDevice(const QString &source) {
  return QUrl(source).scheme().compare("foxicmp-device", Qt::CaseInsensitive) ==
         0;
}
QString DeviceSource::encode(const Request &r) {
  QUrl url;
  url.setScheme("foxicmp-device");
  url.setHost(r.kind);
  QUrlQuery query;
  query.addQueryItem("path", r.path);
  query.addQueryItem("title", QString::number(r.title));
  query.addQueryItem("angle", QString::number(r.angle));
  query.addQueryItem("card", QString::number(r.card));
  if (!r.audio.isEmpty())
    query.addQueryItem("audio", r.audio);
  if (!r.channels.isEmpty())
    query.addQueryItem("channels", r.channels);
  if (!r.channel.isEmpty())
    query.addQueryItem("channel", r.channel);
  url.setQuery(query);
  return url.toString(QUrl::FullyEncoded);
}
bool DeviceSource::decode(const QString &source, Request *r, QString *error) {
  auto fail = [&] {
    if (error)
      *error = uiTr("Неверные параметры диска или устройства");
    return false;
  };
  const QUrl url(source, QUrl::StrictMode);
  if (!isDevice(source) || !url.isValid() || !kinds.contains(url.host()) ||
      !url.userInfo().isEmpty() || url.port() != -1 || !url.path().isEmpty() ||
      url.hasFragment())
    return fail();
  QMap<QString, QString> values;
  const QSet<QString> keys{"path",  "title",    "angle",  "card",
                           "audio", "channels", "channel"};
  for (const auto &item : QUrlQuery(url).queryItems(QUrl::FullyDecoded)) {
    if (!keys.contains(item.first) || values.contains(item.first) ||
        !plain(item.second))
      return fail();
    values[item.first] = item.second;
  }
  Request next;
  next.kind = url.host();
  next.path = values.value("path");
  next.audio = values.value("audio");
  next.channels = values.value("channels");
  next.channel = values.value("channel");
  auto number = [&](const QString &key, int fallback, int low, int high,
                    int *target) {
    const QString text = values.value(key, QString::number(fallback));
    bool ok = false;
    *target = text.toInt(&ok);
    return ok && QRegularExpression("^[0-9]+$").match(text).hasMatch() &&
           *target >= low && *target <= high;
  };
  if (!number("title", 0, 0, 99999, &next.title) ||
      !number("angle", 1, 1, 9, &next.angle) ||
      !number("card", 0, 0, 15, &next.card) ||
      (next.kind == "dvd" && next.title > 99))
    return fail();
  if ((next.kind == "dvd" || next.kind == "bluray" || next.kind == "cdda") &&
      !QDir::isAbsolutePath(next.path))
    return fail();
  if (next.kind == "v4l2" &&
      !QRegularExpression("^/dev/video[0-9]+$").match(next.path).hasMatch())
    return fail();
  if ((next.kind == "pulse" || next.kind == "alsa") && next.path.isEmpty())
    return fail();
  if (!next.audio.isEmpty() && !next.audio.startsWith("pulse:") &&
      !next.audio.startsWith("alsa:"))
    return fail();
  if (next.kind == "dvb" &&
      (next.channel.isEmpty() || next.channels.isEmpty() ||
       QFileInfo(next.channels).fileName() != next.channels ||
       next.channels == "." || next.channels == ".."))
    return fail();
  *r = next;
  return true;
}
QString DeviceSource::label(const QString &source) {
  Request r;
  if (!decode(source, &r))
    return uiTr("Устройство");
  if (r.kind == "test-video")
    return uiTr("Тестовый видеосигнал");
  if (r.kind == "test-audio")
    return uiTr("Тестовый звук");
  if (r.kind == "dvb")
    return uiTr("ТВ: ") + r.channel;
  const QString name = QFileInfo(r.path).fileName();
  const QString type = r.kind == "dvd"      ? "DVD"
                       : r.kind == "bluray" ? "Blu-ray"
                       : r.kind == "cdda"   ? "Audio CD"
                                            : uiTr("Захват");
  return type + ": " + (name.isEmpty() ? r.path : name) +
         ((r.kind == "dvd" || r.kind == "bluray") && r.title
              ? QString(" [%1]").arg(r.title)
              : "");
}
bool DeviceSource::rememberPosition(const QString &source) {
  Request r;
  if (!decode(source, &r))
    return !isDevice(source);
  return (r.kind == "dvd" || r.kind == "bluray") &&
         (QFileInfo(r.path).isFile() || QFileInfo(r.path).isDir());
}
bool DeviceSource::isLive(const QString &source) {
  Request r;
  return decode(source, &r) && r.kind != "dvd" && r.kind != "bluray" &&
         r.kind != "cdda";
}
DeviceSource::Plan DeviceSource::resolve(const QString &source,
                                         const QStringList &protocols,
                                         const QStringList &formats,
                                         const QString &directory) {
  Plan plan;
  Request r;
  if (!decode(source, &r, &plan.error))
    return plan;
  auto require = [&](bool condition, const QString &message) {
    if (!condition)
      plan.error = message;
    return condition;
  };
  const bool optical =
      r.kind == "dvd" || r.kind == "bluray" || r.kind == "cdda";
  if (optical) {
    const QString protocol = r.kind == "bluray" ? "bd" : r.kind;
    if (!require(protocols.contains(protocol),
                 uiTr("Движок не поддерживает ") + r.kind) ||
        !require(QFileInfo(r.path).exists() && QFileInfo(r.path).isReadable(),
                 uiTr("Диск или образ недоступен для чтения")))
      return plan;
    if (r.kind == "cdda" &&
        !require(deviceNode(r.path, true), uiTr("Для Audio CD нужен дисковод")))
      return plan;
    if (QFileInfo(r.path).isDir()) {
      const QDir folder(r.path);
      const bool valid =
          r.kind == "dvd"
              ? QFileInfo::exists(folder.filePath("VIDEO_TS/VIDEO_TS.IFO")) ||
                    QFileInfo::exists(folder.filePath("VIDEO_TS.IFO"))
              : QFileInfo::exists(folder.filePath("BDMV/index.bdmv"));
      if (!require(valid, uiTr("В папке нет структуры выбранного диска")))
        return plan;
    }
    plan.url =
        r.kind == "dvd"
            ? "dvd://" + (r.title ? QString::number(r.title - 1) : "longest")
        : r.kind == "bluray"
            ? "bd://" +
                  (r.title ? "mpls/" + QString::number(r.title) : "longest")
            : "cdda://";
    plan.options[r.kind == "cdda"  ? "cdda-device"
                 : r.kind == "dvd" ? "dvd-device"
                                   : "bluray-device"] = r.path;
    if (r.kind == "dvd")
      plan.options["dvd-angle"] = QString::number(r.angle);
  } else if (r.kind == "dvb") {
    if (!require(protocols.contains("dvb"), uiTr("Движок не поддерживает DVB")))
      return plan;
    const QString config = QDir(directory).filePath(r.channels);
    QFile input(config);
    if (!require(input.open(QIODevice::ReadOnly) && input.size() <= 1024 * 1024,
                 uiTr("Требуется доступный список каналов в папке программы")))
      return plan;
    if (!require(channels(input.readAll()).contains(r.channel),
                 uiTr("Канал отсутствует в выбранном списке")))
      return plan;
    if (!require(QFileInfo::exists(
                     QString("/dev/dvb/adapter%1/frontend0").arg(r.card)),
                 uiTr("Выбранный DVB-тюнер не найден")))
      return plan;
    plan.url = "dvb://";
    plan.options["dvbin-prog"] = r.channel;
    plan.options["dvbin-file"] = config;
    plan.options["dvbin-card"] = QString::number(r.card);
    plan.options["dvbin-timeout"] = "10";
    plan.live = true;
  } else {
    const QString format = r.kind.startsWith("test-") ? "lavfi" : r.kind;
    if (!require(protocols.contains("av") &&
                     (formats.contains(format) ||
                      (format == "v4l2" && formats.contains("video4linux2"))),
                 uiTr("Движок не поддерживает выбранный способ захвата")))
      return plan;
    if (r.kind == "v4l2" &&
        !require(deviceNode(r.path, false) && QFileInfo(r.path).isReadable(),
                 uiTr("Видеоустройство недоступно")))
      return plan;
    const QString input =
        r.kind == "test-video"   ? "testsrc2=size=640x360:rate=25"
        : r.kind == "test-audio" ? "sine=frequency=440:sample_rate=48000"
                                 : r.path;
    plan.url = "av://" + format + ":" + input;
    plan.options["cache"] = "no";
    plan.live = true;
    if (!r.audio.isEmpty()) {
      const QString audioFormat = r.audio.section(':', 0, 0);
      if (!require(formats.contains(audioFormat),
                   uiTr("Движок не поддерживает выбранный аудиовход"))) {
        plan.url.clear();
        return plan;
      }
      plan.options["audio-files-append"] = "av://" + r.audio;
    }
  }
  plan.options["ytdl"] = "no";
  plan.options["audio-file-auto"] = "no";
  plan.options["sub-auto"] = "no";
  return plan;
}
QStringList DeviceSource::opticalDrives() {
  QStringList result;
  for (const auto &entry :
       QDir("/sys/class/block").entryList({"sr*"}, QDir::Dirs)) {
    const QString path = "/dev/" + entry;
    if (deviceNode(path, true))
      result.append(path);
  }
  return result;
}
QList<QPair<QString, QString>> DeviceSource::videoDevices() {
  QList<QPair<QString, QString>> result;
  for (const auto &entry :
       QDir("/sys/class/video4linux").entryList({"video*"}, QDir::Dirs)) {
    const QString path = "/dev/" + entry;
    QFile name("/sys/class/video4linux/" + entry + "/name");
    if (deviceNode(path, false))
      result.append({path, name.open(QIODevice::ReadOnly)
                               ? QString::fromUtf8(name.readAll()).trimmed() +
                                     " — " + path
                               : path});
  }
  return result;
}
QStringList DeviceSource::channels(const QByteArray &data, QString *error) {
  QStringList result;
  QStringDecoder decoder(QStringDecoder::Utf8);
  QString text = decoder(data);
  if (text.startsWith(QChar(0xfeff)))
    text.remove(0, 1);
  if (data.size() > 1024 * 1024 || decoder.hasError() ||
      text.contains(QChar(0))) {
    if (error)
      *error = uiTr("Список каналов должен быть UTF-8 и не больше 1 МБ");
    return result;
  }
  for (const auto &line : text.split('\n')) {
    if (line.trimmed().isEmpty() || line.trimmed().startsWith('#') ||
        !line.contains(':'))
      continue;
    const QString name =
        line.section(':', 0, 0).section(';', 0, 0).section(',', 0, 0).trimmed();
    if (!name.isEmpty() && !result.contains(name))
      result.append(name);
    if (result.size() > 10000) {
      if (error)
        *error = uiTr("В списке более 10000 каналов");
      return {};
    }
  }
  return result;
}
