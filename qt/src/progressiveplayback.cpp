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
  progressive = next;
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
  const auto details=progressive->playbackDetails();for(auto it=details.begin();it!=details.end();++it)result[it.key()]=it.value();return result;
}
void Window::releaseProgressive(const QString &state) {
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
  if (!seekAvailable(seconds, seconds)) {
    showOsd(
        uiTr("Выбранная позиция недоступна или отсутствует в кэше просмотра"));
    timeline->position = pos;
    timeline->update();
    return false;
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
    progressiveStarted = true;
    progressiveStreaming = !status.complete;
    progressiveBuffering = false;
    const QString url = "foxicmp-session://" + progressive->streamId();
    if (!player->loadSource(
            url + "/video",
            {{"audio-files-append", url + "/audio"},
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
             {"demuxer-lavf-format", "mov"},
             {"demuxer-lavf-probesize", "32768"},
             {"demuxer-lavf-analyzeduration", "0.2"}}))
      return;
    if (!progressive)
      return;
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
