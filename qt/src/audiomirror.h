#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <mpv/client.h>
class Player;
class ProgressiveStreams;
// An audio-only decoder follows the primary playback clock. It never captures
// system audio and never changes another application's mixer or output.
class AudioMirror : public QObject {
public:
  AudioMirror(Player *, const QString &device, std::function<void(bool, const QString &)> status,
              const char *audioBackend = "wasapi");
  ~AudioMirror() override;
  void synchronize();
  bool active() const { return running; }
  double driftSeconds() const { return drift; }
  QVariantMap diagnostics() const;
private:
  void stop();
  void report(bool, const QString &);
  bool set(const char *, const QString &);
  double number(const char *) const;
  void command(const QStringList &);
  Player *player;
  mpv_handle *handle = nullptr;
  std::unique_ptr<ProgressiveStreams> streams;
  QTimer timer;
  std::function<void(bool, const QString &)> status;
  QString key, lastMessage, failedKey;
  QByteArray lastFilters;
  bool loaded = false, running = false;
  double drift = 0;
};
