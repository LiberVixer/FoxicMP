#include "instancebroker.h"
#include "launchoptions.h"
#include "devicesource.h"
#include "i18n.h"
#include "mediasource.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <unistd.h>

namespace {
constexpr int limit = 256 * 1024;
QByteArray framed(const QByteArray &data) {
  QByteArray out(4, '\0');
  qToBigEndian<quint32>(data.size(), out.data());
  return out + data;
}
bool localPath(const QString &path) {
  return !path.contains(QChar(0)) && path.toUtf8().size() <= 8192 &&
         QFileInfo(path).isAbsolute();
}
} // namespace
QByteArray LaunchRequest::encode() const {
  QJsonObject object{{"version", 1},
                                   {"files", QJsonArray::fromStringList(files)},
                                   {"disc", disc},
                                   {"session", session},
                                   {"enqueue", enqueue},
                                   {"activate", activate}};
  if (!options.isEmpty()) object["options"] = options;
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
bool LaunchRequest::needsExisting() const {
  return options.contains("command") ||
      (files.isEmpty() && disc.isEmpty() && session.isEmpty() && LaunchOptions::hasPlayback(options));
}
bool LaunchRequest::normalizeFiles(QStringList *files, QString *error) {
  QStringList normalized;
  for (const auto &file : *files) {
    const auto path = MediaSource::normalize(file, error);
    if (path.isEmpty())
      return false;
    normalized << path;
  }
  *files = normalized;
  return true;
}
bool LaunchRequest::decode(const QByteArray &data, LaunchRequest *out,
                           QString *error) {
  const auto fail = [&] {
    if (error)
      *error = uiTr("Неверный запрос запуска или превышен лимит передачи.");
    return false;
  };
  if (!out || data.size() > limit)
    return fail();
  QJsonParseError parse;
  const auto doc = QJsonDocument::fromJson(data, &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject())
    return fail();
  const auto object = doc.object();
  if ((object.size() != 6 && object.size() != 7) ||
      (object.size() == 6 && object.contains("options")) ||
      (object.size() == 7 && !object.value("options").isObject()) ||
      object.value("version") != QJsonValue(1) ||
      !object.value("files").isArray() || !object.value("disc").isString() ||
      !object.value("session").isString() ||
      !object.value("enqueue").isBool() || !object.value("activate").isBool() ||
      object.value("files").toArray().size() > 256)
    return fail();
  LaunchRequest request;
  for (const auto &v : object.value("files").toArray()) {
    const auto file = v.toString();
    QString failure;
    if (!v.isString() || file.isEmpty() || file.contains(QChar(0)) ||
        file.toUtf8().size() > 8192 ||
        (!MediaSource::isUrl(file) && !QFileInfo(file).isAbsolute()) ||
        MediaSource::normalize(file, &failure).isEmpty())
      return fail();
    request.files << file;
  }
  request.disc = object.value("disc").toString();
  request.session = object.value("session").toString();
  request.enqueue = object.value("enqueue").toBool();
  request.activate = object.value("activate").toBool();
  request.options = object.value("options").toObject();
  if (!LaunchOptions::validate(request.options) ||
      (request.enqueue && !request.options.isEmpty()) ||
      ((!request.disc.isEmpty() || !request.session.isEmpty()) && LaunchOptions::hasPlayback(request.options)) ||
      (request.options.contains("command") && (!request.files.isEmpty() || !request.disc.isEmpty() || !request.session.isEmpty())))
    return fail();
  if ((!request.disc.isEmpty() &&
       (!request.files.isEmpty() || !request.session.isEmpty() ||
        request.enqueue)) ||
      (!request.session.isEmpty() &&
       (!request.files.isEmpty() || request.enqueue ||
        !localPath(request.session))))
    return fail();
  if (!request.disc.isEmpty()) {
    DeviceSource::Request device;
    if (request.disc.toUtf8().size() > 8192 ||
        !DeviceSource::decode(request.disc, &device))
      return fail();
  }
  *out = request;
  return true;
}
QString InstanceBroker::endpointFor(const QString &directory) {
  const QFileInfo info(directory);
  const auto canonical = info.canonicalFilePath().isEmpty()
                             ? info.absoluteFilePath()
                             : info.canonicalFilePath();
  const auto id =
      QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256)
          .toHex()
          .left(32);
  return QDir(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation))
      .filePath(QString("FoxicMPQt-%1-%2.sock")
                    .arg(getuid())
                    .arg(QString::fromLatin1(id)));
}
InstanceBroker::InstanceBroker(const QString &directory, QObject *parent)
    : QObject(parent), server(this), endpoint(endpointFor(directory)) {
  lock = std::make_unique<QLockFile>(endpoint + ".lock");
  lock->setStaleLockTime(
      0); // Long-lived lease: a live owner is never aged out.
  server.setSocketOptions(QLocalServer::UserAccessOption);
  server.setMaxPendingConnections(8);
  connect(&server, &QLocalServer::newConnection, this,
          &InstanceBroker::acceptClients);
}
InstanceBroker::~InstanceBroker() { server.close(); }
InstanceBroker::Result InstanceBroker::start(const LaunchRequest &request,
                                             bool reuse, bool requireExisting) {
  LaunchRequest validated;
  const auto bytes = request.encode();
  if (!LaunchRequest::decode(bytes, &validated, &error))
    return Failed;
  const auto runtime =
      QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (runtime.isEmpty() || !QDir(runtime).exists()) {
    error = uiTr("Недоступна папка временного IPC текущего пользователя.");
    return Failed;
  }
  if (lock->tryLock(0)) {
    if (requireExisting) {
      lock->unlock();
      error = uiTr("Нет работающего окна выбранного профиля для этой команды.");
      return Failed;
    }
    // The lease authorizes removing only our own orphaned endpoint. Never
    // remove a socket while another profile owner holds the lease.
    QLocalServer::removeServer(endpoint);
    if (!server.listen(endpoint)) {
      error = server.errorString();
      lock->unlock();
      return Failed;
    }
    pending.enqueue(validated);
    scheduleDelivery();
    return Primary;
  }
  if (lock->error() != QLockFile::LockFailedError) {
    error = uiTr("Не удалось получить блокировку экземпляра FoxicMP.");
    return Failed;
  }
  if (reuse)
    return forward(bytes) ? Forwarded : Failed;
  pending.enqueue(validated);
  scheduleDelivery();
  return Independent;
}
bool InstanceBroker::forward(const QByteArray &data) {
  QElapsedTimer elapsed;
  elapsed.start();
  QLocalSocket socket;
  // Retry connection while the elected owner initializes. Once a request has
  // been sent, do not resend or create an unrequested second playback window.
  while (elapsed.elapsed() < 3000) {
    socket.connectToServer(endpoint);
    if (socket.waitForConnected(100))
      break;
    socket.abort();
    QThread::msleep(20);
  }
  if (socket.state() != QLocalSocket::ConnectedState) {
    error =
        uiTr("Работающий экземпляр FoxicMP недоступен для передачи файлов.");
    return false;
  }
  const auto packet = framed(data);
  if (socket.write(packet) != packet.size() ||
      (socket.bytesToWrite() && !socket.waitForBytesWritten(1000))) {
    error = uiTr("Не удалось передать запрос работающему плееру.");
    return false;
  }
  QByteArray reply;
  while (elapsed.elapsed() < 5000) {
    reply += socket.readAll();
    if (reply.size() > 4096)
      break;
    if (reply.size() >= 4) {
      const auto size = qFromBigEndian<quint32>(reply.constData());
      if (size > 4092)
        break;
      if (reply.size() >= int(size) + 4) {
        const auto object =
            QJsonDocument::fromJson(reply.mid(4, size)).object();
        if (object.value("accepted").toBool())
          return true;
        break;
      }
    }
    if (socket.state() == QLocalSocket::UnconnectedState)
      break;
    socket.waitForReadyRead(100);
  }
  error = uiTr("Работающий плеер не подтвердил приём запроса.");
  return false;
}
void InstanceBroker::setReceiver(
    std::function<void(const LaunchRequest &)> fn) {
  receiver = std::move(fn);
  scheduleDelivery();
}
void InstanceBroker::scheduleDelivery() {
  if (!receiver || pending.isEmpty() || pumpScheduled)
    return;
  pumpScheduled = true;
  QTimer::singleShot(0, this, [this] {
    // Keep the guard while delivering: a modal handler can run a nested
    // event loop. Peers still receive ACKs, but dispatch must stay sequential.
    if (receiver && !pending.isEmpty())
      receiver(pending.dequeue());
    pumpScheduled = false;
    scheduleDelivery();
  });
}
void InstanceBroker::acceptClients() {
  while (server.hasPendingConnections()) {
    auto *socket = server.nextPendingConnection();
    socket->setParent(this);
    if (clients >= 8) {
      socket->abort();
      socket->deleteLater();
      continue;
    }
    ++clients;
    socket->setReadBufferSize(limit + 5);
    auto buffer = std::make_shared<QByteArray>();
    auto settled = std::make_shared<bool>(false);
    auto *deadline = new QTimer(socket);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, socket, [socket] { socket->abort(); });
    connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
      --clients;
      socket->deleteLater();
    });
    const auto read = [this, socket, buffer, settled, deadline] {
      if (*settled)
        return;
      buffer->append(socket->readAll());
      if (buffer->size() < 4)
        return;
      const auto size = qFromBigEndian<quint32>(buffer->constData());
      bool accepted = false;
      if (size > limit || buffer->size() > limit + 4) {
        *settled = true;
      } else if (buffer->size() >= int(size) + 4) {
        *settled = true;
        LaunchRequest request;
        if (buffer->size() == int(size) + 4 && pending.size() < 32 &&
            LaunchRequest::decode(buffer->mid(4, size), &request)) {
          pending.enqueue(request);
          accepted = true;
        }
      } else
        return;
      deadline->stop();
      socket->write(framed(QJsonDocument(QJsonObject{{"accepted", accepted}})
                               .toJson(QJsonDocument::Compact)));
      socket->flush();
      socket->disconnectFromServer();
      scheduleDelivery();
    };
    connect(socket, &QLocalSocket::readyRead, this, read);
    deadline->start(3000);
    read();
  }
}
