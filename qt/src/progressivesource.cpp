#include "progressivesource.h"
#include "../../shared/progressivereaderlease.h"
#include "../../shared/progressiveretirement.h"
#include "../../shared/progressivefmp4bridge.h"
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
      foxicmp::progressive::SourceRetirement::create<ProgressiveSource>([&]{return new ProgressiveSource(manifest);});
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
  if(retirementHook)retirementHook();
  if (worker.joinable())
    worker.join();
  // Readers hold this object. Reaching its destructor proves they are gone.
  for (int fd : files)
    if (fd >= 0)
      ::close(fd);
  v2Bridge.reset();
  if (readerLease) readerLease->filesClosed();
  if (attached && socket < 0)
    connectProvider();
  if (attached)
    send("released");
  if (socket >= 0)
    ::close(socket);
  released->store(true);
}
void ProgressiveSource::refresh() {
  std::lock_guard<std::mutex> publication(publicationMutex);
  auto next=readManifest(manifest);harvester::Snapshot previous;
  {std::lock_guard<std::mutex> lock(mutex);previous=snapshot;}
  const bool advanced=harvester::Advance(previous,next);
  legacyIoCheckpoint(next.revision);
  const auto& checked=advanced?next:previous;
  if(stopping.load())return;
  // Files/indexes are worker-owned; the UI only sees committed metadata.
  for (int i = 0; i < 2; ++i) {
    const auto name = QFile::encodeName(
        QDir(root).filePath(QString::fromStdString(checked.tracks[i].path)));
    struct stat pathInfo{}, openedInfo{};
    if (lstat(name.constData(), &pathInfo) || fstat(files[i], &openedInfo) ||
        !S_ISREG(pathInfo.st_mode) || uint64_t(pathInfo.st_dev) != devices[i] ||
        uint64_t(pathInfo.st_ino) != inodes[i] ||
        uint64_t(openedInfo.st_size) < checked.tracks[i].available)
      throw std::runtime_error(
          "Pinned progressive track was replaced, removed or truncated");
  }
  if(!advanced)return;
  if(next.state==harvester::State::Failed || next.state==harvester::State::Cancelled)
    throw std::runtime_error(next.state==harvester::State::Failed?"Downloader failed":"Downloader cancelled");
  const auto indexedStatus=indexStatus(next);
  {std::lock_guard<std::mutex> lock(mutex);
   if(stopping.load() || !currentStatus.error.isEmpty())return;
   snapshot=next;currentStatus.duration=indexedStatus.duration;
   currentStatus.timestampOffset=indexedStatus.timestampOffset;
   currentStatus.available=indexedStatus.available;currentStatus.complete=indexedStatus.complete;}
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
      if(!stopping.load()){fail(QString::fromUtf8(error.what()));send("error");}
      break;
    }
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_for(lock, std::chrono::milliseconds(100),
                     [this] { return stopping.load(); });
  }
}
