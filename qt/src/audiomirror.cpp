#include "audiomirror.h"
#include "player.h"
#include "mpvlifetime.h"
#include "progressivesource.h"
#include "i18n.h"
#include <QJsonDocument>
#include <cmath>
#include <algorithm>

AudioMirror::AudioMirror(Player *p, const QString &device,
                         std::function<void(bool, const QString &)> callback, const char *backend)
    : QObject(p), player(p), status(std::move(callback)) {
  lifetime=MpvLifetime::create();handle=lifetime->handle;
  try {
  for (const auto &entry : {std::pair{"config", "no"}, {"terminal", "no"}, {"idle", "yes"},
       {"vid", "no"}, {"sid", "no"}, {"vo", "null"}, {"pause", "yes"},
       {"audio-client-name", "FoxiqMP qt secondary"}, {"audio-spdif", ""}, {"ytdl", "no"}})
    mpv_set_option_string(handle, entry.first, entry.second);
  mpv_set_option_string(handle, "ao", backend);
  mpv_set_option_string(handle, "audio-device", device.toUtf8().constData());
  int result = mpv_initialize(handle);
  if(result<0)throw std::runtime_error(mpv_error_string(result));
  lifetime->initialized=true;
  streams = std::make_unique<ProgressiveStreams>(handle, false);
  timer.setInterval(100);
  connect(&timer, &QTimer::timeout, this, &AudioMirror::synchronize);
  timer.start();
  }catch(...){lifetime->streams=std::move(streams);handle=nullptr;throw;}
}
AudioMirror::~AudioMirror() {
  timer.stop();
  if(streams){lifetime->retainedSource=streams->current();streams->clear();}
  if(lifetime){lifetime->requestStop();lifetime->streams=std::move(streams);}
  handle=nullptr;lifetime.reset();
}
bool AudioMirror::set(const char *name, const QString &value) {
  return mpv_set_property_string(handle, name, value.toUtf8().constData()) >= 0;
}
double AudioMirror::number(const char *name) const {
  double value = 0; mpv_get_property(handle, name, MPV_FORMAT_DOUBLE, &value); return value;
}
void AudioMirror::command(const QStringList &args) {
  QList<QByteArray> bytes; for (const auto &arg : args) bytes << arg.toUtf8();
  QList<const char *> pointers; for (const auto &arg : bytes) pointers << arg.constData(); pointers << nullptr;
  mpv_command_async(handle,0,pointers.data());
}
void AudioMirror::report(bool active, const QString &message) {
  running = active;
  if (lastMessage == message) return;
  lastMessage = message; status(active, message);
}
void AudioMirror::stop() {
  if (!key.isEmpty()) command({"stop"});
  streams->clear(); key.clear(); loaded = false;
}
QVariantMap AudioMirror::diagnostics() const {
  int paused=0, muted=0;mpv_get_property(handle,"pause",MPV_FORMAT_FLAG,&paused);mpv_get_property(handle,"mute",MPV_FORMAT_FLAG,&muted);
  return {{"active",running},{"position",number("time-pos")},{"drift",drift},{"paused",bool(paused)},
          {"mute",bool(muted)},{"volume",number("volume")},{"speed",number("speed")}};
}
void AudioMirror::synchronize() {
  while (auto *event = mpv_wait_event(handle, 0)) {
    if (event->event_id == MPV_EVENT_NONE) break;
    if (event->event_id == MPV_EVENT_FILE_LOADED) {
      loaded = true;
      command({"seek", QString::number(player->get("time-pos").toDouble(), 'f', 6), "absolute+exact"});
    } else if (event->event_id == MPV_EVENT_END_FILE && event->data) {
      auto *end = static_cast<mpv_event_end_file *>(event->data);
      if (end->reason == MPV_END_FILE_REASON_ERROR) {
        failedKey = key; stop(); report(false, uiTr("Второй выход недоступен; основной продолжает работу: ") + QString::fromUtf8(mpv_error_string(end->error))); return;
      }
    }
  }
  if (player->QObject::property("foxicmpPassthroughRequested").toBool()) {
    stop(); report(false, uiTr("Bitstream воспроизводится только на основном выходе; для двух выходов нужен PCM")); return;
  }
  if (player->get("idle-active").toBool()) { stop(); report(false, uiTr("Второй выход ожидает воспроизведения")); return; }
  QVariantMap audio;
  for (const auto &value : player->get("track-list").toList()) {
    const auto track = value.toMap();
    if (track.value("type") == "audio" && track.value("selected").toBool()) {audio = track; break;}
  }
  if (audio.isEmpty()) { stop(); report(false, uiTr("У источника нет выбранной аудиодорожки")); return; }
  const bool external = audio.value("external").toBool();
  QString url = external ? audio.value("external-filename").toString() : player->get("stream-open-filename").toString();
  if (url.isEmpty()) url = player->get("path").toString();
  if (url.startsWith("fd://") || url.startsWith("foxicmp-stdin:")) {
    stop(); report(false, uiTr("Одноразовый stdin воспроизводится только на основном выходе")); return;
  }
  const auto nextKey = url + '\n' + audio.value("id").toString();
  if (failedKey == nextKey) return;
  if (key != nextKey) {
    stop(); key = nextKey; failedKey.clear(); lastFilters.clear();
    if (player->progressive)
      streams->activate(player->progressive->current());
    if (url.startsWith("foxicmp-session:") && !external) url.replace("/video", "/audio");
    for (const auto *name : {"http-header-fields", "user-agent", "referrer", "options/audio-channels", "options/audio-format", "options/audio-samplerate"}) {
      mpv_node value{};
      if (mpv_get_property(player->handle, name, MPV_FORMAT_NODE, &value) >= 0) {
        mpv_set_property(handle, name, MPV_FORMAT_NODE, &value); mpv_free_node_contents(&value);
      }
    }
    set("pause", "yes");
    command({"loadfile", url, "replace"});
    report(false, uiTr("Подключение второго аудиовыхода…"));
  }
  if (!loaded) return;
  if (!external && !url.startsWith("foxicmp-session:")) set("aid", audio.value("id").toString());
  for (const auto *name : {"volume", "mute", "audio-delay", "audio-pitch-correction"}) {
    mpv_node value{};
    if (mpv_get_property(player->handle, name, MPV_FORMAT_NODE, &value) >= 0) {
      mpv_set_property(handle, name, MPV_FORMAT_NODE, &value); mpv_free_node_contents(&value);
    }
  }
  const auto filters = QJsonDocument::fromVariant(player->get("af")).toJson(QJsonDocument::Compact);
  if (filters != lastFilters) {
    mpv_node value{};
    if (mpv_get_property(player->handle, "af", MPV_FORMAT_NODE, &value) >= 0) {
      if (mpv_set_property(handle, "af", MPV_FORMAT_NODE, &value) >= 0) lastFilters = filters;
      mpv_free_node_contents(&value);
    }
  }
  const bool paused = player->get("pause").toBool() || player->get("paused-for-cache").toBool() || player->get("seeking").toBool();
  set("pause", paused ? "yes" : "no");
  drift = number("time-pos") - player->get("time-pos").toDouble();
  double rate = player->get("speed").toDouble();
  const double targetPosition = player->get("time-pos").toDouble();
  if (std::isfinite(targetPosition) && targetPosition >= 0. && std::abs(drift) > .25)
    command({"seek", QString::number(targetPosition, 'f', 6), "absolute+exact"});
  else if (!paused) rate *= 1. - std::clamp(drift * .05, -.005, .005);
  set("speed", QString::number(rate, 'f', 6));
  report(true, uiTr("Два аудиовыхода активны; второй следует времени основного"));
}
