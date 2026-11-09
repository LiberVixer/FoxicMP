#include "controls.h"
#include "devicesource.h"
#include "framepreview.h"
#include "i18n.h"
#include "player.h"
#include "progressivesource.h"
#include "../../shared/preparedclip.h"
#include "../../shared/progressivestreamdecoder.h"
#include "timeline.h"
#include "window.h"
#include <QtWidgets>
#include <cmath>

namespace {
QString subtitleFileKey(const QString& path) {
  if(path.isEmpty())return {};
  auto key=QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
  key=key.toCaseFolded();
#endif
  return key;
}
}


void Window::cancelProgressiveOpening() {
  if(legacyOpening && !legacyOpening->abandoned.exchange(true))
    emit progressiveOpeningFinished(false,uiTr("Подготовка источника отменена"));
}
bool Window::openProgressiveSession(const QString &manifest) {
  if(manifest.isEmpty() || legacyOpening)return false;
  emit playbackPreparationStarted();
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
  stdinPlayback = false;preparedPlayback=preparedFinished=false;
  subtitleUserSelection = false;
  watchedSubtitle.clear();
  clearAbLoop();
  pausedByMinimize = false;
  filePlays = playlistPlays = 0;
  releaseProgressive();
  player->command({"stop"});
  completionArmed = false;
  progressive = next;progressivePendingSeek=-1;progressiveSeekTarget=-1;progressiveRestoreAudio.clear();progressiveRestoreSubtitle.clear();progressiveTracksReady=false;
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
  const bool heldSparseEnd=progressive->sparseRf64() &&
      (progressiveUserPause || pausedByMinimize || progressivePendingSeek>=0);
  const QString playback = !status.error.isEmpty() ? "error"
      : status.cancelled ? "cancelled"
      : status.complete && !heldSparseEnd && player->get("eof-reached").toBool() ? "finished"
      : !status.connected ? "waiting-downloader"
      : !progressiveStarted || stopped || progressiveBuffering ? "buffering"
      : progressiveUserPause || pausedByMinimize ? "paused" : "playing";
  QJsonObject result{{"active", true}, {"sessionId", progressive->id()}, {"state", playback},
          {"connected", status.connected}, {"complete", status.complete},
          {"durationSeconds", status.duration}, {"availableSeconds", progressive->availableEnd(pos)},
          {"positionSeconds", pos}, {"error", status.error}};
  const auto details=progressive->playbackDetails();for(auto it=details.begin();it!=details.end();++it)result[it.key()]=it.value();
  if (details.value("mediaMode") == "standalone-mp3-gapless") {
    auto index = result.value("mp3Index").toObject();
    index["gaplessTrimApplied"] = progressiveTracksReady && player->pcmWindowActive();
    result["mp3Index"] = index;
  }
  QJsonArray appliedChapters;
  for(const auto& value:timeline->chapters) {
    const auto chapter=value.toMap();appliedChapters.append(QJsonObject{
      {"time",QString::number(qRound64(chapter.value("time").toDouble()*10000000.))},
      {"title",chapter.value("title").toString()}});
  }
  result["appliedMetadata"]=QJsonObject{{"revision",property("foxicmpAppliedMetadata").toMap().value("revision").toString()},
    {"title",mediaStatusTitle()},{"chapters",appliedChapters}};
  result["controls"]=QJsonObject{{"speed",player->get("speed").toDouble()},{"volumePercent",player->get("volume").toDouble()},{"muted",player->get("mute").toBool()}};
  result["seeking"] = progressiveSeekTarget >= 0;
  if (progressiveSeekTarget >= 0) result["seekTargetSeconds"] = progressiveSeekTarget;
  QJsonArray subtitles;
  for(const auto& value:result.value("subtitleTracks").toArray()) {
    auto track=value.toObject();bool loaded=false;
    for(const auto& actual:player->get("track-list").toList()) {
      const auto item=actual.toMap();loaded |= item.value("type")=="sub" && !track.value("path").toString().isEmpty() && subtitleFileKey(item.value("external-filename").toString())==subtitleFileKey(track.value("path").toString());
    }
    track["loaded"]=loaded;
    const auto error=progressiveSubtitleLoadErrors.value(track.value("id").toString());
    if(!error.isEmpty()){track["backendErrorCode"]="decoder_error";track["backendError"]=error;}
    subtitles.append(track);
  }
  result["subtitleTracks"]=subtitles;
  QJsonArray media;
  for(const auto& value:result.value("mediaTracks").toArray()) {
    auto track=value.toObject();const int index=track.value("index").toInt();bool loaded=false,selected=false;
    const auto suffix=index==0?QStringLiteral("/video"):index==1?QStringLiteral("/audio"):"/track/"+QString::number(index);
    for(const auto& actual:player->get("track-list").toList()) {
      const auto item=actual.toMap();if((index>0 || (index==0 && track.value("kind")=="audio")) && item.value("type")=="audio" && (index==0 || item.value("external-filename").toString().endsWith(suffix))){loaded=true;selected=item.value("selected").toBool();}
    }
    track["loaded"]=index==0?progressiveTracksReady:loaded;track["selected"]=selected;
    const auto error=progressiveAudioLoadErrors.value(track.value("id").toString());if(!error.isEmpty()){track["backendErrorCode"]="decoder_error";track["backendError"]=error;}
    media.append(track);
  }
  result["mediaTracks"]=media;
  int count=0,index=-1;for(const auto& item:player->get("track-list").toList()){const auto track=item.toMap();if(track.value("type")=="audio"){if(track.value("selected").toBool())index=count;++count;}}
  result["audioSelection"]=QJsonObject{{"count",count},{"index",index}};return result;
}
void Window::releaseProgressive(const QString &state) {
  progressiveAttachedAudio.clear();progressiveAudioRequests.clear();progressiveMediaMenuStamp.clear();
  progressiveAttachedSubtitles.clear();
  progressiveSubtitleRequests.clear();
  progressivePendingSeek = progressiveSeekTarget = -1;
  setProperty("foxicmpProgressiveTitle", {});
  setProperty("foxicmpAppliedMetadata", {});
  timeline->chapters.clear();refreshChapters();timeline->update();
  if (!progressive) { progressiveSubtitleLoadErrors.clear();progressiveAudioLoadErrors.clear(); return; }
  const auto sourceError = progressive->status().error;
  const auto error = sourceError.isEmpty() ? lastError : sourceError;
  emit progressiveSessionEnded(progressive->id(), error.isEmpty() ? state : "error", error);
  progressiveSubtitleLoadErrors.clear();progressiveAudioLoadErrors.clear();
  progressive->cancel();
  player->progressiveSource({});
  progressive.reset();
  progressiveStarted = false;
  timeline->available = -1;
  timeline->update();
  actions["frameNext"]->setEnabled(true);
  actions["frameBack"]->setEnabled(true);
}
QString Window::progressiveSubtitlePath(const QString &selection) const {
  for (const auto& value : player->get("track-list").toList()) {
    const auto track=value.toMap();
    if(track.value("type")=="sub" && track.value("id").toString()==selection)
      return subtitleFileKey(track.value("external-filename").toString());
  }
  return {};
}
bool Window::seekTo(double seconds) {
  if (stopped || stdinPlayback)
    return false;
  if (DeviceSource::isLive(current)) {
    showOsd(uiTr("Перемотка прямого входа недоступна"));
    return false;
  }
  if (!seekAvailable(seconds, seconds)) {
    showOsd(
        uiTr("Выбранная позиция недоступна или отсутствует в кэше просмотра"));
    timeline->position = pos;
    timeline->update();
    return false;
  }
  if (progressive && progressive->confirmedByteSeek()) {
    if (progressivePendingSeek >= 0) {
      if (progressive->playbackDetails().value("mediaMode").toString().startsWith("standalone-rf64-")) {
        // Each reader owns an immutable indexed window. A superseding seek,
        // especially backwards, must open its own window rather than seek in
        // the suffix selected by the previous still-pending request.
        reloadProgressive(seconds,progressiveRestoreAudio);
        return true;
      }
      progressivePendingSeek = progressiveSeekTarget = seconds;
      return true;
    }
    QString audio="no";
    for(const auto& value:player->get("track-list").toList()) {
      const auto track=value.toMap();if(track.value("type")=="audio" && track.value("selected").toBool()){audio=track.value("external-filename").toString();if(audio.isEmpty())audio="primary";}
    }
    reloadProgressive(seconds,audio);
    return true;
  }
  const double offset = progressive ? progressive->status().timestampOffset : 0;
  return player->command(
      {"seek", QString::number(seconds + offset, 'f', 3), "absolute+exact"});
}
void Window::reloadProgressive(double seconds,const QString &audioPath) {
  // loadfile cancels blocked stream callbacks without replacing the pinned source.
  // During an earlier reload sid may already be cleared by the backend.
  // Keep the user's saved subtitle path when another audio choice supersedes it.
  if(progressivePendingSeek<0)
    progressiveRestoreSubtitle=progressiveSubtitlePath(player->get("sid").toString());
  progressivePendingSeek=progressiveSeekTarget=seconds;
  progressiveRestoreAudio=audioPath;
  progressiveStarted=false;progressiveTracksReady=false;progressiveBuffering=true;
  progressiveAttachedAudio.clear();progressiveAudioRequests.clear();
  progressiveAttachedSubtitles.clear();progressiveSubtitleRequests.clear();
  updateProgressive();
}
AudioPcmWindow Window::progressivePcmWindow(double seconds) const {
  if (!progressive || !std::isfinite(seconds) || seconds < 0) return {};
  const auto details = progressive->playbackDetails();
  if (details.value("mediaMode") != "standalone-mp3-gapless") return {};
  const auto plan = details.value("mp3Index").toObject().value("trimPlan").toObject();
  bool firstOk = false, endOk = false;
  const auto begin = plan.value("beginSample").toString().toULongLong(&firstOk);
  const auto end = plan.value("endSample").toString().toULongLong(&endOk);
  const int rate = plan.value("sampleRate").toInt();
  if (!plan.value("ready").toBool() || !firstOk || !endOk || end <= begin ||
      (rate != 44100 && rate != 48000)) return {};
  const auto target = quint64(std::min(double(end-begin-1), std::round(seconds * rate)));
  return {begin + target, end, rate};
}
void Window::updateProgressive() {
  if(preparedPlayback && current.startsWith("foxicmp-continuous://")){
    // A paused decoder may never issue another read, and therefore never see
    // a producer failure via mpv. Observe the controller on the existing GUI
    // tick and detach the owned decoder without requiring client status polls.
    if(continuousFailure){
      const auto failure=continuousFailure();
      if(!failure.isEmpty()){
        continuousFailure={};
        if(lastError.isEmpty())lastError=failure;
        player->command({"stop"});
        timeline->setEnabled(false);actions["frameBack"]->setEnabled(false);
        state->setText(uiTr("Ошибка: ")+lastError);state->setToolTip(lastError);
        updateClock();return;
      }
    }
    const bool seek=continuousSeekReady && continuousSeekReady();timeline->setEnabled(seek);actions["frameBack"]->setEnabled(seek);return;
  }
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
  if (progressiveSeekTarget >= 0 && progressivePendingSeek < 0 &&
      !player->get("seeking").toBool() && std::abs(pos - progressiveSeekTarget) < .25)
    progressiveSeekTarget = -1;
  timeline->duration = duration;
  timeline->position = progressiveSeekTarget >= 0 ? progressiveSeekTarget : pos;
  timeline->available = status.available;
  timeline->update();
  if (!progressiveStarted) {
    if (!status.connected || (progressivePendingSeek<0 && !status.complete && status.available < 10))
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
    const QString demuxer=mixed?QString():details.value("videoContainer").toString()=="webm"?"matroska":"mov";
    QStringList audioFiles;
    if(media.size()>2)for(int i=1;i<media.size();++i){const auto track=media[i].toObject();if(!track.value("ready").toBool() || progressiveAudioLoadErrors.contains(track.value("id").toString()))continue;audioFiles.append(url+(i==1?"/audio":"/track/"+QString::number(i)));progressiveAttachedAudio.insert(track.value("id").toString());}
    const bool single=media.size()==1;
    const bool audioOnly=single && media[0].toObject().value("kind")=="audio";
    QMap<QString,QString> options;
    if(!single)options["audio-files-append"]=url+"/audio";
    else {options["aid"]=audioOnly?QStringLiteral("auto"):QStringLiteral("no");options["vid"]=audioOnly?QStringLiteral("no"):QStringLiteral("auto");}
    if(!audioFiles.isEmpty()){
      // mpv path lists use ':' on Unix and ';' on Windows; escape URL colons
      // through the option parser, without constructing a command string.
      const auto separator=QDir::listSeparator();QStringList escaped;
      for(auto file:audioFiles){file.replace("\\","\\\\");file.replace(separator,QString("\\")+separator);escaped.append(file);}
      options.clear();options["audio-files"]=escaped.join(separator);
      options["aid"]=progressiveRestoreAudio=="no"?QStringLiteral("no"):QStringLiteral("1");
    }
    if(progressive->confirmedByteSeek()) {
      // Keep AVIO seekable without MOV scanning the growing tail for an index.
      // Validation/indexing in ResourceSource/FragmentIndex remains enabled.
      options["demuxer-lavf-o"]="fflags=+ignidx";
      options["demuxer-seekable-cache"]="no";
    }
    // Audio-only libmpv can reach EOF during a paused seek very close to the
    // end. Retain this explicit sparse source until the user's pause ends;
    // updateProgressive acknowledges a confirmed, unpaused backend EOF below.
    if(progressive->sparseRf64())options["keep-open"]="yes";
    options.insert(QMap<QString,QString>{
             {"rebase-start-time", "no"},
             {"start", QString::number(status.timestampOffset, 'f', 6)},
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
    if (progressive->confirmedByteSeek()) {
      // A blocking stream callback waiting for an unpublished tail cannot
      // process a queued demuxer seek. Read on playback demand so prefetch
      // cannot park a deselected audio demuxer there. Source's confirmed
      // timeline still supplies the startup/resume buffer and tail guard.
      options["cache"] = "no";
      options["demuxer-readahead-secs"] = "0";
    }
    auto sourceUrl = url+(single?"/track/0":"/video");
    if (single && progressivePendingSeek>0 && details.value("mediaMode").toString().startsWith("standalone-rf64-")) {
      sourceUrl+="?start="+QString::number(progressivePendingSeek,'f',9);
      options["start"]=QString::number(progressivePendingSeek,'f',9);
    }
    if (details.value("mediaMode") == "standalone-mp3-gapless") {
      const auto window = progressivePcmWindow(progressivePendingSeek < 0 ? 0 : progressivePendingSeek);
      if (!window.valid()) {
        progressive->publishedError(uiTr("Недоступны проверенные границы PCM"));
        return;
      }
      const auto decodeSample = window.begin > quint64(window.sampleRate/4)
          ? (window.begin-window.sampleRate/4)/1152*1152 : 0;
      options["start"] = QString::number(double(decodeSample)/window.sampleRate, 'f', 9);
      options["hr-seek"] = "yes";
      options["hr-seek-demuxer-offset"] = "0.25";
      if (!player->loadPcmWindowSource(sourceUrl, options, window)) {
        progressive->publishedError(uiTr("Не удалось применить границы PCM"));return;
      }
    } else if(!player->loadSource(sourceUrl,options))return;
    if (!progressive)
      return;
  }
  // Complete optional audio can arrive after opening. Keep initial selection;
  // the separate asynchronous request namespace isolates attachment failures.
  const auto mediaDetails=progressive->playbackDetails();
  // An already active optional resource may fail after it was attached. Keep
  // the required primary audio alive and let the failed track remain visible
  // as an isolated error instead of closing the whole progressive session.
  for(const auto& value:mediaDetails.value("mediaTracks").toArray()) {
    const auto track=value.toObject();
    bool selected=false;
    const int failedIndex=track.value("index").toInt();
    const QString failedSuffix=failedIndex==1?QStringLiteral("/audio"):QStringLiteral("/track/")+QString::number(failedIndex);
    for(const auto& actual:player->get("track-list").toList()) {
      const auto item=actual.toMap();
      selected|=item.value("type")=="audio" && item.value("selected").toBool() && item.value("external-filename").toString().endsWith(failedSuffix);
    }
    if(!track.value("required").toBool() && track.value("state").toString()=="error" && selected) {
      progressive->selectAudioTrack(1);
      reloadProgressive(progressiveSeekTarget>=0?progressiveSeekTarget:pos,"foxicmp-session://"+progressive->streamId()+"/audio");
      return;
    }
  }
  const auto metadata=mediaDetails.value("metadata").toObject();
  // This stamp is session-local and cleared on release. Empty chapter arrays
  // and title-only revisions are meaningful replacements, too.
  if(property("foxicmpAppliedMetadata").toMap()!=metadata.toVariantMap()) {
    setProperty("foxicmpAppliedMetadata",metadata.toVariantMap());
    updatePlaylistChapters();
    updateWindowTitle();
  }
  auto menuStamp=mediaDetails.value("revision").toString();
  for(const auto& entry:mediaDetails.value("mediaTracks").toArray())menuStamp+=QStringLiteral("/")+entry.toObject().value("errorCode").toString();
  if(progressiveMediaMenuStamp!=menuStamp){progressiveMediaMenuStamp=menuStamp;refreshTracks();}
  if(progressiveTracksReady && progressivePendingSeek<0 && progressiveAudioRequests.isEmpty()) {
    const auto audioUrl="foxicmp-session://"+progressive->streamId();
    for(const auto& value:mediaDetails.value("mediaTracks").toArray()) {
      const auto track=value.toObject();const auto id=track.value("id").toString();const int index=track.value("index").toInt();
      if(index<2 || track.value("required").toBool() || !track.value("ready").toBool() || progressiveAttachedAudio.contains(id) || progressiveAudioLoadErrors.contains(id))continue;
      const auto request=(quint64(0x4155)<<48) | ++progressiveAudioSequence;
      progressiveAudioRequests.insert(request,id);progressiveAttachedAudio.insert(id);
      player->command({"audio-add",audioUrl+"/track/"+QString::number(index),"auto",track.value("label").toString(),track.value("language").toString()},request);break;
    }
  }
  // Subtitles may become ready after A/V has started.
  const auto url="foxicmp-session://"+progressive->streamId();
  if(progressiveTracksReady && player->get("path").toString()==url+"/video" && player->get("track-list").toList().size()>=2){
    int loadedSubtitles=0;
    for(const auto& track:player->get("track-list").toList())
      loadedSubtitles += track.toMap().value("type") == "sub";
    for(const auto& value:progressive->playbackDetails().value("subtitleTracks").toArray()){
      const auto track=value.toObject();const auto id=track.value("id").toString(),path=track.value("path").toString();
      if(!track.value("ready").toBool() || path.isEmpty() || progressiveAttachedSubtitles.contains(id) || progressiveSubtitleLoadErrors.contains(id))continue;
      // sub-add performs asynchronous I/O; serialize additions so runtime
      // IDs retain the stable published order, including after a seek.
      if (loadedSubtitles < progressiveAttachedSubtitles.size()) break;
      const bool selected=!subtitleUserSelection && (track.value("default").toBool() || track.value("forced").toBool());
      const auto request=(quint64(0x5355)<<48) | ++progressiveSubtitleSequence;
      progressiveSubtitleRequests.insert(request,id);progressiveAttachedSubtitles.insert(id);
      player->command({"sub-add",path,selected?"select":"auto",track.value("label").toString(),track.value("language").toString()},request);
      break;
    }
  }
  if (progressivePendingSeek >= 0) {
    bool wantSubtitle=false;
    int loaded=0,ready=0;QString desiredSubtitle="no";
    for(const auto& value:progressive->playbackDetails().value("subtitleTracks").toArray()) {
      const auto track=value.toObject();
      if(!track.value("ready").toBool() || progressiveSubtitleLoadErrors.contains(track.value("id").toString()))continue;
      ++ready;wantSubtitle |= !progressiveRestoreSubtitle.isEmpty() && subtitleFileKey(track.value("path").toString())==progressiveRestoreSubtitle;
    }
    for(const auto& value:player->get("track-list").toList()) {
      const auto track=value.toMap();
      if(track.value("type")=="sub") {
        ++loaded;
        if(wantSubtitle && subtitleFileKey(track.value("external-filename").toString())==progressiveRestoreSubtitle)desiredSubtitle=track.value("id").toString();
      }
    }
    const bool subtitleReady=!wantSubtitle || desiredSubtitle!="no";
    QString desiredAudio="no";
    for(const auto& value:player->get("track-list").toList()) {
      const auto track=value.toMap();if(track.value("type")=="audio" && (track.value("external-filename").toString()==progressiveRestoreAudio || progressiveRestoreAudio=="primary" && track.value("external-filename").toString().isEmpty()))desiredAudio=track.value("id").toString();
    }
    const auto audio=player->get("aid").toInt();
    const bool audioReady=desiredAudio=="no"?audio<=0:audio==desiredAudio.toInt();
    if(progressiveTracksReady && !audioReady)player->set("aid",desiredAudio);
    if (!progressiveTracksReady || !audioReady || !subtitleReady || loaded < ready) {
      progressiveBuffering = true;
      player->set("pause", true);
      return;
    }
    const auto subtitle = player->get("sid").toInt();
    const bool subtitleSelected = desiredSubtitle == "no" ? subtitle <= 0
        : subtitle == desiredSubtitle.toInt();
    if (!subtitleSelected) { player->set("sid", desiredSubtitle); return; }
    if (player->get("seeking").toBool()) return;
    // A newly selected optional track can lag behind the video clock. Keep
    // that target pending until its published packets cover it, without EOF.
    if(!status.complete && progressivePendingSeek>=progressive->availableEnd(progressivePendingSeek)-.2) {
      progressiveBuffering=true;player->set("pause",true);return;
    }
    // Sparse RF64 windows were already loaded with the exact start option.
    if (mediaDetails.value("mediaMode") != "standalone-mp3-gapless" &&
        !progressive->sparseRf64() &&
        !player->command({"seek", QString::number(status.timestampOffset + progressivePendingSeek, 'f', 9), "absolute+exact"}))
      return;
    progressivePendingSeek = -1;
    progressiveRestoreAudio.clear();
    progressiveRestoreSubtitle.clear();
  }
  // While the decoder is still resolving a seek, its last decoded position
  // can remain near zero after reopening. Buffer against the requested target,
  // not that stale clock, so a tail wait stays visible and another seek can
  // retire the blocked readers. positionSeconds remains the real decoder clock.
  const double ahead = progressive->availableEnd(progressiveSeekTarget >= 0 ? progressiveSeekTarget : pos) - (progressiveSeekTarget >= 0 ? progressiveSeekTarget : pos);
  if (!status.connected || (!status.complete && ahead < 1))
    progressiveBuffering = true;
  else if (status.complete || ahead >= 5)
    progressiveBuffering = false;
  const bool pause =
      progressiveBuffering || progressiveUserPause || pausedByMinimize;
  if(progressive->sparseRf64() && status.complete && !pause &&
      progressivePendingSeek<0 && progressiveTracksReady && player->get("eof-reached").toBool()) {
    player->command({"stop"});
    emit player->ended(true);
    return;
  }
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

bool Window::hasPlayback()const{return !stopped || bool(progressive) || stdinPlayback || (preparedPlayback && !preparedFinished);}
bool Window::openPreparedClip(const std::shared_ptr<foxicmp::progressive::PreparedClip>& source,const QString& title){
  if(!source || source->cancelled.load())return false;
  emit playbackIntentChanged();savePosition();closePlaybackFile();
  preparedPlayback=true;preparedFinished=false;preparedDuration=0;holdAfterLoad=false;lastError.clear();
  current=QString::fromStdString(source->uri());expectedPlaybackSource=current;pendingLaunchOptions={};pendingTrackSelections.clear();
  audioUserSelection=subtitleUserSelection=false;pendingResume=-1;pos=duration=0;timeline->position=timeline->duration=0;timeline->available=-1;timeline->chapters.clear();
  applyAudioSourceOptions();player->attachPrepared(source);state->setText(uiTr("Подготовка фрагмента…"));
  return player->loadSource(current,{{"pause","no"},{"aid","auto"},{"vid","auto"},{"sub-auto","no"},{"audio-file-auto","no"},{"force-media-title",title},{"cache","no"}});
}
QString Window::openContinuousSource(const std::shared_ptr<foxicmp::progressive::StreamDecoderSource>& source,double total,const QString& title,std::function<bool()> seekReady,std::function<QString()> failure,double startSeconds){
  if(!source || source->stopped())return {};
  emit playbackIntentChanged();savePosition();closePlaybackFile();
  preparedPlayback=true;preparedFinished=false;preparedDuration=total;continuousSeekReady=std::move(seekReady);continuousFailure=std::move(failure);holdAfterLoad=false;lastError.clear();
  pendingLaunchOptions={};pendingTrackSelections.clear();audioUserSelection=subtitleUserSelection=false;pendingResume=-1;
  pos=0;duration=total;timeline->position=0;timeline->duration=total;timeline->available=-1;timeline->chapters.clear();
  applyAudioSourceOptions();state->setText(uiTr("Подготовка видео…"));
  QMap<QString,QString> options{{"pause","no"},{"force-media-title",title}};
  if(startSeconds>0){options["start"]=QString::number(startSeconds,'f',7);options["hr-seek"]="yes";options["hr-seek-demuxer-offset"]="0.25";}
  if(!player->loadContinuousSource(source,options)){preparedPlayback=false;return {};}
  current=expectedPlaybackSource=player->continuousSourceUri();return current;
}
QJsonObject Window::preparedClipStatus(const QString& uri)const{
  const bool owned=preparedPlayback && current==uri;
  const bool continuous=owned && uri.startsWith("foxicmp-continuous://");
  const bool loaded=owned && player->get("path").toString()==uri && player->get("duration").toDouble()>0;
  // pause is the user's intent; paused-for-cache is mpv's decoder starvation.
  // In particular, an accepted publication or a blocked read-ahead cursor
  // alone must not turn a loaded, progressing decoder into "buffering".
  const auto state=!owned?QString("closed"):!lastError.isEmpty()?QString("error"):preparedFinished?QString("finished"):!loaded?QString("buffering"):player->get("pause").toBool()?QString("paused"):continuous && player->get("paused-for-cache").toBool()?QString("buffering"):QString("playing");
  QJsonObject result{{"state",state},{"active",owned && state!="finished" && state!="error"},{"positionSeconds",owned?(preparedFinished?preparedDuration:player->get("time-pos").toDouble()):0},{"durationSeconds",owned?preparedDuration:0},{"error",owned?lastError:QString()},{"videoCodec",owned?player->get("video-codec").toString():QString()},{"audioCodec",owned?player->get("audio-codec-name").toString():QString()}};
  if(continuous){result["pcmWindowApplied"]=player->pcmWindowActive();result["audioOutput"]=player->get("current-ao").toString();}
  if(continuous)result["reasonCode"]=state=="buffering"?(loaded?"waiting-range":"decoder-opening"):state=="paused"?"paused":state=="finished"?"source-complete":state=="error"?"decoder-error":"";
  return result;
}
bool Window::preparedClipControl(const QString& uri,const QString& command,double seconds){
  if(!preparedPlayback || current!=uri)return false;
  if(command=="cancel"){closeCurrent();return true;}
  if(player->get("path").toString()!=uri || player->get("duration").toDouble()<=0 || preparedFinished)return false;
  if(command=="pause")return player->set("pause",true);
  if(command=="play")return player->set("pause",false);
  if(command=="seek")return seekTo(seconds);
  return false;
}
