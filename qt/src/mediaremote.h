#pragma once
#include <QObject>
#include <QVariantMap>
#include <functional>
class QTimer;
class MediaRemote : public QObject {
  Q_OBJECT
public:
  using Read = std::function<QVariantMap()>;
  using Control = std::function<void(const QString &, const QVariantList &)>;
  MediaRemote(Read read, Control control, QObject *parent = nullptr);
  ~MediaRemote() override;
  void setEnabled(bool enabled);
  QString serviceName() const { return service; }
  QString errorString() const { return error; }
  QVariantMap state() const { return read(); }
  void invoke(const QString &command, const QVariantList &arguments = {});
  void publish();
  void setCommands(const QVariantMap &commands) { commandCatalog = commands; }
  QVariantMap commands() const { return commandCatalog; }
  void seeked();
signals:
  void positionJumped(qlonglong position);

private:
  Read read;
  Control control;
  QString connectionName, service, error;
  QTimer *timer;
  QVariantMap previous, commandCatalog;
};
