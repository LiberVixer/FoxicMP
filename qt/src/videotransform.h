#pragma once
#include <QVariantMap>
class QSettings;
class Player;
struct VideoTransform {
  double width = 1, height = 1, x = 0, y = 0, zoom = 0, fill = 0;
  double left = 0, right = 0, top = 0, bottom = 0;
  int rotation = 0;
  bool mirror = false, flip = false, swapEyes = false;
  QString cropAspect = "no", stereoInput = "auto", stereoOutput = "pass";
  static VideoTransform fromMap(const QVariantMap &map);
  QVariantMap toMap() const;
  static VideoTransform load(QSettings &settings);
  static bool preset(QSettings &, const QString &name, VideoTransform *);
  void save(QSettings &settings) const;
  QString sourceFilters(const QString &detectedStereo = {}) const;
  QString resolvedStereo(const QString &detectedStereo = {}) const;
  bool apply(Player *player, const QString &colorFilter,
             const QString &detectedStereo = {}) const;
};
