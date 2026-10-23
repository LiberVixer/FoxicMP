#pragma once
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QSizeGrip>
#include <algorithm>

// Keep the complete status accessible; shorten only its on-screen rendering.
class ClassicStatusLabel final : public QLabel {
public:
  explicit ClassicStatusLabel(QWidget *parent) : QLabel(parent) {
    setTextFormat(Qt::PlainText);
  }
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
               fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
  }
};

// Windows gives the right-hand timer priority inside the flexible toolbar slot.
class ClassicStatusArea final : public QWidget {
public:
  ClassicStatusLabel *message;
  QLabel *timer, *gpu;
  explicit ClassicStatusArea(QWidget *parent = nullptr) : QWidget(parent) {
    setObjectName("classicStatusArea");
    setProperty("showStatus", true);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    message = new ClassicStatusLabel(this);
    timer = new QLabel(this);
    timer->setTextFormat(Qt::PlainText);
    gpu = new QLabel(this);
    gpu->setAlignment(Qt::AlignCenter);
    gpu->hide();
    QFont font("Tahoma");
    font.setPixelSize(13);
    message->setFont(font);
    timer->setFont(font);
  }
  QSize sizeHint() const override { return QSize(240, 22); }
  void arrange() {
    QRect available = rect().adjusted(6, 1, -6, -1);
    const bool hardware = !gpu->property("decoder").toString().isEmpty();
    const int gpuWidth = gpu->pixmap().deviceIndependentSize().width();
    gpu->setVisible(hardware && gpuWidth > 0 && available.width() > gpuWidth + 6);
    if (!gpu->isHidden()) {
      gpu->setGeometry(available.right() - gpuWidth + 1, available.top(), gpuWidth, available.height());
      available.setRight(gpu->x() - 6);
    }
    const int timerWidth = timer->fontMetrics().horizontalAdvance(timer->text());
    const bool showStatus = property("showStatus").toBool();
    const bool fits = showStatus && (timer->text().isEmpty() || available.width() > timerWidth + 6);
    timer->setVisible(fits);
    timer->setGeometry(available.right() - timerWidth + 1, available.top(), timerWidth, available.height());
    if (!timer->text().isEmpty() && fits)
      available.setRight(timer->x() - 10);
    message->setGeometry(available.x(), available.y(), std::max(0, available.width()), std::max(0, available.height()));
    message->setVisible(showStatus && available.width() > 20);
  }
protected:
  void resizeEvent(QResizeEvent *) override { arrange(); }
};

class ClassicResizeGrip final : public QSizeGrip {
public:
  explicit ClassicResizeGrip(QWidget *parent) : QSizeGrip(parent) {
    setObjectName("classicResizeGrip");
    setFixedSize(14, 20);
  }
protected:
  void paintEvent(QPaintEvent *event) override {
    if (palette().color(QPalette::Window).lightness() >= 128) {
      QSizeGrip::paintEvent(event);
      return;
    }
    QPainter p(this);
    for (int offset = 4; offset <= 12; offset += 4) {
      p.setPen(property("shadow").value<QColor>());
      p.drawLine(width() - offset, height() - 2, width() - 2, height() - offset);
      p.setPen(property("highlight").value<QColor>());
      p.drawLine(width() - offset + 1, height() - 2, width() - 2, height() - offset + 1);
    }
  }
};
