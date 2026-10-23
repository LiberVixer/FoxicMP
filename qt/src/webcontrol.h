#pragma once
#include <QObject>
#include <QSet>
#include <QVariantMap>
#include <QTcpServer>
#include <functional>
class QTcpSocket;
class WebControl : public QObject {
  Q_OBJECT
public:
  using Read = std::function<QVariantMap(int)>;
  using Command = std::function<bool(const QString &, const QVariant &)>;
  using Snapshot = std::function<QByteArray()>;
  WebControl(Read, Command, QObject *parent = nullptr, Snapshot snapshot = {});
  ~WebControl() override;
  struct LanAddress { QString name; QHostAddress address; };
  static bool isPrivateAddress(const QHostAddress &);
  static QList<LanAddress> lanAddresses();
  bool start(quint16 port, const QHostAddress &address = QHostAddress::LocalHost);
  QHostAddress address() const { return server.serverAddress(); }
  bool lan() const { return running() && address() != QHostAddress(QHostAddress::LocalHost); }
  void stop();
  bool running() const { return server.isListening(); }
  quint16 port() const { return server.serverPort(); }
  QString token() const { return secret; }
  QString url() const;
  QString errorString() const { return error; }
private:
  void receive(QTcpSocket *, QByteArray &);
  void respond(QTcpSocket *, int status, const QByteArray &body,
               const QByteArray &type = "application/json; charset=utf-8");
  QTcpServer server;
  QSet<QTcpSocket *> peers;
  Read read;
  Command command;
  Snapshot snapshot;
  QString secret, error;
};
