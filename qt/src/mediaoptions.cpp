#include "mediaoptions.h"
#include "audiooutput.h"
#include "audioprocessing.h"
#include "player.h"
#include "subtitlestyle.h"
#include "videodisplay.h"
#include "videooutput.h"
#include "videoshaders.h"
#include <QColor>
#include <QSettings>
#include <algorithm>
#include <cmath>
QString MediaOptions::colorFilter(QSettings &s) {
  const int brightness =
      std::clamp(s.value("video/brightness", 0).toInt(), -100, 100);
  const int contrast =
      std::clamp(s.value("video/contrast", 0).toInt(), -100, 100);
  const int saturation =
      std::clamp(s.value("video/saturation", 0).toInt(), -100, 100);
  const int hue = std::clamp(s.value("video/hue", 0).toInt(), -180, 180);
  const int gamma = std::clamp(s.value("video/gamma", 0).toInt(), -100, 100);
  if (!brightness && !contrast && !saturation && !hue && !gamma)
    return {};
  QStringList filters;
  if (hue)
    filters << QString("hue=h=%1").arg(hue);
  if (brightness || contrast || saturation || gamma)
    filters << QString("eq=brightness=%1:contrast=%2:saturation=%3:gamma=%4")
                   .arg(brightness / 100.0, 0, 'f', 2)
                   .arg(1 + contrast / 100.0, 0, 'f', 2)
                   .arg(1 + saturation / 100.0, 0, 'f', 2)
                   .arg(std::pow(2.0, gamma / 100.0), 0, 'f', 5);
  return filters.join(',');
}
QString MediaOptions::previewFilters(QSettings &s) {
  QStringList filters;
  const auto eq = colorFilter(s);
  if (!eq.isEmpty())
    filters.append(eq);
  const int rotation = s.value("video/rotation", 0).toInt();
  if (rotation == 90)
    filters.append("transpose=clock");
  if (rotation == 180)
    filters.append("hflip,vflip");
  if (rotation == 270)
    filters.append("transpose=cclock");
  return filters.join(',');
}
void MediaOptions::apply(Player *p, QSettings &s) {
  VideoOutput::apply(p, s);
  p->set("sub-auto",
         s.value("subtitles/autoLoad", true).toBool() ? "fuzzy" : "no");
  p->set("sub-font", s.value("subtitles/font", "DejaVu Sans"));
  p->set("sub-font-size",
         std::clamp(s.value("subtitles/size", 38).toDouble(), 12.0, 100.0));
  QColor color(s.value("subtitles/color", "#ffffff").toString());
  QColor outline(s.value("subtitles/outlineColor", "#000000").toString());
  p->set("sub-color", color.isValid() ? color.name() : "#ffffff");
  p->set("sub-border-color", outline.isValid() ? outline.name() : "#000000");
  p->set("sub-border-size",
         std::clamp(s.value("subtitles/outlineSize", 2).toDouble(), 0.0, 8.0));
  p->set("sub-pos",
         std::clamp(s.value("subtitles/position", 100).toInt(), 0, 100));
  p->set("sub-visibility", s.value("subtitles/visible", true).toBool());
  SubtitleStyle::apply(p, s,
                       p->QObject::property("foxicmpStyledCopy").toBool());
  AudioProcessing::apply(p, s);
  AudioOutput::apply(p, s);
  VideoDisplay::apply(p, s);
  VideoShaders::apply(p, s);
  const auto eq = colorFilter(s);
  p->set("vf", eq.isEmpty() ? QString() : QString("lavfi=[%1]").arg(eq));
  int rotation = s.value("video/rotation", 0).toInt();
  p->set("video-rotate",
         rotation == 90 || rotation == 180 || rotation == 270 ? rotation : 0);
}
