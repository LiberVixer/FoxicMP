#pragma once
#include <QCache>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QSize>
#include <QTimer>
class FramePreview : public QObject {
  Q_OBJECT
public:
  explicit FramePreview(QObject *parent = nullptr);
  ~FramePreview() override;
  void request(const QString &file, double seconds);
  void cancel();
  void setFilters(const QString &filters);
  void setVideoStream(int ffmpegIndex);
  void configure(const QSize &size, bool online, const QString &extractor,
                 const QString &userAgent, int timeoutMs = 10000);
  bool available() const;
signals:
  void ready(const QImage &image, double seconds);
  void unavailable(const QString &reason);

private:
  void start();
  void decode();
  void resolve();
  void fail(const QString &message);
  QTimer debounce, deadline;
  QProcess *process = nullptr;
  QCache<QString, QImage> cache{16 * 1024};
  QString pendingFile, pendingKey, filters, extractor, userAgent;
  QString resolvedSource, resolvedUrl, headers;
  qint64 resolvedAt = 0;
  QSize size{320, 180};
  bool online = false, resolving = false;
  int timeoutMs = 10000;
  int videoStream = -1;
  double pendingTime = 0;
  quint64 generation = 0;
};
