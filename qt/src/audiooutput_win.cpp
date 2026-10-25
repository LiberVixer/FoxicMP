#include "audiooutput.h"
#include "audiomirror.h"
#include "player.h"
#include "i18n.h"
#include <QSettings>
#include <mmdeviceapi.h>
#include <windows.h>

namespace {
QString defaultDevice() {
  IMMDeviceEnumerator *enumerator = nullptr; IMMDevice *device = nullptr; LPWSTR id = nullptr;
  QString result;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&enumerator)))) {
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
      if (SUCCEEDED(device->GetId(&id))) { result = "wasapi/" + QString::fromWCharArray(id); CoTaskMemFree(id); }
      device->Release();
    }
    enumerator->Release();
  }
  return result;
}
}
AudioOutput::AudioOutput(Player *p) : QObject(p), player(p) { setObjectName("audioOutputRouter"); }
AudioOutput::~AudioOutput() = default;
QString AudioOutput::primaryDevice(Player *p, const QString &saved) {
  if (saved == "auto") return saved;
  for (const auto &entry : p->get("audio-device-list").toList())
    if (entry.toMap().value("name").toString() == saved) return saved;
  return "auto";
}
void AudioOutput::apply(Player *p, QSettings &settings) {
  auto *router = p->findChild<AudioOutput *>("audioOutputRouter");
  if (!router) router = new AudioOutput(p);
  router->configure(settings);
}
void AudioOutput::status(const QString &message) { player->setProperty("foxicmpAudioOutputStatus", message); }
void AudioOutput::fallback() {
  mirror.reset(); player->setProperty("foxicmpDualActive", false);
  const auto device = primaryDevice(player, primary);
  if (player->get("audio-device").toString() != device) player->set("audio-device", device);
}
void AudioOutput::configure(QSettings &settings) {
  const auto first = settings.value("audio/device", "auto").toString();
  const auto second = settings.value("audio/secondDevice").toString();
  const bool on = settings.value("audio/dualOutput", false).toBool();
  if (first == primary && second == secondary && on == enabled && (!on || mirror)) return;
  primary = first; secondary = second; enabled = on; fallback();
  if (!enabled) {status(uiTr("Дублирование звука выключено")); return;}
  start();
}
void AudioOutput::start() {
  const auto actualPrimary = primary == "auto" ? defaultDevice() : primary;
  bool present = false;
  for (const auto &entry : player->get("audio-device-list").toList())
    if (entry.toMap().value("name").toString() == secondary) present = true;
  if (!present || !secondary.startsWith("wasapi/") || secondary.compare(actualPrimary, Qt::CaseInsensitive) == 0) {
    status(uiTr("Выберите другой доступный выход WASAPI; используется основной")); return;
  }
  try {
    mirror = std::make_unique<AudioMirror>(player, secondary, [this](bool active, const QString &message) {
      player->setProperty("foxicmpDualActive", active); status(message);
    });
    status(uiTr("Второй выход ожидает воспроизведения"));
  } catch (const std::exception &e) { status(uiTr("Второй выход недоступен: ") + QString::fromUtf8(e.what())); }
}
