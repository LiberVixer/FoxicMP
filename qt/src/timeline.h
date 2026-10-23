#pragma once
#include <QVariantList>
#include <QWidget>
class Timeline : public QWidget {
  Q_OBJECT
public:
  explicit Timeline(QWidget *parent = nullptr);
  double duration = 0, position = 0;
  double available = -1;
  double loopA = -1, loopB = -1;
  QVariantList chapters;
  bool previewEnabled = true;
  bool timeTooltipEnabled = true, tooltipBelow = false, chapterMarkers = true;
signals:
  void seek(double seconds);
  void hovered(double seconds, const QPoint &global, const QString &text);
  void hoverEnded();
  void chapterHovered(const QString &chapter);

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void leaveEvent(QEvent *) override;

private:
  double timeAt(int x) const;
  bool dragging = false;
};
QString clockText(double seconds);
