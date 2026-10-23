#include "audiooutput.h"
#include "i18n.h"
#include "player.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

AudioOutput::AudioOutput(Player *p) : QObject(p), player(p) {
  setObjectName("audioOutputRouter");
  connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
    lines += process.readAllStandardOutput();
    if (lines.size() > 16384) {
      lines.clear();
      return;
    }
    while (lines.contains('\n')) {
      const auto line = lines.left(lines.indexOf('\n'));
      lines.remove(0, line.size() + 1);
      if (changing || !enabled || closing)
        continue;
      const auto item = QJsonDocument::fromJson(line).object();
      const auto device = item.value("device").toString();
      static const QRegularExpression valid(
          "^pulse/foxicmp_dual_[0-9]+_[0-9a-f]{12}$");
      if (item.value("state") == "ready" && valid.match(device).hasMatch()) {
        route = device;
        if (player->get("audio-device").toString() != route &&
            !player->set("audio-device", route)) {
          fallback();
          status(uiTr("Плеер не смог выбрать объединённый выход; используется "
                      "основной"));
          continue;
        }
        player->setProperty("foxicmpDualActive", true);
        status(uiTr("Два выхода активны: %1 + %2")
                   .arg(item.value("primary").toString(),
                        item.value("secondary").toString()));
      } else if (item.value("state") == "fallback") {
        fallback();
        status(uiTr("Второй выход недоступен; используется основной. %1")
                   .arg(item.value("reason").toString()));
      }
    }
  });
  connect(
      &process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        lines.clear();
        if (closing)
          return;
        fallback();
        if (changing) {
          changing = false;
          start();
        } else if (enabled)
          status(
              uiTr("Служба второго выхода завершилась; используется основной"));
      });
  connect(&process, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError) {
            if (closing || changing)
              return;
            fallback();
            status(uiTr("Не удалось запустить второй выход: %1")
                       .arg(process.errorString()));
          });
}
AudioOutput::~AudioOutput() {
  closing = true;
  process.disconnect(this);
  process.closeWriteChannel();
  if (process.state() != QProcess::NotRunning &&
      !process.waitForFinished(3000)) {
    process.terminate();
    if (!process.waitForFinished(2500)) {
      process.kill();
      process.waitForFinished(500);
    }
  }
}
QString AudioOutput::primaryDevice(Player *p, const QString &saved) {
  if (saved == "auto")
    return saved;
  for (const auto &entry : p->get("audio-device-list").toList())
    if (entry.toMap().value("name").toString() == saved &&
        !saved.startsWith("pulse/foxicmp_dual_"))
      return saved;
  return "auto";
}
void AudioOutput::apply(Player *p, QSettings &s) {
  auto *router = p->findChild<AudioOutput *>("audioOutputRouter");
  if (!router)
    router = new AudioOutput(p);
  router->configure(s);
}
void AudioOutput::status(const QString &message) {
  player->setProperty("foxicmpAudioOutputStatus", message);
}
void AudioOutput::fallback() {
  route.clear();
  player->setProperty("foxicmpDualActive", false);
  const auto device = primaryDevice(player, primary);
  if (player->get("audio-device").toString() != device)
    player->set("audio-device", device);
}
void AudioOutput::configure(QSettings &s) {
  const auto first = s.value("audio/device", "auto").toString();
  const auto second = s.value("audio/secondDevice").toString();
  const auto on = s.value("audio/dualOutput", false).toBool();
  if (first == primary && second == secondary && on == enabled) {
    if (!on)
      fallback();
    else if (process.state() == QProcess::NotRunning)
      start();
    return;
  }
  ++generation;
  primary = first;
  secondary = second;
  enabled = on;
  fallback();
  if (process.state() != QProcess::NotRunning) {
    changing = true;
    process.closeWriteChannel();
    const auto token = generation;
    QTimer::singleShot(3500, this, [this, token] {
      if (token == generation && changing &&
          process.state() != QProcess::NotRunning)
        process.terminate();
    });
  } else
    start();
}
void AudioOutput::start() {
  if (!enabled) {
    status(uiTr("Дублирование звука выключено"));
    return;
  }
  if ((!primary.startsWith("pulse/") && primary != "auto") ||
      !secondary.startsWith("pulse/") ||
      secondary.startsWith("pulse/foxicmp_dual_")) {
    status(uiTr("Для двух выходов выберите устройства PulseAudio/PipeWire"));
    return;
  }
  const auto python = QStandardPaths::findExecutable("python3");
  const auto helper = QDir(QCoreApplication::applicationDirPath())
                          .filePath("helpers/audio_output.py");
  if (python.isEmpty() || QStandardPaths::findExecutable("pactl").isEmpty() ||
      !QFileInfo(helper).isFile()) {
    status(uiTr(
        "Для второго выхода нужны python3, pactl и helpers/audio_output.py"));
    return;
  }
  lines.clear();
  process.setProgram(python);
  process.setArguments(
      {helper, "--primary", primary, "--secondary", secondary});
  status(uiTr("Подключение второго выхода…"));
  process.start();
}
