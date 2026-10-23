#include "subtitlerender.h"
#include "player.h"
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <cmath>
#include <memory>

QVariantMap SubtitleRender::defaults() {
  return QVariantMap{
      {"hinting", "none"},         {"shaper", "complex"},
      {"colorCompat", "basic"},    {"bitmapBlur", 0.},
      {"embeddedFonts", true},     {"scaleByWindow", true},
      {"scaleWithWindow", true},   {"assScaleWithWindow", false},
      {"aspectCompat", true},      {"blurCompat", true},
      {"clearOnSeek", false},      {"fixTiming", false},
      {"stretchDurations", false}, {"pastVideoEnd", false},
      {"bitmapGray", false},       {"stretchDvd", false},
      {"stretchImage", false},     {"imageVideoResolution", false}};
}
QVariantMap SubtitleRender::values(QSettings &s) {
  QVariantMap v;
  const auto choice = [&](const char *key, const QString &fallback,
                          const QStringList &choices) {
    const auto value =
        s.value(QString("subtitles/render/") + key, fallback).toString();
    v[key] = choices.contains(value) ? value : fallback;
  };
  choice("hinting", "none", {"none", "light", "normal", "native"});
  choice("shaper", "complex", {"complex", "simple"});
  choice("colorCompat", "basic", {"no", "basic", "full", "force-601"});
  const auto fallback = defaults();
  for (auto it = fallback.cbegin(); it != fallback.cend(); ++it)
    if (it.value().typeId() == QMetaType::Bool)
      v[it.key()] =
          s.value("subtitles/render/" + it.key(), it.value()).toBool();
  bool ok;
  const double gauss = s.value("subtitles/render/bitmapBlur", 0).toDouble(&ok);
  v["bitmapBlur"] = ok && std::isfinite(gauss) ? std::clamp(gauss, 0., 3.) : 0.;
  return v;
}
bool SubtitleRender::apply(Player *p, QSettings &s) {
  const auto v = values(s);
  bool ok = true;
  bool reload = false;
  for (const auto &entry : QList<QPair<const char *, const char *>>{
           {"sub-ass-hinting", "hinting"},
           {"sub-ass-shaper", "shaper"},
           {"embeddedfonts", "embeddedFonts"},
           {"sub-scale-by-window", "scaleByWindow"},
           {"sub-scale-with-window", "scaleWithWindow"},
           {"sub-ass-scale-with-window", "assScaleWithWindow"},
           {"sub-ass-vsfilter-aspect-compat", "aspectCompat"},
           {"sub-ass-vsfilter-blur-compat", "blurCompat"},
           {"sub-ass-vsfilter-color-compat", "colorCompat"},
           {"sub-clear-on-seek", "clearOnSeek"},
           {"sub-fix-timing", "fixTiming"},
           {"sub-stretch-durations", "stretchDurations"},
           {"sub-past-video-end", "pastVideoEnd"},
           {"sub-gray", "bitmapGray"},
           {"sub-gauss", "bitmapBlur"},
           {"stretch-dvd-subs", "stretchDvd"},
           {"stretch-image-subs-to-screen", "stretchImage"},
           {"image-subs-video-resolution", "imageVideoResolution"}}) {
    const auto value = v.value(entry.second);
    const auto old = p->get(entry.first);
    // Avoid unnecessary decoder updates during ordinary preferences changes.
    if (old.isValid() && old == value)
      continue;
    const bool set = p->set(entry.first, value);
    ok = set && ok;
    if (set && (QByteArray(entry.first) == "embeddedfonts" ||
                QByteArray(entry.first) == "sub-fix-timing" ||
                QByteArray(entry.first) == "sub-stretch-durations" ||
                QByteArray(entry.first) == "sub-gray" ||
                QByteArray(entry.first) == "sub-gauss"))
      reload = true;
  }
  if (reload)
    refresh(p);
  return ok;
}
void SubtitleRender::refresh(Player *p) {
  const auto sid = p->get("sid").toString();
  const auto secondary = p->get("secondary-sid").toString();
  QVariantList external;
  for (const auto &entry : p->get("track-list").toList()) {
    const auto track = entry.toMap();
    const auto id = track.value("id").toString();
    if (track.value("type") != "sub" || (id != sid && id != secondary))
      continue;
    if (track.value("external").toBool())
      external.append(track);
    else
      p->setProperty("foxicmpSubtitleRefresh", true);
  }
  if (external.isEmpty())
    return;
  const auto token =
      p->QObject::property("foxicmpSubtitleReload").toULongLong() + 1;
  p->setProperty("foxicmpSubtitleReload", token);
  const auto source = p->get("path").toString();
  // Hard subtitle updates are processed after the API setter returns. Defer
  // reloading both selected external tracks until their decoders are ready.
  QTimer::singleShot(100, p, [p, token, external, sid, secondary, source] {
    if (p->QObject::property("foxicmpSubtitleReload").toULongLong() != token ||
        p->get("path").toString() != source)
      return;
    QVariantMap selections;
    for (const auto &entry : p->get("track-list").toList()) {
      const auto track = entry.toMap();
      if (track.value("type") != "sub")
        continue;
      if (track.value("id").toString() == sid)
        selections["sid"] = track;
      if (track.value("id").toString() == secondary)
        selections["secondary-sid"] = track;
    }
    // sub-reload unconditionally selects its new track as primary in mpv
    // 0.37, including when reloading a secondary track. IDs can also change.
    // Wait for every reload reply, then restore both selections by filename.
    auto *guard = new QObject(p);
    auto pending = std::make_shared<QSet<quint64>>();
    const auto restore = [p, token, source, selections] {
      if (p->QObject::property("foxicmpSubtitleReload").toULongLong() !=
              token ||
          p->get("path").toString() != source)
        return;
      QMap<QString, QString> ids{{"sid", "no"}, {"secondary-sid", "no"}};
      for (auto it = selections.cbegin(); it != selections.cend(); ++it) {
        const auto previous = it.value().toMap();
        for (const auto &entry : p->get("track-list").toList()) {
          const auto current = entry.toMap();
          if (current.value("type") != "sub")
            continue;
          const bool same = previous.value("external").toBool()
                                ? current.value("external-filename") ==
                                      previous.value("external-filename")
                                : current.value("id") == previous.value("id");
          if (same) {
            ids[it.key()] = current.value("id").toString();
            break;
          }
        }
      }
      p->set("secondary-sid", "no");
      p->set("sid", ids["sid"]);
      p->set("secondary-sid", ids["secondary-sid"]);
    };
    QObject::connect(p, &Player::commandFinished, guard,
                     [guard, pending, restore](quint64 id, int) {
                       if (!pending->remove(id) || !pending->isEmpty())
                         return;
                       restore();
                       guard->deleteLater();
                     });
    for (const auto &entry : external) {
      const auto track = entry.toMap();
      const auto id = track.value("id").toString();
      const auto selection = id == sid ? "sid" : "secondary-sid";
      if (p->get(selection).toString() != (id == sid ? sid : secondary))
        continue;
      auto counter =
          p->QObject::property("foxicmpSubtitleRequest").toULongLong() + 1;
      p->setProperty("foxicmpSubtitleRequest", counter);
      const quint64 request = (quint64(0x5342) << 48) | counter;
      pending->insert(request);
      if (!p->command({"sub-reload", id}, request))
        pending->remove(request);
    }
    if (pending->isEmpty())
      guard->deleteLater();
  });
}
