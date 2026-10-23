#include "videooutput.h"
#include "i18n.h"
#include "player.h"
#include <QSettings>

QVariantMap VideoOutput::defaults() {
  return {{"softwareScaler", "default"},
          {"upscaler", "default"},
          {"downscaler", "default"},
          {"chromaScaler", "default"},
          {"softwarePrecision", "8"},
          {"outputLevels", "default"},
          {"fboFormat", "default"},
          {"ditherDepth", "default"},
          {"targetPrim", "default"},
          {"targetTrc", "default"},
          {"toneMapping", "default"},
          {"deinterlace", false},
          {"decoderThreads", 0},
          {"hardwareBackend", "auto-copy"},
          {"hardwareCodecsDefault", true},
          {"hardwareCodecs",
           QStringList{"h264", "hevc", "vp8", "vp9", "av1", "vc1"}},
          {"hardwareFallbackFrames", 3}};
}
QVariantMap VideoOutput::values(QSettings &s) {
  auto v = defaults();
  const auto choice = [&](const QString &key, const QStringList &choices) {
    const auto value = s.value("video/" + key, v.value(key)).toString();
    if (choices.contains(value))
      v[key] = value;
  };
  choice("softwareScaler", {"default", "point", "bilinear", "bicubic",
                            "lanczos", "spline", "area"});
  for (const auto &key : {"upscaler", "downscaler", "chromaScaler"})
    choice(key, {"default", "nearest", "bilinear", "bicubic", "lanczos",
                 "spline36", "ewa_lanczossharp"});
  choice("softwarePrecision", {"8", "16"});
  choice("outputLevels", {"default", "full", "limited"});
  choice("fboFormat",
         {"default", "rgba8", "rgb10_a2", "rgba16", "rgba16f", "rgba32f"});
  choice("ditherDepth", {"default", "no", "8", "10", "12", "16"});
  choice("targetPrim", {"default", "bt.709", "bt.2020", "dci-p3", "adobe"});
  choice("targetTrc", {"default", "srgb", "bt.1886", "gamma2.2", "gamma2.4"});
  choice("toneMapping",
         {"default", "clip", "mobius", "reinhard", "hable", "bt.2390"});
  choice("hardwareBackend",
         {"auto-copy", "vaapi-copy", "nvdec-copy", "vdpau-copy"});
  for (const auto &entry : QList<QPair<QString, QPair<int, int>>>{
           {"decoderThreads", {0, 16}}, {"hardwareFallbackFrames", {1, 16}}}) {
    bool ok;
    const int value =
        s.value("video/" + entry.first, v.value(entry.first)).toInt(&ok);
    if (ok)
      v[entry.first] =
          std::clamp(value, entry.second.first, entry.second.second);
  }
  v["deinterlace"] = s.value("video/deinterlace", false).toBool();
  v["hardwareCodecsDefault"] =
      s.value("video/hardwareCodecsDefault", true).toBool();
  QStringList codecs;
  const auto saved =
      s.value("video/hardwareCodecs", v["hardwareCodecs"]).toStringList();
  for (const auto &codec : QStringList{"h264", "hevc", "vp8", "vp9", "av1",
                                       "vc1", "mpeg2video", "prores"})
    if (saved.contains(codec))
      codecs << codec;
  v["hardwareCodecs"] = codecs;
  return v;
}
bool VideoOutput::apply(Player *p, QSettings &s) {
  const auto v = values(s);
  const QStringList captured{"sws-scaler",
                             "sws-allow-zimg",
                             "scale",
                             "dscale",
                             "cscale",
                             "hwdec-codecs",
                             "video-output-levels",
                             "fbo-format",
                             "dither-depth",
                             "target-prim",
                             "target-trc",
                             "tone-mapping"};
  for (const auto &name : captured) {
    const auto key = "foxicmpDefaultVideo_" + name;
    if (!p->QObject::property(key.toUtf8()).isValid())
      p->setProperty(key.toUtf8(), p->get(name.toUtf8()));
  }
  bool ok = true;
  const auto set = [&](const char *key, const QVariant &value) {
    auto current = p->get(key);
    if (current.typeId() == QMetaType::QVariantList ||
        current.typeId() == QMetaType::QStringList)
      current = current.toStringList().join(',');
    if (current != value)
      ok = p->set(key, value) && ok;
  };
  const auto kernel = [&](const char *key, const QString &value) {
    set(key, value == "default"
                 ? p->QObject::property(
                       (QByteArray("foxicmpDefaultVideo_") + key).constData())
                 : value);
  };
  const auto software = v.value("softwareScaler").toString();
  kernel("sws-scaler", software);
  set("sws-allow-zimg",
      software == "default"
          ? p->QObject::property("foxicmpDefaultVideo_sws-allow-zimg")
          : false);
  kernel("scale", v.value("upscaler").toString());
  kernel("dscale", v.value("downscaler").toString());
  kernel("cscale", v.value("chromaScaler").toString());
  p->setSoftwarePrecision(v.value("softwarePrecision").toInt());
  for (const auto &entry : QList<QPair<const char *, const char *>>{
           {"video-output-levels", "outputLevels"},
           {"fbo-format", "fboFormat"},
           {"dither-depth", "ditherDepth"},
           {"target-prim", "targetPrim"},
           {"target-trc", "targetTrc"},
           {"tone-mapping", "toneMapping"}})
    kernel(entry.first, v.value(entry.second).toString());
  set("deinterlace", v.value("deinterlace"));
  set("vd-lavc-threads", v.value("decoderThreads"));
  set("vd-lavc-software-fallback", v.value("hardwareFallbackFrames"));
  const auto native = p->QObject::property("foxicmpDefaultVideo_hwdec-codecs");
  const auto nativeCodecs = native.typeId() == QMetaType::QVariantList ||
                                    native.typeId() == QMetaType::QStringList
                                ? native.toStringList().join(',')
                                : native.toString();
  const auto codecs = v.value("hardwareCodecsDefault").toBool()
                          ? nativeCodecs
                          : v.value("hardwareCodecs").toStringList().join(',');
  set("hwdec-codecs", codecs);
  const bool hardware =
      s.value("playback/hardwareDecoding", true).toBool() && !codecs.isEmpty();
  set("hwdec", hardware ? v.value("hardwareBackend") : "no");
  p->setProperty("foxicmpVideoOutputStatus",
                 ok ? uiTr("Настройки приняты libmpv. Фактический вывод и "
                           "декодирование показаны ниже.")
                    : uiTr("Часть настроек отклонена libmpv; проверьте "
                           "фактические параметры."));
  return ok;
}
