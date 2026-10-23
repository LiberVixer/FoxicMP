#pragma once
#include <QString>
#include <memory>
#include <mutex>
#include <mpv/stream_cb.h>

// One read-only, non-seekable descriptor for one explicit stdin launch.
// Callbacks never call libmpv; cancellation wakes blocked reads within 100 ms.
class StdinStream {
public:
  explicit StdinStream(mpv_handle *);
  ~StdinStream();
  static bool supported(int descriptor, QString *error = nullptr);
  QString attach(int descriptor, QString *error = nullptr);
  void clear();
  void replacing(const QString &url);
private:
  struct State;
  std::shared_ptr<State> source;
  std::mutex mutex;
  static int open(void *, char *, mpv_stream_cb_info *);
};
