#include "progressivesource.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
harvester::Snapshot snapshotFor(QJsonObject input, QStringList *paths) {
  auto tracks = input.value("tracks").toObject();
  for (const auto &kind : {QStringLiteral("video"), QStringLiteral("audio")}) {
    auto track = tracks.value(kind).toObject();
    const auto path = track.value("path").toString();
    if (!path.startsWith('/') || path.contains(QChar(0)) ||
        path.toUtf8().size() > 8192)
      throw std::runtime_error("Track path must be an absolute local path");
    paths->append(path);
    track["path"] = kind + ".part";
    tracks[kind] = track;
  }
  input["tracks"] = tracks;
  return harvester::Parse(QJsonDocument(input).toJson(QJsonDocument::Compact).toStdString());
}
bool readPublished(int fd, uint64_t at, void *data, size_t count) {
  return at <= uint64_t(INT64_MAX) && count <= uint64_t(INT64_MAX) - at &&
         pread(fd, data, count, off_t(at)) == int64_t(count);
}
void checkFile(int fd, uint64_t available) {
  struct stat info{};
  if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_uid != getuid() ||
      info.st_size < 0 || uint64_t(info.st_size) < available)
    throw std::runtime_error("Track is not an owned regular file or confirmed bytes are missing");
}
}

std::shared_ptr<ProgressiveSource>
ProgressiveSource::openPublished(const QJsonObject &input) {
  auto source = std::shared_ptr<ProgressiveSource>(new ProgressiveSource(input));
  source->worker = std::thread([raw = source.get()] { raw->watchPublished(); });
  return source;
}

ProgressiveSource::ProgressiveSource(const QJsonObject &input) : published(true) {
  snapshot = snapshotFor(input, &publishedPaths);
  if (snapshot.state == harvester::State::Cancelled ||
      snapshot.state == harvester::State::Failed)
    throw std::runtime_error("Cannot start a terminal failed session");
  try {
    for (int i = 0; i < 2; ++i) {
      const auto &track = snapshot.tracks[i];
      if (!track.initializationReady)
        throw std::runtime_error("MP4 initialization is not ready; retry start after the index is written");
      files[i] = ::open(QFile::encodeName(publishedPaths[i]).constData(),
                        O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
      checkFile(files[i], track.available);
      struct stat info{};
      if (fstat(files[i], &info)) throw std::runtime_error("Cannot inspect track");
      devices[i] = info.st_dev;
      inodes[i] = info.st_ino;
    }
    if (devices[0] == devices[1] && inodes[0] == inodes[1])
      throw std::runtime_error("Video and audio must be distinct files");
    for (int i = 0; i < 2; ++i)
      if (!snapshot.tracks[i].fragmented)
        indexed[i].open([&, i](uint64_t at, void *data, size_t count) {
          return readPublished(files[i], at, data, count);
        }, snapshot.tracks[i].available, snapshot.tracks[i].total, i,
        snapshot.timestampOffset);
    updateIndexes();
    currentStatus.connected = true;
    lastPublished = std::chrono::steady_clock::now();
  } catch (...) {
    for (int fd : files) if (fd >= 0) ::close(fd);
    throw;
  }
}

bool ProgressiveSource::publish(const QJsonObject &input, QString *error) {
  QStringList paths;
  harvester::Snapshot next;
  try {
    next = snapshotFor(input, &paths);
    std::lock_guard<std::mutex> lock(mutex);
    if (!published || stopping.load() || !currentStatus.error.isEmpty())
      throw std::runtime_error("Session is no longer writable");
    if (paths != publishedPaths)
      throw std::runtime_error("Track paths cannot change within a session");
    if (!harvester::Advance(snapshot, next)) {
      if (error) *error = "Stale revision; use a strictly increasing revision";
      return false;
    }
    for (int i = 0; i < 2; ++i) checkFile(files[i], next.tracks[i].available);
    snapshot = next;
    try {
      updateIndexes();
    } catch (const std::exception &e) {
      currentStatus.error = QString::fromUtf8(e.what());
      changed.notify_all();
      throw;
    }
    lastPublished = std::chrono::steady_clock::now();
    currentStatus.connected = true;
    if (next.state == harvester::State::Failed || next.state == harvester::State::Cancelled)
      currentStatus.error = next.state == harvester::State::Failed
                                ? "Downloader failed" : "Downloader cancelled";
    changed.notify_all();
    return true;
  } catch (const std::exception &e) {
    if (error) *error = QString::fromUtf8(e.what());
    return false;
  }
}
void ProgressiveSource::publishedHeartbeat() {
  std::lock_guard<std::mutex> lock(mutex);
  if (!published || stopping.load() || !currentStatus.error.isEmpty()) return;
  lastPublished = std::chrono::steady_clock::now();
  currentStatus.connected = true;
  changed.notify_all();
}
void ProgressiveSource::publishedError(const QString &error) {
  if (published) fail(error);
}
void ProgressiveSource::watchPublished() {
  while (!stopping.load()) {
    std::unique_lock<std::mutex> lock(mutex);
    try {
      for (int i = 0; i < 2; ++i) checkFile(files[i], snapshot.tracks[i].available);
      if (!currentStatus.complete && std::chrono::steady_clock::now() - lastPublished >
                                          std::chrono::seconds(15)) {
        currentStatus.connected = false;
        changed.notify_all();
      }
    } catch (const std::exception &e) {
      currentStatus.error = QString::fromUtf8(e.what());
      changed.notify_all();
      return;
    }
    changed.wait_for(lock, std::chrono::milliseconds(100), [this] { return stopping.load(); });
  }
}
