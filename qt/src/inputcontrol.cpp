#include "devicesource.h"
#include "i18n.h"
#include "mediaremote.h"
#include "mediasource.h"
#include "player.h"
#include "playlistpanel.h"
#include "progressivesource.h"
#include "window.h"
#include <QDBusObjectPath>
#include <QtWidgets>
#include <cmath>

namespace {
const QStringList seekCommands{"back",         "forward",   "backSmall",
                               "forwardSmall", "backLarge", "forwardLarge"};
QString buttonGesture(Qt::MouseButton button) {
  switch (button) {
  case Qt::LeftButton:
    return "left";
  case Qt::RightButton:
    return "right";
  case Qt::MiddleButton:
    return "middle";
  case Qt::BackButton:
    return "back";
  case Qt::ForwardButton:
    return "forward";
  default:
    return {};
  }
}
QString defaultMouseCommand(const QSettings &settings, const QString &gesture) {
  if (gesture == "left")
    return settings.value("mouse/clickPause", true).toBool() ? "play" : "none";
  if (gesture == "double_left")
    return settings.value("mouse/doubleClickFullscreen", true).toBool()
               ? "fullscreen"
               : "none";
  if (gesture == "right")
    return "context";
  if (gesture == "back")
    return "previous";
  if (gesture == "forward")
    return "next";
  if (gesture == "wheelUp" || gesture == "wheelDown")
    return settings.value("mouse/wheelVolume", true).toBool()
               ? (gesture == "wheelUp" ? "volumeUp" : "volumeDown")
               : "none";
  return "none";
}
} // namespace

void Window::buildInputActions() {
  const auto add = [&](const QString &id, const QString &text,
                       const QString &key, std::function<void()> callback) {
    auto *action = new QAction(text, this);
    action->setObjectName("action_" + id);
    actions[id] = action;
    defaultKeys[id] = key;
    addAction(action);
    connect(action, &QAction::triggered, this, [this, callback] {
      cancelMouseHold();
      if (clickTimer)
        clickTimer->stop();
      showFullscreenControls();
      callback();
    });
  };
  add("audioProcessingSettings", uiTr("Обработка и микширование звука…"),
      "Ctrl+Alt+A", [this] { audioProcessingDialog(); });
  add("preferences", uiTr("Параметры…"), "O", [this] { preferences(); });
  add("context", uiTr("Контекстное меню"), "Shift+F10",
      [this] { dispatchInputCommand("context"); });
  add("openLast", uiTr("Открыть последний файл"), "Ctrl+Shift+E",
      [this] { openLastFile(); });
  add("bossKey", uiTr("Пауза и свернуть плеер"), "B", [this] { bossKey(); });
  add("quit", uiTr("Выход"), "Ctrl+Q", [this] { exitApplication(); });
  add("audioNext", uiTr("Следующая аудиодорожка"), "A",
      [this] { cycleTrack(true, 1); });
  add("audioPrevious", uiTr("Предыдущая аудиодорожка"), "Shift+A",
      [this] { cycleTrack(true, -1); });
  add("subtitleNext", uiTr("Следующие субтитры"), "S",
      [this] { cycleTrack(false, 1); });
  add("subtitlePrevious", uiTr("Предыдущие субтитры"), "Shift+S",
      [this] { cycleTrack(false, -1); });
  add("subtitleToggle", uiTr("Показать / скрыть субтитры"), "W", [this] {
    const bool visible = !player->get("sub-visibility").toBool();
    player->set("sub-visibility", visible);
    profile.settings.setValue("subtitles/visible", visible);
    showOsd(visible ? uiTr("Субтитры показаны") : uiTr("Субтитры скрыты"));
  });
  add("audioQuick", uiTr("Быстрое меню аудиодорожек"), "Alt+A", [this] {
    audioMenu->popup(player->mapToGlobal(player->rect().center()));
  });
  add("subtitleQuick", uiTr("Быстрое меню субтитров"), "Alt+S", [this] {
    subtitleMenu->popup(player->mapToGlobal(player->rect().center()));
  });
  add("loadSubtitles", uiTr("Добавить субтитры…"), "Ctrl+Alt+S",
      [this] { loadSubtitlesDialog(); });
  add("audioDelayDown", uiTr("Задержка звука −"), "Ctrl+Shift+Left",
      [this] { adjustDelay(true, -1); });
  add("audioDelayUp", uiTr("Задержка звука +"), "Ctrl+Shift+Right",
      [this] { adjustDelay(true, 1); });
  add("subtitleDelayDown", uiTr("Задержка субтитров −"), "F1",
      [this] { adjustDelay(false, -1); });
  add("subtitleDelayUp", uiTr("Задержка субтитров +"), "F2",
      [this] { adjustDelay(false, 1); });
  add("delayReset", uiTr("Сбросить задержки звука и субтитров"), "Ctrl+Shift+R",
      [this] {
        player->set("audio-delay", 0);
        player->set("sub-delay", 0);
        showOsd(uiTr("Задержки сброшены"));
      });
  for (bool audio : {true, false}) {
    add(audio ? "audioDelayDialog" : "subtitleDelayDialog",
        audio ? uiTr("Задержка звука…") : uiTr("Задержка субтитров…"), "",
        [this, audio] {
          bool ok;
          const char *property = audio ? "audio-delay" : "sub-delay";
          const double value = QInputDialog::getDouble(
              this, uiTr("Синхронизация"),
              audio ? uiTr("Задержка звука (секунды)")
                    : uiTr("Задержка субтитров (секунды)"),
              player->get(property).toDouble(), audio ? -30 : -600,
              audio ? 30 : 600, 3, &ok);
          if (ok)
            player->set(property, value);
        });
  }
  add("information", uiTr("Сведения о воспроизведении"), "Ctrl+I",
      [this] { showPlaybackInformation(); });
  const auto pan = [this](const char *property, double delta) {
    player->set(property,
                std::clamp(player->get(property).toDouble() + delta, -3., 3.));
    captureTransformGeometry();
  };
  add("panLeft", uiTr("Сдвинуть изображение влево"), "",
      [pan] { pan("video-pan-x", -.02); });
  add("panRight", uiTr("Сдвинуть изображение вправо"), "",
      [pan] { pan("video-pan-x", .02); });
  add("panUp", uiTr("Сдвинуть изображение вверх"), "",
      [pan] { pan("video-pan-y", -.02); });
  add("panDown", uiTr("Сдвинуть изображение вниз"), "",
      [pan] { pan("video-pan-y", .02); });
  add("panReset", uiTr("Сбросить масштаб и положение изображения"),
      "Alt+Backspace", [this] {
        for (const char *name :
             {"video-pan-x", "video-pan-y", "video-zoom", "panscan"})
          player->set(name, 0);
        videoTransform.width = videoTransform.height = 1;
        videoTransform.x = videoTransform.y = videoTransform.zoom =
            videoTransform.fill = 0;
        transformChanged();
      });
  for (const auto &entry : QList<QPair<QString, Qt::Key>>{
           {"mediaPlay", Qt::Key_MediaPlay},
           {"mediaPause", Qt::Key_MediaPause},
           {"mediaToggle", Qt::Key_MediaTogglePlayPause},
           {"mediaStop", Qt::Key_MediaStop},
           {"mediaNext", Qt::Key_MediaNext},
           {"mediaPrevious", Qt::Key_MediaPrevious}}) {
    const auto id = entry.first;
    const QString target = id == "mediaStop"       ? "stop"
                           : id == "mediaNext"     ? "next"
                           : id == "mediaPrevious" ? "previous"
                                                   : "play";
    add(id, uiTr("Медиаклавиша: %1").arg(actions[target]->text()),
        QKeySequence(entry.second).toString(QKeySequence::PortableText),
        [this, id, target] {
          if (id == "mediaPause" || id == "mediaPlay") {
            mediaRemote->invoke(id == "mediaPause" ? "Pause" : "Play");
            return;
          }
          actions[target]->trigger();
        });
  }
}
void Window::openLastFile() {
  const auto recent = profile.recentFiles();
  if (recent.isEmpty()) {
    showOsd(uiTr("История файлов пуста"));
    return;
  }
  openFiles({recent.first()});
}
void Window::bossKey() {
  heldSeekCommand.clear();
  if (!stopped) {
    pausedByMinimize = false;
    if (progressive)
      progressiveUserPause = true;
    player->set("pause", true);
  }
  if (isMinimized()) {
    setWindowState(bossWindowState & ~Qt::WindowMinimized);
    show();
    raise();
    if (windowHandle())
      windowHandle()->requestActivate();
    activateWindow();
  } else {
    bossWindowState = windowState();
    showMinimized();
  }
}
void Window::cycleTrack(bool audio, int direction) {
  if (stopped)
    return;
  const char *property = audio ? "aid" : "sid";
  QStringList ids;
  if (!audio)
    ids.append("no");
  for (const auto &value : player->get("track-list").toList()) {
    const auto track = value.toMap();
    if (track.value("type").toString() == (audio ? "audio" : "sub") &&
        subtitleTrackAllowed(track))
      ids.append(track.value("id").toString());
  }
  if (ids.isEmpty() || (!audio && ids.size() == 1)) {
    showOsd(audio ? uiTr("Аудиодорожек нет") : uiTr("Субтитров нет"));
    return;
  }
  const auto selected = player->get(property);
  const QString selectedId =
      !audio && (!selected.toBool() || selected.toString() == "no")
          ? QString("no")
          : selected.toString();
  int index = ids.indexOf(selectedId);
  // mpv reports a disabled track as either "no" or boolean false.
  if (index < 0)
    index = direction > 0 ? -1 : 0;
  const auto selection = ids[(index + direction + ids.size()) % ids.size()];
  rememberTrack(property, selection);
  if (!audio && selection != "no") {
    player->set("sub-visibility", true);
    profile.settings.setValue("subtitles/visible", true);
  }
  showOsd((audio ? uiTr("Аудиодорожка: %1") : uiTr("Субтитры: %1"))
              .arg(selection == "no" ? uiTr("Выключены") : selection));
}
void Window::adjustDelay(bool audio, int direction) {
  const char *property = audio ? "audio-delay" : "sub-delay";
  const int step = std::clamp(
      profile.settings
          .value(audio ? "input/audioDelayStepMs" : "input/subtitleDelayStepMs",
                 100)
          .toInt(),
      1, 10000);
  const double limit = audio ? 30 : 600;
  const double value = std::round(std::clamp(player->get(property).toDouble() +
                                                 direction * step / 1000.,
                                             -limit, limit) *
                                  1000) /
                       1000;
  player->set(property, value);
  showOsd((audio ? uiTr("Задержка звука: %1 мс")
                 : uiTr("Задержка субтитров: %1 мс"))
              .arg(qRound(value * 1000)));
}
void Window::loadSubtitlesDialog() {
  const QString file = QFileDialog::getOpenFileName(
      this, uiTr("Открыть субтитры"), {},
      uiTr("Субтитры (*.srt *.ass *.ssa *.vtt *.sub);;Все файлы (*)"));
  if (!file.isEmpty())
    loadExternalSubtitles(file);
}
void Window::showPlaybackInformation() {
  QString text = uiTr("Вывод: ") + player->outputName() + "\n";
  for (auto name :
       {"mpv-version", "video-codec", "audio-codec-name", "hwdec-current",
        "video-params", "audio-params", "estimated-vf-fps", "frame-drop-count",
        "avsync", "audio-delay", "sub-delay"}) {
    const auto value = player->get(name);
    text += QString(name) + ": " +
            (value.typeId() == QMetaType::QVariantMap
                 ? QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(
                       QJsonDocument::Compact))
                 : value.toString()) +
            "\n";
  }
  QMessageBox dialog(QMessageBox::Information, uiTr("Воспроизведение"), text,
                     QMessageBox::Ok, this);
  dialog.setObjectName("playbackInformation");
  dialog.setTextInteractionFlags(Qt::TextSelectableByMouse |
                                 Qt::TextSelectableByKeyboard);
  dialog.exec();
}
QString Window::mouseCommand(const QString &gesture,
                             Qt::KeyboardModifiers modifiers) const {
  QString prefix;
  if (modifiers.testFlag(Qt::ControlModifier))
    prefix += "ctrl_";
  if (modifiers.testFlag(Qt::ShiftModifier))
    prefix += "shift_";
  const QString binding = prefix + gesture;
  const QString key =
      QString("mouse/%1/%2")
          .arg(isFullScreen() ? "fullscreen" : "window", binding);
  return profile.settings
      .value(key, defaultMouseCommand(profile.settings, binding))
      .toString();
}
void Window::dispatchInputCommand(const QString &requested) {
  heldSeekCommand.clear();
  cancelMouseHold();
  clickTimer->stop();
  pendingMouseCommand.clear();
  QString id = requested;
  if (id == "play" && stopped && (!playlist || !playlist->count()) &&
      profile.settings.value("mouse/openLastWhenIdle", false).toBool())
    id = "openLast";
  if (id == "context") {
    showFullscreenControls();
    contextMenu->popup(QCursor::pos());
  } else if (actions.contains(id) && actions[id]->isEnabled())
    actions[id]->trigger();
}
double Window::seekAcceleration() const {
  if (!profile.settings.value("input/accelerateSeek", false).toBool() ||
      heldSeekCommand.isEmpty() || !heldSeekTime.isValid())
    return 1;
  const auto elapsed = heldSeekTime.elapsed();
  return elapsed >= 3000 ? 8 : elapsed >= 1500 ? 4 : elapsed >= 750 ? 2 : 1;
}
bool Window::inputEvent(QObject *object, QEvent *event) {
  if (event->type() == QEvent::ShortcutOverride) {
    cancelMouseHold();
    auto *key = static_cast<QKeyEvent *>(event);
    const auto sequence = QKeySequence(key->keyCombination());
    QString id;
    auto *widget = qobject_cast<QWidget *>(object);
    if (widget && widget->window() == this &&
        !QApplication::activeModalWidget())
      for (const auto &command : seekCommands)
        if (actions[command]->shortcut() == sequence) {
          id = command;
          break;
        }
    if (id.isEmpty())
      heldSeekCommand.clear();
    else if (!key->isAutoRepeat() || heldSeekCommand != id) {
      heldSeekCommand = id;
      heldSeekTime.restart();
    }
  } else if (event->type() == QEvent::FocusOut) {
    cancelMouseHold();
    heldSeekCommand.clear();
  } else if (event->type() == QEvent::KeyRelease) {
    if (!static_cast<QKeyEvent *>(event)->isAutoRepeat())
      heldSeekCommand.clear();
  } else if (event->type() == QEvent::WindowDeactivate && object == this) {
    heldSeekCommand.clear();
    cancelMouseHold();
    dragCandidate = draggingWindow = false;
    pressedMouseButton = Qt::NoButton;
    clickTimer->stop();
    pendingMouseCommand.clear();
  }
  if (event->type() == QEvent::MouseMove ||
      event->type() == QEvent::MouseButtonPress ||
      event->type() == QEvent::Wheel)
    showFullscreenControls();
  if (object != player) {
    if (event->type() == QEvent::MouseButtonPress)
      cancelMouseHold();
    return false;
  }
  if (event->type() == QEvent::ContextMenu) {
    const auto *context = static_cast<QContextMenuEvent *>(event);
    // A platform/accessibility request may arrive without a button sequence.
    // Actual button sequences go through the single/double-click bindings.
    if (context->reason() != QContextMenuEvent::Mouse ||
        (pressedMouseButton == Qt::NoButton &&
         mouseCommand("right", context->modifiers()) == "context" &&
         mouseCommand("double_right", context->modifiers()) == "none")) {
      cancelMouseHold();
      clickTimer->stop();
      pendingMouseCommand.clear();
      heldSeekCommand.clear();
      showFullscreenControls();
      contextMenu->popup(context->reason() == QContextMenuEvent::Mouse
                             ? context->globalPos()
                             : player->mapToGlobal(player->rect().center()));
    }
    return true;
  }
  if (event->type() == QEvent::MouseButtonPress) {
    auto *mouse = static_cast<QMouseEvent *>(event);
    const auto gesture = buttonGesture(mouse->button());
    if (gesture.isEmpty())
      return false;
    if (clickTimer->isActive()) {
      const auto pending = pendingMouseCommand;
      dispatchInputCommand(pending);
    }
    cancelMouseHold();
    mouseHoldConsumed = false;
    heldSeekCommand.clear();
    suppressMouseRelease = false;
    pressedMouseButton = mouse->button();
    mousePressPoint = mouse->globalPosition().toPoint();
    const auto dragMode =
        profile.settings.value("mouse/windowDrag", "altLeft").toString();
    dragCandidate =
        !isFullScreen() && mouse->button() == Qt::LeftButton &&
        ((dragMode == "left" && mouse->modifiers() == Qt::NoModifier) ||
         (dragMode == "altLeft" && mouse->modifiers() == Qt::AltModifier));
    dragConsumesClick = dragCandidate && dragMode == "altLeft";
    windowDragOffset = mousePressPoint - frameGeometry().topLeft();
    if (mouse->button() == Qt::LeftButton &&
        mouse->modifiers() == Qt::NoModifier &&
        mouse->buttons() == Qt::LeftButton && !stopped &&
        !DeviceSource::isLive(current) && !player->get("pause").toBool() &&
        profile.settings.value("mouse/holdSpeed", false).toBool()) {
      mouseHoldGeneration = playbackGeneration;
      mouseHoldTimer->start(std::clamp(
          profile.settings.value("mouse/holdSpeedDelayMs", 300).toInt(), 100,
          2000));
    }
    return true;
  }
  if (event->type() == QEvent::MouseButtonDblClick) {
    auto *mouse = static_cast<QMouseEvent *>(event);
    const auto gesture = buttonGesture(mouse->button());
    if (gesture.isEmpty())
      return false;
    cancelMouseHold();
    clickTimer->stop();
    pendingMouseCommand.clear();
    dragCandidate = draggingWindow = false;
    pressedMouseButton = Qt::NoButton;
    const auto command = mouseCommand("double_" + gesture, mouse->modifiers());
    suppressMouseRelease = command != "none";
    if (command == "none")
      pressedMouseButton = mouse->button();
    else
      dispatchInputCommand(command);
    return true;
  }
  if (event->type() == QEvent::MouseMove &&
      pressedMouseButton == Qt::LeftButton &&
      (mouseHoldTimer->isActive() || mouseHoldActive)) {
    auto *mouse = static_cast<QMouseEvent *>(event);
    if ((mouse->globalPosition().toPoint() - mousePressPoint)
            .manhattanLength() >= QApplication::startDragDistance()) {
      cancelMouseHold();
      mouseHoldConsumed = true;
    }
  }
  if (event->type() == QEvent::MouseMove && dragCandidate) {
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (!mouse->buttons().testFlag(Qt::LeftButton)) {
      dragCandidate = draggingWindow = false;
      return false;
    }
    if ((mouse->globalPosition().toPoint() - mousePressPoint)
            .manhattanLength() >= QApplication::startDragDistance()) {
      draggingWindow = true;
      clickTimer->stop();
      pendingMouseCommand.clear();
    }
    if (draggingWindow)
      move(mouse->globalPosition().toPoint() - windowDragOffset);
    return true;
  }
  if (event->type() == QEvent::MouseButtonRelease) {
    auto *mouse = static_cast<QMouseEvent *>(event);
    const auto gesture = buttonGesture(mouse->button());
    if (gesture.isEmpty())
      return false;
    cancelMouseHold();
    const bool ignore = mouseHoldConsumed || suppressMouseRelease ||
                        draggingWindow || dragConsumesClick ||
                        pressedMouseButton != mouse->button();
    dragCandidate = draggingWindow = dragConsumesClick = suppressMouseRelease =
        false;
    pressedMouseButton = Qt::NoButton;
    mouseHoldConsumed = false;
    if (ignore || !player->rect().contains(mouse->position().toPoint()))
      return true;
    const auto command = mouseCommand(gesture, mouse->modifiers());
    if (mouseCommand("double_" + gesture, mouse->modifiers()) != "none") {
      pendingMouseCommand = command;
      clickTimer->start();
    } else
      dispatchInputCommand(command);
    return true;
  }
  if (event->type() == QEvent::Wheel) {
    auto *wheel = static_cast<QWheelEvent *>(event);
    const auto modifiers =
        wheel->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    if (wheelWasFullscreen != isFullScreen() ||
        lastWheelModifiers != modifiers) {
      wheelRemainder = horizontalWheelRemainder = 0;
      wheelWasFullscreen = isFullScreen();
      lastWheelModifiers = modifiers;
    }
    const bool vertical = wheel->angleDelta().y() != 0;
    auto &remainder = vertical ? wheelRemainder : horizontalWheelRemainder;
    remainder += vertical ? wheel->angleDelta().y() : wheel->angleDelta().x();
    const int steps = std::clamp(remainder / 120, -10, 10);
    remainder %= 120;
    QString gesture = vertical ? (steps > 0 ? "wheelUp" : "wheelDown")
                               : (steps > 0 ? "wheelRight" : "wheelLeft");
    for (int i = 0; i < std::abs(steps); ++i)
      dispatchInputCommand(mouseCommand(gesture, wheel->modifiers()));
    return true;
  }
  return false;
}

void Window::attachInputPreferences(QTabWidget *tabs,
                                    QList<std::function<void()>> *apply) {
  auto *page = new QWidget;
  auto *layout = new QVBoxLayout(page);
  auto *hint =
      new QLabel(uiTr("Действия выполняются над видео. Одинарный щелчок ждёт "
                      "двойного, если ему назначена команда."));
  hint->setWordWrap(true);
  layout->addWidget(hint);
  const QList<QPair<QString, QString>> gestures{
      {"left", uiTr("Левая кнопка")},
      {"double_left", uiTr("Левая дважды")},
      {"right", uiTr("Правая кнопка")},
      {"double_right", uiTr("Правая дважды")},
      {"middle", uiTr("Средняя кнопка")},
      {"double_middle", uiTr("Средняя дважды")},
      {"back", uiTr("Боковая «назад»")},
      {"double_back", uiTr("Боковая «назад» дважды")},
      {"forward", uiTr("Боковая «вперёд»")},
      {"double_forward", uiTr("Боковая «вперёд» дважды")},
      {"wheelUp", uiTr("Колесо вверх")},
      {"wheelDown", uiTr("Колесо вниз")},
      {"wheelLeft", uiTr("Колесо влево")},
      {"wheelRight", uiTr("Колесо вправо")}};
  const QStringList prefixes{"", "ctrl_", "shift_", "ctrl_shift_"};
  auto *modifierChoice = new QComboBox;
  modifierChoice->setObjectName("mouseBindingModifiers");
  modifierChoice->addItems(
      {uiTr("Без модификаторов"), "Ctrl", "Shift", "Ctrl+Shift"});
  auto *modifierRow = new QFormLayout;
  modifierRow->addRow(uiTr("Модификаторы кнопок и колеса:"), modifierChoice);
  layout->addLayout(modifierRow);
  auto *table = new QTableWidget(gestures.size() * prefixes.size(), 3);
  table->setObjectName("mouseBindingsTable");
  table->setHorizontalHeaderLabels(
      {uiTr("Действие мыши"), uiTr("В окне"), uiTr("На полном экране")});
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table->verticalHeader()->hide();
  layout->addWidget(table, 1);
  QMap<QString, QComboBox *> choices;
  for (int row = 0; row < table->rowCount(); ++row) {
    const int index = row % gestures.size();
    auto *item = new QTableWidgetItem(gestures[index].second);
    item->setFlags(Qt::ItemIsEnabled);
    table->setItem(row, 0, item);
    for (int column : {1, 2}) {
      const auto gesture =
          prefixes[row / gestures.size()] + gestures[index].first;
      const auto key = QString("mouse/%1/%2")
                           .arg(column == 1 ? "window" : "fullscreen", gesture);
      auto *box = new QComboBox;
      box->setObjectName(key);
      box->setSizeAdjustPolicy(
          QComboBox::AdjustToMinimumContentsLengthWithIcon);
      box->addItem(uiTr("Ничего"), "none");
      box->addItem(uiTr("Контекстное меню"), "context");
      QList<QAction *> sorted = actions.values();
      std::sort(sorted.begin(), sorted.end(), [](QAction *a, QAction *b) {
        return a->text().localeAwareCompare(b->text()) < 0;
      });
      for (auto *action : sorted) {
        const auto id = actions.key(action);
        if (id.startsWith("media") || id == "context")
          continue;
        box->addItem(action->text(), id);
      }
      box->setCurrentIndex(std::max(
          0, box->findData(profile.settings.value(
                 key, defaultMouseCommand(profile.settings, gesture)))));
      box->setToolTip(box->currentText());
      connect(box, &QComboBox::currentIndexChanged, box,
              [box] { box->setToolTip(box->currentText()); });
      for (int i = 0; i < box->count(); ++i)
        box->setItemData(i, box->itemText(i), Qt::ToolTipRole);
      table->setCellWidget(row, column, box);
      choices[key] = box;
    }
  }
  const auto showModifierRows = [table, count = gestures.size()](int group) {
    for (int row = 0; row < table->rowCount(); ++row)
      table->setRowHidden(row, row / count != group);
  };
  connect(modifierChoice, &QComboBox::currentIndexChanged, table,
          showModifierRows);
  showModifierRows(0);
  auto *reset = new QPushButton(uiTr("Вернуть стандартные действия мыши"));
  layout->addWidget(reset);
  connect(reset, &QPushButton::clicked, page, [choices] {
    // Explicit factory values; never read or write a desktop settings store.
    for (auto it = choices.begin(); it != choices.end(); ++it) {
      const auto gesture = it.key().section('/', -1);
      QString command = gesture == "left"          ? "play"
                        : gesture == "double_left" ? "fullscreen"
                        : gesture == "right"       ? "context"
                        : gesture == "back"        ? "previous"
                        : gesture == "forward"     ? "next"
                        : gesture == "wheelUp"     ? "volumeUp"
                        : gesture == "wheelDown"   ? "volumeDown"
                                                   : "none";
      it.value()->setCurrentIndex(it.value()->findData(command));
    }
  });
  auto *form = new QFormLayout;
  auto *drag = new QComboBox;
  drag->setObjectName("mouse/windowDrag");
  drag->addItem(uiTr("Отключено"), "none");
  drag->addItem(uiTr("Alt + левая кнопка"), "altLeft");
  drag->addItem(uiTr("Левая кнопка"), "left");
  drag->setCurrentIndex(std::max(0, drag->findData(profile.settings.value(
                                        "mouse/windowDrag", "altLeft"))));
  form->addRow(uiTr("Перемещение окна за видео:"), drag);
  layout->addLayout(form);
  auto *last = new QCheckBox(
      uiTr("Щелчок воспроизведения в пустом плеере открывает последний файл"));
  last->setObjectName("mouse/openLastWhenIdle");
  last->setChecked(
      profile.settings.value("mouse/openLastWhenIdle", false).toBool());
  layout->addWidget(last);
  tabs->addTab(page, uiTr("Мышь"));
  auto *commands = new QWidget;
  auto *commandsLayout = new QVBoxLayout(commands);
  auto *accelerate =
      new QCheckBox(uiTr("Ускорять перемотку при удержании клавиши (до 8×)"));
  accelerate->setObjectName("input/accelerateSeek");
  accelerate->setChecked(
      profile.settings.value("input/accelerateSeek", false).toBool());
  commandsLayout->addWidget(accelerate);
  auto *steps = new QFormLayout;
  QMap<QString, QSpinBox *> spinBoxes;
  for (bool audio : {true, false}) {
    const auto key =
        QString(audio ? "input/audioDelayStepMs" : "input/subtitleDelayStepMs");
    auto *spin = new QSpinBox;
    spin->setObjectName(key);
    spin->setRange(1, 10000);
    spin->setSuffix(uiTr(" мс"));
    spin->setValue(profile.settings.value(key, 100).toInt());
    steps->addRow(audio ? uiTr("Шаг задержки звука:")
                        : uiTr("Шаг задержки субтитров:"),
                  spin);
    spinBoxes[key] = spin;
  }
  commandsLayout->addLayout(steps);
  auto *info = new QLabel(uiTr(
      "A / Shift+A — аудио; S / Shift+S — субтитры; W — видимость; B — пауза и "
      "свернуть. Все команды переназначаются в настройках клавиатуры."));
  info->setWordWrap(true);
  commandsLayout->addWidget(info);
  auto *holdSpeed = new QCheckBox(
      uiTr("Удержание левой кнопки временно ускоряет воспроизведение"));
  holdSpeed->setObjectName("mouse/holdSpeed");
  holdSpeed->setChecked(
      profile.settings.value("mouse/holdSpeed", false).toBool());
  commandsLayout->addWidget(holdSpeed);
  auto *holdForm = new QFormLayout;
  auto *holdRate = new QSpinBox;
  holdRate->setObjectName("mouse/holdSpeedRate");
  holdRate->setRange(2, 8);
  holdRate->setSuffix("×");
  holdRate->setValue(profile.settings.value("mouse/holdSpeedRate", 2).toInt());
  auto *holdDelay = new QSpinBox;
  holdDelay->setObjectName("mouse/holdSpeedDelayMs");
  holdDelay->setRange(100, 2000);
  holdDelay->setSuffix(uiTr(" мс"));
  holdDelay->setValue(
      profile.settings.value("mouse/holdSpeedDelayMs", 300).toInt());
  holdForm->addRow(uiTr("Скорость при удержании:"), holdRate);
  holdForm->addRow(uiTr("Задержка удержания:"), holdDelay);
  commandsLayout->addLayout(holdForm);
  const auto enableHoldFields = [holdRate, holdDelay](bool enabled) {
    holdRate->setEnabled(enabled);
    holdDelay->setEnabled(enabled);
  };
  connect(holdSpeed, &QCheckBox::toggled, commands, enableHoldFields);
  enableHoldFields(holdSpeed->isChecked());
  auto *remote =
      new QCheckBox(uiTr("Внешнее управление и медиаклавиши среды (MPRIS)"));
  remote->setObjectName("input/mpris");
  remote->setChecked(profile.settings.value("input/mpris", true).toBool());
  commandsLayout->addWidget(remote);
  auto *remoteInfo = new QLabel(uiTr(
      "Работает через сеансовую шину D-Bus. Медиаклавиши вне плеера назначает "
      "рабочий стол; команды доступны также через playerctl. Пульты, "
      "определяемые как клавиатура, используют обычные назначения клавиш."));
  remoteInfo->setObjectName("settingsMediaKeysHelp");
  remoteInfo->setWordWrap(true);
  commandsLayout->addWidget(remoteInfo);
  auto *remoteStatus = new QLabel(
      mediaRemote->serviceName().isEmpty()
          ? (profile.settings.value("input/mpris", true).toBool()
                 ? uiTr("MPRIS недоступен: %1").arg(mediaRemote->errorString())
                 : uiTr("MPRIS выключен"))
          : uiTr("Служба: %1").arg(mediaRemote->serviceName()));
  remoteStatus->setObjectName("settingsMediaKeysStatus");
  remoteStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
  remoteStatus->setWordWrap(true);
  commandsLayout->addWidget(remoteStatus);
  commandsLayout->addStretch();
  tabs->addTab(commands, uiTr("Управление"));
  apply->append([this, choices, drag, last, accelerate, spinBoxes, remote,
                 holdSpeed, holdRate, holdDelay] {
    cancelMouseHold();
    clickTimer->stop();
    pendingMouseCommand.clear();
    heldSeekCommand.clear();
    for (auto it = choices.begin(); it != choices.end(); ++it)
      profile.settings.setValue(it.key(), it.value()->currentData());
    profile.settings.setValue("mouse/windowDrag", drag->currentData());
    profile.settings.setValue("mouse/openLastWhenIdle", last->isChecked());
    profile.settings.setValue("input/accelerateSeek", accelerate->isChecked());
    for (auto it = spinBoxes.begin(); it != spinBoxes.end(); ++it)
      profile.settings.setValue(it.key(), it.value()->value());
    profile.settings.setValue("mouse/holdSpeed", holdSpeed->isChecked());
    profile.settings.setValue("mouse/holdSpeedRate", holdRate->value());
    profile.settings.setValue("mouse/holdSpeedDelayMs", holdDelay->value());
    profile.settings.setValue("input/mpris", remote->isChecked());
    mediaRemote->setEnabled(remote->isChecked());
    wheelRemainder = horizontalWheelRemainder = 0;
  });
}

void Window::setupMediaRemote() {
  mediaRemote = new MediaRemote(
      [this] {
        const bool active = !stopped;
        const bool live = DeviceSource::isLive(current);
        const bool seekable =
            active && !stdinPlayback && !live && player->get("seekable").toBool();
        const double offset =
            progressive ? progressive->status().timestampOffset : 0;
        const double position =
            active ? std::max(0., player->get("time-pos").toDouble() - offset)
                   : 0;
        QVariantMap metadata;
        if (!current.isEmpty() || stdinPlayback) {
          metadata["mpris:trackid"] = QVariant::fromValue(QDBusObjectPath(
              QString("/org/foxicmp/track/t%1").arg(playbackGeneration)));
          QString title = player->get("media-title").toString();
          if (title.isEmpty())
            title = QFileInfo(current).fileName();
          metadata["xesam:title"] = stdinPlayback ? uiTr("Поток stdin") : title;
          if (!stdinPlayback && !progressive && !live) {
            const auto url = MediaSource::isUrl(current) ? QString() : current;
            metadata["xesam:url"] =
                url.isEmpty()
                    ? current
                    : QUrl::fromLocalFile(url).toString(QUrl::FullyEncoded);
          }
          if (duration > 0)
            metadata["mpris:length"] = qlonglong(duration * 1000000);
        }
        const auto repeat =
            profile.settings.value("playlist/repeat", "repeatNone").toString();
        const bool shuffle =
            profile.settings.value("playlist/shuffle", false).toBool();
        bool next = false, previous = false;
        if (active && !progressive && playlist) {
          const int row = currentItem ? playlist->row(currentItem) : -1;
          for (int i = 0; i < playlist->count(); ++i) {
            if (playlistItemSkipped(i))
              continue;
            if (i < row)
              previous = true;
            if (i > row)
              next = true;
          }
          if (repeat == "repeatAll" && playlist->count())
            next = previous = true;
          if (shuffle) {
            previous = shuffleCursor > 0;
            next = shuffleCursor + 1 < shuffleTrail.size();
            for (int i = 0; i < playlist->count(); ++i)
              if (!playlistItemSkipped(i) &&
                  !shuffled.contains(
                      playlist->item(i)->data(PlaylistPanel::Id).toString()))
                next = true;
            if (repeat == "repeatAll" && playlist->count())
              next = true;
          }
        }
        return QVariantMap{
            {"PlaybackStatus", !active                         ? "Stopped"
                               : player->get("pause").toBool() ? "Paused"
                                                               : "Playing"},
            {"LoopStatus", repeat == "repeatOne"   ? "Track"
                           : repeat == "repeatAll" ? "Playlist"
                                                   : "None"},
            {"Rate", player->get("speed")},
            {"Shuffle", shuffle},
            {"Metadata", metadata},
            {"Volume",
             player->get("mute").toBool() ? 0. : volume->value() / 100.},
            {"Position", qlonglong(position * 1000000)},
            {"MinimumRate", live ? 1. : .125},
            {"MaximumRate", live ? 1. : 16.},
            {"CanGoNext", next},
            {"CanGoPrevious", previous},
            {"CanPlay", active || (!stdinPlayback && playlist && playlist->count() > 0)},
            {"CanPause", active},
            {"CanSeek", seekable},
            {"CanControl", true},
            {"Fullscreen", isFullScreen()}};
      },
      [this](const QString &command, const QVariantList &arguments) {
        cancelMouseHold();
        if (command == "Command") {
          const auto id = arguments.value(0).toString();
          if (!actions.contains(id))
            return;
          // Return the D-Bus reply before opening a modal UI or closing the
          // app.
          QTimer::singleShot(0, this, [this, id] { dispatchInputCommand(id); });
        } else if (command == "Raise") {
          restoreFromTray();
        } else if (command == "Quit") {
          // Let D-Bus send its reply before closing the application's last
          // window.
          QTimer::singleShot(0, this, &Window::exitApplication);
        } else if (command == "Fullscreen") {
          if (arguments.value(0).toBool() != isFullScreen())
            toggleFullscreen();
        } else if (command == "PlayPause") {
          if (stopped)
            mediaRemote->invoke("Play");
          else
            actions["play"]->trigger();
        } else if (command == "Play") {
          if (stopped && !stdinPlayback && playlist && playlist->count())
            playIndex(std::max(0, currentItem ? playlist->row(currentItem)
                                              : playlist->currentRow()),
                      0);
          else if (!stopped) {
            pausedByMinimize = false;
            if (progressive) {
              progressiveUserPause = false;
              updateProgressive();
            } else
              player->set("pause", false);
          }
        } else if (command == "Pause") {
          if (!stopped) {
            pausedByMinimize = false;
            if (progressive)
              progressiveUserPause = true;
            player->set("pause", true);
          }
        } else if (command == "Stop") {
          if (!stopped || stdinPlayback)
            actions["stop"]->trigger();
        } else if (command == "Next" || command == "Previous") {
          const auto state = mediaRemote->state();
          if (!state.value(command == "Next" ? "CanGoNext" : "CanGoPrevious")
                   .toBool())
            return;
          const bool hold = player->get("pause").toBool();
          if (advancePlaylist(command == "Next") && hold) {
            holdAfterLoad = true;
            player->set("pause", true);
          }
        } else if (command == "Seek" || command == "SetPosition") {
          const auto state = mediaRemote->state();
          if (!state.value("CanSeek").toBool())
            return;
          double seconds;
          if (command == "SetPosition") {
            const auto track = state.value("Metadata")
                                   .toMap()
                                   .value("mpris:trackid")
                                   .value<QDBusObjectPath>()
                                   .path();
            if (arguments.value(0).toString() != track)
              return;
            seconds = arguments.value(1).toLongLong() / 1000000.;
            if (seconds < 0 || (duration > 0 && seconds >= duration))
              return;
          } else {
            seconds =
                std::max(0., state.value("Position").toLongLong() / 1000000. +
                                 arguments.value(0).toLongLong() / 1000000.);
            if (duration > 0 && seconds > duration) {
              mediaRemote->invoke("Next");
              return;
            }
          }
          seekTo(seconds);
        } else if (command == "OpenUri") {
          const QUrl url(arguments.value(0).toString());
          if (!url.isValid() ||
              !QStringList{"file", "http", "https"}.contains(
                  url.scheme().toLower()) ||
              (url.scheme().toLower() == "file" && !url.host().isEmpty() &&
               url.host() != "localhost"))
            return;
          openFiles({url.isLocalFile() ? url.toLocalFile()
                                       : url.toString(QUrl::FullyEncoded)});
        } else if (command == "Volume") {
          const double value = arguments.value(0).toDouble();
          if (!std::isfinite(value))
            return;
          volume->setValue(qRound(std::clamp(value, 0., 1.) * 100));
          player->set("mute", false);
        } else if (command == "Rate") {
          const double rate = arguments.value(0).toDouble();
          if (rate == 0)
            mediaRemote->invoke("Pause");
          else if (rate > 0)
            setSpeed(rate);
        } else if (command == "LoopStatus") {
          const auto repeat = arguments.value(0).toString();
          const auto id = repeat == "None"       ? "repeatNone"
                          : repeat == "Track"    ? "repeatOne"
                          : repeat == "Playlist" ? "repeatAll"
                                                 : "";
          if (*id)
            actions[id]->trigger();
        } else if (command == "Shuffle") {
          if (arguments.value(0).toBool() != actions["shuffle"]->isChecked())
            actions["shuffle"]->trigger();
        }
      },
      this);
  QVariantMap commandCatalog;
  for (auto it = actions.begin(); it != actions.end(); ++it)
    commandCatalog[it.key()] = it.value()->text();
  mediaRemote->setCommands(commandCatalog);
  mediaRemote->setObjectName("mediaRemote");
  mediaRemote->setEnabled(profile.settings.value("input/mpris", true).toBool());
  connect(player, &Player::property, mediaRemote,
          [this](const QString &name, const QVariant &value) {
            if (name == "seeking" && !value.toBool())
              mediaRemote->seeked();
          });
}

void Window::setupMouseHold() {
  mouseHoldTimer = new QTimer(this);
  mouseHoldTimer->setSingleShot(true);
  mouseHoldTimer->setTimerType(Qt::PreciseTimer);
  connect(mouseHoldTimer, &QTimer::timeout, this, [this] {
    if (pressedMouseButton != Qt::LeftButton || stopped ||
        mouseHoldGeneration != playbackGeneration || draggingWindow ||
        DeviceSource::isLive(current) || player->get("pause").toBool())
      return;
    mouseHoldPreviousSpeed = player->get("speed").toDouble();
    mouseHoldActive = mouseHoldConsumed = true;
    clickTimer->stop();
    pendingMouseCommand.clear();
    setSpeed(std::clamp(
        profile.settings.value("mouse/holdSpeedRate", 2).toInt(), 2, 8));
    showOsd(uiTr("Удержание: %1×").arg(player->get("speed").toDouble()));
  });
  connect(player, &Player::loaded, this, [this] { cancelMouseHold(); });
  connect(player, &Player::ended, this, [this] { cancelMouseHold(); });
}
bool Window::cancelMouseHold() {
  if (mouseHoldTimer)
    mouseHoldTimer->stop();
  const bool active = mouseHoldActive;
  if (active) {
    mouseHoldActive = false;
    mouseHoldConsumed = true;
    sessionSpeed = mouseHoldPreviousSpeed;
    player->set("speed", mouseHoldPreviousSpeed);
  }
  return active;
}
