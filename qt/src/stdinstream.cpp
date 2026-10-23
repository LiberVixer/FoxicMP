#include "stdinstream.h"
#include "i18n.h"
#include <QUuid>
#include <atomic>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

struct StdinStream::State {
  int descriptor = -1;
  QString url;
  bool opened = false; // protected by StdinStream::mutex
  std::atomic<bool> cancelled{false};
  ~State() { if (descriptor >= 0) ::close(descriptor); }
};
StdinStream::StdinStream(mpv_handle *handle) {
  if (mpv_stream_cb_add_ro(handle, "foxicmp-stdin", this, open) < 0)
    throw std::runtime_error("Cannot register stdin stream callbacks");
}
StdinStream::~StdinStream() { clear(); }
bool StdinStream::supported(int fd, QString *error) {
  struct stat info{};
  const int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || (flags & O_ACCMODE) == O_WRONLY || fstat(fd, &info) < 0 ||
      (!S_ISREG(info.st_mode) && !S_ISFIFO(info.st_mode) && !S_ISSOCK(info.st_mode))) {
    if (error) *error = uiTr("stdin должен быть читаемым файлом или каналом, а не терминалом.");
    return false;
  }
  return true;
}
QString StdinStream::attach(int fd, QString *error) {
  if (!supported(fd, error)) return {};
  const int duplicate = fcntl(fd, F_DUPFD_CLOEXEC, 3);
  if (duplicate < 0) {
    if (error) *error = uiTr("Не удалось открыть stdin для воспроизведения.");
    return {};
  }
  auto next = std::make_shared<State>();
  next->descriptor = duplicate;
  next->url = "foxicmp-stdin://" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  clear();
  std::lock_guard<std::mutex> lock(mutex);
  source = next;
  return next->url;
}
void StdinStream::clear() {
  std::lock_guard<std::mutex> lock(mutex);
  if (source) source->cancelled.store(true);
  source.reset();
}
void StdinStream::replacing(const QString &url) {
  std::lock_guard<std::mutex> lock(mutex);
  if (source && source->url != url) {
    source->cancelled.store(true);
    source.reset();
  }
}
int StdinStream::open(void *self, char *uri, mpv_stream_cb_info *info) {
  auto *streams = static_cast<StdinStream *>(self);
  std::shared_ptr<State> state;
  {
    std::lock_guard<std::mutex> lock(streams->mutex);
    state = streams->source;
    if (!state || state->opened || state->cancelled.load() || state->url != QString::fromUtf8(uri))
      return MPV_ERROR_LOADING_FAILED;
    state->opened = true;
  }
  *info = {};
  info->cookie = new std::shared_ptr<State>(state);
  info->read_fn = [](void *cookie, char *buffer, uint64_t count) -> int64_t {
    const auto &state = *static_cast<std::shared_ptr<State> *>(cookie);
    if (!count) return 0;
    while (!state->cancelled.load()) {
      pollfd descriptor{state->descriptor, POLLIN, 0};
      const int ready = poll(&descriptor, 1, 100);
      if (ready < 0) { if (errno == EINTR) continue; return -1; }
      if (!ready) continue;
      if (state->cancelled.load() || (descriptor.revents & (POLLERR | POLLNVAL))) return -1;
      const auto result = ::read(state->descriptor, buffer, std::min<uint64_t>(count, 64 * 1024));
      if (result < 0 && (errno == EINTR || errno == EAGAIN)) continue;
      return result;
    }
    return -1;
  };
  info->close_fn = [](void *cookie) {
    auto *reader = static_cast<std::shared_ptr<State> *>(cookie);
    // close_fn runs only after read/cancel callbacks have finished.
    if ((*reader)->descriptor >= 0) ::close((*reader)->descriptor);
    (*reader)->descriptor = -1;
    delete reader;
  };
  info->cancel_fn = [](void *cookie) {
    (*static_cast<std::shared_ptr<State> *>(cookie))->cancelled.store(true);
  };
  return 0;
}
