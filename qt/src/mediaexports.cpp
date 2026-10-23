#include "mediaexports.h"
#include "i18n.h"
#include <QDateTime>
#include <QFileInfo>
#include <QImageWriter>
#include <QPainter>
#include <cmath>

QStringList MediaExports::formats() {
  QStringList out;
  const auto supported = QImageWriter::supportedImageFormats();
  for (const auto &format : {"png", "jpg", "bmp", "webp"})
    if (supported.contains(format))
      out << format;
  return out;
}
QString MediaExports::fileFilters() {
  QStringList filters;
  for (const auto &format : formats()) {
    if (format == "jpg")
      filters << "JPEG (*.jpg *.jpeg)";
    else if (format == "webp")
      filters << "WebP (*.webp)";
    else
      filters << format.toUpper() + " (*." + format + ")";
  }
  return filters.join(";;");
}
bool MediaExports::writeImage(const QImage &image, const QString &path,
                              int quality, QString *error) {
  const auto suffix = QFileInfo(path).suffix().toLower();
  const auto format = suffix == "jpeg" ? QString("jpg") : suffix;
  if (error)
    error->clear();
  if (image.isNull() || !formats().contains(format)) {
    if (error)
      *error = uiTr("Нет изображения или формат снимка не поддерживается.");
    return false;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error)
      *error = file.errorString();
    return false;
  }
  QImageWriter writer(&file, format.toLatin1());
  writer.setQuality(std::clamp(quality, 1, 100));
  if (!writer.write(image) || !file.commit()) {
    if (error)
      *error = writer.errorString() + " " + file.errorString();
    return false;
  }
  return true;
}
MediaCopy::MediaCopy(QObject *parent) : QObject(parent) {
  timer.setInterval(1);
  connect(&timer, &QTimer::timeout, this, &MediaCopy::step);
}
bool MediaCopy::start(const QString &path, const QString &target) {
  if (output)
    return false;
  const QFileInfo info(path), destination(target);
  if (!info.isFile() ||
      info.canonicalFilePath() == destination.canonicalFilePath())
    return false;
  source = info.absoluteFilePath();
  input.setFileName(source);
  if (!input.open(QIODevice::ReadOnly))
    return false;
  output = std::make_unique<QSaveFile>(target);
  if (!output->open(QIODevice::WriteOnly)) {
    output.reset();
    input.close();
    return false;
  }
  total = input.size();
  modified = info.lastModified();
  copied = 0;
  timer.start();
  return true;
}
void MediaCopy::step() {
  if (!output)
    return;
  const auto bytes = input.read(1024 * 1024);
  if (input.error() != QFileDevice::NoError ||
      output->write(bytes) != bytes.size()) {
    finish(false, uiTr("Ошибка чтения или записи копии медиафайла."));
    return;
  }
  copied += bytes.size();
  emit progress(copied, total);
  if (!output)
    return;
  if (bytes.isEmpty() || copied >= total) {
    const QFileInfo current(source);
    if (copied != total || input.size() != total || !current.exists() ||
        current.lastModified() != modified) {
      finish(false, uiTr("Исходный файл изменился во время копирования."));
      return;
    }
    const bool committed = output->commit();
    finish(committed, committed
                          ? uiTr("Копия медиафайла сохранена.")
                          : uiTr("Не удалось завершить сохранение копии."));
  }
}
void MediaCopy::finish(bool success, const QString &message) {
  timer.stop();
  output
      .reset(); // QSaveFile cancels an incomplete copy, preserving old target.
  input.close();
  emit finished(success, message);
}
void MediaCopy::cancel() {
  if (output)
    finish(false, uiTr("Копирование отменено."));
}
ThumbnailSheet::ThumbnailSheet(QObject *parent)
    : QObject(parent), decoder(this) {
  connect(&decoder, &FramePreview::unavailable, this,
          [this](const QString &error) {
            if (!running)
              return;
            cancel();
            emit failed(error);
          });
  connect(&decoder, &FramePreview::ready, this,
          [this](const QImage &frame, double seconds) {
            if (!running)
              return;
            QPainter painter(&image);
            const int x = 16 + (index % columns) * (width + 16);
            const int y = 70 + (index / columns) * (height + 40);
            painter.fillRect(QRect(x, y, width, height), Qt::black);
            const auto scaled = frame.scaled(width, height, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
            painter.drawImage(x + (width - scaled.width()) / 2,
                              y + (height - scaled.height()) / 2, scaled);
            painter.setPen(Qt::white);
            const int whole = int(seconds);
            const auto stamp = QString("%1:%2:%3")
                                   .arg(whole / 3600, 2, 10, QChar('0'))
                                   .arg(whole / 60 % 60, 2, 10, QChar('0'))
                                   .arg(whole % 60, 2, 10, QChar('0'));
            painter.drawText(QRect(x, y + height + 4, width, 24),
                             Qt::AlignCenter, stamp);
            painter.end();
            ++index;
            emit progress(index, rows * columns);
            if (!running)
              return;
            if (index == rows * columns) {
              running = false;
              emit ready(image);
            } else
              next();
          });
}
bool ThumbnailSheet::start(const QString &path, double length, int cols,
                           int rowCount, int thumbWidth, int stream) {
  if (running || !QFileInfo(path).isFile() || !std::isfinite(length) ||
      length <= 0 || length > 7 * 24 * 3600 || cols < 1 || cols > 6 ||
      rowCount < 1 || rowCount > 10 || thumbWidth < 80 || thumbWidth > 480 ||
      !decoder.available())
    return false;
  source = QFileInfo(path).absoluteFilePath();
  duration = length;
  columns = cols;
  rows = rowCount;
  width = thumbWidth;
  height = qRound(width * 9. / 16.);
  image = QImage(columns * (width + 16) + 16, 70 + rows * (height + 40),
                 QImage::Format_RGB32);
  if (image.isNull())
    return false;
  image.fill(QColor(32, 36, 40));
  QPainter painter(&image);
  painter.setPen(Qt::white);
  painter.drawText(QRect(16, 8, image.width() - 32, 54),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                   "FoxicMP — " + QFileInfo(source).fileName());
  painter.end();
  decoder.configure(QSize(width, height), false, {}, {});
  decoder.setVideoStream(stream);
  index = 0;
  running = true;
  next();
  return true;
}
void ThumbnailSheet::next() {
  decoder.request(source, duration * (index + .5) / (rows * columns));
}
void ThumbnailSheet::cancel() {
  running = false;
  decoder.cancel();
  image = {};
}
