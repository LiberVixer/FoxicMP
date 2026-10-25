#include "progressiveapi.h"
#include "instancebroker.h"
#include "progressivesource.h"
#include "window.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTimer>
#include <QDir>
#include <QCoreApplication>
#include "platformpaths.h"
#ifndef Q_OS_WIN
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
constexpr int limit = 65536;
QJsonObject failure(const QString &code, const QString &message) {
  return {{"version", 1}, {"ok", false}, {"code", code}, {"error", message}};
}
bool keys(const QJsonObject &value, const QStringList &required, const QStringList &optional = {}) {
  for (const auto &key : required) if (!value.contains(key)) return false;
  for (auto it = value.begin(); it != value.end(); ++it)
    if (!required.contains(it.key()) && !optional.contains(it.key())) return false;
  return true;
}
}
ProgressiveApi::ProgressiveApi(Window *w, QObject *parent) : QObject(parent), window(w) {
  server.setSocketOptions(QLocalServer::UserAccessOption);
  server.setMaxPendingConnections(16);
  connect(&server, &QLocalServer::newConnection, this, &ProgressiveApi::acceptClients);
  connect(window, &Window::progressiveSessionEnded, this,
          [this](const QString &id, const QString &state, const QString &error) {
    if (id != sessionId) return;
    lastOutcome = {{"version", 1}, {"ok", true}, {"active", false},
                   {"sessionId", id}, {"state", state},
                   {"error", error}};
    source.reset();
  });
}
ProgressiveApi::~ProgressiveApi() { server.close(); }
QString ProgressiveApi::endpointFor(const QString &directory) {
  return InstanceBroker::endpointFor(directory) + ".progressive";
}
QJsonObject ProgressiveApi::information(const QString &directory) {
  return {{"version", 1}, {"transport",
#ifdef Q_OS_WIN
          "windows-pipe-json-lines"
#else
          "unix-json-lines"
#endif
          },
          {"endpoint",
#ifdef Q_OS_WIN
          QStringLiteral("\\\\.\\pipe\\") + endpointFor(directory)
#else
          endpointFor(directory)
#endif
          }, {"profile", QDir(directory).absolutePath()},
          {"maxRequestBytes", limit}, {"heartbeatSeconds", 5}, {"disconnectAfterSeconds", 15},
          {"actions", QJsonArray{"capabilities", "start", "update", "complete", "heartbeat", "status", "cancel", "error"}},
          {"containers", QJsonArray{"fast-start-mp4", "fragmented-mp4"}},
          {"videoCodecs", QJsonArray{"h264"}}, {"audioCodecs", QJsonArray{"aac-lc"}},
          {"separateTracksRequired", true}, {"initializedTracksRequired", true},
          {"startBufferSeconds", 10}, {"resumeBufferSeconds", 5}, {"pauseBufferSeconds", 1}};
}
bool ProgressiveApi::listen(const QString &directory) {
  const auto endpoint = endpointFor(directory);
  lease = std::make_unique<QLockFile>(
#ifdef Q_OS_WIN
      QDir(PlatformPaths::runtimeDirectory()).filePath(endpoint + ".lock")
#else
      endpoint + ".lock"
#endif
  );
  lease->setStaleLockTime(0);
  if (!lease->tryLock(0)) return false;
  QLocalServer::removeServer(endpoint);
  if (server.listen(endpoint)) return true;
  lease->unlock();
  return false;
}
QJsonObject ProgressiveApi::request(const QJsonObject &input) {
  if (input.value("version") != QJsonValue(1) || !input.value("action").isString())
    return failure("invalid_request", "Expected version 1 and an action");
  const auto action = input.value("action").toString();
  if (action == "capabilities") {
    if (!keys(input, {"version", "action"})) return failure("invalid_request", "Unexpected fields");
    auto result = information({});
    result.remove("endpoint"); result.remove("profile"); result["ok"] = true;
    result["pid"] = QCoreApplication::applicationPid();
    return result;
  }
  if (action == "start") {
    if (!keys(input, {"version", "action", "session"}, {"replace", "activate", "title"}) ||
        !input.value("session").isObject() ||
        (input.contains("replace") && !input.value("replace").isBool()) ||
        (input.contains("activate") && !input.value("activate").isBool()) ||
        (input.contains("title") && (!input.value("title").isString() || input.value("title").toString().size() > 1024)))
      return failure("invalid_request", "Invalid start request");
    const auto descriptor = input.value("session").toObject();
    auto existing = source.lock();
    if (existing && window->progressiveSession() == existing &&
        descriptor.value("sessionId").toString() == sessionId) {
      auto result = window->progressivePlaybackStatus();
      result["ok"] = true; result["version"] = 1; return result;
    }
    if (window->progressiveSession() && !input.value("replace").toBool())
      return failure("session_busy", "Another progressive session is active; replacement must be explicit");
    try {
      auto next = ProgressiveSource::openPublished(descriptor);
      window->openProgressiveSource(next);
      auto title = input.value("title").toString();
      for (auto &c : title) if (c.category() == QChar::Other_Control) c = QChar(' ');
      window->setProperty("foxicmpProgressiveTitle", title.trimmed());
      source = next;
      sessionId = next->id();
      lastOutcome = {};
      if (input.value("activate").toBool(true)) window->restoreFromTray();
    } catch (const std::exception &e) {
      return failure("source_not_ready", QString::fromUtf8(e.what()));
    }
  } else {
    const QStringList extra = action == "update" || action == "complete" ? QStringList{"session"}
                            : action == "error" ? QStringList{"message"} : QStringList{};
    if (!QStringList{"update", "complete", "heartbeat", "status", "cancel", "error"}.contains(action) ||
        !keys(input, {"version", "action", "sessionId"}, extra) ||
        !input.value("sessionId").isString() || input.value("sessionId").toString() != sessionId || sessionId.isEmpty())
      return failure("unknown_session", "Invalid request or unknown session identifier");
    auto active = source.lock();
    if (!active || window->progressiveSession() != active) {
      if (action == "status" || action == "cancel")
        return lastOutcome.isEmpty()
            ? QJsonObject{{"version", 1}, {"ok", true}, {"active", false}, {"state", "stopped"}, {"sessionId", sessionId}}
            : lastOutcome;
      return failure("session_closed", "Playback was stopped or replaced");
    }
    if (action == "update" || action == "complete") {
      const auto descriptor = input.value("session").toObject();
      if (!input.value("session").isObject() ||
          (action == "complete" && descriptor.value("state") != QJsonValue("complete")))
        return failure("invalid_request", "Expected a complete session snapshot");
      QString error;
      if (!active->publish(descriptor, &error)) return failure("invalid_update", error);
    } else if (action == "heartbeat") {
      active->publishedHeartbeat();
    } else if (action == "cancel") {
      window->closeCurrent();
      lastOutcome = {{"version", 1}, {"ok", true}, {"active", false}, {"state", "cancelled"}, {"sessionId", sessionId}};
      return lastOutcome;
    } else if (action == "error") {
      const auto message = input.value("message").toString();
      if (message.isEmpty() || message.size() > 1024) return failure("invalid_request", "Expected a short error message");
      active->publishedError(message);
    }
  }
  auto result = window->progressivePlaybackStatus();
  result["version"] = 1; result["ok"] = true;
  return result;
}
void ProgressiveApi::acceptClients() {
  while (server.hasPendingConnections()) {
    auto *socket = server.nextPendingConnection();
#ifndef Q_OS_WIN
    struct ucred peer{};
    socklen_t length = sizeof(peer);
    if (clients >= 16 || getsockopt(socket->socketDescriptor(), SOL_SOCKET, SO_PEERCRED, &peer, &length) ||
        peer.uid != getuid()) { socket->abort(); socket->deleteLater(); continue; }
#else
    // QLocalServer::UserAccessOption restricts the named pipe ACL to this user.
    if (clients >= 16) { socket->abort(); socket->deleteLater(); continue; }
#endif
    ++clients;
    socket->setParent(this);
    socket->setReadBufferSize(limit + 1);
    auto buffer = std::make_shared<QByteArray>();
    auto *deadline = new QTimer(socket);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, socket, &QLocalSocket::abort);
    deadline->start(5000);
    connect(socket, &QLocalSocket::disconnected, this, [this, socket] { --clients; socket->deleteLater(); });
    connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer, deadline] {
      if (socket->property("answered").toBool()) return;
      buffer->append(socket->readAll());
      if (!buffer->contains('\n') && buffer->size() <= limit) return;
      socket->setProperty("answered", true);
      QJsonObject reply;
      try {
        if (buffer->size() > limit || buffer->indexOf('\n') != buffer->size() - 1)
          throw std::runtime_error("One JSON line of at most 65536 bytes is required");
        rapidjson::Document checked;
        checked.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(buffer->constData(), size_t(buffer->size() - 1));
        if (checked.HasParseError() || !checked.IsObject()) throw std::runtime_error("Invalid JSON request");
        harvester::UniqueKeys(checked);
        reply = request(QJsonDocument::fromJson(*buffer).object());
      } catch (const std::exception &e) { reply = failure("invalid_request", QString::fromUtf8(e.what())); }
      deadline->stop();
      socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
      socket->disconnectFromServer();
    });
  }
}
