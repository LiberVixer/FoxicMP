#include "audioprocessing.h"
#include "audioencoding.h"
#include "i18n.h"
#include "player.h"
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <cmath>
#include <limits>

QVariantMap AudioProcessing::defaults() {
  return {{"gainDb", 0.},
          {"balance", 0.},
          {"normalize", false},
          {"normalizeFrameMs", 250.},
          {"normalizeWindow", 31.},
          {"normalizePeak", .9},
          {"normalizeMaxGain", 10.},
          {"normalizeRms", 0.},
          {"normalizeCouple", true},
          {"normalizeDc", false},
          {"limiter", false},
          {"limiterPeak", .95},
          {"mixer", false},
          {"mixLayout", "stereo"},
          {"centerLevel", .707107},
          {"surroundLevel", .707107},
          {"lfeLevel", 0.},
          {"mixNormalize", true},
          {"channelMode", "normal"},
          {"channels", "auto-safe"},
          {"replaygain", "no"},
          {"replaygainPreamp", 0.},
          {"replaygainFallback", 0.},
          {"replaygainClip", true},
          {"customEnabled", false},
          {"customGraph", ""}};
}
QVariantMap AudioProcessing::values(QSettings &s) {
  auto v = defaults();
  for (auto it = v.begin(); it != v.end(); ++it)
    if (it.value().typeId() == QMetaType::Bool)
      it.value() = s.value("audio/" + it.key(), it.value()).toBool();
  for (const auto &entry :
       QList<QList<QVariant>>{{"gainDb", -20., 20.},
                              {"balance", -100., 100.},
                              {"normalizeFrameMs", 10., 1000.},
                              {"normalizeWindow", 3., 101.},
                              {"normalizePeak", .1, 1.},
                              {"normalizeMaxGain", 1., 100.},
                              {"normalizeRms", 0., 1.},
                              {"limiterPeak", .1, 1.},
                              {"centerLevel", 0., 2.},
                              {"surroundLevel", 0., 2.},
                              {"lfeLevel", 0., 2.},
                              {"replaygainPreamp", -15., 15.},
                              {"replaygainFallback", -15., 15.}}) {
    const auto key = entry[0].toString();
    bool ok;
    const auto n = s.value("audio/" + key, v[key]).toDouble(&ok);
    if (ok && std::isfinite(n))
      v[key] = std::clamp(n, entry[1].toDouble(), entry[2].toDouble());
  }
  v["normalizeFrameMs"] = std::round(v["normalizeFrameMs"].toDouble());
  v["normalizeWindow"] =
      std::clamp(qRound(v["normalizeWindow"].toDouble()) | 1, 3, 101);
  const QStringList layouts{"mono", "stereo", "quad",      "4.0",
                            "5.0",  "5.1",    "5.1(side)", "7.1"};
  const auto choice = [&](const QString &key, const QStringList &choices) {
    const auto saved = s.value("audio/" + key, v[key]).toString();
    if (choices.contains(saved))
      v[key] = saved;
  };
  choice("channels", QStringList{"auto-safe", "auto"} + layouts);
  choice("mixLayout", layouts);
  choice("channelMode", {"normal", "swap", "left", "right", "mono"});
  choice("replaygain", {"no", "track", "album"});
  v["customGraph"] = s.value("audio/customGraph").toString().trimmed();
  return v;
}
QString AudioProcessing::outputChannels(const QVariantMap &v) {
  if (v.value("balance").toDouble() != 0 || v.value("channelMode") != "normal")
    return "stereo";
  return v.value(v.value("mixer").toBool() ? "mixLayout" : "channels")
      .toString();
}
QString AudioProcessing::graph(const QVariantMap &v) {
  const auto num = [](double n) { return QString::number(n, 'f', 6); };
  QStringList chain;
  if (v.value("mixer").toBool())
    chain << QString("aresample=out_chlayout=%1:center_mix_level=%2:surround_"
                     "mix_level=%3:lfe_mix_level=%4:rematrix_maxval=%5")
                 .arg(v.value("mixLayout").toString(),
                      num(v.value("centerLevel").toDouble()),
                      num(v.value("surroundLevel").toDouble()),
                      num(v.value("lfeLevel").toDouble()),
                      v.value("mixNormalize").toBool() ? "1" : "1000");
  const auto mode = v.value("channelMode").toString();
  const auto balance = v.value("balance").toDouble() / 100.;
  if (mode != "normal" || balance != 0) {
    chain << "aformat=channel_layouts=stereo";
    QString left = "FL", right = "FR";
    if (mode == "swap") {
      left = "FR";
      right = "FL";
    }
    if (mode == "left")
      left = right = "FL";
    if (mode == "right")
      left = right = "FR";
    if (mode == "mono")
      left = right = "0.5*FL+0.5*FR";
    chain << "pan=stereo|FL=" + left + "|FR=" + right;
    if (balance != 0)
      chain << QString("pan=stereo|FL=%1*FL|FR=%2*FR")
                   .arg(num(balance > 0 ? 1 - balance : 1),
                        num(balance < 0 ? 1 + balance : 1));
  }
  if (v.value("gainDb").toDouble() != 0)
    chain << "volume=" + num(v.value("gainDb").toDouble()) + "dB";
  if (v.value("normalize").toBool())
    chain << QString("dynaudnorm=f=%1:g=%2:p=%3:m=%4:r=%5:n=%6:c=%7")
                 .arg(v.value("normalizeFrameMs").toInt())
                 .arg(v.value("normalizeWindow").toInt())
                 .arg(num(v.value("normalizePeak").toDouble()),
                      num(v.value("normalizeMaxGain").toDouble()),
                      num(v.value("normalizeRms").toDouble()),
                      v.value("normalizeCouple").toBool() ? "1" : "0",
                      v.value("normalizeDc").toBool() ? "1" : "0");
  if (v.value("customEnabled").toBool() &&
      !v.value("customGraph").toString().isEmpty())
    chain << v.value("customGraph").toString();
  if (v.value("limiter").toBool())
    chain << "alimiter=limit=" + num(v.value("limiterPeak").toDouble()) +
                 ":level=0:latency=1";
  return chain.join(',');
}
QString AudioProcessing::validateGraph(const QString &graph) {
  if (graph.size() > 8192 || graph.contains(QChar(0)))
    return uiTr("Цепочка должна содержать до 8192 символов без NUL.");
  if (graph.trimmed().isEmpty())
    return {};
  const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
  if (ffmpeg.isEmpty())
    return uiTr("Для проверки цепочки нужен FFmpeg в PATH.");
  QProcess process;
  process.start(ffmpeg, {"-nostdin", "-v", "error", "-f", "lavfi", "-i",
                         "anullsrc=r=48000:cl=stereo:d=0.1", "-af", graph, "-t",
                         "0.1", "-frames:a", "5", "-f", "null", "-"});
  if (!process.waitForFinished(3000)) {
    process.kill();
    process.waitForFinished(500);
    return uiTr("Проверка фильтров не завершилась за 3 секунды.");
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    return uiTr("FFmpeg отклонил цепочку: %1")
        .arg(QString::fromUtf8(process.readAllStandardError()).left(1500));
  return {};
}
bool AudioProcessing::apply(Player *p, QSettings &s) {
  const auto oldPosition = p->get("time-pos");
  auto v = values(s);
  bool ok = true;
  const auto custom = v.value("customGraph").toString();
  if (v.value("customEnabled").toBool() && !custom.isEmpty()) {
    if (p->QObject::property("foxicmpCheckedCustomGraph").toString() !=
            custom ||
        !p->QObject::property("foxicmpCheckedCustomGraph").isValid()) {
      p->setProperty("foxicmpCheckedCustomGraph", custom);
      p->setProperty("foxicmpCustomGraphError", validateGraph(custom));
    }
    const auto error =
        p->QObject::property("foxicmpCustomGraphError").toString();
    if (!error.isEmpty()) {
      v["customEnabled"] = false;
      ok = false;
    }
  } else {
    p->setProperty("foxicmpCheckedCustomGraph", QVariant());
    p->setProperty("foxicmpCustomGraphError", QString());
  }
  const auto set = [&](const char *name, const QVariant &value) {
    if (p->get(name) != value)
      ok = p->set(name, value) && ok;
  };
  bool passthrough = false, restart = false;
  ok = AudioEncoding::applyDecoder(p, s, v, &passthrough, &restart) && ok;
  set("audio-channels", outputChannels(v));
  set("replaygain", v.value("replaygain"));
  set("replaygain-preamp", v.value("replaygainPreamp"));
  set("replaygain-fallback", v.value("replaygainFallback"));
  set("replaygain-clip", v.value("replaygainClip"));
  auto next = graph(v);
  const auto encoding = AudioEncoding::values(s);
  QString format =
      passthrough ? QString() : encoding.value("pcmFormat").toString();
  if (format == "auto")
    format.clear();
  if (format == "s24") {
    const auto quantizer = AudioEncoding::quantizer(encoding);
    next += (next.isEmpty() ? QString() : ",") + quantizer;
    format = "s32";
  }
  const int rate = passthrough ? 0 : encoding.value("sampleRate").toInt();
  const auto previous = p->QObject::property("foxicmpAudioGraph");
  const auto previousFormat =
      p->QObject::property("foxicmpAudioFormat").toString();
  const int previousRate = p->QObject::property("foxicmpAudioRate").toInt();
  if (!previous.isValid() || previous.toString() != next ||
      previousFormat != format || previousRate != rate) {
    if (p->setAudioGraph(next, format, rate)) {
      p->setProperty("foxicmpAudioGraph", next);
      p->setProperty("foxicmpAudioFormat", format);
      p->setProperty("foxicmpAudioRate", rate);
    } else {
      p->setAudioGraph(previous.toString(), previousFormat, previousRate);
      p->setProperty(
          "foxicmpCustomGraphError",
          uiTr("libmpv отклонил обработку; восстановлена предыдущая цепочка."));
      ok = false;
    }
  }
  if (restart) {
    const double position = oldPosition.isValid()
                                ? oldPosition.toDouble()
                                : std::numeric_limits<double>::quiet_NaN();
    ok = p->restartAudioDecoder(position) && ok;
  }
  return ok;
}
