#include "videotransform.h"
#include "player.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <algorithm>
#include <cmath>
namespace {
double number(const QVariantMap &map, const char *key, double fallback,
              double low, double high) {
  bool ok = false;
  const double value = map.value(key, fallback).toDouble(&ok);
  return ok && std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
QString choice(const QVariantMap &map, const char *key, const QString &fallback,
               const QStringList &values) {
  const auto value = map.value(key, fallback).toString();
  return values.contains(value) ? value : fallback;
}
const QStringList inputs{"auto",  "mono", "sbsl", "sbsr", "sbs2l",
                         "sbs2r", "abl",  "abr",  "ab2l", "ab2r",
                         "irl",   "irr",  "icl",  "icr"};
QString decimal(double value) { return QString::number(value, 'f', 12); }
} // namespace
bool VideoTransform::preset(QSettings &settings, const QString &name, VideoTransform *out) {
  if (!out) return false;
  VideoTransform state;
  if (name == "fit") {}
  else if (name == "fill") state.fill = 1;
  else if (name == "wide") state.width = 4. / 3;
  else if (name == "crop169") state.cropAspect = "16:9";
  else if (name == "crop235") state.cropAspect = "2.35:1";
  else {
    if (!name.startsWith("saved:")) return false;
    const auto bytes = settings.value("transform/presetsV1").toByteArray();
    if (bytes.size() > 256 * 1024) return false;
    int count = 0;
    bool found = false;
    for (const auto &v : QJsonDocument::fromJson(bytes).array()) {
      const auto entry = v.toObject();
      const auto label = entry.value("name").toString().trimmed().left(80);
      if (label.isEmpty() || !entry.value("state").isObject()) continue;
      if (label == name.mid(6)) {
        state = fromMap(entry.value("state").toObject().toVariantMap());
        found = true; break;
      }
      if (++count >= 32) break;
    }
    if (!found) return false;
  }
  *out = state;
  return true;
}
VideoTransform VideoTransform::fromMap(const QVariantMap &map) {
  VideoTransform state;
  state.width = number(map, "width", 1, .2, 5);
  state.height = number(map, "height", 1, .2, 5);
  state.x = number(map, "x", 0, -3, 3);
  state.y = number(map, "y", 0, -3, 3);
  state.zoom = number(map, "zoom", 0, -3, 3);
  state.fill = number(map, "fill", 0, 0, 1);
  state.left = number(map, "left", 0, 0, 45);
  state.right = number(map, "right", 0, 0, 45);
  state.top = number(map, "top", 0, 0, 45);
  state.bottom = number(map, "bottom", 0, 0, 45);
  const int rotation = map.value("rotation", 0).toInt();
  state.rotation =
      rotation == 90 || rotation == 180 || rotation == 270 ? rotation : 0;
  state.mirror = map.value("mirror", false).toBool();
  state.flip = map.value("flip", false).toBool();
  state.swapEyes = map.value("swapEyes", false).toBool();
  state.cropAspect =
      choice(map, "cropAspect", "no",
             {"no", "4:3", "16:9", "1.85:1", "2.35:1", "2.39:1"});
  state.stereoInput = choice(map, "stereoInput", "auto", inputs);
  state.stereoOutput = choice(map, "stereoOutput", "pass",
                              {"pass", "ml", "mr", "irl", "irl2", "abl", "ab2l",
                               "sbsl", "sbs2l", "arcd", "arcc", "icl", "chl"});
  return state;
}
QVariantMap VideoTransform::toMap() const {
  return {{"width", width},
          {"height", height},
          {"x", x},
          {"y", y},
          {"zoom", zoom},
          {"fill", fill},
          {"left", left},
          {"right", right},
          {"top", top},
          {"bottom", bottom},
          {"rotation", rotation},
          {"mirror", mirror},
          {"flip", flip},
          {"swapEyes", swapEyes},
          {"cropAspect", cropAspect},
          {"stereoInput", stereoInput},
          {"stereoOutput", stereoOutput}};
}
VideoTransform VideoTransform::load(QSettings &settings) {
  if (!settings.value("transform/remember", false).toBool()) {
    QVariantMap legacy{{"rotation", settings.value("video/rotation", 0)}};
    return fromMap(legacy);
  }
  return fromMap(
      QJsonDocument::fromJson(settings.value("transform/stateV1").toByteArray())
          .object()
          .toVariantMap());
}
void VideoTransform::save(QSettings &settings) const {
  if (settings.value("transform/remember", false).toBool())
    settings.setValue("transform/stateV1",
                      QJsonDocument(QJsonObject::fromVariantMap(toMap()))
                          .toJson(QJsonDocument::Compact));
}
QString VideoTransform::resolvedStereo(const QString &detectedStereo) const {
  if (stereoInput != "auto")
    return stereoInput == "mono" ? QString() : stereoInput;
  QString detected = detectedStereo;
  if (detected == "tbl")
    detected = "abl";
  if (detected == "tbr")
    detected = "abr";
  if (detected == "tb2l")
    detected = "ab2l";
  if (detected == "tb2r")
    detected = "ab2r";
  return inputs.contains(detected) && detected != "auto" && detected != "mono"
             ? detected
             : QString();
}
QString VideoTransform::sourceFilters(const QString &detectedStereo) const {
  QStringList filters;
  auto input = resolvedStereo(detectedStereo);
  if (!input.isEmpty() && (stereoOutput != "pass" || swapEyes)) {
    const QString original = input;
    if (swapEyes) {
      if (input.endsWith('l'))
        input[input.size() - 1] = 'r';
      else if (input.endsWith('r'))
        input[input.size() - 1] = 'l';
    }
    // Packed formats need whole eye pairs; pad at most three edge pixels.
    filters << "pad=ceil(iw/4)*4:ceil(ih/4)*4";
    if (stereoOutput == "irl2")
      filters << QString("stereo3d=%1:abl,scale=iw*2:ih*2:flags=neighbor,"
                         "stereo3d=abl:irl")
                     .arg(input);
    else
      filters << QString("stereo3d=%1:%2")
                     .arg(input,
                          stereoOutput == "pass" ? original : stereoOutput);
  }
  if (left || right || top || bottom)
    filters << QString("crop=w='max(2,trunc(iw*%1/2)*2)':h='max(2,trunc(ih*%2/"
                       "2)*2)':x='trunc(iw*%3/2)*2':y='trunc(ih*%4/2)*2'")
                   .arg(decimal(1 - (left + right) / 100),
                        decimal(1 - (top + bottom) / 100), decimal(left / 100),
                        decimal(top / 100));
  if (cropAspect != "no") {
    const auto parts = cropAspect.split(':');
    const double ratio = parts[0].toDouble() / parts[1].toDouble();
    // Account for sample aspect ratio; crop the source, do not stretch it.
    filters << QString("crop=w='max(2,trunc(min(iw,ih*%1/sar)/"
                       "2+0.000001)*2)':h='max(2,trunc(min(ih,iw*sar/%1)/"
                       "2+0.000001)*2)'")
                   .arg(decimal(ratio));
  }
  if (mirror)
    filters << "hflip";
  if (flip)
    filters << "vflip";
  if (rotation == 90)
    filters << "transpose=clock";
  else if (rotation == 180)
    filters << "hflip" << "vflip";
  else if (rotation == 270)
    filters << "transpose=cclock";
  return filters.join(',');
}
bool VideoTransform::apply(Player *player, const QString &colorFilter,
                           const QString &detectedStereo) const {
  QStringList filters;
  const auto source = sourceFilters(detectedStereo);
  if (!source.isEmpty())
    filters << source;
  if (!colorFilter.isEmpty())
    filters << colorFilter;
  if (!player->set("vf", filters.isEmpty()
                             ? QString()
                             : "lavfi=[" + filters.join(',') + "]")) {
    return false;
  }
  bool ok = true;
  for (const auto &entry :
       QList<QPair<const char *, QVariant>>{{"video-rotate", 0},
                                            {"video-scale-x", width},
                                            {"video-scale-y", height},
                                            {"video-pan-x", x},
                                            {"video-pan-y", y},
                                            {"video-zoom", zoom},
                                            {"panscan", fill}})
    ok = player->set(entry.first, entry.second) && ok;
  return ok;
}
