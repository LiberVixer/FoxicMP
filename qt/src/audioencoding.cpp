#include "audioencoding.h"
#include "audioprocessing.h"
#include "i18n.h"
#include "player.h"
#include <QRegularExpression>
#include <QSettings>
#include <cmath>

QVariantMap AudioEncoding::defaults() {
  return {
      {"pcmFormat", "auto"},     {"sampleRate", 0},
      {"decoderThreads", 1},     {"ac3Drc", 0.},
      {"decoderDownmix", false}, {"preferredDecoder", ""},
      {"passthrough", false},    {"spdifCodecs", QStringList{"ac3", "dts"}}};
}
QVariantMap AudioEncoding::values(QSettings &s) {
  auto v = defaults();
  const auto format = s.value("audio/pcmFormat", "auto").toString();
  if (QStringList{"auto", "s16", "s24", "s32", "float"}.contains(format))
    v["pcmFormat"] = format;
  const int rate = s.value("audio/sampleRate", 0).toInt();
  if (QList<int>{0, 32000, 44100, 48000, 88200, 96000, 176400, 192000}.contains(
          rate))
    v["sampleRate"] = rate;
  bool ok;
  const int threads = s.value("audio/decoderThreads", 1).toInt(&ok);
  if (ok)
    v["decoderThreads"] = std::clamp(threads, 0, 16);
  const double drc = s.value("audio/ac3Drc", 0.).toDouble(&ok);
  if (ok && std::isfinite(drc))
    v["ac3Drc"] = std::clamp(drc, 0., 1.);
  v["decoderDownmix"] = s.value("audio/decoderDownmix", false).toBool();
  v["passthrough"] = s.value("audio/passthrough", false).toBool();
  const auto decoder = s.value("audio/preferredDecoder").toString();
  if (decoder.isEmpty() ||
      QRegularExpression("^[a-zA-Z0-9_]{1,64}$").match(decoder).hasMatch())
    v["preferredDecoder"] = decoder;
  QStringList codecs;
  const auto saved =
      s.value("audio/spdifCodecs", v["spdifCodecs"]).toStringList();
  for (const auto &codec :
       QStringList{"ac3", "dts", "dts-hd", "eac3", "truehd"})
    if (saved.contains(codec))
      codecs << codec;
  v["spdifCodecs"] = codecs;
  return v;
}
QString AudioEncoding::quantizer(const QVariantMap &v) {
  if (v.value("pcmFormat") != "s24")
    return {};
  QString chain;
  if (v.value("sampleRate").toInt() > 0)
    chain = "aresample=" + v.value("sampleRate").toString() + ",";
  return chain + "aeval=exprs=trunc(val(ch)*8388608)/8388608:c=same";
}
bool AudioEncoding::applyDecoder(Player *p, QSettings &s,
                                 const QVariantMap &processing,
                                 bool *passthrough, bool *needsRestart) {
  const auto v = values(s);
  const bool mixer = processing.value("mixer").toBool();
  const bool dsp = !AudioProcessing::graph(processing).isEmpty() ||
                   processing.value("replaygain") != "no" ||
                   processing.value("channels") != "auto-safe" &&
                       processing.value("channels") != "auto";
  const bool blocked = dsp || s.value("audio/dualOutput", false).toBool() ||
                       std::abs(p->get("speed").toDouble() - 1.) > .0001 ||
                       v.value("decoderDownmix").toBool() ||
                       v.value("ac3Drc").toDouble() > 0;
  *passthrough = v.value("passthrough").toBool() && !blocked &&
                 !v.value("spdifCodecs").toStringList().isEmpty();
  bool ok = true, restart = false;
  const auto set = [&](const char *name, const QVariant &value) {
    if (p->get(name) != value) {
      const bool changed = p->set(name, value);
      ok = changed && ok;
      restart = changed || restart;
    }
  };
  auto preferred = v.value("preferredDecoder").toString();
  if (!preferred.isEmpty()) {
    bool found = false;
    for (const auto &entry : p->get("decoder-list").toList())
      found = found || entry.toMap().value("driver").toString() == preferred;
    if (!found)
      preferred.clear();
  }
  set("ad", preferred);
  set("ad-lavc-threads", v.value("decoderThreads"));
  set("ad-lavc-ac3drc", v.value("ac3Drc"));
  set("ad-lavc-downmix", v.value("decoderDownmix").toBool() && !mixer);
  set("audio-spdif", *passthrough
                         ? v.value("spdifCodecs").toStringList().join(',')
                         : QString());
  // The caller restarts only after the entire filter/output configuration is
  // installed, including when an earlier reconfiguration is still in flight.
  *needsRestart = restart;
  p->setProperty("foxicmpPassthroughRequested", *passthrough);
  QString status =
      !v.value("passthrough").toBool()
          ? uiTr("Bitstream выключен; используется PCM.")
      : *passthrough
          ? uiTr("Bitstream разрешён для: %1. Фактический формат показан ниже.")
                .arg(v.value("spdifCodecs").toStringList().join(", "))
          : uiTr("Bitstream временно отключён: обработка, раскладка, два "
                 "выхода, скорость, DRC или пустой список кодеков. "
                 "Используется PCM.");
  if (mixer && v.value("decoderDownmix").toBool())
    status +=
        "\n" +
        uiTr("Микшер получает исходные каналы; сведение декодером отключено.");
  if (!v.value("preferredDecoder").toString().isEmpty() && preferred.isEmpty())
    status +=
        "\n" + uiTr("Сохранённый декодер недоступен; используется автовыбор.");
  p->setProperty("foxicmpAudioEncodingStatus", status);
  return ok;
}
