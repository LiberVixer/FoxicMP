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
    const QRect interior = track.adjusted(1, 1, -1, -1);
    const QColor customFill(property("volumeFill").toString());
    const QColor customBackground(property("volumeBackground").toString());
    const bool tintedFill = customFill.isValid() && customFill != QColor("#ffffff");
    // The legacy gray default now means the toolbar itself. Leave these pixels
    // transparent so the parent's gradient continues at exactly the same height.
    if (customBackground.isValid() && customBackground != QColor("#808080")) {
      QLinearGradient trough(interior.left(), interior.top() + .5,
                             interior.left(), interior.bottom() + .5);
      trough.setColorAt(0, customBackground.darker(dark ? 130 : 110));
      trough.setColorAt(1, customBackground);
      p.fillRect(interior, trough);
    }
    const int end = interior.left() + value() * interior.width() / 100;
    if (end > interior.left()) {
      const int xEnd = std::min(interior.right() + 1, end);
      QLinearGradient fill(interior.left(), interior.top() + .5,
                           interior.left(), interior.bottom() + .5);
      const bool muted = property("muted").toBool();
      if (tintedFill) {
        fill.setColorAt(0, customFill.darker(400));
        fill.setColorAt(1, customFill);
      } else {
        // The fill follows the supplied swatch; the empty area follows the panel.
        fill.setColorAt(0, dark ? QColor(24, 29, 34) : QColor(126, 133, 139));
        fill.setColorAt(1, dark ? QColor(112, 117, 122) : QColor(198, 205, 211));
      }
      p.fillRect(QRect(interior.left(), interior.top(), xEnd - interior.left(), interior.height()), fill);
      if (muted) {
        p.setPen(dark ? themeColor(242, 13, 13) : QColor(200, 35, 35));
        p.drawLine(xEnd - 1, interior.top(), xEnd - 1, interior.bottom());
      } else {
        const int thumbWidth = 4;
        const int thumbLeft = std::max(interior.left(), xEnd - thumbWidth);
        QLinearGradient thumb(thumbLeft, interior.top() + .5, thumbLeft, interior.bottom() + .5);
        const QColor top = tintedFill ? customFill.lighter(120) : QColor(245, 248, 250);
        const QColor bottom = tintedFill ? customFill.lighter(170) : QColor(255, 255, 255);
        thumb.setColorAt(0, top);
        thumb.setColorAt(1, bottom);
        p.fillRect(QRect(thumbLeft, interior.top(), xEnd - thumbLeft, interior.height()), thumb);
      }
    }
    p.setPen(dark ? themeColor(30, 35, 40) : QColor(145, 152, 158));
    p.drawLine(track.left(), track.top(), track.right(), track.top());
    p.drawLine(track.left(), track.top(), track.left(), track.bottom());
    p.setPen(dark ? themeColor(80, 85, 90) : QColor(248, 250, 252));
    p.drawLine(track.left(), track.bottom(), track.right(), track.bottom());
    p.drawLine(track.right(), track.top(), track.right(), track.bottom());
    if (end > track.left() && !property("muted").toBool()) {
      p.setPen(tintedFill ? customFill : dark ? QColor(245, 248, 250) : QColor(184, 191, 197));
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
