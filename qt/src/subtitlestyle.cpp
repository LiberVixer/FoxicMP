#include "subtitlestyle.h"
#include "player.h"
#include "subtitledocument.h"
#include "subtitlerender.h"
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <cmath>

namespace {
double number(QSettings &s, const char *key, double fallback, double low,
              double high) {
  bool ok;
  const double value =
      s.value(QString("subtitles/") + key, fallback).toDouble(&ok);
  return ok && std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
QString choice(QSettings &s, const char *key, const QString &fallback,
               const QStringList &choices) {
  const auto value = s.value(QString("subtitles/") + key, fallback).toString();
  return choices.contains(value) ? value : fallback;
}
QColor color(QSettings &s, const char *key, const QString &fallback,
             const char *opacity, bool enabled) {
  QColor result(s.value(QString("subtitles/") + key, fallback).toString());
  if (!result.isValid())
    result = QColor(fallback);
  if (enabled)
    result.setAlphaF(result.alphaF() * number(s, opacity, 100, 0, 100) / 100.);
  return result;
}
QString assColor(const QString &text) {
  const QColor c(text);
  const quint32 value = (quint32(255 - c.alpha()) << 24) | (c.blue() << 16) |
                        (c.green() << 8) | c.red();
  return "&H" + QString::number(value, 16).rightJustified(8, '0').toUpper();
}
QString decimal(double value) { return QString::number(value, 'f', 3); }
int alignment(const QVariantMap &v) {
  const int x = QStringList{"left", "center", "right"}.indexOf(
      v.value("alignX").toString());
  const int y = QStringList{"bottom", "center", "top"}.indexOf(
      v.value("alignY").toString());
  return 1 + x + y * 3;
}
QString plain(QString text, bool ass) {
  if (ass) {
    text.remove(QRegularExpression("\\{[^}]*\\}"));
    text.replace("\\N", "\n").replace("\\n", "\n").replace("\\h", " ");
  }
  text.replace(QRegularExpression("<br\\s*/?>",
                                  QRegularExpression::CaseInsensitiveOption),
               "\n");
  text.remove(QRegularExpression("<[^>]*>"));
  text.replace("&lt;", "<")
      .replace("&gt;", ">")
      .replace("&amp;", "&")
      .replace("&nbsp;", " ");
  // Braces and backslashes must not become executable ASS tags in plain text.
  text.replace('{', QChar(0xff5b))
      .replace('}', QChar(0xff5d))
      .replace('\\', QChar(0xff3c));
  text.replace('\r', ' ');
  return text.replace('\n', "\\N");
}
} // namespace
QVariantMap SubtitleStyle::values(QSettings &s) {
  const bool enabled = s.value("subtitles/styleEnabled", false).toBool();
  QVariantMap v{
      {"enabled", enabled},
      {"alignX", choice(s, "alignX", "center", {"left", "center", "right"})},
      {"alignY", choice(s, "alignY", "bottom", {"top", "center", "bottom"})},
      {"justify",
       choice(s, "justify", "auto", {"auto", "left", "center", "right"})},
      {"font", s.value("subtitles/font", "DejaVu Sans").toString()},
      {"bold", s.value("subtitles/bold", false).toBool()},
      {"italic", s.value("subtitles/italic", false).toBool()},
      {"useMargins", s.value("subtitles/useMargins", true).toBool()},
      {"color", color(s, "color", "#ffffff", "textOpacity", enabled)
                    .name(QColor::HexArgb)},
      {"outlineColor",
       color(s, "outlineColor", "#000000", "outlineOpacity", enabled)
           .name(QColor::HexArgb)},
      {"secondaryColor",
       color(s, "secondaryColor", "#ffff00", "secondaryOpacity", enabled)
           .name(QColor::HexArgb)},
      {"shadowColor",
       color(s, "shadowColor", "#000000", "shadowOpacity", enabled)
           .name(QColor::HexArgb)},
      {"backColor", color(s, "backColor", "#00000000", "backOpacity", enabled)
                        .name(QColor::HexArgb)}};
  for (const auto &entry :
       QList<QList<QVariant>>{{"marginX", 25, 0, 500},
                              {"marginY", 22, 0, 500},
                              {"spacing", 0, -10, 30},
                              {"lineSpacing", 0, -20, 80},
                              {"scaleX", 100, 20, 300},
                              {"scaleY", 100, 20, 300},
                              {"angle", 0, -180, 180},
                              {"horizontalPosition", 50, 0, 100},
                              {"size", 38, 12, 100},
                              {"outlineSize", 2, 0, 8},
                              {"shadowOffset", 0, 0, 20},
                              {"blur", 0, 0, 20},
                              {"position", 100, 0, 100}})
    v[entry[0].toString()] =
        number(s, entry[0].toString().toLatin1().constData(),
               entry[1].toDouble(), entry[2].toDouble(), entry[3].toDouble());
  return v;
}
bool SubtitleStyle::isCopy(const QString &path, const QString &directory) {
  const QFileInfo file(path);
  return file.fileName().startsWith("foxicmp-styled-") &&
         file.suffix() == "ass" &&
         QDir::cleanPath(file.absolutePath()) ==
             QDir::cleanPath(
                 QDir(directory).absoluteFilePath("Subtitles/Edits"));
}
bool SubtitleStyle::apply(Player *p, QSettings &s, bool copy) {
  const bool renderOk = SubtitleRender::apply(p, s);
  const auto v = values(s);
  const bool enabled = v.value("enabled").toBool();
  const bool override = s.value("subtitles/overrideAss", false).toBool();
  const auto oldModeValue = p->get("sub-ass-override");
  const QString oldMode = oldModeValue.typeId() == QMetaType::Bool
                              ? (oldModeValue.toBool() ? "yes" : "no")
                              : oldModeValue.toString();
  const auto oldStyles = p->get("sub-ass-style-overrides").toStringList();
  const auto oldMarginY = p->get("sub-margin-y").toDouble();
  p->setProperty("foxicmpStyledCopy", copy);
  bool ok = renderOk;
  const auto set = [&](const char *key, const QVariant &value) {
    ok = p->set(key, value) && ok;
  };
  set("sub-color", v.value("color"));
  set("sub-border-color", v.value("outlineColor"));
  for (const auto &entry : QList<QPair<const char *, QVariant>>{
           {"sub-align-x", enabled ? v["alignX"] : QVariant("center")},
           {"sub-align-y", enabled ? v["alignY"] : QVariant("bottom")},
           {"sub-justify", enabled ? v["justify"] : QVariant("auto")},
           {"sub-margin-x", enabled ? v["marginX"] : QVariant(25)},
           {"sub-margin-y", enabled ? v["marginY"] : QVariant(22)},
           {"sub-spacing", enabled ? v["spacing"] : QVariant(0)},
           {"sub-ass-line-spacing", enabled ? v["lineSpacing"] : QVariant(0)},
           {"sub-shadow-offset", enabled ? v["shadowOffset"] : QVariant(0)},
           {"sub-shadow-color",
            enabled ? v["shadowColor"] : QVariant("#ff000000")},
           {"sub-back-color", enabled ? v["backColor"] : QVariant("#00000000")},
           {"sub-blur", enabled ? v["blur"] : QVariant(0)},
           {"sub-bold", enabled && v["bold"].toBool()},
           {"sub-italic", enabled && v["italic"].toBool()},
           {"sub-use-margins", !enabled || v["useMargins"].toBool()}})
    set(entry.first, entry.second);
  set("sub-ass-force-margins",
      enabled && (override || copy) && v["useMargins"].toBool());
  set("sub-ass-justify",
      enabled && (override || copy) && v["justify"] != "auto");
  QStringList styles;
  if (enabled && override && !copy) {
    QString font = v["font"].toString();
    font.remove(QRegularExpression("[\\r\\n,=]"));
    styles << "FontName=" + font
           << "FontSize=" + decimal(v["size"].toDouble())
           // libass force-style uses normalized scale, unlike ASS Style lines.
           << "ScaleX=" + decimal(v["scaleX"].toDouble() / 100.)
           << "ScaleY=" + decimal(v["scaleY"].toDouble() / 100.)
           << "Angle=" + decimal(v["angle"].toDouble())
           << "PrimaryColour=" + assColor(v["color"].toString())
           << "SecondaryColour=" + assColor(v["secondaryColor"].toString())
           << "OutlineColour=" + assColor(v["outlineColor"].toString())
           << "BackColour=" +
                  assColor(v[QColor(v["backColor"].toString()).alpha() > 0
                                 ? "backColor"
                                 : "shadowColor"]
                               .toString())
           << QString("BorderStyle=%1")
                  .arg(QColor(v["backColor"].toString()).alpha() > 0 ? 4 : 1)
           << "Shadow=" + decimal(v["shadowOffset"].toDouble())
           << "Outline=" + decimal(v["outlineSize"].toDouble())
           << "Spacing=" + decimal(v["spacing"].toDouble())
           << "Blur=" + decimal(v["blur"].toDouble())
           << QString("Justify=%1")
                  .arg(v["justify"] == "left"     ? 1
                       : v["justify"] == "right"  ? 3
                       : v["justify"] == "center" ? 2
                                                  : 0)
           << "Alignment=" + QString::number(alignment(v))
           << "MarginL=" + decimal(v["marginX"].toDouble())
           << "MarginR=" + decimal(v["marginX"].toDouble())
           << "MarginV=" + decimal(v["marginY"].toDouble())
           << QString("Bold=%1").arg(v["bold"].toBool() ? -1 : 0)
           << QString("Italic=%1").arg(v["italic"].toBool() ? -1 : 0);
  }
  ok = p->setList("sub-ass-style-overrides", styles) && ok;
  const QString mode = copy && enabled       ? "yes"
                       : enabled && override ? "yes"
                       : override            ? "force"
                                             : "no";
  set("sub-ass-override", mode);
  set("sub-pos", copy && enabled ? 100 : v["position"].toInt());
  // mpv 0.37 can clear paused ASS events on a mode change and caches vertical
  // margins until reinitialization. Refresh the selected external track only.
  if (oldMode != mode || oldStyles != styles ||
      oldMarginY != (enabled ? v["marginY"].toDouble() : 22.)) {
    SubtitleRender::refresh(p);
  }
  return ok;
}
QString SubtitleStyle::assCopy(const SubtitleDocument &doc, QSettings &s,
                               QString *error) {
  if (!doc.valid(error))
    return {};
  auto v = values(s);
  if (!v.value("enabled").toBool()) {
    for (const auto &entry : QVariantMap{{"alignX", "center"},
                                         {"alignY", "bottom"},
                                         {"marginX", 25},
                                         {"marginY", 22},
                                         {"scaleX", 100},
                                         {"scaleY", 100},
                                         {"angle", 0},
                                         {"horizontalPosition", 50},
                                         {"spacing", 0},
                                         {"shadowOffset", 0},
                                         {"blur", 0},
                                         {"bold", false},
                                         {"italic", false},
                                         {"backColor", "#00000000"}}
                                 .toStdMap())
      v[entry.first] = entry.second;
  }
  QString font = v["font"].toString();
  font.remove(QRegularExpression("[\\r\\n,]"));
  const bool background = QColor(v["backColor"].toString()).alpha() > 0;
  QStringList style{
      "FoxicMP",
      font,
      decimal(v["size"].toDouble()),
      assColor(v["color"].toString()),
      assColor(v["secondaryColor"].toString()),
      assColor(v["outlineColor"].toString()),
      assColor(v[background ? "backColor" : "shadowColor"].toString()),
      v["bold"].toBool() ? "-1" : "0",
      v["italic"].toBool() ? "-1" : "0",
      "0",
      "0",
      decimal(v["scaleX"].toDouble()),
      decimal(v["scaleY"].toDouble()),
      decimal(v["spacing"].toDouble()),
      decimal(v["angle"].toDouble()),
      background ? "4" : "1",
      decimal(v["outlineSize"].toDouble()),
      decimal(v["shadowOffset"].toDouble()),
      QString::number(alignment(v)),
      "0",
      "0",
      "0",
      "1"};
  QStringList out{
      "[Script Info]",
      "; FoxicMP styled text copy. Rebuilt from plain cue text when style "
      "preferences change.",
      "ScriptType: v4.00+",
      "PlayResX: 1280",
      "PlayResY: 720",
      "WrapStyle: 0",
      "ScaledBorderAndShadow: yes",
      "",
      "[V4+ Styles]",
      "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
      "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
      "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
      "MarginL, MarginR, MarginV, Encoding",
      "Style: " + style.join(','),
      "",
      "[Events]",
      "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
      "Effect, Text"};
  const auto xMode = v["alignX"].toString(), yMode = v["alignY"].toString();
  double x = xMode == "left"    ? v["marginX"].toDouble()
             : xMode == "right" ? 1280 - v["marginX"].toDouble()
                                : 640;
  x += (v["horizontalPosition"].toDouble() - 50) * 12.8;
  double y = yMode == "top"      ? v["marginY"].toDouble()
             : yMode == "center" ? 360
                                 : 720 - v["marginY"].toDouble();
  y -= (100 - v["position"].toDouble()) * 7.2;
  const QString prefix =
      QString("{\\pos(%1,%2)\\blur%3}")
          .arg(decimal(x), decimal(y), decimal(v["blur"].toDouble()));
  for (const auto &cue : doc.cues) {
    out << "Dialogue: 0," + SubtitleDocument::timeText(cue.start, true) + ',' +
               SubtitleDocument::timeText(
                   std::max((cue.end + 9) / 10 * 10, cue.start / 10 * 10 + 10),
                   true) +
               ",FoxicMP,,0,0,0,," + prefix +
               plain(cue.text, doc.format == "ass");
  }
  return out.join('\n') + '\n';
}
