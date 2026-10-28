#include "controls.h"
#include "devicesource.h"
#include "framepreview.h"
#include "i18n.h"
#include "player.h"
#include "progressivesource.h"
#include "timeline.h"
#include "window.h"
#include <QtWidgets>

void Window::cancelProgressiveOpening() {
  if(legacyOpening && !legacyOpening->abandoned.exchange(true))
    emit progressiveOpeningFinished(false,uiTr("Подготовка источника отменена"));
}
bool Window::openProgressiveSession(const QString &manifest) {
  if(manifest.isEmpty() || legacyOpening)return false;
  emit playbackIntentChanged();
  const auto hook=legacyOpeningHook;
  try {
    legacyOpening=foxicmp::progressive::SourceOpening<ProgressiveSource>::launch([manifest,hook]{
      if(hook)hook();return ProgressiveSource::open(manifest);
    });
  }catch(const std::exception& error){
    emit progressiveOpeningFinished(false,QString::fromUtf8(error.what()));return false;
  }
  state->setText(uiTr("Подготовка прогрессивного источника…"));
  const auto job=legacyOpening;auto* poll=new QTimer(this);poll->setInterval(20);
  connect(poll,&QTimer::timeout,this,[this,job,poll]{
    if(job->expired() && !job->abandoned.exchange(true)) {
        const auto error=uiTr("Истекло время подготовки прогрессивного источника");
        state->setText(error);state->setToolTip(error);emit progressiveOpeningFinished(false,error);
    }
    if(!job->finished.load(std::memory_order_acquire))return;
    poll->stop();poll->deleteLater();legacyOpening.reset();
    if(job->abandoned.load())return;
    if(!job->candidate) {
      const auto error=uiTr("Прогрессивный источник: ")+QString::fromStdString(job->error);
      state->setText(error);state->setToolTip(error);emit progressiveOpeningFinished(false,error);return;
    }
    const bool opened=openProgressiveSource(job->candidate);
    emit progressiveOpeningFinished(opened,{});
  });
  poll->start();return true;
}
bool Window::openProgressiveSource(const std::shared_ptr<ProgressiveSource> &next) {
  if (!next) return false;
  emit playbackIntentChanged();
  ++playbackGeneration;
  pendingLaunchOptions = {};
  savePosition();
  stdinPlayback = false;
  subtitleUserSelection = false;
  watchedSubtitle.clear();
  clearAbLoop();
  pausedByMinimize = false;
  filePlays = playlistPlays = 0;
  releaseProgressive();
  player->command({"stop"});
  completionArmed = false;
  progressive = next;progressiveStartPosition=0;progressiveRestoreAudio.clear();progressiveRestoreSubtitle.clear();progressiveTracksReady=false;
  audioUserSelection = false;
  applyAudioSourceOptions();
  player->progressiveSource(next);
  progressiveStarted = false;
  progressiveStreaming = false;
  progressiveBuffering = true;
  progressiveUserPause = false;
  stopped = true;
  actions["frameNext"]->setEnabled(false);
  actions["frameBack"]->setEnabled(false);
  current.clear();
  currentId.clear();
  currentItem = nullptr;
  currentIndex = -1;
  pendingResume = -1;
  pos = 0;
  duration = next->status().duration;
  timeline->duration = duration;
  timeline->position = 0;
  timeline->available = next->status().available;
  preview->cancel();
  previewPopup->hide();
  lastError.clear();
  state->setText(uiTr("Буферизация локальных дорожек…"));
  updateProgressive();
  return true;
}
QJsonObject Window::progressivePlaybackStatus() const {
  if (!progressive) return {{"active", false}, {"state", "stopped"}};
  const auto status = progressive->status();
  const QString playback = !status.error.isEmpty() ? "error"
      : status.cancelled ? "cancelled"
      : status.complete && player->get("eof-reached").toBool() ? "finished"
      : !status.connected ? "waiting-downloader"
      : !progressiveStarted || stopped || progressiveBuffering ? "buffering"
      : progressiveUserPause || pausedByMinimize ? "paused" : "playing";
  QJsonObject result{{"active", true}, {"sessionId", progressive->id()}, {"state", playback},
          {"connected", status.connected}, {"complete", status.complete},
          {"durationSeconds", status.duration}, {"availableSeconds", status.available},
          {"positionSeconds", pos}, {"error", status.error}};
  const auto details=progressive->playbackDetails();for(auto it=details.begin();it!=details.end();++it)result[it.key()]=it.value();
  int count=0,index=-1;for(const auto& item:player->get("track-list").toList()){const auto track=item.toMap();if(track.value("type")=="audio"){if(track.value("selected").toBool())index=count;++count;}}
  result["audioSelection"]=QJsonObject{{"count",count},{"index",index}};return result;
}
void Window::releaseProgressive(const QString &state) {
  progressiveAttachedAudio.clear();progressiveAttachedSubtitles.clear();
  setProperty("foxicmpProgressiveTitle", {});
  if (!progressive)
    return;
  const auto sourceError = progressive->status().error;
  const auto error = sourceError.isEmpty() ? lastError : sourceError;
  emit progressiveSessionEnded(progressive->id(), error.isEmpty() ? state : "error", error);
  progressive->cancel();
  player->progressiveSource({});
  progressive.reset();
  progressiveStarted = false;
  timeline->available = -1;
  timeline->update();
  actions["frameNext"]->setEnabled(true);
  actions["frameBack"]->setEnabled(true);
}
bool Window::seekTo(double seconds) {
  if (stopped || stdinPlayback)
    return false;
  if (DeviceSource::isLive(current)) {
    showOsd(uiTr("Перемотка прямого входа недоступна"));
    return false;
  }
  if(progressive && !progressive->status().complete && progressive->playbackDetails().value("mediaTracks").toArray().size()>2 && seconds>10){
    showOsd(uiTr("При нескольких аудиодорожках переход дальше 10 секунд доступен после завершения загрузки"));return false;
  }
  if (!seekAvailable(seconds, seconds)) {
    showOsd(
        uiTr("Выбранная позиция недоступна или отсутствует в кэше просмотра"));
    timeline->position = pos;
    timeline->update();
    return false;
  }
  if(progressive && progressive->playbackDetails().value("mediaTracks").toArray().size()>2){
    // An unselected external audio demuxer need not retain its old packet cache.
    // Reopen decoder streams against the same pinned source/reader group; retain
    // IPC identity and re-read only confirmed bytes to restore all track clocks.
    progressiveStartPosition=seconds;progressiveRestoreAudio=player->get("aid").toInt()>0?QString::number(player->get("aid").toInt()):QStringLiteral("no");
    progressiveRestoreSubtitle=player->get("sid").toInt()>0?QString::number(player->get("sid").toInt()):QStringLiteral("no");
    progressiveStarted=false;progressiveTracksReady=false;progressiveBuffering=true;
    progressiveAttachedAudio.clear();progressiveAttachedSubtitles.clear();
    updateProgressive();return true;
  }
  const double offset = progressive ? progressive->status().timestampOffset : 0;
  return player->command(
      {"seek", QString::number(seconds + offset, 'f', 3), "absolute+exact"});
}
void Window::updateProgressive() {
  if (!progressive)
    return;
  const auto status = progressive->status();
  if (loopB >= 0 && !seekAvailable(loopA, loopB)) {
    clearAbLoop();
    showOsd(uiTr("Повтор A–B отключён: отрезок больше не доступен в кэше"));
  }
  if (!status.error.isEmpty()) {
    lastError = status.error;
    releaseProgressive();
    player->command({"stop"});
    state->setText(uiTr("Прогрессивный источник: ") + lastError);
    state->setToolTip(lastError);
    return;
  }
  duration = status.duration;
  timeline->duration = duration;
  timeline->available = status.available;
  timeline->update();
  if (!progressiveStarted) {
    if (!status.connected || (!status.complete && status.available < 10))
      return;
    progressiveStarted = true;progressiveTracksReady=false;
    progressiveStreaming = !status.complete;
    progressiveBuffering = false;
    const QString url = "foxicmp-session://" + progressive->streamId();
    const auto details=progressive->playbackDetails();
    const auto media=details.value("mediaTracks").toArray();
    const bool mixed=media.size()>2 || !details.value("subtitleTracks").toArray().isEmpty() || details.value("videoContainer")!=details.value("audioContainer");
    // The external audio demuxer inherits lavf options. A forced mov/matroska
    // format would send the other container to the wrong demuxer in mixed pairs.
    // Both headers have already been validated by the per-track media bridge.
    const QString demuxer=mixed?QString():details.value("mediaMode").toString().startsWith("separate-webm-")?"matroska":"mov";
    QStringList audioFiles;
    if(media.size()>2)for(int i=1;i<media.size();++i){audioFiles.append(url+(i==1?"/audio":"/track/"+QString::number(i)));if(i>1)progressiveAttachedAudio.insert(i);}
    QMap<QString,QString> options{{"audio-files-append",url+"/audio"}};
    if(!audioFiles.isEmpty()){
      // mpv path lists use ':' on Unix and ';' on Windows; escape URL colons
      // through the option parser, without constructing a command string.
      const auto separator=QDir::listSeparator();QStringList escaped;
      for(auto file:audioFiles){file.replace("\\","\\\\");file.replace(separator,QString("\\")+separator);escaped.append(file);}
      options.clear();options["audio-files"]=escaped.join(separator);
      options["aid"]=progressiveRestoreAudio.isEmpty()?QStringLiteral("1"):progressiveRestoreAudio;
    }
    options.insert(QMap<QString,QString>{
             {"rebase-start-time", "no"},
             {"start", QString::number(status.timestampOffset+(status.complete?progressiveStartPosition:0), 'f', 6)},
             {"pause", "yes"},
             {"cache", "yes"},
             {"cache-pause", "no"},
             {"demuxer-readahead-secs", "20"},
             {"demuxer-max-bytes", "128MiB"},
             {"demuxer-max-back-bytes", "128MiB"},
             {"sub-auto", "no"},
             {"audio-file-auto", "no"},
             {"demuxer", "lavf"},
             {"demuxer-lavf-format", demuxer},
             {"demuxer-lavf-probesize", "32768"},
             {"demuxer-lavf-analyzeduration", "0.2"}});
    if(!player->loadSource(url+"/video",options))return;
    if(status.complete)progressiveStartPosition=0;
    if (!progressive)
      return;
  }
  // Subtitles may become ready after A/V has started.
  const auto url="foxicmp-session://"+progressive->streamId();
  if(progressiveTracksReady && player->get("path").toString()==url+"/video" && player->get("track-list").toList().size()>=2){
    for(const auto& value:progressive->playbackDetails().value("subtitleTracks").toArray()){
      const auto track=value.toObject();const auto id=track.value("id").toString(),path=track.value("path").toString();
      if(!track.value("ready").toBool() || path.isEmpty() || progressiveAttachedSubtitles.contains(id))continue;
      const bool selected=!subtitleUserSelection && (track.value("default").toBool() || track.value("forced").toBool());
      if(player->command({"sub-add",path,selected?"select":"auto",track.value("label").toString(),track.value("language").toString()}))progressiveAttachedSubtitles.insert(id);
    }
  }
  if(progressiveTracksReady && !progressiveRestoreAudio.isEmpty() && progressiveAttachedAudio.size()+2==progressive->playbackDetails().value("mediaTracks").toArray().size()){
    player->set("aid",progressiveRestoreAudio);progressiveRestoreAudio.clear();
  }
  if(progressiveTracksReady && !progressiveRestoreSubtitle.isEmpty()){
    int ready=0;for(const auto& value:progressive->playbackDetails().value("subtitleTracks").toArray())if(value.toObject().value("ready").toBool())++ready;
    int loaded=0;bool present=progressiveRestoreSubtitle=="no";
    for(const auto& value:player->get("track-list").toList()){const auto track=value.toMap();if(track.value("type")=="sub"){++loaded;present|=track.value("id").toString()==progressiveRestoreSubtitle;}}
    // sub-add is asynchronous: wait for the actual streams, not queued commands.
    if(present && loaded>=ready && player->set("sid",progressiveRestoreSubtitle))progressiveRestoreSubtitle.clear();
  }
  if(progressiveStartPosition>0){
    // The growing external streams are nonseekable at open. Build the chosen
    // audio/video packet caches from their starts, then seek within that cache.
    // A nonzero loadfile start would ask the external demuxer for a byte seek
    // before its cache exists and can silently disable the chosen audio.
    const double target=status.timestampOffset+progressiveStartPosition;
    bool cached=false;
    for(const auto& range:player->get("demuxer-cache-state").toMap().value("seekable-ranges").toList()){
      const auto bounds=range.toMap();if(bounds.value("start").toDouble()<=target && bounds.value("end").toDouble()>=target+.25)cached=true;
    }
    if(!progressiveTracksReady || !cached){progressiveBuffering=true;player->set("pause",true);return;}
    if(!player->command({"seek",QString::number(target,'f',3),"absolute+exact"}))return;
    progressiveStartPosition=0;
  }
  const double ahead = status.available - pos;
  if (!status.connected || (!status.complete && ahead < 1))
    progressiveBuffering = true;
  else if (status.complete || ahead >= 5)
    progressiveBuffering = false;
  const bool pause =
      progressiveBuffering || progressiveUserPause || pausedByMinimize;
  if (player->get("pause").toBool() != pause)
    player->set("pause", pause);
  const QString message =
      progressiveBuffering ? (status.connected ? uiTr("Буферизация…")
                                               : uiTr("Ожидание загрузчика…"))
      : progressiveUserPause || pausedByMinimize ? uiTr("Пауза")
                                                 : uiTr("Воспроизведение");
  state->setText(message + uiTr(" • загружено до ") +
                 clockText(status.available));
  progressive->notify(progressiveBuffering                       ? "buffering"
                      : progressiveUserPause || pausedByMinimize ? "paused"
                                                                 : "playing");
  actions["frameNext"]->setEnabled(!progressiveBuffering);
  actions["frameBack"]->setEnabled(!progressiveBuffering);
  updateClock();
}
