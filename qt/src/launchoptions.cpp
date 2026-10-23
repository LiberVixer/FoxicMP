#include "launchoptions.h"
#include "i18n.h"
#include "mediasource.h"
#include <QFileInfo>
#include <QMimeData>
#include <QJsonArray>
#include <QSet>
#include <QUrl>
#include <QRegularExpression>
#include <cmath>

namespace LaunchOptions {
QStringList externalFiles(const QJsonObject &options, const QString &key) {
  const auto value = options.value(key);
  if (value.isString()) return {value.toString()};
  QStringList files;
  for (const auto &entry : value.toArray()) files << entry.toString();
  return files;
}
bool clipboardFiles(const QMimeData *mime, QStringList *out, QString *error) {
  const auto fail = [&] {
    if (error) *error = uiTr("Буфер обмена не содержит допустимого списка существующих файлов или URL (не более 256 записей).");
    return false;
  };
  if (!mime || !out) return fail();
  QStringList input, normalized;
  if (mime->hasUrls()) {
    const auto urls = mime->urls();
    if (urls.size() > 256) return fail();
    for (const auto &url : urls)
      input << (url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded));
  } else {
    const auto text = mime->text();
    if (text.size() > 256 * 1024) return fail();
    input = text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
  }
  if (input.isEmpty() || input.size() > 256) return fail();
  for (const auto &item : input) {
    if (item.contains(QChar(0))) return fail();
    const auto path = MediaSource::normalize(item.trimmed());
    if (path.isEmpty() || path.contains(QChar(0)) || path.toUtf8().size() > 8192 ||
        (!MediaSource::isUrl(path) && !QFileInfo::exists(path))) return fail();
    normalized << path;
  }
  *out = normalized;
  return true;
}
void addArguments(QCommandLineParser &p) {
  p.addOption({"stdin", uiTr("Открыть одноразовый медиапоток из stdin в отдельном окне; также можно указать -")});
  p.addOption({"clipboard", uiTr("Открыть файлы или URL из буфера обмена вызывающего процесса")});
  p.addOption({"web-port", uiTr("Включить локальное веб-управление на порту 1024–65535 только для текущего запуска"), "port"});
  p.addOption({"reset-settings", uiTr("Сбросить настройки закрытого профиля с резервной копией и завершить работу")});
  p.addOption({"reset-library", uiTr("При --reset-settings также очистить историю, избранное, позиции и плейлисты")});
  p.addOption({"start", uiTr("Начальная позиция: секунды или ЧЧ:ММ:СС"), "time"});
  p.addOption({"pause", uiTr("Открыть файл на паузе")});
  p.addOption({"volume", uiTr("Громкость от 0 до 100"), "number"});
  p.addOption({"rate", uiTr("Скорость от 0.125 до 16"), "number"});
  p.addOption({"audio-track", uiTr("ID аудиодорожки; 0 — без звука"), "id"});
  p.addOption({"subtitle-track", uiTr("ID субтитров; 0 — отключить"), "id"});
  p.addOption({"audio-file", uiTr("Подключить локальную аудиодорожку; ключ повторяется, выбирается последняя"), "file"});
  p.addOption({"subtitle-file", uiTr("Подключить локальные субтитры; ключ повторяется, выбираются последние"), "file"});
  p.addOption({"audio-delay", uiTr("Задержка внешнего звука в секундах от −30 до 30; требуется --audio-file"), "seconds"});
  p.addOption({"fullscreen", uiTr("Полноэкранное окно")});
  p.addOption({"minimized", uiTr("Свёрнутое окно")});
  p.addOption({"normal", uiTr("Обычное окно")});
  p.addOption({"window-size", uiTr("Размер обычного окна: ШИРИНАxВЫСОТА"), "size"});
  p.addOption({"window-position", uiTr("Положение окна: X,Y (включая отрицательные координаты)"), "position"});
  p.addOption({"command", uiTr("Команда работающему окну: play, pause, toggle, stop, next, previous, mute"), "name"});
  p.addOption({"monitor", uiTr("Монитор: имя, номер с 1 или primary"), "screen"});
  p.addOption({"audio-device", uiTr("Аудиоустройство libmpv или auto"), "device"});
  p.addOption({"panscan", uiTr("Пресет: fit, fill, wide, crop169, crop235 или saved:Имя"), "preset"});
  p.addOption({"shuffle", uiTr("Включить случайный порядок списка")});
  p.addOption({"no-shuffle", uiTr("Выключить случайный порядок списка")});
  p.addOption({"after", uiTr("Однократное действие после списка: none, exit, close, minimize, nextDirectory, loopDirectory, suspend, hibernate, shutdown, logout, lock"), "action"});
  p.addOption({"list-monitors", uiTr("Вывести мониторы в JSON без открытия профиля")});
  p.addOption({"list-audio-devices", uiTr("Вывести аудиоустройства в JSON без открытия профиля")});
}
bool hasPlayback(const QJsonObject &o) {
  for (const auto &key : {"start", "pause", "volume", "rate", "audio-track", "subtitle-track", "audio-file", "subtitle-file", "audio-device", "panscan", "shuffle", "after", "audio-delay"})
    if (o.contains(key)) return true;
  return false;
}
bool validate(const QJsonObject &o) {
  const QStringList allowed{"start", "pause", "volume", "rate", "audio-track", "subtitle-track", "audio-file", "subtitle-file", "mode", "width", "height", "x", "y", "command", "monitor", "audio-device", "panscan", "shuffle", "after", "web-port", "audio-delay"};
  const auto number = [&](const QString &key, double low, double high, bool integer = false) {
    const auto v = o.value(key);
    const double n = v.toDouble();
    return v.isDouble() && std::isfinite(n) && n >= low && n <= high && (!integer || std::floor(n) == n);
  };
  for (auto it = o.begin(); it != o.end(); ++it) {
    const auto key = it.key();
    if (!allowed.contains(key)) return false;
    if (key == "web-port" && !number(key, 1024, 65535, true)) return false;
    if (key == "start" && !number(key, 0, 604800)) return false;
    if (key == "volume" && !number(key, 0, 100)) return false;
    if (key == "rate" && !number(key, .125, 16)) return false;
    if (key == "audio-delay" && !number(key, -30, 30)) return false;
    if ((key == "audio-track" || key == "subtitle-track") && !number(key, 0, 999999, true)) return false;
    if (key == "pause" && it.value() != QJsonValue(true)) return false;
    if (key == "shuffle" && !it.value().isBool()) return false;
    if (key == "monitor" || key == "audio-device" || key == "panscan") {
      const auto text = it.value().toString();
      if (!it.value().isString() || text.isEmpty() || text.toUtf8().size() > 1024) return false;
      for (auto c : text) if (c.category() == QChar::Other_Control) return false;
      if (key == "audio-device" && text.startsWith("pulse/foxicmp_dual_")) return false;
      if (key == "panscan" && !QStringList{"fit", "fill", "wide", "crop169", "crop235"}.contains(text) &&
          (!text.startsWith("saved:") || text.mid(6).trimmed().isEmpty() || text.mid(6).size() > 80)) return false;
    }
    if (key == "after" && !QStringList{"none", "exit", "close", "minimize", "nextDirectory", "loopDirectory", "suspend", "hibernate", "shutdown", "logout", "lock"}.contains(it.value().toString())) return false;
    if (key.endsWith("-file")) {
      if (!it.value().isString() && !it.value().isArray()) return false;
      if (it.value().isArray())
        for (const auto &entry : it.value().toArray()) if (!entry.isString()) return false;
      const auto files = externalFiles(o, key);
      if (files.isEmpty() || files.size() > 32) return false;
      QSet<QString> seen;
      for (const auto &path : files) {
        const QFileInfo file(path);
        if (path.isEmpty() || path.contains(QChar(0)) || path.toUtf8().size() > 8192 ||
            !file.isAbsolute() || !file.isFile() || !file.isReadable() || seen.contains(path)) return false;
        seen.insert(path);
      }
    }
    if (key == "mode" && !QStringList{"fullscreen", "minimized", "normal"}.contains(it.value().toString())) return false;
    if (key == "width" && !number(key, 320, 16384, true)) return false;
    if (key == "height" && !number(key, 200, 16384, true)) return false;
    if ((key == "x" || key == "y") && !number(key, -32768, 32767, true)) return false;
    if (key == "command" && !QStringList{"play", "pause", "toggle", "stop", "next", "previous", "mute"}.contains(it.value().toString())) return false;
  }
  return o.contains("width") == o.contains("height") && o.contains("x") == o.contains("y") &&
      !(o.contains("audio-track") && o.contains("audio-file")) &&
      !(o.contains("subtitle-track") && o.contains("subtitle-file")) &&
      !(o.contains("audio-delay") && !o.contains("audio-file")) &&
      !(o.contains("command") && o.size() != 1);
}
bool parse(const QCommandLineParser &p, QJsonObject *out, QString *error) {
  QJsonObject o;
  const auto fail = [&] {
    if (error) *error = uiTr("Неверные параметры запуска: проверьте диапазоны, локальные файлы и несовместимые ключи (--help).");
    return false;
  };
  for (const auto &key : {"start", "volume", "rate", "audio-track", "subtitle-track", "web-port", "audio-delay"}) {
    if (!p.isSet(key)) continue;
    if (p.values(key).size() != 1) return fail();
    auto text = p.value(key);
    bool ok = false;
    double n = 0;
    if (QString(key) == "start" && text.contains(':')) {
      const auto m = QRegularExpression("^(?:(\\d{1,3}):)?([0-5]?\\d):([0-5]\\d(?:\\.\\d{1,6})?)$").match(text);
      if (!m.hasMatch()) return fail();
      n = m.captured(1).toInt() * 3600 + m.captured(2).toInt() * 60 + m.captured(3).toDouble();
      ok = true;
    } else n = text.toDouble(&ok);
    if (!ok) return fail();
    o[key] = n;
  }
  if (p.isSet("pause")) o["pause"] = true;
  if (p.isSet("shuffle") && p.isSet("no-shuffle")) return fail();
  if (p.isSet("shuffle") || p.isSet("no-shuffle")) o["shuffle"] = p.isSet("shuffle");
  for (const auto &key : {"monitor", "audio-device", "panscan", "after"}) {
    if (!p.isSet(key)) continue;
    if (p.values(key).size() != 1) return fail();
    o[key] = p.value(key);
  }
  for (const auto &key : {"audio-file", "subtitle-file"}) {
    if (!p.isSet(key)) continue;
    const auto values = p.values(key);
    if (values.size() > 32) return fail();
    QStringList paths;
    for (const auto &value : values) {
      if (value.isEmpty()) return fail();
      const auto path = QFileInfo(value).absoluteFilePath();
      paths.removeAll(path);
      paths.append(path); // Preserve the selection from the last occurrence.
    }
    o[key] = QJsonArray::fromStringList(paths);
  }
  int modes = 0;
  for (const auto &key : {"fullscreen", "minimized", "normal"})
    if (p.isSet(key)) { ++modes; o["mode"] = key; }
  if (modes > 1) return fail();
  for (const auto &key : {"window-size", "window-position"}) {
    if (!p.isSet(key)) continue;
    if (p.values(key).size() != 1) return fail();
    const bool size = QString(key) == "window-size";
    const auto match = QRegularExpression(size ? "^(\\d{1,5})[xX](\\d{1,5})$" : "^(-?\\d{1,5}),(-?\\d{1,5})$").match(p.value(key));
    if (!match.hasMatch()) return fail();
    o[size ? "width" : "x"] = match.captured(1).toInt();
    o[size ? "height" : "y"] = match.captured(2).toInt();
  }
  if (p.isSet("command")) {
    if (p.values("command").size() != 1) return fail();
    o["command"] = p.value("command");
  }
  if (!validate(o)) return fail();
  *out = o;
  return true;
}
}
