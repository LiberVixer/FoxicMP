#include "progressivesource.h"
#include "progressivefile.h"
#include "i18n.h"
#include "../../shared/progressivefmp4bridge.h"
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <cmath>
void ProgressiveSource::legacyIoCheckpoint(uint64_t revision) {
  const auto hook=std::atomic_load(&legacyIoHook);if(hook)(*hook)(revision);
}
void ProgressiveSource::cancel() {
  stopping.store(true);
  if (v2Bridge) v2Bridge->cancel();
  {
    std::lock_guard<std::mutex> lock(mutex);
    currentStatus.cancelled = true;
  }
  changed.notify_all();
}
void ProgressiveSource::notify(const QString &state) {
  if (state != "buffering" && state != "playing" && state != "paused" &&
      state != "error")
    return;
  std::lock_guard<std::mutex> lock(mutex);
  playbackState = state;
}
ProgressiveSource::Status ProgressiveSource::status() const {
  std::lock_guard<std::mutex> lock(mutex);
  auto result=currentStatus;
  if(selectedAudioTrack>0 && size_t(selectedAudioTrack)<snapshot.tracks.size() && !snapshot.tracks[selectedAudioTrack].required) {
    result.available=std::min(result.available,result.mediaAvailable[selectedAudioTrack]);
    result.complete=result.complete && result.mediaComplete[selectedAudioTrack];
  }
  return result;
}
void ProgressiveSource::selectAudioTrack(int track) {
  std::lock_guard<std::mutex> lock(mutex);
  selectedAudioTrack=track>0 && size_t(track)<snapshot.tracks.size()?track:-1;
}
QString ProgressiveSource::id() const {
  std::lock_guard<std::mutex> lock(mutex);
  return QString::fromStdString(snapshot.sessionId);
}
int64_t ProgressiveSource::size(int track) const {
  std::lock_guard<std::mutex> lock(mutex);
  return track >= 0 && size_t(track) < snapshot.tracks.size() ? int64_t(snapshot.tracks[track].total) : -1;
}
bool ProgressiveSource::confirmedByteSeek() const {
  std::lock_guard<std::mutex> lock(mutex);
  // A decoder opened while downloading may still have an unseekable stream.
  // Once every required track is complete, reload against the same pinned
  // source instead of limiting ordinary seeks to that decoder's packet cache.
  return currentStatus.complete || (v2Bridge && (snapshot.tracks.size()>2 || v2Bridge->isSegmented() || v2Bridge->isGrowing() || v2Bridge->isStandalone()));
}
ProgressiveSource::DecoderWindow ProgressiveSource::decoderWindow(double seconds) const {
  if (!std::isfinite(seconds) || seconds <= 0) return {};
  // Samples() belongs to the publication worker. Protect it against vector
  // growth, and wait before taking the UI-visible metadata mutex.
  std::lock_guard<std::mutex> indexLock(indexMutex);
  std::lock_guard<std::mutex> lock(mutex);
  if (!v2Bridge ||
      seconds >= (v2Bridge->sparseRf64()?double(v2Bridge->sparseReadyEnd(int64_t(seconds*10000000)))/10000000.:currentStatus.available) || snapshot.tracks.size()!=1 ||
      stopping.load() || !currentStatus.error.isEmpty()) return {};
  const auto details=QJsonDocument::fromJson(QByteArray::fromStdString(v2Bridge->details())).object();
  const auto mode=details.value("mediaMode").toString();
  if (!mode.startsWith("standalone-rf64-")) return {};
  const auto& samples=indexes[0].Samples();
  const auto target=harvester::AudioSeekSample(samples,int64_t(seconds*10000000));
  if (samples.empty() || target>=samples.size()) return {};
  // The validated RF64 bridge emits one PCM packet per moof/mdat pair.
  // Derive framing from the first fragment, rather than duplicating its size.
  uint64_t init=0,moof=0;
  while(init<samples.front().offset) {
    uint8_t bytes[8];
    if(!v2Bridge->readIndex(0,init,bytes,sizeof bytes))return {};
    harvester::BoxData box{bytes,sizeof bytes};const auto n=box.U32(0);
    if(n<8 || n>samples.front().offset-init)return {};
    if(box.U32(4)==harvester::FourCC("moof")){moof=n;break;}
    init+=n;
  }
  if(!moof || samples.front().offset-init!=moof+8)return {};
  const auto fragment=samples[target].offset-(moof+8);
  uint8_t bytes[8];
  if(!v2Bridge->readIndex(0,fragment,bytes,sizeof bytes))return {};
  harvester::BoxData box{bytes,sizeof bytes};
  if(box.U32(0)!=moof || box.U32(4)!=harvester::FourCC("moof"))return {};
  return {init,fragment};
}
bool ProgressiveSource::seekable(int track, uint64_t offset) const {
  std::lock_guard<std::mutex> lock(mutex);
  return track >= 0 && size_t(track) < snapshot.tracks.size() && !stopping.load() &&
         currentStatus.error.isEmpty() &&
         offset <= snapshot.tracks[track].available;
}
bool ProgressiveSource::sparseRf64() const { return v2Bridge && v2Bridge->sparseRf64(); }
double ProgressiveSource::availableEnd(double seconds) const {
  if(sparseRf64() && std::isfinite(seconds) && seconds>=0 && seconds<=status().duration)
    return double(v2Bridge->sparseReadyEnd(int64_t(seconds*10000000)))/10000000.;
  return status().available;
}
int64_t ProgressiveSource::read(int track, uint64_t offset, char *buffer,
                                uint64_t count, const std::atomic<bool> *interrupted) {
  if (track < 0 || !buffer || count == 0 ||
      offset > uint64_t(INT64_MAX))
    return -1;
  std::unique_lock<std::mutex> lock(mutex);
  if(size_t(track)>=snapshot.tracks.size())return -1;
  for (;;) {
    if (stopping.load() || (interrupted && interrupted->load()) || !currentStatus.error.isEmpty())
      return -1;
    if(v2Bridge && v2Bridge->optionalTrackFailed(track))return -1;
    const auto item = snapshot.tracks[track];
    if (currentStatus.connected) {
      if (offset < item.available) {
        const uint64_t length =
            std::min<uint64_t>({count, item.available - offset, 1024 * 1024});
        if(v2Bridge && !v2Bridge->payloadReady(track,offset,std::min<uint64_t>(length,65536))) {
          changed.wait_for(lock,std::chrono::milliseconds(50));continue;
        }
        lock.unlock();
        const auto result = v2Bridge ? v2Bridge->readDecoderPayload(track,offset,buffer,size_t(length))
                                    : ProgressiveFile::read(files[track], offset, buffer, size_t(length));
        lock.lock();
        if(stopping.load() || (interrupted && interrupted->load()) || !currentStatus.error.isEmpty())return -1;
        if (result <= 0) {
          if (v2Bridge && v2Bridge->optionalTrackFailed(track)) {
            changed.notify_all();
            return -1;
          }
          currentStatus.error = uiTr("Ошибка чтения подтверждённых данных");
          changed.notify_all();
          return -1;
        }
        return result;
      }
      if (harvester::EndReady(item))
        return offset == item.total ? 0 : -1;
    }
    if (interrupted) changed.wait_for(lock, std::chrono::milliseconds(50));
    else changed.wait(lock);
  }
}
ProgressiveSource::Status ProgressiveSource::indexStatus(const harvester::Snapshot &value) {
  std::lock_guard<std::mutex> indexLock(indexMutex);
  Status result;
  const int64_t duration =
      value.duration > 0 ? value.duration : indexed[0].duration();
  double end = double(duration) / 10000000.;
  bool complete = true;
  for (int i = 0; size_t(i) < value.tracks.size(); ++i) {
    const auto &track = value.tracks[i];
    if (!track.initializationReady) continue;
    if (v2Bridge && !track.required && v2Bridge->optionalTrackFailed(i)) {
      result.mediaAvailable[i] = 0;
      result.mediaComplete[i] = false;
      continue;
    }
    if (track.fragmented) try {
      indexes[i].Update(
          [&, i](uint64_t offset, void *buffer, size_t count) {
            if(stopping.load())return false;
            return v2Bridge ? v2Bridge->readIndex(i,offset,buffer,count)
                            : ProgressiveFile::read(files[i], offset, buffer, count) == int64_t(count);
          },
          track.available, i || track.audio?1:0, harvester::EndReady(track),
          value.timestampOffset, v2Bridge && v2Bridge->videoCodec()=="hevc",
          v2Bridge && v2Bridge->videoCodec()=="av1",v2Bridge && v2Bridge->videoCodec()=="vp9",v2Bridge && v2Bridge->audioCodec(i?i:1)=="opus",v2Bridge && v2Bridge->isWebm(i),v2Bridge && v2Bridge->isStandaloneAudio() && v2Bridge->audioCodec(i?i:1)=="flac",v2Bridge && v2Bridge->isStandaloneAudio() && v2Bridge->audioCodec(i?i:1)=="pcm-s16le",v2Bridge && v2Bridge->isStandaloneAudio() && v2Bridge->audioCodec(i?i:1)=="ac3",v2Bridge && v2Bridge->isStandaloneAudio() && v2Bridge->audioCodec(i?i:1)=="eac3",v2Bridge && v2Bridge->isStandaloneAudio() && v2Bridge->audioCodec(i?i:1)=="pcm-s24le");
    } catch(const std::exception& error) {
      if(!v2Bridge || track.required)throw;
      v2Bridge->rejectOptionalIndex(i,error.what());continue;
    }
    result.mediaAvailable[i]=double(harvester::ReadyEnd(track.fragmented ? indexes[i].Samples() : indexed[i].samples(),track.available,duration))/10000000.;
    result.mediaComplete[i]=harvester::EndReady(track);
    if (!track.required) continue;
    end=std::min(end,result.mediaAvailable[i]);
    complete=complete && result.mediaComplete[i];
  }
  result.duration = double(duration) / 10000000.;
  result.timestampOffset = double(value.timestampOffset) / 10000000.;
  result.available = end;
  if(v2Bridge && v2Bridge->sparseRf64())result.available=result.mediaAvailable[0]=double(v2Bridge->sparseReadyEnd(0))/10000000.;
  result.complete = complete;
  return result;
}
void ProgressiveSource::updateIndexes() {
  const auto value=indexStatus(snapshot);
  currentStatus.duration=value.duration;currentStatus.timestampOffset=value.timestampOffset;
  currentStatus.available=value.available;currentStatus.complete=value.complete;currentStatus.mediaAvailable=value.mediaAvailable;currentStatus.mediaComplete=value.mediaComplete;
}

void ProgressiveSource::fail(const QString &error) {
  std::lock_guard<std::mutex> lock(mutex);
  currentStatus.error = error;
  changed.notify_all();
}

namespace {
struct StreamReader {
  std::shared_ptr<ProgressiveSource> source;
  int track;
  uint64_t position = 0;
  std::atomic<bool> cancelled{false};
  ProgressiveSource::DecoderWindow window;
  uint64_t mapped(uint64_t at) const {
    return at<window.initialization ? at : at+window.fragment-window.initialization;
  }
};
} // namespace
ProgressiveStreams::ProgressiveStreams(mpv_handle *handle, bool ownsSession) : ownsSession(ownsSession) {
  if (mpv_stream_cb_add_ro(handle, "foxicmp-session", this, open) < 0)
    throw std::runtime_error("Cannot register progressive stream callbacks");
}
void ProgressiveStreams::activate(
    const std::shared_ptr<ProgressiveSource> &next) {
  std::lock_guard<std::mutex> lock(mutex);
  if (source && ownsSession)
    source->cancel();
  source = next;
}
void ProgressiveStreams::clear() { activate({}); }
std::shared_ptr<ProgressiveSource> ProgressiveStreams::current() {
  std::lock_guard<std::mutex> lock(mutex); return source;
}
int ProgressiveStreams::open(void *self, char *uri, mpv_stream_cb_info *info) {
  auto *streams = static_cast<ProgressiveStreams *>(self);
  std::shared_ptr<ProgressiveSource> source;
  {
    std::lock_guard<std::mutex> lock(streams->mutex);
    source = streams->source;
  }
  const QUrl url(QString::fromUtf8(uri));
  int track=url.path()=="/video"?0:url.path()=="/audio"?1:-1;
  if(url.path().startsWith("/track/")){bool ok=false;const auto index=url.path().mid(7).toInt(&ok);if(ok && source && index>=0 && source->size(index)>=0)track=index;}
  if (!source || track < 0 || url.host() != source->streamId())
    return MPV_ERROR_LOADING_FAILED;
  auto *reader = new StreamReader{source, track};
  if(track==0)reader->window=source->decoderWindow(QUrlQuery(url).queryItemValue("start").toDouble());
  *info = {};
  info->cookie = reader;
  info->read_fn = [](void *cookie, char *buffer, uint64_t count) -> int64_t {
    auto *reader = static_cast<StreamReader *>(cookie);
    if (reader->cancelled.load())
      return -1;
    if(reader->position<reader->window.initialization)
      count=std::min(count,reader->window.initialization-reader->position);
    const auto result =
        reader->source->read(reader->track, reader->mapped(reader->position), buffer, count, &reader->cancelled);
    if (result > 0)
      reader->position += uint64_t(result);
    return result;
  };
  info->seek_fn = [](void *cookie, int64_t position) -> int64_t {
    auto *reader = static_cast<StreamReader *>(cookie);
    if (reader->cancelled.load() || position < 0 ||
        uint64_t(position)>uint64_t(INT64_MAX)-(reader->window.fragment-reader->window.initialization) ||
        !reader->source->seekable(reader->track, reader->mapped(uint64_t(position))))
      return MPV_ERROR_UNSUPPORTED;
    reader->position = uint64_t(position);
    return position;
  };
  // Keep legacy/two-track startup unchanged. Schema-2 multi-audio uses lavf
  // fflags=+ignidx so MOV stops its opening scan at the first media fragment.
  // Its byte seeks remain bounded by the current writer-confirmed prefix;
  // reaching an unfinished tail still waits rather than returning EOF.
  if (!source->status().complete && !source->confirmedByteSeek())
    info->seek_fn = nullptr;
  info->size_fn = [](void *cookie) -> int64_t {
    auto *reader = static_cast<StreamReader *>(cookie);
    const auto size = reader->source->size(reader->track);
    return size > 0 ? size-int64_t(reader->window.fragment-reader->window.initialization) : int64_t(MPV_ERROR_UNSUPPORTED);
  };
  info->close_fn = [](void *cookie) {
    delete static_cast<StreamReader *>(cookie);
  };
  info->cancel_fn = [](void *cookie) {
    auto *reader = static_cast<StreamReader *>(cookie);
    reader->cancelled.store(true);
  };
  return 0;
}
