#include "stdinstream.h"
#include "i18n.h"
#include <QUuid>
#include <atomic>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <windows.h>
#include <io.h>
#include <stdexcept>
#include <sys/stat.h>


struct StdinStream::State {
  HANDLE input = INVALID_HANDLE_VALUE;
  QString url;
  bool opened = false; // protected by StdinStream::mutex
  std::atomic<bool> cancelled{false};
  ~State() { if (input != INVALID_HANDLE_VALUE) CloseHandle(input); }
};
StdinStream::StdinStream(mpv_handle *handle) {
  if (mpv_stream_cb_add_ro(handle, "foxicmp-stdin", this, open) < 0)
    throw std::runtime_error("Cannot register stdin stream callbacks");
}
StdinStream::~StdinStream() { clear(); }
bool StdinStream::supported(int fd, QString *error) {
  const auto native = _get_osfhandle(fd);
  const DWORD type = native == -1 ? FILE_TYPE_UNKNOWN : GetFileType(reinterpret_cast<HANDLE>(native));
  if (type != FILE_TYPE_DISK && type != FILE_TYPE_PIPE) {
    if (error) *error = uiTr("stdin должен быть читаемым файлом или каналом, а не терминалом.");
    return false;
  }
  return true;
}
QString StdinStream::attach(int fd, QString *error) {
  if (!supported(fd, error)) return {};
  HANDLE duplicate = INVALID_HANDLE_VALUE;
  if (!DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(_get_osfhandle(fd)),
      GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
    if (error) *error = uiTr("Не удалось открыть stdin для воспроизведения.");
    return {};
  }
  auto next = std::make_shared<State>();
  next->input = duplicate;
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
      const auto handle = state->input;
      uint64_t ready = std::min<uint64_t>(count, 64 * 1024);
      if (GetFileType(handle) == FILE_TYPE_PIPE) {
        DWORD available = 0;
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr))
          return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
        if (!available) { Sleep(50); continue; }
        ready = std::min<uint64_t>(ready, available);
      }
      DWORD received = 0;
      if (!ReadFile(handle, buffer, DWORD(ready), &received, nullptr))
        return GetLastError() == ERROR_BROKEN_PIPE || GetLastError() == ERROR_HANDLE_EOF ? 0 : -1;
      return received;
    }
    return -1;
  };
  info->close_fn = [](void *cookie) {
    auto *reader = static_cast<std::shared_ptr<State> *>(cookie);
    // close_fn runs only after read/cancel callbacks have finished.
    if ((*reader)->input != INVALID_HANDLE_VALUE) CloseHandle((*reader)->input);
    (*reader)->input = INVALID_HANDLE_VALUE;
    delete reader;
  };
  info->cancel_fn = [](void *cookie) {
    (*static_cast<std::shared_ptr<State> *>(cookie))->cancelled.store(true);
  };
  return 0;
}
