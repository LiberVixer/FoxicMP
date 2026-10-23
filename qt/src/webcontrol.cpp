#include "webcontrol.h"
#include "i18n.h"
#include <QTcpSocket>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <cmath>
#include <memory>
namespace {
constexpr int maximumRequest = 16384;
bool validCommand(const QString &name, const QJsonValue &value) {
  if (QStringList{"play", "pause", "toggle", "stop", "next", "previous", "mute"}.contains(name))
    return value.isUndefined();
  if (name == "open" || name == "enqueue")
    return value.isString() && !value.toString().trimmed().isEmpty() && value.toString().size() <= 4096 &&
        !value.toString().contains(QRegularExpression("[\\x00-\\x1f\\x7f]"));
  if (name == "queuePlay" || name == "queueRemove") {
    if (!value.isObject()) return false;
    const auto target = value.toObject();
    return target.size() == 2 && target.value("list").isString() && target.value("id").isString() &&
        !target.value("list").toString().isEmpty() && !target.value("id").toString().isEmpty() &&
        target.value("list").toString().size() <= 128 && target.value("id").toString().size() <= 128;
  }
  if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
  const double n = value.toDouble();
  return (name == "seek" && n >= 0 && n <= 604800) ||
         (name == "volume" && n >= 0 && n <= 100) ||
         (name == "rate" && n >= .125 && n <= 16);
}
}
WebControl::WebControl(Read reader, Command handler, QObject *parent, Snapshot image)
    : QObject(parent), read(std::move(reader)), command(std::move(handler)), snapshot(std::move(image)) {
  server.setProxy(QNetworkProxy::NoProxy);
  server.setMaxPendingConnections(8);
  connect(&server, &QTcpServer::newConnection, this, [this] {
    while (server.hasPendingConnections()) {
      auto *socket = server.nextPendingConnection();
      if ((lan() && !isPrivateAddress(socket->peerAddress()) && !socket->peerAddress().isLoopback()) || peers.size() >= 8) { socket->abort(); socket->deleteLater(); continue; }
      peers.insert(socket);
      socket->setReadBufferSize(maximumRequest + 1);
      auto *timer = new QTimer(socket);
      timer->setSingleShot(true); timer->start(3000);
      connect(timer, &QTimer::timeout, socket, &QTcpSocket::abort);
      connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
        peers.remove(socket); socket->deleteLater();
      });
      auto bytes = std::make_shared<QByteArray>();
      connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
        if (socket->property("answered").toBool()) return;
        bytes->append(socket->readAll());
        receive(socket, *bytes);
      });
    }
  });
}
WebControl::~WebControl() { stop(); }
bool WebControl::isPrivateAddress(const QHostAddress &address) {
  if (address.protocol() != QAbstractSocket::IPv4Protocol) return false;
  const auto n = address.toIPv4Address();
  return (n & 0xff000000) == 0x0a000000 ||
         (n & 0xfff00000) == 0xac100000 ||
         (n & 0xffff0000) == 0xc0a80000 ||
         (n & 0xffff0000) == 0xa9fe0000;
}
QList<WebControl::LanAddress> WebControl::lanAddresses() {
  QList<LanAddress> result;
  QSet<QString> seen;
  for (const auto &iface : QNetworkInterface::allInterfaces()) {
    const auto flags = iface.flags();
    if (!flags.testFlag(QNetworkInterface::IsUp) ||
        !flags.testFlag(QNetworkInterface::IsRunning) || flags.testFlag(QNetworkInterface::IsLoopBack)) continue;
    for (const auto &entry : iface.addressEntries()) {
      const auto address = entry.ip();
      if (!isPrivateAddress(address) || seen.contains(address.toString())) continue;
      seen.insert(address.toString());
      result.append({iface.humanReadableName(), address});
    }
  }
  return result;
}
bool WebControl::start(quint16 wanted, const QHostAddress &address) {
  if (address != QHostAddress(QHostAddress::LocalHost)) {
    bool available = false;
    for (const auto &entry : lanAddresses()) if (entry.address == address) available = true;
    if (!available) {
      error = uiTr("Выбранный частный IPv4-адрес отсутствует на активном сетевом интерфейсе.");
      return false;
    }
  }
  stop(); error.clear();
  if (!server.listen(address, wanted)) {
    error = server.errorString(); return false;
  }
  QByteArray random(32, '\0');
  for (int i = 0; i < random.size(); ++i)
    random[i] = char(QRandomGenerator::system()->generate() & 255);
  secret = QString::fromLatin1(random.toHex());
  return true;
}
void WebControl::stop() {
  server.close();
  const auto active = peers;
  for (auto *socket : active) socket->abort();
  peers.clear(); secret.clear();
}
QString WebControl::url() const {
  return running() ? QString("http://%1:%2/#%3").arg(address().toString()).arg(port()).arg(secret) : QString();
}
void WebControl::respond(QTcpSocket *socket, int status, const QByteArray &body,
                         const QByteArray &type) {
  socket->setProperty("answered", true);
  const auto reason = status == 200 ? "OK" : status == 400 ? "Bad Request"
                    : status == 403 ? "Forbidden" : status == 404 ? "Not Found"
                    : status == 405 ? "Method Not Allowed" : status == 413 ? "Payload Too Large"
                    : status == 409 ? "Conflict" : "Error";
  socket->write("HTTP/1.1 " + QByteArray::number(status) + " " + reason +
      "\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) +
      "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n\r\n" + body);
  socket->disconnectFromHost();
}
void WebControl::receive(QTcpSocket *socket, QByteArray &bytes) {
  const auto reject = [&](int status) { respond(socket, status, "{\"ok\":false}"); };
  if (bytes.size() > maximumRequest) { reject(413); return; }
  const int end = bytes.indexOf("\r\n\r\n");
  if (end < 0) return;
  const auto lines = bytes.left(end).split('\n');
  const auto first = lines.value(0).trimmed().split(' ');
  if (first.size() != 3 || first[2] != "HTTP/1.1" ||
      (first[0] != "GET" && first[0] != "POST")) { reject(405); return; }
  QMap<QByteArray, QByteArray> headers;
  for (int i = 1; i < lines.size(); ++i) {
    const auto line = lines[i];
    const int colon = line.indexOf(':');
    if (colon <= 0 || line.startsWith(' ') || line.startsWith('\t')) { reject(400); return; }
    const auto name = line.left(colon).toLower(), value = line.mid(colon + 1).trimmed();
    if (headers.contains(name)) { reject(400); return; }
    headers[name] = value;
  }
  const auto host = address().toString().toLatin1() + ":" + QByteArray::number(port());
  const auto localhost = "localhost:" + QByteArray::number(port());
  if (headers.value("host") != host && (lan() || headers.value("host") != localhost)) { reject(403); return; }
  if (headers.contains("origin") && headers.value("origin") != "http://" + headers.value("host")) { reject(403); return; }
  if (headers.contains("transfer-encoding") || headers.contains("expect")) { reject(400); return; }
  int length = 0;
  if (headers.contains("content-length")) {
    const auto value = headers.value("content-length");
    if (value.isEmpty() || value.size() > 5) { reject(400); return; }
    for (char c : value) if (c < '0' || c > '9') { reject(400); return; }
    length = value.toInt();
  }
  if (length + end + 4 > maximumRequest) { reject(413); return; }
  if (bytes.size() < end + 4 + length) return;
  if (bytes.size() != end + 4 + length || (first[0] == "GET" && length)) { reject(400); return; }
  const auto path = first[1];
  int offset = 0;
  const bool statePath = path == "/api/state" || path.startsWith("/api/state?");
  if (statePath && path != "/api/state") {
    const auto query = QUrlQuery(QUrl::fromEncoded(path));
    const auto items = query.queryItems();
    if (items.size() != 1 || items.first().first != "offset" ||
        !QRegularExpression("^[0-9]{1,7}$").match(items.first().second).hasMatch()) { reject(400); return; }
    offset = items.first().second.toInt();
  }
  const bool snapshotPath = path == "/api/snapshot.jpg";
  const bool api = statePath || path == "/api/command" || snapshotPath;
  if (api && headers.value("authorization") != "Bearer " + secret.toLatin1()) { reject(403); return; }
  if (first[0] == "GET" && snapshotPath) {
    if (!snapshot) { reject(404); return; }
    const auto image = snapshot();
    if (image.isEmpty()) { reject(409); return; }
    respond(socket, 200, image, "image/jpeg");
  } else if (first[0] == "GET" && !api) {
    const QString resource = path == "/" ? ":/web/index.html"
                           : path == "/app.js" ? ":/web/app.js"
                           : path == "/app.css" ? ":/web/app.css" : QString();
    if (resource.isEmpty()) { reject(404); return; }
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly)) { reject(404); return; }
    respond(socket, 200, file.readAll(), path == "/" ? "text/html; charset=utf-8"
           : path == "/app.js" ? "text/javascript; charset=utf-8" : "text/css; charset=utf-8");
  } else if (first[0] == "GET" && statePath) {
    respond(socket, 200, QJsonDocument(QJsonObject::fromVariantMap(read(offset))).toJson(QJsonDocument::Compact));
  } else if (first[0] == "POST" && path == "/api/command") {
    if (headers.value("content-type") != "application/json" || !headers.contains("content-length")) { reject(400); return; }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes.mid(end + 4), &error);
    const auto obj = doc.object();
    const auto name = obj.value("command").toString();
    if (error.error != QJsonParseError::NoError || !doc.isObject() ||
        obj.size() != (obj.contains("value") ? 2 : 1) ||
        !validCommand(name, obj.value("value"))) { reject(400); return; }
    if (!command(name, obj.value("value").toVariant())) { reject(409); return; }
    respond(socket, 200, "{\"ok\":true}");
  } else reject(405);
}
