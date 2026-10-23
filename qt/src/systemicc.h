#pragma once
#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <functional>
class Player;
class QTimer;
class QWindow;

// Read-only colord lookup. Settings remain owned by VideoDisplay; asynchronous
// replies never retain a QSettings reference or write a monitor into the INI.
class SystemIcc : public QObject {
  Q_OBJECT
  Player *player;
  QDBusConnection bus;
  QString platform, directory, output;
  QTimer *timer;
  QElapsedTimer checked;
  QPointer<QWindow> window;
  QMetaObject::Connection screenConnection;
  bool active = false, pending = false, followWindow;
  quint64 generation = 0;
  void tick();
  void ask(quint64, const QString &, const QString &, const QString &,
           const QVariantList &, std::function<void(const QDBusMessage &)>);
  void finish(quint64, const QString &error, const QString &source = {});

public:
  explicit SystemIcc(Player *,
                     const QDBusConnection & = QDBusConnection::systemBus(),
                     const QString &platform = {}, bool followWindow = true);
  void configure(bool enabled, const QString &profileDirectory);
  void refresh(const QString &screenName);
};
