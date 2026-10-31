#include "progressiveapi.h"
#include "mpvlifetime.h"
#include "instancebroker.h"
#include "progressivesource.h"
#include "window.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTimer>
#include <QDir>
#include <QCoreApplication>
#include <QPointer>
#include <thread>
#include "platformpaths.h"
#include "../../shared/progressivesession.h"
#include "../../shared/progressivereaderlease.h"
#include "../../shared/progressiveretirement.h"
#include "../../shared/progressiveresourceprobe.h"
#include "../../shared/progressivefmp4bridge.h"
#ifndef Q_OS_WIN
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
constexpr int limit = 65536;
// Legacy numeric capability: ceiling of signed 64-bit 100 ns VOD timestamps.
constexpr double maxVodSeconds = 922337203686.0;
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
struct ProgressiveApi::Preparation {
  bool requested=false, ready=false, probe=false, publication=false, readerQuery=false, readerRepeat=false;
  unsigned readerPass=0;
  std::atomic<bool> finished{false}, abandoned{false};
  QJsonObject descriptor, reply;
  QString registry;
  std::shared_ptr<ProgressiveSource> candidate;
};
ProgressiveApi::ProgressiveApi(Window *w, QObject *parent) : QObject(parent), window(w) {
  connect(window,&Window::playbackIntentChanged,this,[this]{if(preparation)preparation->abandoned.store(true);});
  server.setSocketOptions(QLocalServer::UserAccessOption);
  server.setMaxPendingConnections(16);
  connect(&server, &QLocalServer::newConnection, this, &ProgressiveApi::acceptClients);
  connect(window, &Window::progressiveSessionEnded, this,
          [this](const QString &id, const QString &state, const QString &error) {
    if (id != sessionId) return;
    lastOutcome = {{"version", 1}, {"ok", true}, {"active", false},
                   {"sessionId", id}, {"state", state},
                   {"error", error}};
    if(auto active=source.lock()){const auto details=active->playbackDetails();for(auto it=details.begin();it!=details.end();++it)lastOutcome[it.key()]=it.value();}
    if(window){const auto actual=window->progressivePlaybackStatus();for(const auto* key:{"subtitleTracks","mediaTracks"})if(lastOutcome.contains(key))lastOutcome[key]=actual.value(key);}
    source.reset();
  });
}
ProgressiveApi::~ProgressiveApi() { if(preparation)preparation->abandoned.store(true);if(registryPreparation)registryPreparation->abandoned.store(true);server.close(); }
QString ProgressiveApi::endpointFor(const QString &directory) {
  return InstanceBroker::endpointFor(directory) + ".progressive";
}
QJsonObject ProgressiveApi::information(const QString &directory) {
  return {{"version", 1}, {"schemaVersion", 1},
          {"protocol", "progressive-manifest-v1"}, {"transport",
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
          {"actions", QJsonArray{"capabilities", "validate", "probe", "start", "update", "complete", "heartbeat", "status", "cancel", "release", "error", "readerStatus", "readerRelease", "readerForget", "readers", "resourceProbe", "resourceProbeUpdate", "resourceProbeStatus", "resourceProbeCancel", "resourceProbeForget"}},
          {"backendDemuxProbe",QJsonDocument::fromJson(foxicmp::progressive::BackendDemuxCapabilities()).object()},
          {"resourceIoProbeVersion", 1}, {"resourceIoSchemaVersions", QJsonArray{2}}, {"resourceIoPlaybackSupported", false}, {"segmentProbeVersion", 1},
          {"engineRetirement",QJsonObject{{"maxEngines",int(EngineRetirement::Capacity)},{"workerLimit",int(EngineRetirement::WorkerLimit)},{"nativeDestroyOffUi",true},{"softwareRendererFreeOffUi",true},{"openGlRendererFreeOffUi",false},{"callbackDataRetained",true},{"processOwned",true}}},
          {"legacyRefresh",QJsonObject{{"fileIoOutsideMetadataLock",true},{"snapshotIndexCommitAtomic",true},{"notificationsMetadataOnly",true}}},
          {"legacyPreparation",QJsonObject{{"offUi",true},{"maxConcurrent",1},{"responseDeadlineMilliseconds",5000},{"cancellableDiskIo",false},{"previousPlaybackUntilCommit",true}}},
          {"readerRegistryOffUi",true},{"sourceRetirement",QJsonObject{{"maxSources",int(foxicmp::progressive::SourceRetirement::Capacity)},{"workerLimit",int(foxicmp::progressive::SourceRetirement::WorkerLimit)},{"processOwned",true},{"filesClosedBeforeRelease",true}}},
          {"readerLeaseProtocol", 1}, {"readerLeaseOptIn", true}, {"maxReaderReceipts", 128},
          {"playbackSchemaVersions", QJsonArray{1,2}}, {"validationSchemaVersions", QJsonArray{2}},
          {"schema2Playback",QJsonDocument::fromJson(foxicmp::progressive::Fmp4V2Capabilities()).object()},
          {"qtMultiAudioSeek",QJsonObject{{"maxTargetSecondsWhileIncomplete",maxVodSeconds},{"completeSourceByteSeek",true},{"confirmedRangesOnly",true},{"mode","confirmed-byte-seek"}}},
          {"releaseAcknowledgement", true},
          {"mediaPreparation",QJsonObject{{"offUi",QJsonArray{"probe","start","update","complete"}},{"maxConcurrent",1},{"responseDeadlineMilliseconds",5000},{"updatesOffUi",true},{"cancellableDiskIo",false}}},
          {"containers", QJsonArray{"fast-start-mp4", "fragmented-mp4"}},
          {"videoCodecs", QJsonArray{"h264"}}, {"audioCodecs", QJsonArray{"aac-lc"}},
          {"progressiveModes", QJsonArray{"native-progressive", "post-download"}},
          {"fallback", QJsonObject{{"available", false}, {"reasonCode", "unsupported-native-format"}, {"networkRetry", false}}},
          {"separateTracksRequired", true}, {"initializedTracksRequired", true},
          {"startBufferSeconds", 10}, {"resumeBufferSeconds", 5}, {"pauseBufferSeconds", 1}};
}
bool ProgressiveApi::listen(const QString &directory) {
  readerRegistry = readerRegistryFor(directory);
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
QString ProgressiveApi::readerRegistryFor(const QString &directory) {
#ifdef Q_OS_WIN
  return QDir(PlatformPaths::runtimeDirectory()).filePath(endpointFor(directory) + ".readers");
#else
  return endpointFor(directory) + ".readers";
#endif
}
QJsonObject ProgressiveApi::request(const QJsonObject &input) {return execute(input,nullptr);}
QJsonObject ProgressiveApi::execute(const QJsonObject &input, Preparation *prepared) {
  if (input.value("version") != QJsonValue(1) || !input.value("action").isString())
    return failure("invalid_request", "Expected version 1 and an action");
  const auto action = input.value("action").toString();
  if (QStringList{"resourceProbe", "resourceProbeUpdate", "resourceProbeStatus", "resourceProbeCancel", "resourceProbeForget"}.contains(action)) {
    if (!resourceProbes) resourceProbes = std::make_unique<foxicmp::progressive::ResourceProbeManager>();
    return QJsonDocument::fromJson(QByteArray::fromStdString(resourceProbes->request(
        std::filesystem::u8path(readerRegistry.toUtf8().toStdString()),
        QJsonDocument(input).toJson(QJsonDocument::Compact).toStdString()))).object();
  }
  if (action == "readerStatus" || action == "readerRelease" || action == "readerForget" || action == "readers") {
    const auto raw=QJsonDocument(input).toJson(QJsonDocument::Compact).toStdString();
    if(prepared && !prepared->ready) {
      try{foxicmp::progressive::ReaderCommand(raw);}catch(const foxicmp::progressive::Error& e){return failure(QString::fromStdString(e.code),QString::fromUtf8(e.what()));}
      prepared->requested=true;prepared->readerQuery=true;prepared->descriptor=input;prepared->registry=readerRegistry;
      if(action=="readerRelease" && input["readerId"]==readerId){auto active=source.lock();if(active && window->progressiveSession()==active)prepared->candidate=active;}
      return {};
    }
    const auto query=[&]{return QJsonDocument::fromJson(QByteArray::fromStdString(foxicmp::progressive::ReaderRequest(
        std::filesystem::u8path(readerRegistry.toUtf8().toStdString()),raw))).object();};
    auto reply=prepared?prepared->reply:query();
    if(action=="readerRelease" && reply["ok"].toBool() && !readerId.isEmpty() && input["readerId"]==readerId && (!prepared || !prepared->readerPass)) {
      auto active=source.lock();
      if(active && window->progressiveSession()==active && (!prepared || prepared->candidate==active)) {
        window->closeCurrent();
        if(prepared){prepared->readerRepeat=true;return {};}
        reply=query();
      }
    }
    return reply;
  }

  if (action == "capabilities") {
    if (!keys(input, {"version", "action"})) return failure("invalid_request", "Unexpected fields");
    auto result = information({});
    result.remove("endpoint"); result.remove("profile"); result["ok"] = true;
    result["pid"] = QCoreApplication::applicationPid();
    result["legacyPreparationPending"] = window->progressiveOpening();
    return result;
  }
  if (action == "validate") {
    if (!keys(input, {"version", "action", "session"}, {"previous"}) ||
        !input.value("session").isObject() || (input.contains("previous") && !input.value("previous").isObject()))
      return failure("invalid_request", "Expected a schema-2 session and optional previous snapshot");
    const auto current = QJsonDocument(input["session"].toObject()).toJson(QJsonDocument::Compact);
    const auto previous = input.contains("previous") ? QJsonDocument(input["previous"].toObject()).toJson(QJsonDocument::Compact) : QByteArray();
    return QJsonDocument::fromJson(QByteArray::fromStdString(foxicmp::progressive::ValidateReply(current.toStdString(), previous.toStdString()))).object();
  }
  if ((action == "start" || action == "probe") && input.value("session").isObject() &&
      input["session"].toObject().value("schemaVersion") == QJsonValue(2)) {
    try {foxicmp::progressive::Fmp4V2Bridge::CheckProfile(QJsonDocument(input["session"].toObject()).toJson(QJsonDocument::Compact).toStdString());}
    catch(const foxicmp::progressive::Error& e) {
      if(action=="probe")return {{"version",1},{"ok",true},{"action","probe"},{"supported",false},{"mode","unsupported"},{"reasonCode",QString::fromStdString(e.code)},{"reason",QString::fromUtf8(e.what())}};
      return failure(QString::fromStdString(e.code),QString::fromUtf8(e.what()));
    }
  }
  if (action == "probe") {
    if (!keys(input, {"version", "action", "session"}, {"sessionDirectory"}) ||
        !input.value("session").isObject() ||
        (input.contains("sessionDirectory") &&
         (!input.value("sessionDirectory").isString() || input.value("sessionDirectory").toString().size() > 8192)))
      return failure("invalid_request", "Invalid probe request");
    auto descriptor = input.value("session").toObject();
    if (input.contains("sessionDirectory")) descriptor["sessionDirectory"] = input.value("sessionDirectory");
    if(prepared && !prepared->ready){prepared->requested=true;prepared->probe=true;prepared->descriptor=descriptor;prepared->registry=readerRegistry;return {};}
    if(prepared && prepared->reply.value("ok")==QJsonValue(false))return prepared->reply;
    auto result = prepared ? prepared->reply : ProgressiveSource::probePublished(descriptor, readerRegistry);
    result["version"] = 1; result["ok"] = true; result["action"] = "probe";
    return result;
  }
  if (action == "start") {
    if (!keys(input, {"version", "action", "session"}, {"replace", "activate", "title", "sessionDirectory", "trackReader"}) ||
        !input.value("session").isObject() ||
        (input.contains("replace") && !input.value("replace").isBool()) ||
        (input.contains("activate") && !input.value("activate").isBool()) ||
        (input.contains("trackReader") && !input.value("trackReader").isBool()) ||
        (input.contains("title") && (!input.value("title").isString() || input.value("title").toString().size() > 1024)) ||
        (input.contains("sessionDirectory") && (!input.value("sessionDirectory").isString() || input.value("sessionDirectory").toString().size() > 8192)))
      return failure("invalid_request", "Invalid start request");
    auto descriptor = input.value("session").toObject();
    if (input.contains("sessionDirectory")) descriptor["sessionDirectory"] = input.value("sessionDirectory");
    auto existing = source.lock();
    if (existing && window->progressiveSession() == existing &&
        descriptor.value("sessionId").toString() == sessionId && descriptor.value("generation") == sessionGeneration) {
      if(descriptor["schemaVersion"]==QJsonValue(2) && existing->playbackDetails()["writerInstanceId"]!=descriptor["writer"].toObject()["instanceId"])
        return failure("generation_changed","Writer incarnation changed; use a new generation");
      auto result = window->progressivePlaybackStatus();
      result["ok"] = true; result["version"] = 1;
      if (input.value("trackReader").toBool() && readerId.isEmpty())
        return failure("reader_not_tracked", "Existing session was opened without a reader lease");
      if (!readerId.isEmpty()) result["readerId"] = readerId;
      return result;
    }
    if (window->progressiveSession() && !input.value("replace").toBool())
      return failure("session_busy", "Another progressive session is active; replacement must be explicit");
    try {
      const auto registry=input.value("trackReader").toBool() || descriptor["schemaVersion"]==QJsonValue(2) ? readerRegistry : QString();
      if(!prepared || !prepared->ready)emit window->playbackIntentChanged();
      if(prepared && !prepared->ready){prepared->requested=true;prepared->descriptor=descriptor;prepared->registry=registry;return {};}
      if(prepared && !prepared->reply.isEmpty())return prepared->reply;
      auto next = prepared ? prepared->candidate : ProgressiveSource::openPublished(descriptor, registry);
      if(!next || !next->status().error.isEmpty())return failure("source_not_ready","Prepared source is no longer readable");
      window->openProgressiveSource(next);
      auto title = input.value("title").toString();
      for (auto &c : title) if (c.category() == QChar::Other_Control) c = QChar(' ');
      window->setProperty("foxicmpProgressiveTitle", title.trimmed());
      source = next;
      releaseReceipt = next->releaseReceipt();
      sessionId = next->id();
      sessionGeneration = descriptor.value("generation");
      readerId = next->registeredReaderId();
      lastOutcome = {};
      if (input.value("activate").toBool(true)) window->restoreFromTray();
    } catch (const foxicmp::progressive::RetirementBusy& e) {
      return failure("source_limit",QString::fromUtf8(e.what()));
    } catch (const foxicmp::progressive::Error &e) {
      return failure(QString::fromStdString(e.code),QString::fromUtf8(e.what()));
    } catch (const std::exception &e) {
      return failure("source_not_ready", QString::fromUtf8(e.what()));
    }
  } else {
    QStringList extra = action == "update" || action == "complete" ? QStringList{"session", "sessionDirectory"}
                            : action == "error" ? QStringList{"message"} : QStringList{};
    extra.append("readerId");
    if (!QStringList{"update", "complete", "heartbeat", "status", "cancel", "release", "error"}.contains(action) ||
        !keys(input, {"version", "action", "sessionId"}, extra) ||
        !input.value("sessionId").isString() || input.value("sessionId").toString() != sessionId || sessionId.isEmpty())
      return failure("unknown_session", "Invalid request or unknown session identifier");
    if (input.contains("readerId") && (!input.value("readerId").isString() || readerId.isEmpty() || input.value("readerId") != readerId))
      return failure("reader_mismatch", "Command belongs to a different reader; use readerStatus/readerRelease for its receipt");
    auto active = source.lock();
    if (!active || window->progressiveSession() != active) {
      if (action == "status" || action == "cancel" || action == "release") {
        auto result = lastOutcome.isEmpty()
            ? QJsonObject{{"version", 1}, {"ok", true}, {"active", false}, {"state", "stopped"}, {"sessionId", sessionId}}
            : lastOutcome;
        result["released"] = releaseReceipt && releaseReceipt->load();
        if (!readerId.isEmpty()) result["readerId"] = readerId;
        return result;
      }
      return failure("session_closed", "Playback was stopped or replaced");
    }
    if (action == "update" || action == "complete") {
      auto descriptor = input.value("session").toObject();
      if (input.contains("sessionDirectory")) descriptor["sessionDirectory"] = input.value("sessionDirectory");
      if (!input.value("session").isObject() ||
          (action == "complete" && (descriptor["schemaVersion"]==QJsonValue(2)?descriptor["writer"].toObject()["state"]:descriptor["state"]) != QJsonValue("complete")))
        return failure("invalid_request", "Expected a complete session snapshot");
      if(prepared && !prepared->ready){prepared->requested=true;prepared->publication=true;prepared->descriptor=descriptor;prepared->candidate=active;return {};}
      if(prepared){
        if(!prepared->reply.isEmpty())return prepared->reply;
        if(prepared->candidate!=active)return failure("reader_mismatch","Prepared update belongs to another reader");
      }else{QString error,reason;if(!active->publish(descriptor,&error,&reason))return failure(reason.isEmpty()?"invalid_update":reason,error);}
    } else if (action == "heartbeat") {
      active->publishedHeartbeat();
    } else if (action == "cancel" || action == "release") {
      const auto details=active->playbackDetails();
      window->closeCurrent();
      lastOutcome = {{"version", 1}, {"ok", true}, {"active", false}, {"state", "cancelled"}, {"sessionId", sessionId}};
      for(auto it=details.begin();it!=details.end();++it)lastOutcome[it.key()]=it.value();
      auto result = lastOutcome;
      result["released"] = releaseReceipt && releaseReceipt->load();
      if (!readerId.isEmpty()) result["readerId"] = readerId;
      return result;
    } else if (action == "error") {
      const auto message = input.value("message").toString();
      if (message.isEmpty() || message.size() > 1024) return failure("invalid_request", "Expected a short error message");
      active->publishedError(message);
    }
  }
  auto result = window->progressivePlaybackStatus();
  result["version"] = 1; result["ok"] = true;
  result["released"] = releaseReceipt && releaseReceipt->load();
  if (!readerId.isEmpty()) result["readerId"] = readerId;
  return result;
}
void ProgressiveApi::dispatch(const QJsonObject &input, QLocalSocket *socket, QTimer *deadline) {
  const auto answer=[socket,deadline](const QJsonObject &reply){
    deadline->stop();socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact)+'\n');socket->disconnectFromServer();
  };
  const auto action=input["action"].toString();
  const bool reader=QStringList{"readerStatus","readerRelease","readerForget","readers"}.contains(action);
  if(!reader && action!="start" && action!="probe" && action!="update" && action!="complete"){answer(request(input));return;}
  auto& slot=reader?registryPreparation:preparation;
  if(slot){answer(failure(reader?"reader_registry_busy":"preparation_busy","Another request is using this worker slot"));return;}
  auto job=std::make_shared<Preparation>();const auto admitted=execute(input,job.get());
  if(!job->requested){answer(admitted);return;}
  slot=job;const auto hook=reader?readerIoHook:preparationHook;
  // Workers own their inputs, never the window or API. Registry I/O has its own
  // slot, so release requests are not blocked behind a media publication.
  const auto launch=[job,hook]{std::thread([job,hook]{
    try {
      if(hook && !job->readerPass)hook();
      if(!job->abandoned.load()) {
        if(job->readerQuery)job->reply=QJsonDocument::fromJson(QByteArray::fromStdString(foxicmp::progressive::ReaderRequest(
            std::filesystem::u8path(job->registry.toUtf8().toStdString()),QJsonDocument(job->descriptor).toJson(QJsonDocument::Compact).toStdString()))).object();
        else if(job->publication){QString error,reason;if(!job->candidate->publish(job->descriptor,&error,&reason))job->reply=failure(reason.isEmpty()?"invalid_update":reason,error);}
        else if(job->probe)job->reply=ProgressiveSource::probePublished(job->descriptor,job->registry);
        else job->candidate=ProgressiveSource::openPublished(job->descriptor,job->registry);
      }
    }catch(const foxicmp::progressive::RetirementBusy& e){job->reply=failure("source_limit",QString::fromUtf8(e.what()));}
    catch(const foxicmp::progressive::Error& e){job->reply=failure(QString::fromStdString(e.code),QString::fromUtf8(e.what()));}
    catch(const std::exception& e){job->reply=failure(job->readerQuery?"reader_registry_error":"source_not_ready",QString::fromUtf8(e.what()));}
    if(job->abandoned.load())job->candidate.reset();
    job->finished.store(true,std::memory_order_release);
  }).detach();};
  try{launch();}catch(const std::exception& e){slot.reset();answer(failure("preparation_unavailable",QString::fromUtf8(e.what())));return;}
  const QPointer<QLocalSocket> guarded(socket);auto* poll=new QTimer(this);poll->setInterval(10);
  connect(socket,&QLocalSocket::disconnected,this,[job]{job->abandoned.store(true);});
  connect(poll,&QTimer::timeout,this,[this,job,poll,guarded,deadline,input,launch]{
    if(!job->finished.load(std::memory_order_acquire))return;
    auto& slot=job->readerQuery?registryPreparation:preparation;
    if(!guarded || guarded->state()!=QLocalSocket::ConnectedState){slot.reset();poll->stop();poll->deleteLater();return;}
    job->ready=true;
    auto reply=job->abandoned.load()?failure("preparation_cancelled","Playback intent changed while processing request"):execute(input,job.get());
    if(job->readerRepeat) {
      job->readerRepeat=false;++job->readerPass;job->ready=false;job->finished.store(false);
      try{launch();return;}catch(const std::exception& e){reply=failure("reader_registry_error",QString::fromUtf8(e.what()));}
    }
    slot.reset();poll->stop();poll->deleteLater();deadline->stop();
    guarded->write(QJsonDocument(reply).toJson(QJsonDocument::Compact)+'\n');guarded->disconnectFromServer();
  });
  poll->start();
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
        dispatch(QJsonDocument::fromJson(*buffer).object(),socket,deadline);
        return;
      } catch (const std::exception &e) { reply = failure("invalid_request", QString::fromUtf8(e.what())); }
      deadline->stop();
      socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
      socket->disconnectFromServer();
    });
  }
}
