#include "progressivesource.h"
#include "progressivefile.h"
#include "i18n.h"
#include "../../shared/progressivefmp4bridge.h"
#include <QUrl>
void ProgressiveSource::legacyIoCheckpoint(uint64_t revision) {
  const auto hook=std::atomic_load(&legacyIoHook);if(hook)(*hook)(revision);
}
void ProgressiveSource::cancel() {
  stopping.store(true);
  if (v2Bridge) v2Bridge->cancel();
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
                                uint64_t count, const std::atomic<bool> *interrupted) {
  if (track < 0 || track >= 2 || !buffer || count == 0 ||
      offset > uint64_t(INT64_MAX))
    return -1;
  std::unique_lock<std::mutex> lock(mutex);
  for (;;) {
    if (stopping.load() || (interrupted && interrupted->load()) || !currentStatus.error.isEmpty())
      return -1;
    const auto item = snapshot.tracks[track];
    if (currentStatus.connected) {
      if (offset < item.available) {
        const uint64_t length =
            std::min<uint64_t>({count, item.available - offset, 1024 * 1024});
        lock.unlock();
        const auto result = v2Bridge ? v2Bridge->readPayload(track,offset,buffer,size_t(length))
                                    : ProgressiveFile::read(files[track], offset, buffer, size_t(length));
        lock.lock();
        if(stopping.load() || (interrupted && interrupted->load()) || !currentStatus.error.isEmpty())return -1;
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
    if (interrupted) changed.wait_for(lock, std::chrono::milliseconds(50));
    else changed.wait(lock);
  }
}
ProgressiveSource::Status ProgressiveSource::indexStatus(const harvester::Snapshot &value) {
  Status result;
  const int64_t duration =
      value.duration > 0 ? value.duration : indexed[0].duration();
  double end = double(duration) / 10000000.;
  bool complete = true;
  for (int i = 0; i < 2; ++i) {
    const auto &track = value.tracks[i];
    if (track.fragmented)
      indexes[i].Update(
          [&, i](uint64_t offset, void *buffer, size_t count) {
            if(stopping.load())return false;
            return v2Bridge ? v2Bridge->readIndex(i,offset,buffer,count)
                            : ProgressiveFile::read(files[i], offset, buffer, count) == int64_t(count);
          },
          track.available, i, harvester::EndReady(track),
          value.timestampOffset, v2Bridge && v2Bridge->videoCodec()=="hevc",
          v2Bridge && v2Bridge->videoCodec()=="av1",v2Bridge && v2Bridge->videoCodec()=="vp9");
    end = std::min(
        end, double(harvester::ReadyEnd(track.fragmented ? indexes[i].Samples()
                                                         : indexed[i].samples(),
                                        track.available, duration)) /
                 10000000.);
    complete = complete && harvester::EndReady(track);
  }
  result.duration = double(duration) / 10000000.;
  result.timestampOffset = double(value.timestampOffset) / 10000000.;
  result.available = end;
  result.complete = complete;
  return result;
}
void ProgressiveSource::updateIndexes() {
  const auto value=indexStatus(snapshot);
  currentStatus.duration=value.duration;currentStatus.timestampOffset=value.timestampOffset;
  currentStatus.available=value.available;currentStatus.complete=value.complete;
}

void ProgressiveSource::fail(const QString &error) {
  std::lock_guard<std::mutex> lock(mutex);
  currentStatus.error = error;
  changed.notify_all();
}

namespace {
struct StreamReader {
  std::shared_ptr<ProgressiveSource> source;
  int track;
  uint64_t position = 0;
  std::atomic<bool> cancelled{false};
};
} // namespace
ProgressiveStreams::ProgressiveStreams(mpv_handle *handle, bool ownsSession) : ownsSession(ownsSession) {
  if (mpv_stream_cb_add_ro(handle, "foxicmp-session", this, open) < 0)
    throw std::runtime_error("Cannot register progressive stream callbacks");
}
void ProgressiveStreams::activate(
    const std::shared_ptr<ProgressiveSource> &next) {
  std::lock_guard<std::mutex> lock(mutex);
  if (source && ownsSession)
    source->cancel();
  source = next;
}
void ProgressiveStreams::clear() { activate({}); }
std::shared_ptr<ProgressiveSource> ProgressiveStreams::current() {
  std::lock_guard<std::mutex> lock(mutex); return source;
}
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
  if (!source || track < 0 || url.host() != source->streamId())
    return MPV_ERROR_LOADING_FAILED;
  auto *reader = new StreamReader{source, track};
  *info = {};
  info->cookie = reader;
  info->read_fn = [](void *cookie, char *buffer, uint64_t count) -> int64_t {
    auto *reader = static_cast<StreamReader *>(cookie);
    if (reader->cancelled.load())
      return -1;
    const auto result =
        reader->source->read(reader->track, reader->position, buffer, count, &reader->cancelled);
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
  };
  return 0;
}
