#include "progressivesource.h"
#include "i18n.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUuid>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
harvester::Snapshot readManifest(const QString &path) {
  const QByteArray native = QFile::encodeName(path);
  int fd = ::open(native.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    throw std::runtime_error("Cannot open local session manifest");
  struct stat info{};
  if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_uid != getuid() ||
      info.st_size > int64_t(harvester::MaxManifestBytes)) {
    ::close(fd);
    throw std::runtime_error("Unsafe or oversized session manifest");
  }
  std::string text(size_t(info.st_size), '\0');
  const auto length = ::read(fd, text.data(), text.size());
  ::close(fd);
  if (length != int64_t(text.size()))
    throw std::runtime_error("Session manifest I/O failed");
  return harvester::Parse(text);
}
bool readAt(int fd, uint64_t offset, void *buffer, size_t size) {
  if (offset > uint64_t(INT64_MAX) || size > uint64_t(INT64_MAX) - offset)
    return false;
  return pread(fd, buffer, size, off_t(offset)) == int64_t(size);
}
} // namespace

std::shared_ptr<ProgressiveSource>
ProgressiveSource::open(const QString &manifest) {
  auto source =
      std::shared_ptr<ProgressiveSource>(new ProgressiveSource(manifest));
  source->worker = std::thread([raw = source.get()] { raw->watch(); });
  return source;
}
ProgressiveSource::ProgressiveSource(const QString &path)
    : manifest(QFileInfo(path).absoluteFilePath()),
      root(QFileInfo(manifest).absolutePath()),
      snapshot(readManifest(manifest)) {
  readerId = QUuid::createUuid().toString(QUuid::Id128);
  struct stat directory{};
  const auto nativeRoot = QFile::encodeName(root);
  if (lstat(nativeRoot.constData(), &directory) ||
      !S_ISDIR(directory.st_mode) || directory.st_uid != getuid() ||
      (directory.st_mode & 0077) || QFileInfo(root).canonicalFilePath() != root)
    throw std::runtime_error("Session directory must be private and local");
  try {
    if (snapshot.state == harvester::State::Failed ||
        snapshot.state == harvester::State::Cancelled)
      throw std::runtime_error("Session is failed or cancelled");
    if (!connectProvider())
      throw std::runtime_error("Cannot acquire a progressive reader lease");
    for (int i = 0; i < 2; ++i) {
      const auto &track = snapshot.tracks[i];
      if (!track.initializationReady)
        throw std::runtime_error(
            "Progressive reader requires initialized MP4 tracks");
      const auto name = QFile::encodeName(
          QDir(root).filePath(QString::fromStdString(track.path)));
      files[i] = ::open(name.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
      struct stat info{};
      if (files[i] < 0 || fstat(files[i], &info) || !S_ISREG(info.st_mode) ||
          info.st_uid != getuid() || uint64_t(info.st_size) < track.available)
        throw std::runtime_error("Unsafe track or unpublished data");
      devices[i] = info.st_dev;
      inodes[i] = info.st_ino;
    }
    if (devices[0] == devices[1] && inodes[0] == inodes[1])
      throw std::runtime_error("Video and audio refer to the same file");
    for (int i = 0; i < 2; ++i)
      if (!snapshot.tracks[i].fragmented)
        indexed[i].open(
            [&, i](uint64_t offset, void *buffer, size_t count) {
              return readAt(files[i], offset, buffer, count);
            },
            snapshot.tracks[i].available, snapshot.tracks[i].total, i,
            snapshot.timestampOffset);
    if (snapshot.duration == 0 && !snapshot.tracks[0].fragmented)
      currentStatus.duration = double(indexed[0].duration()) / 10000000.;
    updateIndexes();
  } catch (...) {
    for (int fd : files)
      if (fd >= 0)
        ::close(fd);
    if (attached)
      send("released");
    if (socket >= 0)
      ::close(socket);
    throw;
  }
}
ProgressiveSource::~ProgressiveSource() {
  cancel();
  if (worker.joinable())
    worker.join();
  // Readers hold this object. Reaching its destructor proves they are gone.
  for (int fd : files)
    if (fd >= 0)
      ::close(fd);
  if (attached && socket < 0)
    connectProvider();
  if (attached)
    send("released");
  if (socket >= 0)
    ::close(socket);
}
void ProgressiveSource::cancel() {
  stopping.store(true);
  {
    std::lock_guard<std::mutex> lock(mutex);
    currentStatus.cancelled = true;
  }
  changed.notify_all();
}
void ProgressiveSource::notify(const QString &state) {
  if (state != "buffering" && state != "playing" && state != "paused" &&
      state != "error")
    return;
  std::lock_guard<std::mutex> lock(mutex);
  playbackState = state;
}
ProgressiveSource::Status ProgressiveSource::status() const {
  std::lock_guard<std::mutex> lock(mutex);
  return currentStatus;
}
QString ProgressiveSource::id() const {
  std::lock_guard<std::mutex> lock(mutex);
  return QString::fromStdString(snapshot.sessionId);
}
int64_t ProgressiveSource::size(int track) const {
  std::lock_guard<std::mutex> lock(mutex);
  return track >= 0 && track < 2 ? int64_t(snapshot.tracks[track].total) : -1;
}
bool ProgressiveSource::seekable(int track, uint64_t offset) const {
  std::lock_guard<std::mutex> lock(mutex);
  return track >= 0 && track < 2 && !stopping.load() &&
         currentStatus.error.isEmpty() &&
         offset <= snapshot.tracks[track].available;
}
int64_t ProgressiveSource::read(int track, uint64_t offset, char *buffer,
                                uint64_t count) {
  if (track < 0 || track >= 2 || !buffer || count == 0 ||
      offset > uint64_t(INT64_MAX))
    return -1;
  std::unique_lock<std::mutex> lock(mutex);
  for (;;) {
    if (stopping.load() || !currentStatus.error.isEmpty())
      return -1;
    const auto &item = snapshot.tracks[track];
    if (currentStatus.connected) {
      if (offset < item.available) {
        const uint64_t length =
            std::min<uint64_t>({count, item.available - offset, 1024 * 1024});
        const auto result = pread(files[track], buffer, length, off_t(offset));
        if (result <= 0) {
          currentStatus.error = uiTr("Ошибка чтения подтверждённых данных");
          changed.notify_all();
          return -1;
        }
        return result;
      }
      if (harvester::EndReady(item))
        return offset == item.total ? 0 : -1;
    }
    changed.wait(lock);
  }
}
void ProgressiveSource::updateIndexes() {
  const int64_t duration =
      snapshot.duration > 0 ? snapshot.duration : indexed[0].duration();
  double end = double(duration) / 10000000.;
  bool complete = true;
  for (int i = 0; i < 2; ++i) {
    const auto &track = snapshot.tracks[i];
    if (track.fragmented)
      indexes[i].Update(
          [&, i](uint64_t offset, void *buffer, size_t count) {
            return readAt(files[i], offset, buffer, count);
          },
          track.available, i, harvester::EndReady(track),
          snapshot.timestampOffset);
    end = std::min(
        end, double(harvester::ReadyEnd(track.fragmented ? indexes[i].Samples()
                                                         : indexed[i].samples(),
                                        track.available, duration)) /
                 10000000.);
    complete = complete && harvester::EndReady(track);
  }
  currentStatus.duration = double(duration) / 10000000.;
  currentStatus.timestampOffset = double(snapshot.timestampOffset) / 10000000.;
  currentStatus.available = end;
  currentStatus.complete = complete;
}
void ProgressiveSource::refresh() {
  auto next = readManifest(manifest);
  std::lock_guard<std::mutex> lock(mutex);
  if (!harvester::Advance(snapshot, next))
    return;
  for (int i = 0; i < 2; ++i) {
    const auto name = QFile::encodeName(
        QDir(root).filePath(QString::fromStdString(next.tracks[i].path)));
    struct stat pathInfo{}, openedInfo{};
    if (lstat(name.constData(), &pathInfo) || fstat(files[i], &openedInfo) ||
        !S_ISREG(pathInfo.st_mode) || uint64_t(pathInfo.st_dev) != devices[i] ||
        uint64_t(pathInfo.st_ino) != inodes[i] ||
        uint64_t(openedInfo.st_size) < next.tracks[i].available)
      throw std::runtime_error(
          "Pinned progressive track was replaced, removed or truncated");
  }
  if (next.state == harvester::State::Failed ||
      next.state == harvester::State::Cancelled)
    throw std::runtime_error(next.state == harvester::State::Failed
                                 ? "Downloader failed"
                                 : "Downloader cancelled");
  snapshot = next;
  updateIndexes();
  changed.notify_all();
}
bool ProgressiveSource::connectProvider() {
  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0)
    return false;
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  const std::string name = "FoxicMP.Harvester." + snapshot.sessionId;
  std::memcpy(address.sun_path + 1, name.data(), name.size());
  if (::connect(fd, reinterpret_cast<sockaddr *>(&address),
                offsetof(sockaddr_un, sun_path) + 1 + name.size())) {
    ::close(fd);
    return false;
  }
  struct ucred credentials{};
  socklen_t length = sizeof(credentials);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) ||
      credentials.uid != getuid()) {
    ::close(fd);
    return false;
  }
  socket = fd;
  if (!send("attached")) {
    ::close(socket);
    socket = -1;
    return false;
  }
  attached = true;
  // An acknowledged lease must protect the names before we open either file.
  // During reconnect it also confirms that the provider has accepted our PID.
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  QByteArray raw;
  bool malformed = false;
  while (std::chrono::steady_clock::now() < deadline && !stopping.load()) {
    pollfd descriptor{socket, POLLIN, 0};
    if (::poll(&descriptor, 1, 100) <= 0)
      continue;
    char buffer[4096];
    const auto count = ::recv(socket, buffer, sizeof(buffer), 0);
    if (count <= 0)
      break;
    raw.append(buffer, int(count));
    if (raw.size() > int(harvester::MaxManifestBytes))
      break;
    for (int newline; (newline = raw.indexOf('\n')) >= 0;) {
      const auto line = raw.left(newline);
      raw.remove(0, newline + 1);
      try {
        rapidjson::Document value;
        value.Parse<rapidjson::kParseValidateEncodingFlag |
                    rapidjson::kParseIterativeFlag>(line.constData(),
                                                    size_t(line.size()));
        if (value.HasParseError() || !value.IsObject())
          throw std::runtime_error("Invalid lease acknowledgement");
        harvester::UniqueKeys(value);
        if (harvester::Number(value, "schemaVersion") != 1 ||
            harvester::String(value, "sessionId") != snapshot.sessionId ||
            harvester::Number(value, "generation") != snapshot.generation)
          throw std::runtime_error("Lease belongs to another session");
        const auto type = harvester::String(value, "type");
        const auto revision = harvester::Number(value, "revision");
        if (type == "attached" &&
            harvester::String(value, "readerId") == readerId.toStdString() &&
            revision >= snapshot.revision) {
          std::lock_guard<std::mutex> lock(mutex);
          currentStatus.connected = true;
          return true;
        }
        if (type != "heartbeat")
          throw std::runtime_error("Unexpected lease reply");
      } catch (const std::exception &) {
        malformed = true;
        break;
      }
    }
    if (malformed)
      break;
  }
  send("released");
  ::close(socket);
  socket = -1;
  {
    std::lock_guard<std::mutex> lock(mutex);
    currentStatus.connected = false;
  }
  return false;
}
bool ProgressiveSource::send(const char *type) {
  if (socket < 0)
    return false;
  QJsonObject message;
  {
    std::lock_guard<std::mutex> lock(mutex);
    message = {{"schemaVersion", 1},
               {"sessionId", QString::fromStdString(snapshot.sessionId)},
               {"generation", qint64(snapshot.generation)},
               {"revision", qint64(snapshot.revision)},
               {"pid", int(getpid())},
               {"readerId", readerId},
               {"leaseAck", true},
               {"type", QString::fromLatin1(type)}};
  }
  const QByteArray raw =
      QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
  return ::send(socket, raw.constData(), size_t(raw.size()), MSG_NOSIGNAL) ==
         raw.size();
}
void ProgressiveSource::fail(const QString &error) {
  std::lock_guard<std::mutex> lock(mutex);
  currentStatus.error = error;
  changed.notify_all();
}
void ProgressiveSource::watch() {
  using Clock = std::chrono::steady_clock;
  auto heartbeat = Clock::now();
  QByteArray raw;
  while (!stopping.load()) {
    try {
      refresh();
      if (socket < 0 && connectProvider()) {
        heartbeat = Clock::now();
        raw.clear();
      }
      if (socket >= 0) {
        pollfd descriptor{socket, POLLIN, 0};
        if (::poll(&descriptor, 1, 100) > 0) {
          char buffer[4096];
          const auto length = ::recv(socket, buffer, sizeof(buffer), 0);
          if (length <= 0) {
            ::close(socket);
            socket = -1;
            std::lock_guard<std::mutex> lock(mutex);
            currentStatus.connected = false;
          } else {
            raw.append(buffer, int(length));
            if (raw.size() > int(harvester::MaxManifestBytes))
              throw std::runtime_error("Oversized provider message");
            for (int newline; (newline = raw.indexOf('\n')) >= 0;) {
              const auto line = raw.left(newline);
              raw.remove(0, newline + 1);
              rapidjson::Document value;
              value.Parse<rapidjson::kParseValidateEncodingFlag |
                          rapidjson::kParseIterativeFlag>(line.constData(),
                                                          size_t(line.size()));
              if (value.HasParseError() || !value.IsObject())
                throw std::runtime_error("Invalid provider message");
              harvester::UniqueKeys(value);
              std::lock_guard<std::mutex> lock(mutex);
              if (harvester::Number(value, "schemaVersion") != 1 ||
                  harvester::String(value, "sessionId") != snapshot.sessionId ||
                  harvester::Number(value, "generation") !=
                      snapshot.generation ||
                  harvester::Number(value, "revision") == 0 ||
                  harvester::String(value, "type") != "heartbeat")
                throw std::runtime_error("Provider session changed");
              heartbeat = Clock::now();
              currentStatus.connected = true;
              changed.notify_all();
            }
          }
        }
        if (Clock::now() - heartbeat > std::chrono::seconds(15)) {
          ::close(socket);
          socket = -1;
          std::lock_guard<std::mutex> lock(mutex);
          currentStatus.connected = false;
        }
        QString state;
        {
          std::lock_guard<std::mutex> lock(mutex);
          state = playbackState;
        }
        if (socket >= 0 && !send(state.toLatin1().constData())) {
          ::close(socket);
          socket = -1;
          std::lock_guard<std::mutex> lock(mutex);
          currentStatus.connected = false;
        }
      }
    } catch (const std::exception &error) {
      fail(QString::fromUtf8(error.what()));
      send("error");
      break;
    }
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_for(lock, std::chrono::milliseconds(100),
                     [this] { return stopping.load(); });
  }
}

namespace {
struct StreamReader {
  std::shared_ptr<ProgressiveSource> source;
  int track;
  uint64_t position = 0;
  std::atomic<bool> cancelled{false};
};
} // namespace
ProgressiveStreams::ProgressiveStreams(mpv_handle *handle) {
  if (mpv_stream_cb_add_ro(handle, "foxicmp-session", this, open) < 0)
    throw std::runtime_error("Cannot register progressive stream callbacks");
}
void ProgressiveStreams::activate(
    const std::shared_ptr<ProgressiveSource> &next) {
  std::lock_guard<std::mutex> lock(mutex);
  if (source)
    source->cancel();
  source = next;
}
void ProgressiveStreams::clear() { activate({}); }
int ProgressiveStreams::open(void *self, char *uri, mpv_stream_cb_info *info) {
  auto *streams = static_cast<ProgressiveStreams *>(self);
  std::shared_ptr<ProgressiveSource> source;
  {
    std::lock_guard<std::mutex> lock(streams->mutex);
    source = streams->source;
  }
  const QUrl url(QString::fromUtf8(uri));
  const int track = url.path() == "/video"   ? 0
                    : url.path() == "/audio" ? 1
                                             : -1;
  if (!source || track < 0 || url.host() != source->id())
    return MPV_ERROR_LOADING_FAILED;
  auto *reader = new StreamReader{source, track};
  *info = {};
  info->cookie = reader;
  info->read_fn = [](void *cookie, char *buffer, uint64_t count) -> int64_t {
    auto *reader = static_cast<StreamReader *>(cookie);
    if (reader->cancelled.load())
      return -1;
    const auto result =
        reader->source->read(reader->track, reader->position, buffer, count);
    if (result > 0)
      reader->position += uint64_t(result);
    return result;
  };
  info->seek_fn = [](void *cookie, int64_t position) -> int64_t {
    auto *reader = static_cast<StreamReader *>(cookie);
    if (reader->cancelled.load() || position < 0 ||
        !reader->source->seekable(reader->track, uint64_t(position)))
      return MPV_ERROR_UNSUPPORTED;
    reader->position = uint64_t(position);
    return position;
  };
  // FFmpeg's MOV header reader scans a seekable fragmented stream to its end.
  // A growing fMP4 must open as a stream; mpv can still seek its retained
  // packet cache. Completed sources opened afresh use ordinary byte seeking.
  if (!source->status().complete)
    info->seek_fn = nullptr;
  info->size_fn = [](void *cookie) -> int64_t {
    auto *reader = static_cast<StreamReader *>(cookie);
    const auto size = reader->source->size(reader->track);
    return size > 0 ? size : int64_t(MPV_ERROR_UNSUPPORTED);
  };
  info->close_fn = [](void *cookie) {
    delete static_cast<StreamReader *>(cookie);
  };
  info->cancel_fn = [](void *cookie) {
    auto *reader = static_cast<StreamReader *>(cookie);
    reader->cancelled.store(true);
    reader->source->cancel();
  };
  return 0;
}
