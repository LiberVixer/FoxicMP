#include "osdlabel.h"
#include <QPainter>
#include <QSettings>
OsdLabel::OsdLabel(QWidget *parent) : QLabel(parent) {
  setTextFormat(Qt::PlainText);
  setWordWrap(true);
  setMargin(8);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setStyleSheet("background:transparent;");
}
void OsdLabel::configure(QSettings &settings) {
  const auto color = [&](const char *key, const char *fallback) {
    const QColor value(settings.value(key, fallback).toString());
    return value.isValid() ? value : QColor(fallback);
  };
  foreground = color("osd/textColor", "#ffffff");
  background = color("osd/backgroundColor", "#121518");
  background.setAlpha(
      std::clamp(settings.value("osd/opacity", 210).toInt(), 0, 255));
  border = color("osd/borderColor", "#69727a");
  borderWidth = std::clamp(settings.value("osd/borderWidth", 0).toInt(), 0, 8);
  shadow = settings.value("osd/shadow", true).toBool();
  antialias = settings.value("osd/antialias", true).toBool();
  QFont font(settings.value("osd/font", "DejaVu Sans").toString());
  font.setPixelSize(
      std::clamp(settings.value("osd/fontSize", 18).toInt(), 10, 72));
  font.setStyleStrategy(antialias ? QFont::PreferAntialias
                                  : QFont::NoAntialias);
  setFont(font);
  adjustSize();
  update();
}
void OsdLabel::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, antialias);
  painter.setRenderHint(QPainter::TextAntialiasing, antialias);
  painter.setPen(borderWidth ? QPen(border, borderWidth) : QPen(Qt::NoPen));
  painter.setBrush(background);
  painter.drawRoundedRect(
      QRectF(rect()).adjusted(borderWidth / 2., borderWidth / 2.,
                              -borderWidth / 2., -borderWidth / 2.),
      5, 5);
  const auto area = rect().adjusted(margin(), margin(), -margin(), -margin());
  const int flags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap;
  if (shadow) {
    painter.setPen(QColor(0, 0, 0, 220));
    painter.drawText(area.translated(2, 2), flags, text());
  }
  painter.setPen(foreground);
  painter.drawText(area, flags, text());
}
