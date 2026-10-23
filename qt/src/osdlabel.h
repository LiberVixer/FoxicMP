#pragma once
#include <QLabel>
class QSettings;
class OsdLabel : public QLabel {
public:
  explicit OsdLabel(QWidget *parent = nullptr);
  void configure(QSettings &);

protected:
  void paintEvent(QPaintEvent *) override;

private:
  QColor foreground, background, border;
  bool shadow = true, antialias = true;
  int borderWidth = 0;
};
