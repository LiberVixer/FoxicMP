#include "progressivesource.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include "progressivefile.h"
#include <QDir>
#include "../../shared/progressivereaderlease.h"
#include "../../shared/progressiveretirement.h"
#include "../../shared/progressivefmp4bridge.h"

namespace {
bool samePath(const QString &a, const QString &b) {
#ifdef Q_OS_WIN
  return a.compare(b, Qt::CaseInsensitive) == 0;
#else
  return a == b;
#endif
}
QString sessionDirectory(const QJsonObject &input, const QString *bound = nullptr) {
  if (!input.contains("sessionDirectory")) return bound ? *bound : QString();
  const auto value = input.value("sessionDirectory");
  const auto path = value.toString();
  if (!value.isString() || path.isEmpty() || path.contains(QChar(0)) ||
      path.toUtf8().size() > 8192 || !ProgressiveFile::absolute(path))
    throw std::runtime_error("sessionDirectory must be an absolute local directory");
  const auto normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
  if (bound && !samePath(normalized, *bound))
    throw std::runtime_error("sessionDirectory cannot change within a session");
  return bound ? *bound : normalized;
}
QString resolveTrackPath(const QString &path, const QString &root) {
  if (path.contains(QChar(0)) || path.toUtf8().size() > 8192)
    throw std::runtime_error("Track path is too long or contains an embedded null");
  if (ProgressiveFile::absolute(path))
    return QFileInfo(path).absoluteFilePath();
  if (root.isEmpty() || !ProgressiveFile::absolute(root))
    throw std::runtime_error("Relative track paths require an absolute sessionDirectory");
  if (path.isEmpty() || path == "." || path == ".." || path.contains('/') || path.contains('\\'))
    throw std::runtime_error("Track name must be a plain local filename");
  const auto base = QFileInfo(root).absoluteFilePath();
  const auto resolved = QFileInfo(QDir(base).filePath(path)).absoluteFilePath();
  const auto relative = QDir(base).relativeFilePath(resolved);
  if (relative == ".." || relative.startsWith("../") || relative.startsWith("..\\"))
    throw std::runtime_error("Track path escapes sessionDirectory");
  return resolved;
}
harvester::Snapshot snapshotFor(QJsonObject input, QStringList *paths, const QString &root) {
  input.remove("sessionDirectory");
  auto tracks = input.value("tracks").toObject();
  for (const auto &kind : {QStringLiteral("video"), QStringLiteral("audio")}) {
    auto track = tracks.value(kind).toObject();
    const auto path = resolveTrackPath(track.value("path").toString(), root);
    paths->append(path);
    track["path"] = kind + ".part";
    tracks[kind] = track;
  }
  input["tracks"] = tracks;
  return harvester::Parse(QJsonDocument(input).toJson(QJsonDocument::Compact).toStdString());
}
bool readPublished(intptr_t file, uint64_t at, void *data, size_t count) {
  return ProgressiveFile::read(file, at, data, count) == int64_t(count);
}
void checkFile(intptr_t file, uint64_t available) { ProgressiveFile::inspect(file, available); }

}

std::shared_ptr<ProgressiveSource>
ProgressiveSource::openPublished(const QJsonObject &input, const QString &readerRegistry) {
  auto source = foxicmp::progressive::SourceRetirement::create<ProgressiveSource>([&]{return new ProgressiveSource(input,readerRegistry);});
  if (!readerRegistry.isEmpty())
  {
    std::vector<foxicmp::progressive::BoundResource> identities;
    for(size_t i=0;i<source->snapshot.tracks.size();++i)if(source->snapshot.tracks[i].initializationReady)identities.push_back({i?(i==1?"audio":"audio-"+std::to_string(i)):"video",source->devices[i],source->inodes[i]});
    source->readerLease = foxicmp::progressive::ReaderLease::Create(
        std::filesystem::u8path(readerRegistry.toUtf8().toStdString()), source->snapshot.sessionId,
        source->snapshot.generation, identities);
  }
  source->worker = std::thread([raw = source.get()] { raw->watchPublished(); });
  return source;
}
QString ProgressiveSource::registeredReaderId() const {
  return readerLease ? QString::fromStdString(readerLease->id()) : QString();
}

QJsonObject ProgressiveSource::probePublished(const QJsonObject &input, const QString &readerRegistry) {
  try {
    auto source = std::shared_ptr<ProgressiveSource>(new ProgressiveSource(input, readerRegistry));
    const auto current = source->status();
    return {{"supported", true}, {"mode", "native-progressive"},
            {"reasonCode", "supported"},
            {"reason", "Tracks are initialized and the confirmed prefix is readable"},
            {"readySeconds", current.available}, {"complete", current.complete}};
  } catch (const foxicmp::progressive::RetirementBusy &error) {
    return {{"supported",false},{"mode","waiting-player"},{"reasonCode","source_limit"},{"reason",QString::fromUtf8(error.what())},{"requiresCompleteFile",false}};
  } catch (const foxicmp::progressive::Error &error) {
    return {{"supported", false}, {"mode", "unsupported"}, {"reasonCode", QString::fromStdString(error.code)},
            {"reason", QString::fromUtf8(error.what())}, {"requiresCompleteFile", false}};
  } catch (const std::exception &error) {
    return {{"supported", false}, {"mode", "post-download"},
            {"reasonCode", "native-progressive-rejected"},
            {"reason", QString::fromUtf8(error.what())},
            {"requiresCompleteFile", true}};
  }
}

ProgressiveSource::ProgressiveSource(const QJsonObject &input, const QString &readerRegistry) : published(true) {
  publishedRoot = sessionDirectory(input);
  if (input.value("schemaVersion") == QJsonValue(2)) {
    auto manifest = input; manifest.remove("sessionDirectory");
    foxicmp::progressive::Fmp4V2Bridge::CheckProfile(QJsonDocument(manifest).toJson(QJsonDocument::Compact).toStdString());
    if (readerRegistry.isEmpty()) throw std::runtime_error("Schema 2 requires a reader registry");
    v2Bridge = std::make_shared<foxicmp::progressive::Fmp4V2Bridge>(
        std::filesystem::u8path(publishedRoot.toUtf8().toStdString()), std::filesystem::u8path(readerRegistry.toUtf8().toStdString()),
        QJsonDocument(manifest).toJson(QJsonDocument::Compact).toStdString());
    snapshot = v2Bridge->snapshot();const auto bindings = v2Bridge->bindings();
    size_t bound=0;
    for (int i=0;size_t(i)<snapshot.tracks.size();++i) {
      publishedPaths.append(resolveTrackPath(QString::fromStdString(snapshot.tracks[i].path), publishedRoot));
      if(snapshot.tracks[i].initializationReady){devices[i]=bindings[bound].device; inodes[i]=bindings[bound++].fileId;}
    }
  } else snapshot = snapshotFor(input, &publishedPaths, publishedRoot);
  if (snapshot.state == harvester::State::Cancelled ||
      snapshot.state == harvester::State::Failed)
    throw std::runtime_error("Cannot start a terminal failed session");
  try {
    for (int i = 0; size_t(i)<snapshot.tracks.size(); ++i) {
      const auto &track = snapshot.tracks[i];
      if (!track.initializationReady && track.required)
        throw std::runtime_error("MP4 initialization is not ready; retry start after the index is written");
      if (!v2Bridge) {
        files[i] = ProgressiveFile::open(publishedPaths[i]);
        const auto info = ProgressiveFile::inspect(files[i], track.available);
        devices[i] = info.device; inodes[i] = info.inode;
      }
    }
    if ((!v2Bridge || !v2Bridge->isMuxed()) && devices[0] == devices[1] && inodes[0] == inodes[1])
      throw std::runtime_error("Video and audio must be distinct files");
    for (int i = 0; size_t(i)<snapshot.tracks.size(); ++i)
      if (!snapshot.tracks[i].fragmented)
        indexed[i].open([&, i](uint64_t at, void *data, size_t count) {
          return readPublished(files[i], at, data, count);
        }, snapshot.tracks[i].available, snapshot.tracks[i].total, i,
        snapshot.timestampOffset);
    updateIndexes();
    currentStatus.connected = true;
    lastPublished = std::chrono::steady_clock::now();
  } catch (...) {
    for (auto file : files) if (file != -1) ProgressiveFile::close(file);
    throw;
  }
}

bool ProgressiveSource::publish(const QJsonObject &input, QString *error, QString *reasonCode) {
  std::lock_guard<std::mutex> publication(publicationMutex);
  try {
    harvester::Snapshot previous;
    {std::lock_guard<std::mutex> lock(mutex);
     if(!published || stopping.load() || !currentStatus.error.isEmpty())throw std::runtime_error("Session is no longer writable");
     previous=snapshot;}
    harvester::Snapshot next;
    if(v2Bridge) {
      sessionDirectory(input,&publishedRoot);auto manifest=input;manifest.remove("sessionDirectory");
      if(publicationIoHook)publicationIoHook();
      if(stopping.load())throw std::runtime_error("Session is closed");
      v2Bridge->publish(QJsonDocument(manifest).toJson(QJsonDocument::Compact).toStdString());next=v2Bridge->snapshot();
    }else {
      QStringList paths;next=snapshotFor(input,&paths,sessionDirectory(input,&publishedRoot));
      if(paths.size()!=publishedPaths.size() || !samePath(paths[0],publishedPaths[0]) || !samePath(paths[1],publishedPaths[1]))throw std::runtime_error("Track paths cannot change within a session");
      if(!harvester::Advance(previous,next))throw std::runtime_error("Stale revision; use a strictly increasing revision");
      if(publicationIoHook)publicationIoHook();
      if(stopping.load())throw std::runtime_error("Session is closed");
      for(int i=0;i<2;++i)checkFile(files[i],next.tracks[i].available);
    }
    Status indexedStatus;
    try{indexedStatus=indexStatus(next);}catch(const std::exception& e){if(!stopping.load())fail(QString::fromUtf8(e.what()));throw;}
    {std::lock_guard<std::mutex> lock(mutex);
     if(stopping.load() || !currentStatus.error.isEmpty())throw std::runtime_error("Session is no longer writable");
     snapshot=next;currentStatus.duration=indexedStatus.duration;currentStatus.timestampOffset=indexedStatus.timestampOffset;
     currentStatus.available=indexedStatus.available;currentStatus.complete=indexedStatus.complete;currentStatus.mediaAvailable=indexedStatus.mediaAvailable;currentStatus.mediaComplete=indexedStatus.mediaComplete;
     lastPublished=std::chrono::steady_clock::now();currentStatus.connected=true;
     if(next.state==harvester::State::Failed || next.state==harvester::State::Cancelled)currentStatus.error="Writer failed or cancelled";
    }
    changed.notify_all();return true;
  }catch(const foxicmp::progressive::Error& e){
    if(error)*error=QString::fromUtf8(e.what());
    if(reasonCode && v2Bridge && v2Bridge->usesCatalog())*reasonCode=QString::fromStdString(e.code);
    return false;
  }catch(const std::exception& e){if(error)*error=QString::fromUtf8(e.what());return false;}
}

void ProgressiveSource::publishedHeartbeat() {
  std::lock_guard<std::mutex> lock(mutex);
  if (!published || stopping.load() || !currentStatus.error.isEmpty()) return;
  lastPublished = std::chrono::steady_clock::now();
  currentStatus.connected = true;
  changed.notify_all();
}
void ProgressiveSource::publishedError(const QString &error) {
  if (published) fail(error);
}
void ProgressiveSource::watchPublished() {
  while(!stopping.load()) {
    harvester::Snapshot current;
    {std::lock_guard<std::mutex> lock(mutex);current=snapshot;}
    try {
      if(v2Bridge)v2Bridge->verify();
      else for(int i=0;i<2;++i)checkFile(files[i],current.tracks[i].available);
    }catch(const std::exception& e){if(!stopping.load())fail(QString::fromUtf8(e.what()));return;}
    std::unique_lock<std::mutex> lock(mutex);
    const bool selectedComplete=selectedAudioTrack<=0 || size_t(selectedAudioTrack)>=snapshot.tracks.size() ||
        snapshot.tracks[selectedAudioTrack].required || currentStatus.mediaComplete[selectedAudioTrack];
    if((!currentStatus.complete || !selectedComplete) && std::chrono::steady_clock::now()-lastPublished>std::chrono::seconds(15)) {
      currentStatus.connected=false;changed.notify_all();
    }
    changed.wait_for(lock,std::chrono::milliseconds(100),[this]{return stopping.load();});
  }
}
QJsonObject ProgressiveSource::playbackDetails() const {
  if(!v2Bridge)return {{"schemaVersion",1}};
  std::lock_guard<std::mutex> lock(mutex);
  auto result=QJsonDocument::fromJson(QByteArray::fromStdString(v2Bridge->details())).object();
  result["generation"]=QString::number(snapshot.generation);result["revision"]=QString::number(snapshot.revision);return result;
}

QString ProgressiveSource::streamId() const {return v2Bridge ? registeredReaderId()+".v2" : id();}
