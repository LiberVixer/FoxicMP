#include "timeline.h"
#include "i18n.h"
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <cmath>
QString clockText(double seconds) {
  const auto s = qint64(std::max(0.0, seconds));
  return QString("%1:%2:%3")
      .arg(s / 3600, 2, 10, QChar('0'))
      .arg(s / 60 % 60, 2, 10, QChar('0'))
      .arg(s % 60, 2, 10, QChar('0'));
}
Timeline::Timeline(QWidget *p) : QWidget(p) {
  setFixedHeight(16);
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setAccessibleName(uiTr("Позиция воспроизведения"));
}
double Timeline::timeAt(int x) const {
  return duration * std::clamp(double(x) / std::max(1, width() - 1), 0.0, 1.0);
}
void Timeline::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.fillRect(rect(), QColor(31, 35, 38));
  if (duration <= 0)
    return;
  if (available >= 0) {
    const int ready =
        qRound(std::clamp(available / duration, 0.0, 1.0) * width());
    p.fillRect(0, 0, ready, height(), QColor(45, 55, 61));
  }
  const int x =
      qRound(std::clamp(position / duration, 0.0, 1.0) * (width() - 1));
  QColor progress(property("appearance/progress").toString());
  p.fillRect(0, 0, x, height(),
             progress.isValid() ? progress : QColor(68, 75, 80));
  p.setPen(QColor(94, 99, 104));
  if (chapterMarkers) for (const auto &c : chapters) {
    const double t = c.toMap().value("time").toDouble();
    const int cx = qRound(t / duration * (width() - 1));
    p.drawLine(cx, 0, cx, height() - 1);
  }
  p.setPen(QColor(180, 190, 197));
  p.drawLine(x, 0, x, height() - 1);
  p.setPen(QColor(151, 178, 143));
  for (double t : {loopA, loopB})
    if (t >= 0 && t <= duration) {
      const int marker = qRound(t / duration * (width() - 1));
      p.drawLine(marker, 0, marker, height() - 1);
    }
}
void Timeline::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton && duration > 0) {
    emit hoverEnded();
    dragging = true;
    position = timeAt(e->position().x());
    update();
  }
}
void Timeline::mouseMoveEvent(QMouseEvent *e) {
  const double t = timeAt(e->position().x());
  if (dragging) {
    position = t;
    update();
  }
  QString tip = clockText(t);
  QString chapterHover;
  for (int i = chapters.size() - 1; i >= 0; --i)
    if (chapters[i].toMap().value("time").toDouble() <= t) {
      const QString name = chapters[i].toMap().value("title").toString().simplified();
      const QString number = QString::number(i + 1);
      chapterHover = name.isEmpty() ? uiTr("Глава %1").arg(i + 1)
          : name.startsWith(number + ".") || name.startsWith(number + " -") ||
            name.startsWith("0" + number + ".") || name.startsWith("0" + number + " -")
              ? name : number + ". " + name;
      tip += QString(uiTr("\nГлава %1: %2"))
                 .arg(i + 1)
                 .arg(chapters[i].toMap().value("title").toString());
      break;
    }
  emit chapterHovered(duration > 0 && !dragging ? chapterHover : QString());
  if (duration > 0) {
    if (previewEnabled && !dragging)
      emit hovered(t, e->globalPosition().toPoint(), tip);
    else if (timeTooltipEnabled) {
      const int tipHeight = QFontMetrics(QToolTip::font()).boundingRect(
          QRect(0, 0, 1000, 1000), Qt::TextExpandTabs, tip).height();
      const auto anchor = mapToGlobal(QPoint(e->position().x(),
          tooltipBelow ? height() + 8 : -tipHeight - 32));
      QToolTip::showText(anchor, tip, this);
    } else
      QToolTip::hideText();
  }
}
void Timeline::mouseReleaseEvent(QMouseEvent *e) {
  if (dragging && e->button() == Qt::LeftButton) {
    dragging = false;
    emit seek(timeAt(e->position().x()));
  }
}

void Timeline::leaveEvent(QEvent *event) {
  emit hoverEnded();
  QToolTip::hideText();
  QWidget::leaveEvent(event);
}
