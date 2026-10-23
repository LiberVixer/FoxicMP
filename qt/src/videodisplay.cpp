#include "videodisplay.h"
#include "i18n.h"
#include "player.h"
#include "systemicc.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#define CMS_NO_REGISTER_KEYWORD
#include <lcms2.h>

QVariantMap VideoDisplay::defaults() {
  return {{"iccEnabled", false},        {"iccProfile", ""},
          {"iccSource", "manual"},      {"iccIntent", 1},
          {"syncMode", "audio"},        {"vsync", "on"},
          {"displayFps", 0.},           {"maxVideoChange", 1.},
          {"maxAudioChange", .125},     {"interpolation", false},
          {"temporalScaler", "default"}};
}
QVariantMap VideoDisplay::values(QSettings &s) {
  auto v = defaults();
  for (const auto &entry : QList<QPair<QString, QStringList>>{
           {"iccSource", {"manual", "system"}},
           {"syncMode",
            {"audio", "display-resample", "display-tempo", "display-vdrop"}},
           {"vsync", {"on", "off"}},
           {"temporalScaler",
            {"default", "oversample", "linear", "mitchell"}}}) {
    const auto saved =
        s.value("video/" + entry.first, v.value(entry.first)).toString();
    if (entry.second.contains(saved))
      v[entry.first] = saved;
  }
  for (const auto &entry :
       QList<QPair<QString, double>>{{"displayFps", 1000.},
                                     {"maxVideoChange", 5.},
                                     {"maxAudioChange", 1.}}) {
    bool ok;
    const double saved =
        s.value("video/" + entry.first, v.value(entry.first)).toDouble(&ok);
    if (ok && std::isfinite(saved))
      v[entry.first] = std::clamp(saved, 0., entry.second);
  }
  if (v["displayFps"].toDouble() > 0 && v["displayFps"].toDouble() < 1)
    v["displayFps"] = 0.;
  v["iccEnabled"] = s.value("video/iccEnabled", false).toBool();
  v["iccProfile"] = s.value("video/iccProfile").toString().left(4096);
  v["iccIntent"] = std::clamp(s.value("video/iccIntent", 1).toInt(), 0, 3);
  v["interpolation"] = s.value("video/interpolation", false).toBool();
  return v;
}
QString VideoDisplay::validateIcc(const QByteArray &bytes) {
  if (bytes.size() < 132 || bytes.size() > 16 * 1024 * 1024 ||
      qFromBigEndian<quint32>(bytes.constData()) != quint32(bytes.size()) ||
      bytes.mid(36, 4) != "acsp")
    return uiTr("ICC: неверный заголовок или размер (до 16 МиБ).");
  // Use a private context: malformed profiles must not alter global LCMS state.
  auto context = cmsCreateContext(nullptr, nullptr);
  if (!context)
    return uiTr("ICC: не удалось создать контекст LittleCMS.");
  auto profile =
      cmsOpenProfileFromMemTHR(context, bytes.constData(), bytes.size());
  const bool valid = profile && cmsGetColorSpace(profile) == cmsSigRgbData &&
                     cmsGetDeviceClass(profile) == cmsSigDisplayClass;
  bool transformable = false;
  if (valid) {
    auto input = cmsCreate_sRGBProfileTHR(context);
    auto transform =
        input ? cmsCreateTransformTHR(context, input, TYPE_RGB_16, profile,
                                      TYPE_RGB_16, INTENT_RELATIVE_COLORIMETRIC,
                                      cmsFLAGS_NOCACHE)
              : nullptr;
    transformable = transform != nullptr;
    if (transform)
      cmsDeleteTransform(transform);
    if (input)
      cmsCloseProfile(input);
  }
  if (profile)
    cmsCloseProfile(profile);
  cmsDeleteContext(context);
  return valid && transformable
             ? QString()
             : uiTr("ICC: нужен читаемый RGB-профиль дисплея с поддерживаемым "
                    "преобразованием.");
}
QByteArray VideoDisplay::readIcc(const QString &path, QString *error) {
  if (error)
    error->clear();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
    if (error)
      *error = uiTr("ICC: файл недоступен или превышает 16 МиБ.");
    return {};
  }
  const auto bytes = file.read(16 * 1024 * 1024 + 1);
  const auto failure = validateIcc(bytes);
  if (!failure.isEmpty()) {
    if (error)
      *error = failure;
    return {};
  }
  return bytes;
}
QString VideoDisplay::resolveIcc(const QString &saved,
                                 const QString &directory) {
  return saved.isEmpty() ? QString() : QDir(directory).absoluteFilePath(saved);
}
QString VideoDisplay::importIcc(const QString &source, const QString &directory,
                                QString *error) {
  const auto bytes = readIcc(resolveIcc(source, directory), error);
  if (bytes.isEmpty())
    return {};
  const auto relative =
      "ColorProfiles/" +
      QString::fromLatin1(
          QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) +
      ".icc";
  const auto target = QDir(directory).filePath(relative);
  QFile existing(target);
  if (existing.open(QIODevice::ReadOnly) && existing.size() == bytes.size() &&
      existing.readAll() == bytes)
    return relative;
  existing.close();
  if (!QDir(directory).mkpath("ColorProfiles")) {
    if (error)
      *error = uiTr("ICC: не удалось создать папку ColorProfiles.");
    return {};
  }
  QSaveFile file(target);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit()) {
    if (error)
      *error = uiTr("ICC: не удалось сохранить профиль рядом с программой.");
    return {};
  }
  return relative;
}
QString VideoDisplay::effectiveSync(const QVariantMap &v, bool openGl,
                                    int swapInterval, bool bitstream,
                                    double refresh, bool visible) {
  const auto requested = v.value("syncMode", "audio").toString();
  return requested.startsWith("display-") && openGl && swapInterval == 1 &&
                 !bitstream && std::isfinite(refresh) && refresh > 0 && visible
             ? requested
             : "audio";
}
void VideoDisplay::updateTiming(Player *p) {
  const auto v = p->QObject::property("foxicmpDisplayValues").toMap();
  const auto *screen = p->screen();
  const double screenRate = screen ? screen->refreshRate() : 0.;
  const double refresh = v.value("displayFps").toDouble() > 0
                             ? v.value("displayFps").toDouble()
                             : screenRate;
  const int reported =
      p->QObject::property("foxicmpSwapIntervalReported").isValid()
          ? p->QObject::property("foxicmpSwapIntervalReported").toInt()
          : p->requestedSwapInterval();
  const auto effective = effectiveSync(
      v, p->outputName() == "OpenGL" && p->ready(), reported,
      p->QObject::property("foxicmpPassthroughRequested").toBool(), refresh,
      p->isVisible() && !p->window()->isMinimized());
  bool ok = true;
  const auto set = [&](const char *name, const QVariant &value) {
    if (p->get(name) != value)
      ok = p->set(name, value) && ok;
  };
  set("display-fps-override", effective == "audio" ? 0. : refresh);
  set("video-sync-max-video-change", v.value("maxVideoChange"));
  set("video-sync-max-audio-change", v.value("maxAudioChange"));
  set("video-sync", effective);
  set("interpolation",
      effective != "audio" && v.value("interpolation").toBool());
  const auto scaler = v.value("temporalScaler").toString();
  if (!p->QObject::property("foxicmpDefaultTemporalScaler").isValid())
    p->setProperty("foxicmpDefaultTemporalScaler", p->get("tscale"));
  set("tscale", scaler == "default"
                    ? p->QObject::property("foxicmpDefaultTemporalScaler")
                    : scaler);
  p->setRenderInterval(
      effective == "audio"
          ? 16
          : std::clamp(int(std::floor(1000. / refresh)), 1, 16));
  p->setProperty("foxicmpDisplayRefreshHint", refresh);
  p->setProperty("foxicmpEffectiveSync", effective);
  p->setProperty("foxicmpTimingAccepted", ok);
  p->setProperty("foxicmpTimingStatus",
                 effective == v.value("syncMode").toString()
                     ? uiTr("Режим запрошен. Фактическое состояние "
                            "синхронизации показано ниже.")
                     : uiTr("Временно используется синхронизация по звуку: "
                            "нужен видимый OpenGL-вывод, VSync 1, частота "
                            "экрана и выключенный bitstream. Выбор сохранён."));
}
bool VideoDisplay::apply(Player *p, QSettings &s) {
  const auto v = values(s);
  p->setProperty("foxicmpDisplayValues", v);
  const bool automatic = v.value("iccEnabled").toBool() &&
                         v.value("iccSource").toString() == "system";
  auto *system = p->findChild<SystemIcc *>("foxicmpSystemIcc");
  if (!system)
    system = new SystemIcc(p);
  system->configure(automatic, QFileInfo(s.fileName()).absolutePath());
  QString error, path;
  if (v.value("iccEnabled").toBool() && !automatic) {
    path = resolveIcc(v.value("iccProfile").toString(),
                      QFileInfo(s.fileName()).absolutePath());
    if (readIcc(path, &error).isEmpty())
      path.clear();
  }
  bool ok = error.isEmpty();
  const auto set = [&](const char *name, const QVariant &value) {
    if (p->get(name) != value)
      ok = p->set(name, value) && ok;
  };
  set("icc-cache", false);        // Never put LUT caches in ~/.cache/mpv.
  set("icc-profile-auto", false); // FoxicMP resolves colord; Render API cannot.
  set("icc-intent", v.value("iccIntent"));
  set("icc-profile", path);
  p->setProperty(
      "foxicmpIccStatus",
      automatic          ? uiTr("Авто ICC: ожидается профиль текущего экрана…")
      : !error.isEmpty() ? error
      : !ok ? uiTr("libmpv отклонил ICC-настройку; профиль не подтверждён.")
      : path.isEmpty()
          ? uiTr("ICC выключен.")
          : uiTr("ICC-профиль передан libmpv. Применяется только в "
                 "OpenGL; результат требует проверки на дисплее."));
  if (!p->findChild<QTimer *>("foxicmpVideoTimingTimer")) {
    auto *timer = new QTimer(p);
    timer->setObjectName("foxicmpVideoTimingTimer");
    timer->setInterval(1000);
    QObject::connect(timer, &QTimer::timeout, p, [p] { updateTiming(p); });
    QObject::connect(p, &Player::initialized, p, [p] { updateTiming(p); });
    timer->start();
  }
  updateTiming(p);
  return ok && p->QObject::property("foxicmpTimingAccepted").toBool();
}
