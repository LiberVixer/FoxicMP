#include "progressivesource.h"
#include "../../shared/progressivereaderlease.h"
#include <QUrl>
#include <stdexcept>

// The downloader/provider contract remains Linux-only. Ordinary Windows media
// playback uses the same libmpv backend and does not instantiate these sources.
std::shared_ptr<ProgressiveSource> ProgressiveSource::open(const QString &) {
  throw std::runtime_error("The local downloader protocol is available only on Linux");
}
std::shared_ptr<ProgressiveSource> ProgressiveSource::openPublished(const QJsonObject &) {
  throw std::runtime_error("The local downloader protocol is available only on Linux");
}
ProgressiveSource::~ProgressiveSource() = default;
bool ProgressiveSource::publish(const QJsonObject &, QString *error) {
  if (error) *error = "The local downloader protocol is available only on Linux";
  return false;
}
void ProgressiveSource::publishedHeartbeat() {}
void ProgressiveSource::publishedError(const QString &error) { currentStatus.error = error; }
void ProgressiveSource::cancel() { currentStatus.cancelled = true; }
void ProgressiveSource::notify(const QString &) {}
ProgressiveSource::Status ProgressiveSource::status() const { return currentStatus; }
QString ProgressiveSource::id() const { return {}; }
int64_t ProgressiveSource::read(int, uint64_t, char *, uint64_t) { return -1; }
int64_t ProgressiveSource::size(int) const { return -1; }
bool ProgressiveSource::seekable(int, uint64_t) const { return false; }

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
