#include "controls.h"
#include "appearance.h"
#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <cmath>

QIcon controlIcon(const QString &name) {
  QIcon icon;
  const auto external = Appearance::toolbarCell(name);
  if (!external.isNull())
    return QIcon(QPixmap::fromImage(external));
  for (int scale : {1, 2, 3}) {
    QPixmap pix(20 * scale, 20 * scale);
    pix.setDevicePixelRatio(scale);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QApplication::palette().color(QPalette::ButtonText), 1.5,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(QApplication::palette().color(QPalette::ButtonText));
    if (name == "play")
      p.drawPolygon(QPolygonF{{6, 3}, {16, 10}, {6, 17}});
    else if (name == "pause") {
      p.drawRect(QRectF(5, 4, 3, 12));
      p.drawRect(QRectF(12, 4, 3, 12));
    } else if (name == "stop")
      p.drawRect(QRectF(5, 5, 10, 10));
    else if (name == "previous" || name == "next" || name == "frame") {
      if (name == "previous") {
        p.translate(20, 0);
        p.scale(-1, 1);
      }
      if (name == "frame")
        p.drawPolygon(QPolygonF{{4, 4}, {13, 10}, {4, 16}});
      else {
        p.drawPolygon(QPolygonF{{2, 4}, {8, 10}, {2, 16}});
        p.drawPolygon(QPolygonF{{8, 4}, {14, 10}, {8, 16}});
      }
      p.drawLine(16, 4, 16, 16);
    } else if (name == "playlist") {
      for (int y : {5, 10, 15}) {
        p.drawPoint(3, y);
        p.drawLine(7, y, 17, y);
      }
    } else if (name == "open") {
      p.setBrush(Qt::NoBrush);
      p.drawPolyline(QPolygonF{{3, 15}, {3, 5}, {8, 5}, {10, 7}, {17, 7}});
      p.drawPolygon(QPolygonF{{3, 15}, {6, 9}, {18, 9}, {15, 15}});
    } else if (name == "audio") {
      p.drawPolygon(QPolygonF{{2, 8}, {5, 8}, {9, 4}, {9, 16}, {5, 12}, {2, 12}});
      p.setBrush(Qt::NoBrush);
      p.drawArc(QRectF(8, 6, 7, 8), -60 * 16, 120 * 16);
    } else if (name == "subtitles") {
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(QRectF(2, 4, 16, 12), 2, 2);
      p.drawLine(5, 9, 15, 9);
      p.drawLine(5, 12, 12, 12);
    } else if (name == "volume" || name == "mute") {
      p.drawPolygon(
          QPolygonF{{2, 8}, {5, 8}, {9, 4}, {9, 16}, {5, 12}, {2, 12}});
      p.setBrush(Qt::NoBrush);
      if (name == "mute") {
        p.drawLine(13, 7, 18, 13);
        p.drawLine(18, 7, 13, 13);
      } else {
        p.drawArc(QRectF(8, 6, 7, 8), -60 * 16, 120 * 16);
        p.drawArc(QRectF(6, 3, 13, 14), -55 * 16, 110 * 16);
      }
    }
    p.end();
    icon.addPixmap(pix);
  }
  return icon;
}
bool parsePlaybackTime(const QString &text, double *seconds) {
  // Seconds, MM:SS or HH:MM:SS, with an optional millisecond fraction.
  static const QRegularExpression pattern(
      "^\\d+(?::\\d{1,2}){0,2}(?:[.,]\\d{1,3})?$");
  QString value = text.trimmed();
  if (!pattern.match(value).hasMatch())
    return false;
  value.replace(',', '.');
  const auto parts = value.split(':');
  double total = 0;
  for (int i = 0; i < parts.size(); ++i) {
    bool ok;
    double part = parts[i].toDouble(&ok);
    if (!ok || (i > 0 && part >= 60))
      return false;
    total = total * 60 + part;
  }
  if (!std::isfinite(total))
    return false;
  *seconds = total;
  return true;
}
