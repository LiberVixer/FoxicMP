#include "mediaremote.h"
#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QTimer>

namespace {
constexpr auto objectPath = "/org/mpris/MediaPlayer2";
class CommandAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.foxicmp.Control1")
  Q_PROPERTY(QVariantMap Commands READ commands CONSTANT)
public:
  explicit CommandAdaptor(MediaRemote *remote)
      : QDBusAbstractAdaptor(remote), remote(remote) {}
  QVariantMap commands() const { return remote->commands(); }
public slots:
  bool Trigger(const QString &command) {
    if (!remote->commands().contains(command))
      return false;
    remote->invoke("Command", {command});
    return true;
  }

private:
  MediaRemote *remote;
};
class RootAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
  Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
  Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
  Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
  Q_PROPERTY(QString Identity READ identity CONSTANT)
  Q_PROPERTY(bool Fullscreen READ fullscreen WRITE setFullscreen)
  Q_PROPERTY(bool CanSetFullscreen READ canSetFullscreen CONSTANT)
  Q_PROPERTY(QStringList SupportedUriSchemes READ schemes CONSTANT)
  Q_PROPERTY(QStringList SupportedMimeTypes READ mimeTypes CONSTANT)
public:
  explicit RootAdaptor(MediaRemote *remote)
      : QDBusAbstractAdaptor(remote), remote(remote) {}
  bool canQuit() const { return true; }
  bool canRaise() const { return true; }
  bool hasTrackList() const { return false; }
  QString identity() const { return "FoxicMP Qt"; }
  bool fullscreen() const {
    return remote->state().value("Fullscreen").toBool();
  }
  void setFullscreen(bool value) { remote->invoke("Fullscreen", {value}); }
  bool canSetFullscreen() const { return true; }
  QStringList schemes() const { return {"file", "http", "https"}; }
  QStringList mimeTypes() const {
    return {"video/mp4",       "video/x-matroska", "video/webm",
            "video/x-msvideo", "video/quicktime",  "video/mpeg",
            "video/ogg",       "video/mp2t",       "audio/mpeg",
            "audio/flac",      "audio/ogg",        "audio/mp4",
            "audio/wav"};
  }
public slots:
  void Raise() { remote->invoke("Raise"); }
  void Quit() { remote->invoke("Quit"); }

private:
  MediaRemote *remote;
};
class PlayerAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
  Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
  Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
  Q_PROPERTY(double Rate READ rate WRITE setRate)
  Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
  Q_PROPERTY(QVariantMap Metadata READ metadata)
  Q_PROPERTY(double Volume READ volume WRITE setVolume)
  Q_PROPERTY(qlonglong Position READ position)
  Q_PROPERTY(double MinimumRate READ minimumRate)
  Q_PROPERTY(double MaximumRate READ maximumRate)
  Q_PROPERTY(bool CanGoNext READ canGoNext)
  Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
  Q_PROPERTY(bool CanPlay READ canPlay)
  Q_PROPERTY(bool CanPause READ canPause)
  Q_PROPERTY(bool CanSeek READ canSeek)
  Q_PROPERTY(bool CanControl READ canControl CONSTANT)
public:
  explicit PlayerAdaptor(MediaRemote *remote)
      : QDBusAbstractAdaptor(remote), remote(remote) {
    connect(remote, &MediaRemote::positionJumped, this, &PlayerAdaptor::Seeked);
  }
  QString playbackStatus() const { return value("PlaybackStatus").toString(); }
  QString loopStatus() const { return value("LoopStatus").toString(); }
  void setLoopStatus(const QString &status) {
    remote->invoke("LoopStatus", {status});
  }
  double rate() const { return value("Rate").toDouble(); }
  void setRate(double rate) { remote->invoke("Rate", {rate}); }
  bool shuffle() const { return value("Shuffle").toBool(); }
  void setShuffle(bool shuffle) { remote->invoke("Shuffle", {shuffle}); }
  QVariantMap metadata() const { return value("Metadata").toMap(); }
  double volume() const { return value("Volume").toDouble(); }
  void setVolume(double volume) { remote->invoke("Volume", {volume}); }
  qlonglong position() const { return value("Position").toLongLong(); }
  double minimumRate() const { return value("MinimumRate").toDouble(); }
  double maximumRate() const { return value("MaximumRate").toDouble(); }
  bool canGoNext() const { return value("CanGoNext").toBool(); }
  bool canGoPrevious() const { return value("CanGoPrevious").toBool(); }
  bool canPlay() const { return value("CanPlay").toBool(); }
  bool canPause() const { return value("CanPause").toBool(); }
  bool canSeek() const { return value("CanSeek").toBool(); }
  bool canControl() const { return true; }
public slots:
  void Next() { remote->invoke("Next"); }
  void Previous() { remote->invoke("Previous"); }
  void Pause() { remote->invoke("Pause"); }
  void PlayPause() { remote->invoke("PlayPause"); }
  void Stop() { remote->invoke("Stop"); }
  void Play() { remote->invoke("Play"); }
  void Seek(qlonglong offset) { remote->invoke("Seek", {offset}); }
  void SetPosition(const QDBusObjectPath &track, qlonglong position) {
    remote->invoke("SetPosition", {track.path(), position});
  }
  void OpenUri(const QString &uri) { remote->invoke("OpenUri", {uri}); }
signals:
  void Seeked(qlonglong Position);

private:
  QVariant value(const char *key) const { return remote->state().value(key); }
  MediaRemote *remote;
};
} // namespace

MediaRemote::MediaRemote(Read read, Control control, QObject *parent)
    : QObject(parent), read(std::move(read)), control(std::move(control)),
      timer(new QTimer(this)) {
  new RootAdaptor(this);
  new CommandAdaptor(this);
  new PlayerAdaptor(this);
  timer->setInterval(250);
  connect(timer, &QTimer::timeout, this, &MediaRemote::publish);
}
MediaRemote::~MediaRemote() { setEnabled(false); }
void MediaRemote::setEnabled(bool enabled) {
  if (enabled == !service.isEmpty())
    return;
  if (!enabled) {
    timer->stop();
    if (!connectionName.isEmpty()) {
      auto bus = QDBusConnection(connectionName);
      bus.unregisterService(service);
      bus.unregisterObject(objectPath);
      QDBusConnection::disconnectFromBus(connectionName);
    }
    connectionName.clear();
    service.clear();
    previous.clear();
    return;
  }
  static quint64 serial = 0;
  const auto suffix = QString("instance%1_%2")
                          .arg(QCoreApplication::applicationPid())
                          .arg(++serial);
  connectionName = "foxicmp-mpris-" + suffix;
  // Honor the current session address, including isolated test/desktop
  // sessions. The enum overload may retain a session address cached by another
  // Qt service.
  const auto address = qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS");
  auto bus = address.isEmpty()
                 ? QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                 connectionName)
                 : QDBusConnection::connectToBus(address, connectionName);
  if (bus.isConnected() &&
      bus.registerObject(objectPath, this, QDBusConnection::ExportAdaptors)) {
    service = "org.mpris.MediaPlayer2.FoxicMP";
    if (!bus.registerService(service)) {
      service += "." + suffix;
      if (!bus.registerService(service))
        service.clear();
    }
  }
  if (service.isEmpty()) {
    error = bus.lastError().message();
    bus.unregisterObject(objectPath);
    QDBusConnection::disconnectFromBus(connectionName);
    connectionName.clear();
    return;
  }
  error.clear();
  previous.clear();
  publish();
  timer->start();
}
void MediaRemote::invoke(const QString &command,
                         const QVariantList &arguments) {
  control(command, arguments);
  publish();
}
void MediaRemote::publish() {
  if (service.isEmpty())
    return;
  const auto current = state();
  for (const auto &interface : {QString("org.mpris.MediaPlayer2"),
                                QString("org.mpris.MediaPlayer2.Player")}) {
    QVariantMap changed;
    for (auto it = current.begin(); it != current.end(); ++it) {
      if (it.key() == "Position" || ((it.key() == "Fullscreen") !=
                                     (interface == "org.mpris.MediaPlayer2")))
        continue;
      if (!previous.contains(it.key()) ||
          previous.value(it.key()) != it.value())
        changed[it.key()] = it.value();
    }
    if (!changed.isEmpty()) {
      auto signal = QDBusMessage::createSignal(
          objectPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
      signal << interface << changed << QStringList{};
      QDBusConnection(connectionName).send(signal);
    }
  }
  previous = current;
}
void MediaRemote::seeked() {
  if (!service.isEmpty())
    emit positionJumped(state().value("Position").toLongLong());
}
#include "mediaremote.moc"
