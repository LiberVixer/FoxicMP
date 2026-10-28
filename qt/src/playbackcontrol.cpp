#include "../../shared/familydefaults.h"
#include "devicesource.h"
#include "i18n.h"
#include "player.h"
#include "progressivesource.h"
#include "timeline.h"
#include "window.h"
#include <QtWidgets>
#include <cmath>

bool Window::seekAvailable(double start, double end) const {
  if (stdinPlayback) return false;
  if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end < start)
    return false;
  if (!progressive) {
    const double length = player->get("duration").toDouble();
    return player->get("seekable").toBool() &&
           (length <= 0 || end <= length + .001);
  }
  const auto status = progressive->status();
  const double boundary =
      status.complete ? status.duration : std::max(0., status.available - .2);
  if (!status.connected || end > boundary)
    return false;
  if (!progressiveStreaming || progressive->confirmedByteSeek())
    return true;
  for (const auto &value : player->get("demuxer-cache-state")
                               .toMap()
                               .value("seekable-ranges")
                               .toList()) {
    const auto range = value.toMap();
    const double left = range.value("start").toDouble();
    if ((start + status.timestampOffset >= left ||
         (start == 0 && left <= status.timestampOffset + .25)) &&
        end + status.timestampOffset <= range.value("end").toDouble())
      return true;
  }
  return false;
}
void Window::seekStep(int direction, const QString &key, int fallback) {
  if (stopped || stdinPlayback || DeviceSource::isLive(current))
    return;
  const double seconds =
      std::clamp(profile.settings.value(key, fallback).toInt(), 1, 600000) /
      1000. * seekAcceleration();
  const double now = player->get("time-pos").toDouble() -
                     (progressive ? progressive->status().timestampOffset : 0);
  const double target = std::clamp(now + direction * seconds, 0.,
                                   duration > 0 ? duration : now + seconds);
  if (!seekAvailable(target, target)) {
    showOsd(uiTr("Выбранная позиция недоступна для перемотки"));
    return;
  }
  if (!progressive &&
      profile.settings.value("playback/keyframeSeek", false).toBool())
    player->command(
        {"seek", QString::number(target - now, 'f', 6), "relative+keyframes"});
  else
    seekTo(target);
}
void Window::seekKeyframe(int direction) {
  if (stopped || stdinPlayback || progressive || DeviceSource::isLive(current) ||
      !player->get("seekable").toBool())
    return;
  // Relative keyframe seeks preserve the demuxer's rounding direction.
  player->command(
      {"seek", direction > 0 ? "0.001" : "-0.001", "relative+keyframes"});
}
void Window::setSpeed(double speed) {
  if (!std::isfinite(speed) || DeviceSource::isLive(current))
    return;
  sessionSpeed = std::clamp(speed, .125, 16.);
  player->set("speed", sessionSpeed);
}
void Window::changeSpeed(int direction) {
  const double speed = player->get("speed").toDouble();
  const int step = std::clamp(
      profile.settings.value("playback/speedStep", 0).toInt(), 0, 100);
  if (step) {
    const double grid = step / 100.;
    setSpeed((direction > 0 ? std::floor(speed / grid + .000001) + 1
                            : std::ceil(speed / grid - .000001) - 1) *
             grid);
    return;
  }
  const QList<double> rates{.125, .25, .5, 1., 1.2, 1.5, 2., 4., 8., 16.};
  if (direction > 0) {
    for (double value : rates)
      if (value > speed + .000001) {
        setSpeed(value);
        return;
      }
  } else {
    for (auto it = rates.crbegin(); it != rates.crend(); ++it)
      if (*it < speed - .000001) {
        setSpeed(*it);
        return;
      }
  }
}
void Window::clearAbLoop() {
  player->set("ab-loop-b", "no");
  player->set("ab-loop-a", "no");
  player->set("ab-loop-count", "inf");
  loopA = loopB = timeline->loopA = timeline->loopB = -1;
  timeline->update();
  refreshPlaybackControls();
}
bool Window::setAbLoop(double a, double b, int plays) {
  if (stopped || stdinPlayback || DeviceSource::isLive(current) || !std::isfinite(a) ||
      !std::isfinite(b) || b - a < .05 || plays < 0 || plays > 9999 ||
      !seekAvailable(a, b)) {
    showOsd(uiTr("A–B: нужен доступный отрезок от A до B (не меньше 50 мс)"));
    return false;
  }
  clearAbLoop();
  const double offset = progressive ? progressive->status().timestampOffset : 0;
  if (!player->set("ab-loop-count",
                   plays ? QString::number(plays - 1) : "inf") ||
      !player->set("ab-loop-a", a + offset) ||
      !player->set("ab-loop-b", b + offset)) {
    clearAbLoop();
    return false;
  }
  loopA = timeline->loopA = a;
  loopB = timeline->loopB = b;
  timeline->update();
  if (!seekTo(a)) {
    clearAbLoop();
    return false;
  }
  refreshPlaybackControls();
  showOsd("A–B: " + QString::number(a, 'f', 3) + " — " +
          QString::number(b, 'f', 3) +
          (plays ? QString(uiTr(" • %1 проходов")).arg(plays)
                 : uiTr(" • без ограничения")));
  return true;
}
void Window::setAbPoint(bool first) {
  if (stopped || stdinPlayback || DeviceSource::isLive(current))
    return;
  const double now = player->get("time-pos").toDouble() -
                     (progressive ? progressive->status().timestampOffset : 0);
  if (!seekAvailable(now, now))
    return;
  if (first) {
    clearAbLoop();
    loopA = timeline->loopA = now;
    timeline->update();
    refreshPlaybackControls();
    showOsd(uiTr("Точка A: ") + QString::number(now, 'f', 3));
  } else if (loopA >= 0)
    setAbLoop(loopA, now,
              profile.settings.value("playback/abPlays", 0).toInt());
}
void Window::abLoopDialog() {
  if (stopped || stdinPlayback || DeviceSource::isLive(current) || duration <= 0)
    return;
  QDialog dialog(this);
  dialog.setObjectName("abLoopDialog");
  dialog.setWindowTitle(uiTr("Повтор отрезка A–B"));
  auto *form = new QFormLayout(&dialog);
  auto field = [&](const QString &label, const QString &name, double value) {
    auto *spin = new QDoubleSpinBox;
    spin->setObjectName(name);
    spin->setDecimals(3);
    spin->setRange(0, duration);
    spin->setValue(value);
    form->addRow(label, spin);
    return spin;
  };
  auto *a = field(uiTr("A, секунды"), "abStart", loopA >= 0 ? loopA : pos);
  auto *b = field(uiTr("B, секунды"), "abEnd",
                  loopB >= 0 ? loopB : std::min(pos + 5, duration));
  auto *plays = new QSpinBox;
  plays->setObjectName("abPlays");
  plays->setRange(0, 9999);
  plays->setSpecialValueText(uiTr("Без ограничения"));
  plays->setValue(profile.settings.value("playback/abPlays", 0).toInt());
  form->addRow(uiTr("Всего проходов"), plays);
  auto *message = new QLabel(uiTr(
      "После заданного числа проходов просмотр продолжается за точкой B."));
  message->setWordWrap(true);
  form->addRow(message);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    if (!setAbLoop(a->value(), b->value(), plays->value())) {
      message->setText(
          uiTr("Отрезок недоступен: B должна быть позже A, обе точки "
               "должны находиться в одном диапазоне кэша."));
      return;
    }
    profile.settings.setValue("playback/abPlays", plays->value());
    dialog.accept();
  });
  dialog.exec();
}
void Window::refreshPlaybackControls() {
  const bool active = !stopped && !DeviceSource::isLive(current);
  for (const auto &id : {"back", "forward", "backSmall", "forwardSmall",
                         "backLarge", "forwardLarge", "abA", "abDialog",
                         "speedUp", "speedDown", "speedReset"})
    if (actions.contains(id))
      actions[id]->setEnabled(active && (!stdinPlayback || QString(id).startsWith("speed")));
  if (actions.contains("abB"))
    actions["abB"]->setEnabled(active && !stdinPlayback && loopA >= 0);
  if (actions.contains("abClear"))
    actions["abClear"]->setEnabled(loopA >= 0);
  if (actions.contains("restart"))
    actions["restart"]->setEnabled(!stdinPlayback && !DeviceSource::isLive(current) &&
                                   (active || currentItem));
  for (const auto &id : {"keyframeBack", "keyframeNext"})
    if (actions.contains(id))
      actions[id]->setEnabled(active && !stdinPlayback && !progressive &&
                              !player->get("video-params").toMap().isEmpty());
  for (const auto &entry : QList<QPair<QString, int>>{
           {"Small", FoxicMPFamily::SeekSmallMs}, {"", FoxicMPFamily::SeekMediumMs}, {"Large", FoxicMPFamily::SeekLargeMs}}) {
    const QString key = entry.first == "Small"   ? "playback/seekSmallMs"
                        : entry.first == "Large" ? "playback/seekLargeMs"
                                                 : "playback/seekMediumMs";
    const QString value = QString::number(
        std::clamp(profile.settings.value(key, entry.second).toInt(), 1,
                   600000) /
            1000.,
        'g', 6);
    if (actions.contains("back" + entry.first))
      actions["back" + entry.first]->setText(uiTr("Назад на ") + value +
                                             uiTr(" секунд"));
    if (actions.contains("forward" + entry.first))
      actions["forward" + entry.first]->setText(uiTr("Вперёд на ") + value +
                                                uiTr(" секунд"));
  }
}
void Window::updateMinimizePause() {
  if (isMinimized()) {
    bool video = false;
    for (const auto &value : player->get("track-list").toList()) {
      const auto track = value.toMap();
      video |= track.value("type").toString() == "video" &&
               track.value("selected").toBool();
    }
    if (!stopped && video &&
        profile.settings.value("playback/pauseMinimized", false).toBool() &&
        (!player->get("pause").toBool() ||
         (progressive && progressiveBuffering)) &&
        !progressiveUserPause) {
      pausedByMinimize = true;
      player->set("pause", true);
    }
  } else if (pausedByMinimize) {
    pausedByMinimize = false;
    if (!stopped) {
      if (progressive)
        updateProgressive();
      else
        player->set("pause", false);
    }
  }
}
void Window::changeEvent(QEvent *event) {
  QMainWindow::changeEvent(event);
  if (event->type() == QEvent::WindowStateChange && player)
    updateMinimizePause();
  if (event->type() == QEvent::WindowStateChange && isMinimized())
    QTimer::singleShot(0, this, &Window::minimizeToTray);
}
void Window::attachPlaybackPreferences(QTabWidget *tabs,
                                       QList<std::function<void()>> *apply) {
  auto *page = new QWidget;
  auto *form = new QFormLayout(page);
  tabs->addTab(page, uiTr("Воспроизведение"));
  auto number = [&](const QString &label, const QString &key, int def,
                    int max) {
    auto *spin = new QSpinBox;
    spin->setObjectName(key);
    spin->setRange(key.endsWith("Count") ? 0 : 1, max);
    spin->setValue(profile.settings.value(key, def).toInt());
    form->addRow(label, spin);
    apply->append(
        [this, key, spin] { profile.settings.setValue(key, spin->value()); });
    return spin;
  };
  number(uiTr("Короткая перемотка, мс"), "playback/seekSmallMs", FoxicMPFamily::SeekSmallMs, 600000);
  number(uiTr("Средняя перемотка, мс"), "playback/seekMediumMs", FoxicMPFamily::SeekMediumMs, 600000);
  number(uiTr("Длинная перемотка, мс"), "playback/seekLargeMs", FoxicMPFamily::SeekLargeMs, 600000);
  auto *count = number(uiTr("Всего проходов файла / списка при повторе"),
                       "playlist/repeatCount", 0, 9999);
  count->setSpecialValueText(uiTr("Без ограничения"));
  auto boolean = [&](const QString &label, const QString &key, bool def) {
    auto *check = new QCheckBox(label);
    check->setObjectName(key);
    check->setChecked(profile.settings.value(key, def).toBool());
    form->addRow(check);
    apply->append([this, key, check] {
      profile.settings.setValue(key, check->isChecked());
    });
  };
  boolean(uiTr("Перематывать по ключевым кадрам (обычные источники)"),
          "playback/keyframeSeek", false);
  boolean(
      uiTr("Вернуться к началу и поставить на паузу после последнего прохода"),
      "playback/rewindAtEnd", false);
  boolean(uiTr("Сохранять скорость при смене файлов в текущем запуске"),
          "playback/keepSpeed", false);
  boolean(uiTr("Приостанавливать видео при сворачивании окна"),
          "playback/pauseMinimized", false);
  auto *step = new QComboBox;
  step->setObjectName("playback/speedStep");
  step->addItem(uiTr("Автоматически"), 0);
  for (int value : {1, 5, 10, 20, 25, 50, 100})
    step->addItem(QString::number(value) + "%", value);
  step->setCurrentIndex(std::max(
      0, step->findData(profile.settings.value("playback/speedStep", 0))));
  form->addRow(uiTr("Шаг скорости"), step);
  apply->append([this, step] {
    profile.settings.setValue("playback/speedStep", step->currentData());
  });
  auto *hint = new QLabel(uiTr(
      "Число проходов применяется к режиму повтора файла или списка. Явное "
      "действие после просмотра выполняется до повтора, как прежде. Ручная "
      "пауза сохраняется после восстановления окна."));
  hint->setWordWrap(true);
  form->addRow(hint);
}
