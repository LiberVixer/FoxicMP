#pragma once
#include "DSUtil/HarvesterMP4.h"
#include "indexedmp4.h"
#include <QString>
#include <QJsonObject>
#include <QStringList>
#include <chrono>
#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mpv/stream_cb.h>
#include <mutex>
#include <thread>

// Local confirmed-prefix reader. The decoding bridge must keep a shared_ptr
// until both streams are closed; only the last owner releases the provider
// lease. No libmpv call is made from this worker or a blocking reader callback.
class ProgressiveSource {
public:
  struct Status {
    bool connected = false, complete = false, cancelled = false;
    double duration = 0, available = 0, timestampOffset = 0;
    QString error;
  };
  static std::shared_ptr<ProgressiveSource> open(const QString &manifest);
  // Player-owned published-prefix session, without a downloader-side server.
  static std::shared_ptr<ProgressiveSource> openPublished(const QJsonObject &);
  static QJsonObject probePublished(const QJsonObject &);
  bool publish(const QJsonObject &, QString *error);
  void publishedHeartbeat();
  void publishedError(const QString &error);
  ~ProgressiveSource();
  void cancel();
  void notify(const QString &state);
  Status status() const;
  // Short reads are legal. 0 means confirmed complete EOF, -1 means error or
  // cancellation. Unpublished bytes wait without a timeout-to-EOF conversion.
  int64_t read(int track, uint64_t offset, char *buffer, uint64_t count, const std::atomic<bool> *interrupted = nullptr);
  int64_t size(int track) const;
  bool seekable(int track, uint64_t offset) const;
  QString id() const;

private:
  explicit ProgressiveSource(const QString &manifest);
  explicit ProgressiveSource(const QJsonObject &);
  void watchPublished();
  void watch();
  void refresh();
  bool connectProvider();
  bool send(const char *type);
  void fail(const QString &error);
  void updateIndexes();
  QString manifest, root;
  harvester::Snapshot snapshot;
  std::array<intptr_t, 2> files{{-1, -1}};
  std::array<uint64_t, 2> devices{}, inodes{};
  std::array<harvester::FragmentIndex, 2> indexes;
  std::array<IndexedMp4, 2> indexed;
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::thread worker;
  std::atomic<bool> stopping{false};
  Status currentStatus;
  QString playbackState = "buffering";
  QString readerId;
  int socket = -1;
  bool attached = false;
  bool published = false;
  std::chrono::steady_clock::time_point lastPublished;
  QStringList publishedPaths;
};

class ProgressiveStreams {
public:
  explicit ProgressiveStreams(mpv_handle *handle, bool ownsSession = true);
  std::shared_ptr<ProgressiveSource> current();
  void activate(const std::shared_ptr<ProgressiveSource> &source);
  void clear();

private:
  static int open(void *self, char *uri, mpv_stream_cb_info *info);
  std::mutex mutex;
  bool ownsSession;
  std::shared_ptr<ProgressiveSource> source;
};
