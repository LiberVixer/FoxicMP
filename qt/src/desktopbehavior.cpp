#include "i18n.h"
#include "window.h"
#include "player.h"
#include "launchoptions.h"
#include "audiooutput.h"
#include <QtWidgets>

namespace {
QScreen *launchScreen(const QString &name) {
  if (name == "primary") return QGuiApplication::primaryScreen();
  const auto screens = QGuiApplication::screens();
  for (auto *screen : screens) if (screen->name() == name) return screen;
  bool ok = false;
  const auto index = name.toInt(&ok);
  return ok && index > 0 && index <= screens.size() ? screens[index - 1] : nullptr;
}
}
void Window::handleLaunchRequest(const LaunchRequest &request) {
  LaunchRequest checked;
  if (!LaunchRequest::decode(request.encode(), &checked)) return;
  if (request.options.contains("command")) {
    if (!webCommand(request.options.value("command").toString(), {}))
      showOsd(uiTr("Команда сейчас недоступна"));
    return;
  }
  // Resolve all named resources before changing any playback/profile state.
  VideoTransform preset;
  const auto &options = request.options;
  QString unavailable;
  if (options.contains("monitor") && !launchScreen(options.value("monitor").toString()))
    unavailable = uiTr("Указанный монитор недоступен");
  if (options.contains("panscan") && !VideoTransform::preset(profile.settings, options.value("panscan").toString(), &preset))
    unavailable = uiTr("Указанный пресет преобразований недоступен");
  const auto device = options.value("audio-device").toString();
  if (options.contains("audio-device") && AudioOutput::primaryDevice(player, device) != device)
    unavailable = uiTr("Указанное аудиоустройство недоступно");
  if (!unavailable.isEmpty()) {
    diagnosticMessages.append(unavailable);
    while (diagnosticMessages.size() > 100) diagnosticMessages.removeFirst();
    showOsd(unavailable);
    return;
  }
  if (options.contains("web-port") &&
      !setTemporaryWebPort(quint16(options.value("web-port").toInt())))
    return;
  if (options.contains("audio-device")) {
    profile.settings.setValue("audio/device", device);
    AudioOutput::apply(player, profile.settings);
  }
  if (options.contains("panscan")) {
    videoTransform = preset;
    transformChanged();
  }
  if (options.contains("shuffle") && actions["shuffle"]->isChecked() != options.value("shuffle").toBool())
    actions["shuffle"]->trigger();
  if (options.contains("after")) {
    onceAfterPlayback = options.value("after").toString();
    refreshAfterPlaybackMenu();
  }
  // Volume is applied before submitting the source to avoid its first samples
  // being played at the previous volume. Other media options bind to a load.
  if (request.options.contains("volume"))
    webCommand("volume", request.options.value("volume").toDouble());
  const auto generation = playbackGeneration;
  openingLaunchOptions = request.options;
  if (!request.disc.isEmpty())
    openDeviceSource(request.disc);
  else if (!request.session.isEmpty())
    openProgressiveSession(request.session);
  else if (!request.files.isEmpty()) {
    if (request.enqueue)
      addFiles(request.files);
    else
      openFiles(request.files);
  }
  openingLaunchOptions = {};
  const bool opening = playbackGeneration != generation;
  if (!opening && request.files.isEmpty() && request.disc.isEmpty() && request.session.isEmpty())
    applyLaunchPlaybackOptions(request.options);
  if (request.activate)
    restoreFromTray();
  // For a new source, repeat after media-driven sizing has finished.
  applyLaunchWindowOptions(request.options);
}
void Window::applyLaunchWindowOptions(const QJsonObject &o) {
  bool restoreFullscreen = false;
  const bool wayland = QGuiApplication::platformName().startsWith(
      "wayland", Qt::CaseInsensitive);
  if (o.contains("monitor")) {
    if (auto *target = launchScreen(o.value("monitor").toString())) {
      setProperty("foxicmpLaunchScreen", target->name());
      if (screen() != target) {
        restoreFullscreen = isFullScreen();
        if (restoreFullscreen) toggleFullscreen();
        setScreen(target);
        if (!wayland) {
          const auto work = target->availableGeometry();
          move(work.center() - QPoint(width() / 2, height() / 2));
        }
      }
    }
  }
  if (o.contains("width") || o.contains("x")) {
    if (isFullScreen()) toggleFullscreen();
    if (isMaximized() || isMinimized()) showNormal();
    autoSizedGeneration = playbackGeneration;
    if (o.contains("width"))
      resize(o.value("width").toInt(), o.value("height").toInt());
    if (o.contains("x") && !wayland)
      move(o.value("x").toInt(), o.value("y").toInt());
  }
  const auto mode = o.value("mode").toString(restoreFullscreen ? "fullscreen" : "");
  if (mode == "fullscreen" && !isFullScreen()) toggleFullscreen();
  else if (mode == "normal") {
    if (isFullScreen()) toggleFullscreen();
    showNormal();
  } else if (mode == "minimized") showMinimized();
}
void Window::applyLaunchPlaybackOptions(const QJsonObject &o) {
  if (o.contains("volume")) webCommand("volume", o.value("volume").toDouble());
  if (stopped) return;
  if (o.value("pause").toBool()) webCommand("pause", {});
  if (o.contains("start") && !seekTo(o.value("start").toDouble()))
    showOsd(uiTr("Выбранная позиция недоступна для перемотки"));
  if (o.contains("rate")) webCommand("rate", o.value("rate").toDouble());
  for (const auto &pair : {qMakePair(QString("audio-track"), QString("aid")), qMakePair(QString("subtitle-track"), QString("sid"))}) {
    if (!o.contains(pair.first)) continue;
    const int id = o.value(pair.first).toInt();
    bool found = id == 0;
    for (const auto &v : player->get("track-list").toList()) {
      const auto track = v.toMap();
      if (track.value("type") == (pair.second == "aid" ? "audio" : "sub") &&
          track.value("id").toInt() == id && (pair.second != "sid" || subtitleTrackAllowed(track))) found = true;
    }
    if (found) rememberTrack(pair.second.toLatin1().constData(), id ? QString::number(id) : QString("no"));
    else showOsd(uiTr("Указанная дорожка недоступна"));
  }
  if (o.contains("audio-delay") &&
      !player->set("file-local-options/audio-delay", o.value("audio-delay").toDouble()))
    showOsd(uiTr("Задержка звука сейчас недоступна"));
  for (const auto &key : {QString("audio-file"), QString("subtitle-file")}) {
    const auto files = LaunchOptions::externalFiles(o, key);
    for (int i = 0; i < files.size(); ++i) {
      const bool select = i == files.size() - 1;
      if (key == "audio-file") loadExternalAudio(files[i], select);
      else loadExternalSubtitles(files[i], select);
    }
  }
}
void Window::finishLaunchOptions() {
  if (launchOptionsGeneration != playbackGeneration || pendingLaunchOptions.isEmpty()) return;
  const auto options = pendingLaunchOptions;
  pendingLaunchOptions = {};
  applyLaunchPlaybackOptions(options);
  const auto generation = playbackGeneration;
  QTimer::singleShot(0, this, [this, options, generation] {
    if (generation == playbackGeneration && !stopped) applyLaunchWindowOptions(options);
  });
}
void Window::exitApplication() {
  forceExit = true;
  // Closing an already hidden window does not emit lastWindowClosed.
  // Persist through closeEvent first, then stop the application event loop.
  if (close())
    QCoreApplication::quit();
}
bool Window::hideToTray() {
  if (!profile.settings.value("desktop/tray", false).toBool() || !trayIcon ||
      !trayIcon->isVisible() ||
      !(trayAvailability ? trayAvailability()
                         : QSystemTrayIcon::isSystemTrayAvailable()))
    return false;
  if (!hiddenToTray) {
    trayWindowState = windowState() & ~Qt::WindowMinimized;
    hiddenToTray = true;
    if (isFullScreen())
      restoreDisplayMode();
    hide();
  }
  return true;
}
void Window::minimizeToTray() {
  if (isMinimized() &&
      profile.settings.value("desktop/minimizeToTray", false).toBool())
    hideToTray();
}
void Window::restoreFromTray() {
  if (hiddenToTray) {
    hiddenToTray = false;
    if (trayWindowState.testFlag(Qt::WindowFullScreen)) {
      // Re-enter through the normal path so a requested display mode is
      // applied.
      if (isFullScreen())
        toggleFullscreen();
      toggleFullscreen();
    } else if (trayWindowState.testFlag(Qt::WindowMaximized))
      showMaximized();
    else
      showNormal();
  } else {
    setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
  }
  raise();
  if (windowHandle())
    windowHandle()->requestActivate();
  activateWindow();
}
void Window::applyDesktopPreferences() {
  const bool enabled = profile.settings.value("desktop/tray", false).toBool();
  if (enabled && !trayIcon) {
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setObjectName("desktopTrayIcon");
    trayIcon->setIcon(windowIcon().isNull()
                          ? style()->standardIcon(QStyle::SP_MediaPlay)
                          : windowIcon());
    auto *menu = new QMenu(this);
    menu->setObjectName("desktopTrayMenu");
    auto *restore =
        menu->addAction(uiTr("Показать плеер"), this, &Window::restoreFromTray);
    restore->setObjectName("trayRestore");
    auto *hide =
        menu->addAction(uiTr("Скрыть в трей"), this, [this] { hideToTray(); });
    hide->setObjectName("trayHide");
    menu->addSeparator();
    for (const auto &id : {"play", "stop", "previous", "next", "mute"})
      if (actions.contains(id))
        menu->addAction(actions[id]);
    menu->addSeparator();
    menu->addAction(actions["quit"]);
    trayIcon->setContextMenu(menu);
    connect(trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
              if (reason == QSystemTrayIcon::Trigger ||
                  reason == QSystemTrayIcon::DoubleClick)
                restoreFromTray();
            });
    connect(this, &QWidget::windowTitleChanged, trayIcon,
            &QSystemTrayIcon::setToolTip);
    auto *watch = new QTimer(trayIcon);
    watch->setObjectName("trayAvailabilityWatch");
    watch->setInterval(500);
    connect(watch, &QTimer::timeout, this, [this] {
      if (hiddenToTray &&
          !(trayAvailability ? trayAvailability()
                             : QSystemTrayIcon::isSystemTrayAvailable()))
        restoreFromTray();
    });
  }
  if (!trayIcon)
    return;
  auto *watch = trayIcon->findChild<QTimer *>("trayAvailabilityWatch");
  trayIcon->setToolTip(windowTitle());
  trayIcon->setVisible(enabled);
  if (enabled)
    watch->start();
  else {
    watch->stop();
    if (hiddenToTray)
      restoreFromTray();
  }
}
void Window::attachDesktopPreferences(QTabWidget *tabs,
                                      QList<std::function<void()>> *apply) {
  auto *page = new QWidget;
  page->setObjectName("desktopPreferencesPage");
  auto *form = new QFormLayout(page);
  const QList<QPair<QString, QString>> options = {
      {"instance/reuseExisting",
       uiTr("Открывать файлы в первом запущенном окне этого профиля")},
      {"instance/activateOnOpen", uiTr("Показывать окно при передаче файлов")},
      {"desktop/tray", uiTr("Значок в системном трее")},
      {"desktop/closeToTray", uiTr("Кнопка закрытия скрывает окно в трей")},
      {"desktop/minimizeToTray",
       uiTr("При сворачивании скрывать окно в трей")}};
  QList<QCheckBox *> boxes;
  for (const auto &[key, label] : options) {
    auto *box = new QCheckBox(label);
    box->setObjectName(key);
    box->setChecked(
        profile.settings.value(key, key == "instance/activateOnOpen").toBool());
    form->addRow(box);
    boxes << box;
  }
  const auto update = [boxes] {
    boxes[3]->setEnabled(boxes[2]->isChecked());
    boxes[4]->setEnabled(boxes[2]->isChecked());
  };
  connect(boxes[2], &QCheckBox::toggled, page, [update] { update(); });
  update();
  auto *profileLabel = new QLabel(uiTr("Папка профиля: ") +
                                  QDir::toNativeSeparators(profile.directory));
  profileLabel->setWordWrap(true);
  profileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  form->addRow(profileLabel);
  auto *note = new QLabel(uiTr(
      "Режим одного окна применяется при следующем запуске. --enqueue "
      "добавляет файлы в очередь, --new-instance открывает отдельное окно, "
      "--profile выбирает папку профиля. Выход через меню или Alt+X завершает "
      "плеер. Если трей недоступен, окно не скрывается."));
  note->setWordWrap(true);
  form->addRow(note);
  auto *available = new QLabel(QSystemTrayIcon::isSystemTrayAvailable()
                                   ? uiTr("Системный трей доступен")
                                   : uiTr("Системный трей сейчас недоступен"));
  available->setObjectName("trayAvailabilityLabel");
  form->addRow(available);
  auto *reset = new QPushButton(uiTr("Сбросить параметры запуска и трея"));
  connect(reset, &QPushButton::clicked, page, [boxes] {
    for (int i = 0; i < boxes.size(); ++i)
      boxes[i]->setChecked(i == 1);
  });
  form->addRow(reset);
  tabs->addTab(page, uiTr("Запуск и трей"));
  apply->append([this, boxes, options] {
    for (int i = 0; i < boxes.size(); ++i)
      profile.settings.setValue(options[i].first, boxes[i]->isChecked());
  });
}
void Window::desktopSettingsDialog() {
  QDialog dialog(this);
  dialog.setObjectName("desktopSettingsDialog");
  dialog.setWindowTitle(uiTr("Запуск и системный трей"));
  dialog.resize(650, 410);
  auto *layout = new QVBoxLayout(&dialog);
  auto *tabs = new QTabWidget;
  QList<std::function<void()>> apply;
  attachDesktopPreferences(tabs, &apply);
  layout->addWidget(tabs);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  if (dialog.exec() == QDialog::Accepted) {
    for (const auto &fn : apply)
      fn();
    applyDesktopPreferences();
  }
}
