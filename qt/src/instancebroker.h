#pragma once
#include <QJsonObject>
#include <QLocalServer>
#include <QLockFile>
#include <QQueue>
#include <functional>
#include <memory>

struct LaunchRequest {
  QStringList files;
  QString disc, session;
  QJsonObject options;
  bool enqueue = false, activate = true;
  bool needsExisting() const;
  QByteArray encode() const;
  static bool decode(const QByteArray &, LaunchRequest *,
                     QString *error = nullptr);
  static bool normalizeFiles(QStringList *, QString *error);
};

class InstanceBroker : public QObject {
  Q_OBJECT
public:
  enum Result { Primary, Independent, Forwarded, Failed };
  explicit InstanceBroker(const QString &profileDirectory,
                          QObject *parent = nullptr);
  ~InstanceBroker() override;
  Result start(const LaunchRequest &, bool reuse, bool requireExisting = false);
  void setReceiver(std::function<void(const LaunchRequest &)>);
  QString errorString() const { return error; }
  QString serverName() const { return endpoint; }
  static QString endpointFor(const QString &profileDirectory);

private:
  QLocalServer server;
  std::unique_ptr<QLockFile> lock;
  QString endpoint, error;
  QQueue<LaunchRequest> pending;
  std::function<void(const LaunchRequest &)> receiver;
  int clients = 0;
  bool pumpScheduled = false;
  void acceptClients();
  void scheduleDelivery();
  bool forward(const QByteArray &);
};
