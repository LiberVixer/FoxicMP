#pragma once

#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <algorithm>
#include <array>

// Windows Classic uses a self-drawn 16 px volume track instead of the native
// theme slider. Keep the hit area and rendering geometry in one widget.
class ClassicVolumeSlider final : public QSlider {
public:
  explicit ClassicVolumeSlider(QWidget *parent = nullptr) : QSlider(Qt::Horizontal, parent) {
    setObjectName("volumeSlider");
    setFixedSize(66, 22);
    setMouseTracking(true);
  }

  void setMuted(bool muted) {
    setProperty("muted", muted);
    update();
    repaint();
  }

protected:
  QRect trackRect() const {
    const int height = 16;
    const int top = (rect().height() - height) / 2;
    return QRect(4, top, std::max(1, rect().width() - 11), height);
  }

  QColor themeColor(int r, int g, int b) const {
    const int brightness = property("themeBrightness").isValid()
        ? property("themeBrightness").toInt() : 15;
    const auto scale = [this, brightness](int value, const char *key) {
      const int multiplier = property(key).isValid() ? property(key).toInt() : 255;
      return std::clamp((value + brightness) * multiplier / 255, 0, 255);
    };
    return QColor(scale(r, "themeRed"), scale(g, "themeGreen"),
                  scale(b, "themeBlue"));
  }

  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRect track = trackRect();
    const bool dark = property("themeDark").isValid()
        ? property("themeDark").toBool()
        : palette().color(QPalette::Window).lightness() < 128;
    const QRect interior = track.adjusted(1, 1, -1, -1);
    const QColor customFill(property("volumeFill").toString());
    const QColor customBackground(property("volumeBackground").toString());
    const bool tintedFill = customFill.isValid() && customFill != QColor("#ffffff");
    // Sampled scanlines retain the reference's rounding at native pixel size.
    static const std::array<QRgb, 14> fillRows{{
        qRgb(24,29,34), qRgb(30,35,40), qRgb(37,42,47), qRgb(44,49,54),
        qRgb(51,56,61), qRgb(57,62,67), qRgb(64,69,74), qRgb(71,76,81),
        qRgb(78,83,88), qRgb(85,90,95), qRgb(91,96,101), qRgb(98,103,108),
        qRgb(105,110,115), qRgb(112,117,122)}};
    static const std::array<QRgb, 14> thumbRows{{
        qRgb(34,39,44), qRgb(47,52,57), qRgb(61,66,71), qRgb(75,80,85),
        qRgb(89,94,99), qRgb(103,108,113), qRgb(117,122,127), qRgb(131,136,141),
        qRgb(145,150,155), qRgb(159,164,169), qRgb(173,178,183), qRgb(187,192,197),
        qRgb(201,206,211), qRgb(215,220,225)}};
    static const std::array<QRgb, 14> emptyRows{{
        qRgb(28,31,34), qRgb(28,31,35), qRgb(29,32,36), qRgb(30,33,37),
        qRgb(31,34,38), qRgb(32,35,39), qRgb(33,36,40), qRgb(33,37,41),
        qRgb(34,38,42), qRgb(35,39,43), qRgb(36,40,44), qRgb(37,41,45),
        qRgb(38,42,46), qRgb(39,43,47)}};
    const auto sampled = [&p](const QRect &area, const std::array<QRgb, 14> &rows) {
      for (int y = 0; y < area.height(); ++y)
        p.fillRect(QRect(area.left(), area.top() + y, area.width(), 1),
                   QColor::fromRgb(rows[std::min(13, y * 14 / area.height())]));
    };
    const auto gradient = [&p](const QRect &area, const QColor &top, const QColor &bottom) {
      QLinearGradient paint(area.left(), area.top() + .5, area.left(), area.bottom() + .5);
      paint.setColorAt(0, top);
      paint.setColorAt(1, bottom);
      p.fillRect(area, paint);
    };
    if (customBackground.isValid() && customBackground != QColor("#808080")) {
      gradient(interior, customBackground.darker(dark ? 130 : 110), customBackground);
    } else if (dark) sampled(interior, emptyRows);
    else gradient(interior, QColor(226,229,232), QColor(237,240,243));
    const int end = interior.left() + value() * interior.width() / 100;
    if (end > interior.left()) {
      const int xEnd = std::min(interior.right() + 1, end);
      const bool muted = property("muted").toBool();
      const QRect filled(interior.left(), interior.top(), xEnd - interior.left(), interior.height());
      if (tintedFill) gradient(filled, customFill.darker(400), customFill);
      else if (dark) sampled(filled, fillRows);
      else gradient(filled, QColor(126,133,139), QColor(198,205,211));
      if (muted) {
        p.setPen(dark ? themeColor(242, 13, 13) : QColor(200, 35, 35));
        p.drawLine(xEnd - 1, interior.top(), xEnd - 1, interior.bottom());
      } else {
        const int thumbWidth = 4;
        const int thumbLeft = std::max(interior.left(), xEnd - thumbWidth);
        const QRect thumb(thumbLeft, interior.top(), xEnd - thumbLeft, interior.height());
        if (tintedFill) gradient(thumb, customFill.lighter(120), customFill.lighter(170));
        else if (dark) sampled(thumb, thumbRows);
        else gradient(thumb, QColor(166,173,179), QColor(245,248,250));
      }
    }
    const QColor edge = dark ? QColor(94,99,104) : QColor(157,165,172);
    p.setPen(dark ? QColor(44,49,54) : QColor(186,192,197));
    p.drawLine(track.left(), track.top(), track.right(), track.top());
    p.drawLine(track.left(), track.top(), track.left(), track.bottom());
    p.setPen(edge);
    p.drawLine(track.left(), track.bottom(), track.right(), track.bottom());
    p.drawLine(track.right(), track.top(), track.right(), track.bottom());
    if (end > interior.left())
      p.drawLine(track.left(), track.top(), std::min(interior.right(), end - 1), track.top());
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      setValueFromPosition(event->position().x());
      event->accept();
      return;
    }
    QSlider::mousePressEvent(event);
  }
  void mouseMoveEvent(QMouseEvent *event) override {
    if (event->buttons() & Qt::LeftButton) {
      setValueFromPosition(event->position().x());
      event->accept();
      return;
    }
    QSlider::mouseMoveEvent(event);
  }

private:
  void setValueFromPosition(int x) {
    const QRect track = trackRect();
    setValue(qRound(std::clamp(double(x - track.left()) /
                                   std::max(1, track.width() - 1), 0., 1.) * 100.));
  }
};
