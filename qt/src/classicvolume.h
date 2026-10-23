#pragma once

#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <algorithm>

// Windows Classic uses a self-drawn 16 px volume track instead of the native
// theme slider. Keep the hit area and rendering geometry in one widget.
class ClassicVolumeSlider final : public QSlider {
public:
  explicit ClassicVolumeSlider(QWidget *parent = nullptr) : QSlider(Qt::Horizontal, parent) {
    setObjectName("volumeSlider");
    setFixedSize(95, 22);
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
    if (!dark) {
      const QRect interior = track.adjusted(8, 4, -10, -6);
      p.setPen(palette().color(QPalette::Shadow));
      p.drawLine(interior.right(), interior.top(), interior.right(), interior.bottom());
      p.drawLine(interior.right(), interior.bottom(), interior.left(), interior.bottom());
      p.setPen(palette().color(QPalette::Light));
      p.drawLine(interior.left(), interior.bottom(), interior.right(), interior.top());
      p.fillRect(interior.adjusted(1, 1, -1, -1), palette().color(QPalette::Button));
      return;
    }

    const QRect interior = track.adjusted(1, 1, -1, -1);
    QLinearGradient trough(interior.topLeft(), interior.bottomLeft());
    const QColor customFill(property("volumeFill").toString());
    const QColor customBackground(property("volumeBackground").toString());
    trough.setColorAt(0, customBackground.isValid() ? customBackground.darker(130)
                                                     : themeColor(14, 17, 20));
    trough.setColorAt(1, customBackground.isValid() ? customBackground
                                                     : themeColor(26, 30, 34));
    p.fillRect(interior, trough);
    const int end = interior.left() + value() * interior.width() / 100;
    if (end > interior.left()) {
      const int xEnd = std::min(interior.right() + 1, end);
      QLinearGradient fill(interior.left(), interior.top(), interior.left(), interior.bottom());
      const bool muted = property("muted").toBool();
      if (muted) {
        fill.setColorAt(0, themeColor(15, 18, 20));
        fill.setColorAt(1, themeColor(45, 48, 52));
      } else if (customFill.isValid()) {
        fill.setColorAt(0, customFill.darker(400));
        fill.setColorAt(1, customFill);
      } else {
        fill.setColorAt(0, themeColor(10, 15, 20));
        fill.setColorAt(1, themeColor(105, 110, 115));
      }
      p.fillRect(QRect(interior.left(), interior.top(), xEnd - interior.left(), interior.height()), fill);
      if (muted) {
        p.setPen(themeColor(242, 13, 13));
        p.drawLine(xEnd - 1, interior.top(), xEnd - 1, interior.bottom());
      } else {
        const int thumbWidth = 4;
        const int thumbLeft = std::max(interior.left(), xEnd - thumbWidth);
        QLinearGradient thumb(thumbLeft, interior.top(), thumbLeft, interior.bottom());
        const QColor top = customFill.isValid() ? customFill.lighter(120) : themeColor(20, 25, 30);
        const QColor bottom = customFill.isValid() ? customFill.lighter(170) : themeColor(215, 220, 225);
        thumb.setColorAt(0, top);
        thumb.setColorAt(1, bottom);
        p.fillRect(QRect(thumbLeft, interior.top(), xEnd - thumbLeft, interior.height()), thumb);
      }
    }
    p.setPen(themeColor(30, 35, 40));
    p.drawLine(track.left(), track.top(), track.right(), track.top());
    p.drawLine(track.left(), track.top(), track.left(), track.bottom());
    p.setPen(themeColor(80, 85, 90));
    p.drawLine(track.left(), track.bottom() - 1, track.right(), track.bottom() - 1);
    p.drawLine(track.right() - 1, track.top(), track.right() - 1, track.bottom());
    if (end > track.left() && !property("muted").toBool()) {
      p.setPen(customFill.isValid() ? customFill : themeColor(80, 85, 90));
      p.drawLine(track.left(), track.top(), std::min(track.right(), end), track.top());
    }
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
