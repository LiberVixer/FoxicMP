#pragma once
#include "framepreview.h"
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QSaveFile>
#include <QTimer>
#include <memory>
namespace MediaExports {
QStringList formats();
QString fileFilters();
bool writeImage(const QImage &, const QString &path, int quality,
                QString *error);
} // namespace MediaExports
class MediaCopy : public QObject {
  Q_OBJECT
  QFile input;
  std::unique_ptr<QSaveFile> output;
  QTimer timer;
  qint64 total = 0, copied = 0;
  QString source;
  QDateTime modified;
  void step();
  void finish(bool, const QString &);

public:
  explicit MediaCopy(QObject *parent = nullptr);
  bool start(const QString &source, const QString &target);
  void cancel();
signals:
  void progress(qint64 copied, qint64 total);
  void finished(bool success, const QString &message);
};
class ThumbnailSheet : public QObject {
  Q_OBJECT
  FramePreview decoder;
  QImage image;
  QString source;
  double duration = 0;
  int columns = 0, rows = 0, width = 0, height = 0, index = 0;
  bool running = false;
  void next();

public:
  explicit ThumbnailSheet(QObject *parent = nullptr);
  bool start(const QString &source, double duration, int columns, int rows,
             int thumbWidth, int stream = -1);
  void cancel();
signals:
  void progress(int done, int total);
  void ready(const QImage &image);
  void failed(const QString &reason);
};
