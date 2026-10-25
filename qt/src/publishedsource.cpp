#include "progressivesource.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include "progressivefile.h"
#include <QDir>

namespace {
bool samePath(const QString &a, const QString &b) {
#ifdef Q_OS_WIN
  return a.compare(b, Qt::CaseInsensitive) == 0;
#else
  return a == b;
#endif
}
QString sessionDirectory(const QJsonObject &input, const QString *bound = nullptr) {
  if (!input.contains("sessionDirectory")) return bound ? *bound : QString();
  const auto value = input.value("sessionDirectory");
  const auto path = value.toString();
  if (!value.isString() || path.isEmpty() || path.contains(QChar(0)) ||
      path.toUtf8().size() > 8192 || !ProgressiveFile::absolute(path))
    throw std::runtime_error("sessionDirectory must be an absolute local directory");
  const auto normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
  if (bound && !samePath(normalized, *bound))
    throw std::runtime_error("sessionDirectory cannot change within a session");
  return bound ? *bound : normalized;
}
QString resolveTrackPath(const QString &path, const QString &root) {
  if (path.contains(QChar(0)) || path.toUtf8().size() > 8192)
    throw std::runtime_error("Track path is too long or contains an embedded null");
  if (ProgressiveFile::absolute(path))
    return QFileInfo(path).absoluteFilePath();
  if (root.isEmpty() || !ProgressiveFile::absolute(root))
    throw std::runtime_error("Relative track paths require an absolute sessionDirectory");
  if (path.isEmpty() || path == "." || path == ".." || path.contains('/') || path.contains('\\'))
    throw std::runtime_error("Track name must be a plain local filename");
  const auto base = QFileInfo(root).absoluteFilePath();
  const auto resolved = QFileInfo(QDir(base).filePath(path)).absoluteFilePath();
  const auto relative = QDir(base).relativeFilePath(resolved);
  if (relative == ".." || relative.startsWith("../") || relative.startsWith("..\\"))
    throw std::runtime_error("Track path escapes sessionDirectory");
  return resolved;
}
harvester::Snapshot snapshotFor(QJsonObject input, QStringList *paths, const QString &root) {
  input.remove("sessionDirectory");
  auto tracks = input.value("tracks").toObject();
  for (const auto &kind : {QStringLiteral("video"), QStringLiteral("audio")}) {
    auto track = tracks.value(kind).toObject();
    const auto path = resolveTrackPath(track.value("path").toString(), root);
    paths->append(path);
    track["path"] = kind + ".part";
    tracks[kind] = track;
  }
  input["tracks"] = tracks;
  return harvester::Parse(QJsonDocument(input).toJson(QJsonDocument::Compact).toStdString());
}
bool readPublished(intptr_t file, uint64_t at, void *data, size_t count) {
  return ProgressiveFile::read(file, at, data, count) == int64_t(count);
}
void checkFile(intptr_t file, uint64_t available) { ProgressiveFile::inspect(file, available); }

}

std::shared_ptr<ProgressiveSource>
ProgressiveSource::openPublished(const QJsonObject &input) {
  auto source = std::shared_ptr<ProgressiveSource>(new ProgressiveSource(input));
  source->worker = std::thread([raw = source.get()] { raw->watchPublished(); });
  return source;
}

QJsonObject ProgressiveSource::probePublished(const QJsonObject &input) {
  try {
    auto source = std::shared_ptr<ProgressiveSource>(new ProgressiveSource(input));
    const auto current = source->status();
    return {{"supported", true}, {"mode", "native-progressive"},
            {"reasonCode", "supported"},
            {"reason", "MP4/M4A tracks are initialized and the confirmed prefix is readable"},
            {"readySeconds", current.available}, {"complete", current.complete}};
  } catch (const std::exception &error) {
    return {{"supported", false}, {"mode", "post-download"},
            {"reasonCode", "native-progressive-rejected"},
            {"reason", QString::fromUtf8(error.what())},
            {"requiresCompleteFile", true}};
  }
}

ProgressiveSource::ProgressiveSource(const QJsonObject &input) : published(true) {
  publishedRoot = sessionDirectory(input);
  snapshot = snapshotFor(input, &publishedPaths, publishedRoot);
  if (snapshot.state == harvester::State::Cancelled ||
      snapshot.state == harvester::State::Failed)
    throw std::runtime_error("Cannot start a terminal failed session");
  try {
    for (int i = 0; i < 2; ++i) {
      const auto &track = snapshot.tracks[i];
      if (!track.initializationReady)
        throw std::runtime_error("MP4 initialization is not ready; retry start after the index is written");
      files[i] = ProgressiveFile::open(publishedPaths[i]);
      const auto info = ProgressiveFile::inspect(files[i], track.available);
      devices[i] = info.device;
      inodes[i] = info.inode;
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
    for (auto file : files) if (file != -1) ProgressiveFile::close(file);
    throw;
  }
}

bool ProgressiveSource::publish(const QJsonObject &input, QString *error) {
  QStringList paths;
  harvester::Snapshot next;
  try {
    next = snapshotFor(input, &paths, sessionDirectory(input, &publishedRoot));
    std::lock_guard<std::mutex> lock(mutex);
    if (!published || stopping.load() || !currentStatus.error.isEmpty())
      throw std::runtime_error("Session is no longer writable");
    if (paths.size() != publishedPaths.size() ||
        !samePath(paths[0], publishedPaths[0]) || !samePath(paths[1], publishedPaths[1]))
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
