#include "../../shared/familydefaults.h"
#include "window.h"
#include "classicpreferences.h"
#include "classicstatus.h"
#include "classicvolume.h"
#include "launchoptions.h"
#include "webcontrol.h"
#include "windowtitle.h"
#include "associationoptions.h"
#include "appearance.h"
#include "controls.h"
#include "devicesource.h"
#include "framepreview.h"
#include "i18n.h"
#include "integration.h"
#include "mediaoptions.h"
#include "mediaexports.h"
#include "mediaremote.h"
#include "mediasource.h"
#include "onlineoptions.h"
#include "player.h"
#include "playlistfile.h"
#include "playlistpanel.h"
#include "profileoptions.h"
#include "progressivesource.h"
#include "timeline.h"
#include "videodisplay.h"
#include "videooutput.h"
#include "videoshaders.h"
#include <QMimeData>
#include <QProcess>
#include <QUrl>
#include <QtWidgets>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
class PlayerLogo final : public QLabel {
public:
  explicit PlayerLogo(QWidget *parent) : QLabel(parent) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  }
protected:
  void paintEvent(QPaintEvent *) override {
    const auto image = pixmap();
    if (image.isNull()) return;
    QSizeF wanted = QSizeF(image.size()) / image.devicePixelRatio();
    if (wanted.width() > width() || wanted.height() > height())
      wanted.scale(size(), Qt::KeepAspectRatio);
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(QRectF((width() - wanted.width()) / 2,
                             (height() - wanted.height()) / 2,
                             wanted.width(), wanted.height()), image, image.rect());
  }
};
class ClassicButtonStyle final : public QProxyStyle {
public:
  ClassicButtonStyle() : QProxyStyle("Fusion") {}
  int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                const QWidget *widget = nullptr, QStyleHintReturn *data = nullptr) const override {
    return hint == QStyle::SH_DialogButtonLayout ? QDialogButtonBox::WinLayout
        : QProxyStyle::styleHint(hint, option, widget, data);
  }
};
}
Window::Window(Profile &p, std::function<void(const QString &)> systemHandler,
               DisplayMode::Command displayCommand, std::function<bool()> trayCheck,
               int initialWebPort)
    : trayAvailability(std::move(trayCheck)), systemActionHandler(std::move(systemHandler)), profile(p),
      displayMode(std::move(displayCommand)) {
  static const bool resourcesReady = [] { Q_INIT_RESOURCE(resources); return true; }();
  Q_UNUSED(resourcesReady);
  connect(this, &Window::playbackIntentChanged, this, &Window::cancelProgressiveOpening);
  videoTransform = VideoTransform::load(profile.settings);
  const bool languageLoaded = I18n::load(profile.settings, profile.directory);
  Appearance::apply(*qApp, profile.settings);
  setWindowTitle(WindowTitle::idle());
  setWindowIcon(QIcon(":/icon.png"));
  resize(1060, 680);
  setMinimumSize(560, 320);
  setAcceptDrops(true);
  auto *root = new QWidget;
  auto *layout = new QVBoxLayout(root);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  setCentralWidget(root);
  const int initialVolume = profile.settings.value("audio/volume", FoxicMPFamily::Volume).toInt();
  const bool initialMute = profile.settings.value("audio/mute", FoxicMPFamily::Mute).toBool();
  player = new Player(
      root, profile.settings.value("video/output", "software").toString(),
      OnlineOptions::mpvOptions(profile.settings),
      VideoDisplay::values(profile.settings).value("vsync") == "off" ? 0 : 1);
  player->set("volume", initialVolume);
  player->set("mute", initialMute);
  VideoOutput::apply(player, profile.settings);
  VideoDisplay::apply(player, profile.settings);
  VideoShaders::apply(player, profile.settings);
  player->set("alang",
              profile.settings.value("audio/languages", "").toString());
  player->set("slang",
              profile.settings.value("subtitles/languages", "").toString());
  layout->addWidget(player, 1);
  logo = new PlayerLogo(player);
  logo->setPixmap(
      QPixmap::fromImage(Appearance::builtinLogo(profile.settings))
          .scaled(505, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  logo->setAlignment(Qt::AlignCenter);
  logo->setStyleSheet("background:transparent;");
  logo->setAttribute(Qt::WA_TransparentForMouseEvents);
  auto *overlay = new QVBoxLayout(player);
  overlay->setContentsMargins(0, 0, 0, 0);
  overlay->addWidget(logo);
  timeline = new Timeline(root);
  layout->addWidget(timeline);
  buildActions();
  auto *bar = new QWidget;
  controls = bar;
  bar->setObjectName("controls");
  auto *buttons = new QHBoxLayout(bar);
  buttons->setContentsMargins(5, 1, 8, 1);
  buttons->setSpacing(0);
  layout->addWidget(bar);
  auto button = [&](const QString &id, const QString &icon) {
    auto *b = new QToolButton;
    b->setObjectName("control_" + id);
    b->setDefaultAction(actions[id]);
    actions[id]->setIcon(controlIcon(icon));
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setAccessibleName(actions[id]->text());
    b->setIconSize(QSize(16, 16));
    b->setFixedSize(23, 22);
    b->setFocusPolicy(Qt::NoFocus);
    const QMap<QString, QString> tooltips{
        {"play", uiTr("Воспроизведение / пауза")},
        {"stop", uiTr("Стоп / закрыть файл")},
        {"previous", uiTr("Предыдущий / предыдущий файл")},
        {"next", uiTr("Следующий / следующий файл")},
#ifdef Q_OS_WIN
        {"frameNext", uiTr("Шаг / Перейти")},
#else
        {"frameNext", uiTr("Следующий кадр / главы")},
#endif
        {"open", uiTr("Открыть / последние файлы")},
        {"audioQuick", uiTr("Аудиодорожка / параметры")},
        {"subtitleQuick", uiTr("Субтитры / параметры")},
        {"mute", uiTr("Включить или отключить звук")}};
    if (tooltips.contains(id)) b->setToolTip(tooltips.value(id));
    buttons->addWidget(b);
    return b;
  };
  play = button("play", "play");
  button("stop", "stop");
  button("previous", "previous");
  button("next", "next");
  button("frameNext", "frame");
  button("open", "open");
  button("audioQuick", "audio");
  button("subtitleQuick", "subtitles");
  auto *status = new ClassicStatusArea(bar);
  statusArea = status;
  state = status->message;
  state->setObjectName("playbackStatus");
  time = status->timer;
  time->setObjectName("playbackClock");
  time->setCursor(Qt::PointingHandCursor);
  time->setToolTip(uiTr("Щелчок — оставшееся время; правая кнопка — миллисекунды"));
  gpuIndicator = status->gpu;
  gpuIndicator->setObjectName("hardwareDecoderIndicator");
  gpuIndicator->setPixmap(QIcon(":/classic-gpu.svg").pixmap(16, 16));
  buttons->addWidget(status, 1);
  mute = button("mute", initialMute ? "mute" : "volume");
  volume = new ClassicVolumeSlider;
  volume->setRange(0, 100);
  volume->setValue(profile.settings.value("audio/volume", FoxicMPFamily::Volume).toInt());
  static_cast<ClassicVolumeSlider *>(volume)->setMuted(initialMute);
  volume->setToolTip(uiTr("Громкость"));
  buttons->addWidget(volume);
  auto *grip = new ClassicResizeGrip(bar);
  buttons->addWidget(grip);
  connect(volume, &QSlider::valueChanged, this, [this](int v) {
    player->set("volume", v);
    profile.settings.setValue("audio/volume", v);
    static_cast<ClassicVolumeSlider *>(volume)->setMuted(player->get("mute").toBool());
  });
  dock = new QDockWidget(uiTr("Плейлист"), this);
  dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  queuePanel = new PlaylistPanel(profile, this);
  playlist = queuePanel->savedPlayingList();
  connect(queuePanel, &PlaylistPanel::importFailed, this,
          [this](const QString &message) {
            state->setText(uiTr("Плейлист: ") + message);
            state->setToolTip(message);
          });
  for (auto *action : queuePanel->findChildren<QAction *>()) {
    const auto name = action->objectName();
    if (!name.startsWith("action_"))
      continue;
    const auto id = name.mid(7);
    actions[id] = action;
    defaultKeys[id] = "";
    addAction(action);
  }
  applyShortcuts();
  auto *queueButtons = new QHBoxLayout;
  for (auto id : {"playlistAdd", "playlistRemove", "playlistUp", "playlistDown",
                  "playlistLoad", "playlistSave"}) {
    auto *button = new QToolButton;
    button->setDefaultAction(actions[id]);
    const QString label = id == QString("playlistAdd")      ? "+"
                          : id == QString("playlistRemove") ? "−"
                          : id == QString("playlistUp")     ? "↑"
                          : id == QString("playlistDown")   ? "↓"
                          : id == QString("playlistLoad")   ? uiTr("Открыть")
                                                            : uiTr("Сохранить");
    button->setText(label);
    connect(actions[id], &QAction::changed, button,
            [button, label] { button->setText(label); });
    queueButtons->addWidget(button);
  }
  qobject_cast<QVBoxLayout *>(queuePanel->layout())
      ->insertLayout(0, queueButtons);
  dock->setWidget(queuePanel);
  addDockWidget(Qt::RightDockWidgetArea, dock);
  dock->hide();
  setupPlaylistDock();
  const auto savedTabs =
      QJsonDocument::fromJson(
          profile.settings.value("playlist/tabsV1").toByteArray())
          .object();
  const QString savedId = savedTabs.value("currentId").toString();
  const int savedIndex =
      profile.settings.value("files/currentIndex", 0).toInt();
  if (playlist->count()) {
    currentItem =
        playlist->item(std::clamp(savedIndex, 0, playlist->count() - 1));
    for (int i = 0; i < playlist->count(); ++i)
      if (playlist->item(i)->data(Qt::UserRole + 1).toString() == savedId)
        currentItem = playlist->item(i);
    currentId = currentItem->data(Qt::UserRole + 1).toString();
    current = currentItem->data(Qt::UserRole).toString();
    playlist->setCurrentItem(currentItem);
  }
  connect(queuePanel, &PlaylistPanel::playRequested, this,
          [this](QListWidget *list, int row) {
            if (row < 0)
              return;
            selectPlaybackList(list);
            playIndex(row);
          });
  connect(queuePanel, &PlaylistPanel::removing, this,
          [this](QListWidget *list, const QList<QListWidgetItem *> &items) {
            if (list == playlist && items.contains(currentItem)) {
              savePosition();
              closePlaybackFile();
            }
          });
  connect(queuePanel, &PlaylistPanel::removingList, this,
          [this](QListWidget *list) {
            if (list != playlist)
              return;
            savePosition();
            closePlaybackFile();
            playlist = nullptr;
          });
  connect(queuePanel, &PlaylistPanel::changed, this,
          [this](QListWidget *list, bool resetOrder) {
            if (!playlist)
              playlist = queuePanel->activeList();
            syncPlaylist();
            if (list == playlist && resetOrder)
              resetShuffle();
          });
  for (auto id :
       {"playlistRemove", "playlistUp", "playlistDown", "playlistClear",
        "playlistAdd", "playlistLoad", "playlistSave"})
    queuePanel->operationsMenu()->addAction(actions[id]);
  resetShuffle();
  syncPlaylist();
  buildMenus();
  menuBar()->setVisible(profile.settings.value("ui/showMenu", true).toBool());
  hideControls = new QTimer(this);
  hideControls->setSingleShot(true);
  hideControls->setInterval(2500);
  connect(hideControls, &QTimer::timeout, this, [this] {
    if (!isFullScreen() || hideControls->interval() == 0)
      return;
    if (contextMenu->isVisible() || QApplication::activePopupWidget() ||
        dock->isVisible() || controls->underMouse() || timeline->underMouse() ||
        QApplication::activeModalWidget()) {
      hideControls->start();
      return;
    }
    applyPanelVisibility(false);
    player->setCursor(Qt::BlankCursor);
  });
  clickTimer = new QTimer(this);
  clickTimer->setSingleShot(true);
  clickTimer->setInterval(QApplication::doubleClickInterval());
  connect(clickTimer, &QTimer::timeout, this, [this] {
    const auto command = pendingMouseCommand;
    pendingMouseCommand.clear();
    dispatchInputCommand(command);
  });
  setupMouseHold();
  setupSubtitleTools();
  player->setMouseTracking(true);
  for (auto *child : controls->findChildren<QWidget *>()) {
    child->setMouseTracking(true);
  }
  connect(timeline, &Timeline::seek, this,
          [this](double seconds) { seekTo(seconds); });
  connect(player, &Player::loaded, this, [this] {
    const auto params = player->get("video-params").toMap();
    transformStereoHint = params.value("stereo-in").toString();
    applyVideoTransform();
    windowMediaLoaded();
    failedEntries.clear();
    if (!progressive && currentItem) {
      currentItem->setData(PlaylistPanel::Invalid, false);
      currentItem->setForeground(QBrush());
      currentItem->setToolTip(current);
    }
    preview->cancel();
    previewPopup->hide();
    lastError.clear();
    completionArmed = !DeviceSource::isLive(current);
    stopped = false;
    titleMetadata = player->get("media-title").toString();
    updateWindowTitle();
    enforceAudioPolicy();
    enforceSubtitlePolicy();
    timeline->setEnabled(!stdinPlayback && !DeviceSource::isLive(current));
    actions["frameNext"]->setEnabled(!DeviceSource::isLive(current));
    actions["frameBack"]->setEnabled(!stdinPlayback && !DeviceSource::isLive(current));
    DeviceSource::Request disc;
    actions["discTitle"]->setEnabled(
        DeviceSource::decode(current, &disc) &&
        (disc.kind == "dvd" || disc.kind == "bluray"));
    logo->hide();
    updateClock();
    actions["play"]->setIcon(controlIcon("pause"));
    if (progressive) {
      progressiveTracksReady=true;
      completionArmed = false;
      setSpeed((progressivePendingSeek >= 0 || profile.settings.value("playback/keepSpeed", false).toBool())
                   ? sessionSpeed
                   : 1);
      player->set("pause", progressiveBuffering || progressiveUserPause ||
                               pausedByMinimize);
      refreshPlaybackControls();
      updateMinimizePause();
      return;
    }
    if (stdinPlayback) {
      player->set("pause", holdAfterLoad);
      holdAfterLoad = false;
      setSpeed(1);
      finishLaunchOptions();
      refreshPlaybackControls();
      updateMinimizePause();
      return;
    }
    if (DeviceSource::isLive(current))
      player->set("speed", 1);
    else
      setSpeed((repeatSameSource ||
                profile.settings.value("playback/keepSpeed", false).toBool())
                   ? sessionSpeed
                   : 1);
    repeatSameSource = false;
    if (currentItem) {
      const auto data = currentItem->data(PlaylistPanel::Metadata).toMap();
      for (const auto &entry : QList<QPair<QString, QString>>{
               {"audioFiles", "audio-add"}, {"subtitleFiles", "sub-add"}}) {
        const bool audio = entry.first == "audioFiles";
        const auto launchKey = audio ? QString("audio-file") : QString("subtitle-file");
        const auto launchFiles = LaunchOptions::externalFiles(pendingLaunchOptions, launchKey);
        for (const auto &file : data.value(entry.first).toStringList()) {
          // CLI will submit these once below. Their async add commands may not
          // have appeared in track-list yet, so inspecting it alone can double-add.
          if (launchFiles.contains(file)) continue;
          const bool select =
              launchFiles.isEmpty() &&
              data.value(audio ? "selectedAudioFile" : "selectedSubtitleFile")
                      .toString() == file &&
              profile.settings.value("playlist/rememberTracks", true).toBool();
          player->addExternalTrack(audio ? "audio" : "sub", file, select);
        }
      }
    }
    player->set("pause", holdAfterLoad);
    holdAfterLoad = false;
    profile.recordRecent(current);
    double resume =
        pendingResume >= 0 ? pendingResume : profile.position(current);
    if (DeviceSource::isLive(current))
      resume = 0;
    pendingResume = -1;
    const double length = player->get("duration").toDouble();
    // DVD VM commands can chain or restart titles without reporting EOF.
    // Opening a film selects that title, so stop at its advertised duration.
    // A file-local limit is automatically removed for the next source.
    if (disc.kind == "dvd" && length > 0)
      player->set("file-local-options/end", QString::number(length, 'f', 3));
    if (resume > 0 && (length <= 0 || resume < length))
      player->command(
          {"seek", QString::number(resume, 'f', 3), "absolute+exact"});
    restorePlaylistTracks();
    finishLaunchOptions();
    updatePlaylistChapters();
    syncPlaylist();
    refreshPlaybackControls();
    updateMinimizePause();
  });
  const auto rememberDiagnostic = [this](const QString &message) {
    diagnosticMessages.append(QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + " " + message.left(2048));
    while (diagnosticMessages.size() > 100) diagnosticMessages.removeFirst();
  };
  connect(player, &Player::diagnosticMessage, this, rememberDiagnostic);
  connect(player, &Player::commandFinished, this, [this](quint64 request,int status) {
    const auto found=progressiveSubtitleRequests.find(request);
    if(found==progressiveSubtitleRequests.end())return;
    const auto id=found.value();progressiveSubtitleRequests.erase(found);
    if(status<0) {
      progressiveSubtitleLoadErrors[id]=QString::fromUtf8(mpv_error_string(status));
      progressiveAttachedSubtitles.remove(id);
    }
  });
  connect(player, &Player::error, this, rememberDiagnostic);
  connect(player, &Player::error, this, [this](const QString &msg) {
    lastError = msg;
    if (progressive) {
      releaseProgressive();
      player->command({"stop"});
    }
    state->setText(uiTr("Ошибка: ") + msg);
    state->setToolTip(msg);
  });
  connect(player, &Player::fileFailed, this, [this](const QString &path) {
    if (path.isEmpty() || path == current || path == expectedPlaybackSource)
      handlePlaylistFailure();
  });
  connect(player, &Player::ended, this, [this](bool natural) {
    clearAbLoop();
    pausedByMinimize = false;
    if (progressive) {
      if (natural) {
        const bool complete = progressive->status().complete;
        if (!complete)
          lastError = uiTr("Ошибка: неподтверждённый конец источника");
        releaseProgressive(complete ? "finished" : "error");
        state->setText(complete
                           ? uiTr("Просмотр завершён")
                           : uiTr("Ошибка: неподтверждённый конец источника"));
      }
      completionArmed = false;
      if (natural)
        exitFullscreenAtEnd();
      return;
    }
    if (natural)
      finishPlayback();
    else
      completionArmed = false;
  });
  connect(
      player, &Player::property, this,
      [this](const QString &name, const QVariant &v) {
        if (name == "time-pos") {
          if (v.isValid())
            pos = std::max(
                0.,
                v.toDouble() -
                    (progressive ? progressive->status().timestampOffset : 0));
          timeline->position = pos;
          timeline->update();
        } else if (name == "duration") {
          duration =
              progressive ? progressive->status().duration : v.toDouble();
          timeline->duration = duration;
          timeline->update();
        } else if (name == "pause") {
          paused = v.toBool();
          actions["play"]->setIcon(controlIcon(paused ? "play" : "pause"));
          if (!stopped) {
            showOsd(paused ? uiTr("Пауза") : uiTr("Воспроизведение"));
          }
        } else if (name == "volume") {
          QSignalBlocker b(volume);
          volume->setValue(qRound(v.toDouble()));
          if (!stopped)
            showOsd(uiTr("Громкость: ") +
                    QString::number(qRound(v.toDouble())) + "%");
        } else if (name == "mute") {
          profile.settings.setValue("audio/mute", v.toBool());
          static_cast<ClassicVolumeSlider *>(volume)->setMuted(v.toBool());
          actions["mute"]->setIcon(controlIcon(v.toBool() ? "mute" : "volume"));
          actions["mute"]->setChecked(v.toBool());
          if (!stopped)
            showOsd(v.toBool() ? uiTr("Звук выключен") : uiTr("Звук включён"));
        } else if (name == "speed") {
          if (profile.settings.value("audio/passthrough", false).toBool())
            applyMediaOptions();
          if (!stopped)
            showOsd(uiTr("Скорость: ") + QString::number(v.toDouble()) + "×");
        } else if (name == "seeking") {
          if (v.toBool())
            seekingNotice = true;
          else if (seekingNotice && !stopped) {
            seekingNotice = false;
            showOsd(uiTr("Позиция: ") +
                    clockText(player->get("time-pos").toDouble()));
          }
        } else if (name == "video-params" || name == "video-out-params") {
          if (name == "video-params") {
            const auto hint = v.toMap().value("stereo-in").toString();
            if (hint != transformStereoHint) {
              transformStereoHint = hint;
              if (videoTransform.stereoInput == "auto")
                applyVideoTransform();
            }
          }
          refreshPlaybackControls();
          QTimer::singleShot(0, this, &Window::autoSizeVideo);
        } else if (name == "track-list")
          refreshTracks();
        else if (name == "chapter-list") {
          updatePlaylistChapters();
        } else if (name == "media-title") {
          titleMetadata = v.toString();
        } else if (name == "idle-active" && v.toBool()) {
          stopped = true;
          refreshPlaybackControls();
          timeline->setEnabled(!stdinPlayback);
          actions["frameNext"]->setEnabled(true);
          actions["frameBack"]->setEnabled(!stdinPlayback);
          actions["discTitle"]->setEnabled(false);
          preview->cancel();
          previewPopup->hide();
          logo->show();
          pos = duration = 0;
          timeline->position = timeline->duration = 0;
          timeline->chapters.clear();
          timeline->update();
          actions["play"]->setIcon(controlIcon("play"));
          state->setText(lastError.isEmpty() ? uiTr("Остановлено")
                                             : uiTr("Ошибка: ") + lastError);
          setWindowTitle(WindowTitle::idle());
        } else if (name == "paused-for-cache" && v.toBool())
          state->setText(uiTr("Буферизация…"));
        if (name == "pause" || name == "idle-active" ||
            name == "video-params" || name == "track-list")
          QTimer::singleShot(0, this, &Window::applyWindowMode);
        updateClock();
      });
  auto *save = new QTimer(this);
  save->setInterval(5000);
  connect(save, &QTimer::timeout, this, &Window::savePosition);
  save->start();
  auto *progressiveTimer = new QTimer(this);
  progressiveTimer->setInterval(100);
  connect(progressiveTimer, &QTimer::timeout, this, &Window::updateProgressive);
  progressiveTimer->start();
  if (profile.settings.value("ui/rememberGeometry", FoxicMPFamily::RememberGeometry).toBool() &&
      profile.settings.contains("window/geometry"))
    restoreGeometry(profile.settings.value("window/geometry").toByteArray());
  setupPlaybackTools();
  qApp->installEventFilter(this);
  setupMediaRemote();
  setupWebControl(initialWebPort);
  QTimer::singleShot(0, this, &Window::setupWindowPresentation);
  QTimer::singleShot(0, this, &Window::integrationPrompt);
  if (!languageLoaded)
    QTimer::singleShot(0, this, [this] {
      showOsd(uiTr("Не удалось загрузить язык; используется русский"));
    });
}

#ifdef Q_OS_WIN
bool Window::nativeEvent(const QByteArray &eventType, void *message,
                         qintptr *result) {
  if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
    auto *msg = static_cast<MSG *>(message);
    if (msg && msg->message == WM_APPCOMMAND) {
      const auto command = GET_APPCOMMAND_LPARAM(msg->lParam);
      const auto trigger = [this](const char *id) {
        if (auto *action = actions.value(id)) { action->trigger(); return true; }
        return false;
      };
      bool handled = false;
      switch (command) {
      case APPCOMMAND_MEDIA_PLAY_PAUSE: handled = trigger("play"); break;
      case APPCOMMAND_MEDIA_STOP: handled = trigger("stop"); break;
      case APPCOMMAND_MEDIA_NEXTTRACK: handled = trigger("next"); break;
      case APPCOMMAND_MEDIA_PREVIOUSTRACK: handled = trigger("previous"); break;
      case APPCOMMAND_MEDIA_FAST_FORWARD: handled = trigger("forward"); break;
      case APPCOMMAND_MEDIA_REWIND: handled = trigger("back"); break;
      case APPCOMMAND_VOLUME_UP: handled = trigger("volumeUp"); break;
      case APPCOMMAND_VOLUME_DOWN: handled = trigger("volumeDown"); break;
      case APPCOMMAND_VOLUME_MUTE: handled = trigger("mute"); break;
      default: break;
      }
      if (handled) { if (result) *result = 1; return true; }
    }
  }
  return QMainWindow::nativeEvent(eventType, message, result);
}
#endif
void Window::buildMenus() {
  contextMenu = new QMenu(this);
  contextMenu->setObjectName("videoContextMenu");
  historyMenu = new QMenu(uiTr("Недавние файлы"), contextMenu);
  historyMenu->setObjectName("historyMenu");
  favoritesMenu = new QMenu(uiTr("Избранное и закладки"), contextMenu);
  favoritesMenu->setObjectName("favoritesMenu");
  for (auto *menu : {historyMenu, favoritesMenu}) {
    menu->setToolTipsVisible(true);
    connect(menu, &QMenu::aboutToShow, this, &Window::refreshLibraryMenus);
  }
  audioMenu = new QMenu(uiTr("Аудиодорожка"), contextMenu);
  audioMenu->setObjectName("audioTracksMenu");
  subtitleMenu = new QMenu(uiTr("Дорожка субтитров"), contextMenu);
  subtitleMenu->setObjectName("subtitleTracksMenu");
  chapterMenu = new QMenu(
#ifdef Q_OS_WIN
      uiTr("Перейти"),
#else
      uiTr("Перейти к главе"),
#endif
      contextMenu);
  chapterMenu->setObjectName("chaptersMenu");
  buildAfterPlaybackMenu(contextMenu);
  refreshLibraryMenus();
  applyClassicMenus();
  refreshTracks();
  refreshChapters();
  player->setContextMenuPolicy(Qt::DefaultContextMenu);
}
void Window::openDialog() {
  openFiles(QFileDialog::getOpenFileNames(
      this, uiTr("Открыть медиафайл"),
      profile.settings.value("files/lastDirectory").toString(),
      uiTr("Медиафайлы (*)")));
}
void Window::openDirectoryDialog() {
  const QString path = QFileDialog::getExistingDirectory(
      this, uiTr("Открыть папку"),
      profile.settings.value("files/lastDirectory").toString());
  if (!path.isEmpty())
    openDirectory(path);
}
void Window::openUrlDialog() {
  QDialog dialog(this);
  dialog.setObjectName("urlDialog");
  dialog.setWindowTitle(uiTr("Открыть"));
  dialog.resize(520, 230);
  auto *layout = new QVBoxLayout(&dialog);
  auto *heading = new QHBoxLayout;
  auto *icon = new QLabel; icon->setPixmap(windowIcon().pixmap(32, 32));
  heading->addWidget(icon);
  heading->addWidget(new QLabel(uiTr("Введите путь медиафайла или ссылку URL\n(на вашем компьютере или в Интернете)")), 1);
  layout->addLayout(heading);
  auto *form = new QFormLayout;
  const auto entry = [&](const QString &name, const QString &caption) {
    auto *row = new QWidget; auto *line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    auto *box = new QComboBox; box->setEditable(true); box->setInsertPolicy(QComboBox::NoInsert);
    box->lineEdit()->setObjectName(name); line->addWidget(box, 1);
    auto *browse = new QPushButton(uiTr("Выбрать...")); line->addWidget(browse);
    connect(browse, &QPushButton::clicked, &dialog, [this, box, &dialog] {
      const auto path = QFileDialog::getOpenFileName(&dialog, uiTr("Открыть медиафайл"), profile.settings.value("files/lastDirectory").toString());
      if (!path.isEmpty()) box->setEditText(path);
    });
    form->addRow(caption, row); return box;
  };
  auto *source = entry("sourceAddress", uiTr("Открыть:"));
  source->addItems(profile.recentFiles()); source->setCurrentText({});
  auto *dub = entry("sourceDubbing", uiTr("Дубляж:"));
  layout->addLayout(form);
  auto *enqueue = new QCheckBox(uiTr("Только добавить в плейлист"));
  enqueue->setObjectName("sourceEnqueue"); layout->addWidget(enqueue);
  auto *clipboard = new QCheckBox(uiTr("Вставлять URL из буфера обмена"));
  clipboard->setObjectName("sourcePasteUrls");
  clipboard->setChecked(profile.settings.value("files/pasteUrl", false).toBool());
  layout->addWidget(clipboard);
  const auto pasteUrl = [source] {
    const auto value = QApplication::clipboard()->text().trimmed();
    if (MediaSource::isNetwork(value)) source->setEditText(value);
  };
  if (clipboard->isChecked()) pasteUrl();
  connect(clipboard, &QCheckBox::toggled, &dialog, [pasteUrl](bool checked) { if (checked) pasteUrl(); });
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  auto *paste = buttons->addButton(uiTr("Вставить из буфера"), QDialogButtonBox::ActionRole);
  paste->setObjectName("pasteSourceAddress");
  connect(paste, &QPushButton::clicked, &dialog, [source] { source->setEditText(QApplication::clipboard()->text().trimmed()); });
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  QString error;
  const auto path = MediaSource::normalize(source->currentText(), &error);
  if (path.isEmpty()) { state->setText(uiTr("Адрес: ") + error); return; }
  const auto audio = dub->currentText().trimmed();
  LaunchRequest request; request.files = {path}; request.enqueue = enqueue->isChecked();
  if (!audio.isEmpty()) {
    const auto normalized = MediaSource::normalize(audio, &error);
    if (normalized.isEmpty()) { state->setText(error); return; }
    request.options.insert("audio-file", QJsonArray{normalized});
  }
  profile.settings.setValue("files/pasteUrl", clipboard->isChecked());
  handleLaunchRequest(request);
}

void Window::openExternalAudioDialog() {
  const QString path = QFileDialog::getOpenFileName(
      this, uiTr("Открыть внешнюю аудиодорожку"),
      profile.settings.value("files/lastDirectory").toString(),
      uiTr("Аудиофайлы (*.aac *.ac3 *.aiff *.alac *.ape *.dts *.flac *.m4a "
           "*.mka "
           "*.mp3 *.ogg *.opus *.wav *.wma);;Все файлы (*)"));
  if (!path.isEmpty())
    loadExternalAudio(path);
}
void Window::openOnlinePlaylistDialog() {
  bool ok = false;
  const QString url = QInputDialog::getText(
      this, uiTr("Открыть онлайн-плейлист"), uiTr("Адрес плейлиста:"),
      QLineEdit::Normal, {}, &ok);
  if (ok && !url.trimmed().isEmpty())
    loadOnlinePlaylist(url);
}
void Window::buildActions() {
  auto add = [&](const QString &id, const QString &text, const QString &key,
                 auto callback) {
    auto *a = new QAction(text, this);
    a->setObjectName("action_" + id);
    actions[id] = a;
    defaultKeys[id] = key;
    addAction(a);
    connect(a, &QAction::triggered, this, [this, callback] {
      cancelMouseHold();
      if (clickTimer) {
        clickTimer->stop();
        showFullscreenControls();
      }
      callback();
    });
  };
  add("open", uiTr("Открыть…"), "Ctrl+O", [this] { openDialog(); });
  add("openDirectory", uiTr("Открыть папку…"), "Ctrl+Shift+D",
      [this] { openDirectoryDialog(); });
  add("openUrl", uiTr("Открыть адрес…"), "Ctrl+U", [this] { openUrlDialog(); });
  add("openDisc", uiTr("Открыть диск или ISO…"), "Ctrl+Shift+I",
      [this] { openDiscDialog(); });
  add("openCapture", uiTr("Открыть устройство захвата…"), "Ctrl+Shift+C",
      [this] { openCaptureDialog(); });
  add("openTelevision", uiTr("Цифровое ТВ…"), "",
      [this] { openTelevisionDialog(); });
  add("discTitle", uiTr("Заголовок DVD / плейлист Blu-ray…"), "",
      [this] { changeDiscTitle(); });
  add("chapterPrevious", uiTr("Предыдущая глава / трек CD"), "Ctrl+PgUp",
      [this] { changeChapter(-1); });
  add("chapterNext", uiTr("Следующая глава / трек CD"), "Ctrl+PgDown",
      [this] { changeChapter(1); });
  actions["discTitle"]->setEnabled(false);
  add("onlinePlaylist", uiTr("Открыть онлайн-плейлист…"), "Ctrl+Shift+U",
      [this] { openOnlinePlaylistDialog(); });
  add("openClipboard", uiTr("Открыть из буфера обмена"), "Ctrl+V",
      [this] { openClipboard(); });
  add("externalAudio", uiTr("Открыть внешнюю аудиодорожку…"), "",
      [this] { openExternalAudioDialog(); });
  add("reopen", uiTr("Открыть текущий файл заново"), "Ctrl+E",
      [this] { reopenCurrent(); });
  add("closeFile", uiTr("Закрыть файл"), "Ctrl+W", [this] { closeCurrent(); });
  add("play", uiTr("Воспроизведение / пауза"), "Space", [this] {
    pausedByMinimize = false;
    if (progressive) {
      progressiveUserPause = !progressiveUserPause;
      updateProgressive();
      return;
    }
    if (stopped && stdinPlayback) {
      showOsd(uiTr("Поток stdin одноразовый. Запустите передающую программу снова."));
      return;
    }
    if (stopped && playlist->count()) {
      int row = currentIndex >= 0 ? currentIndex : playlist->currentRow();
      playIndex(row >= 0 ? row : 0);
    } else if (!stopped)
      player->command({"cycle", "pause"});
  });
  add("stop", uiTr("Стоп"), ".", [this] {
    emit playbackIntentChanged();
    ++playbackGeneration;
    savePosition();
    completionArmed = false;
    releaseProgressive();
    player->command({"stop"});
  });
  add("previous", uiTr("Предыдущий"), "PgUp",
      [this] { navigateSkip(-1); });
  add("next", uiTr("Следующий"), "PgDown",
      [this] { navigateSkip(1); });
  add("playlist", uiTr("Плейлист"), "Ctrl+L", [this] {
    dock->setVisible(!dock->isVisible());
    showFullscreenControls();
  });
  add("mute", uiTr("Отключить звук"), "M",
      [this] { player->command({"cycle", "mute"}); });
  actions["mute"]->setCheckable(true);
  add("back", uiTr("Назад на 5 секунд"), "Left",
      [this] { seekStep(-1, "playback/seekMediumMs", FoxicMPFamily::SeekMediumMs); });
  add("forward", uiTr("Вперёд на 5 секунд"), "Right",
      [this] { seekStep(1, "playback/seekMediumMs", FoxicMPFamily::SeekMediumMs); });
  add("backSmall", uiTr("Короткая перемотка назад"), "Shift+Left",
      [this] { seekStep(-1, "playback/seekSmallMs", FoxicMPFamily::SeekSmallMs); });
  add("forwardSmall", uiTr("Короткая перемотка вперёд"), "Shift+Right",
      [this] { seekStep(1, "playback/seekSmallMs", FoxicMPFamily::SeekSmallMs); });
  add("backLarge", uiTr("Длинная перемотка назад"), "Ctrl+Alt+Left",
      [this] { seekStep(-1, "playback/seekLargeMs", FoxicMPFamily::SeekLargeMs); });
  add("forwardLarge", uiTr("Длинная перемотка вперёд"), "Ctrl+Alt+Right",
      [this] { seekStep(1, "playback/seekLargeMs", FoxicMPFamily::SeekLargeMs); });
  add("keyframeBack", uiTr("Предыдущий ключевой кадр"), "Alt+Left",
      [this] { seekKeyframe(-1); });
  add("keyframeNext", uiTr("Следующий ключевой кадр"), "Alt+Right",
      [this] { seekKeyframe(1); });
  add("restart", uiTr("В начало"), "Home", [this] {
    clearAbLoop();
    if (!stopped)
      seekTo(0);
    else if (currentItem)
      playIndex(playlist->row(currentItem), 0, false, false, true);
  });
  add("speedUp", uiTr("Увеличить скорость"), "Ctrl+Up",
      [this] { changeSpeed(1); });
  add("speedDown", uiTr("Уменьшить скорость"), "Ctrl+Down",
      [this] { changeSpeed(-1); });
  add("speedReset", uiTr("Обычная скорость — 1×"), "Ctrl+R",
      [this] { setSpeed(1); });
  add("abA", uiTr("Установить точку A"), "[", [this] { setAbPoint(true); });
  add("abB", uiTr("Установить точку B и повторять"), "]",
      [this] { setAbPoint(false); });
  add("abClear", uiTr("Отменить повтор A–B"), "Backspace",
      [this] { clearAbLoop(); });
  add("abDialog", uiTr("Задать отрезок и число проходов…"), "Ctrl+Shift+B",
      [this] { abLoopDialog(); });
  add("volumeUp", uiTr("Увеличить громкость"), "Up",
      [this] { volume->setValue(volume->value() + FoxicMPFamily::VolumeStep); });
  add("volumeDown", uiTr("Уменьшить громкость"), "Down",
      [this] { volume->setValue(volume->value() - FoxicMPFamily::VolumeStep); });
  add("fullscreen", uiTr("Полный экран"), "F11",
      [this] { toggleFullscreen(); });
  actions["fullscreen"]->setCheckable(true);
  add("escape", uiTr("Выйти из полного экрана"), "Esc", [this] {
    if (isFullScreen())
      toggleFullscreen();
  });
  add("frameNext", uiTr("Следующий кадр"), "Ctrl+Right", [this] {
    pausedByMinimize = false;
    if (!stopped) {
      if (progressive)
        progressiveUserPause = true;
      player->command({"frame-step"});
      QTimer::singleShot(100, this, [this] {
        showOsd(uiTr("Кадр: ") + clockText(player->get("time-pos").toDouble()));
      });
    }
  });
  add("frameBack", uiTr("Предыдущий кадр"), "Ctrl+Left", [this] {
    pausedByMinimize = false;
    if (!stopped) {
      if (progressive)
        progressiveUserPause = true;
      player->command({"frame-back-step"});
    }
  });
  add("goto", uiTr("Перейти к времени…"), "Ctrl+G", [this] { goToTime(); });
  add("size50", "50%", "Alt+1", [this] { resizeVideo(0.5); });
  add("size100", "100%", "Alt+2", [this] { resizeVideo(1); });
  add("size200", "200%", "Alt+3", [this] { resizeVideo(2); });
  add("fit", uiTr("Вписать в окно"), "Alt+4", [this] {
    player->set("video-zoom", 0);
    player->set("panscan", 0);
    captureTransformGeometry();
  });
  add("fill", uiTr("Заполнить окно (обрезать края)"), "", [this] {
    player->set("video-zoom", 0);
    player->set("panscan", 1);
    captureTransformGeometry();
  });
  add("zoomIn", uiTr("Увеличить изображение"), "Ctrl++", [this] {
    player->set("video-zoom",
                std::min(3.0, player->get("video-zoom").toDouble() + 0.1));
    captureTransformGeometry();
  });
  add("zoomOut", uiTr("Уменьшить изображение"), "Ctrl+-", [this] {
    player->set("video-zoom",
                std::max(-3.0, player->get("video-zoom").toDouble() - 0.1));
    captureTransformGeometry();
  });
  add("menu", uiTr("Показывать главное меню"), "Ctrl+0", [this] {
    const bool visible = actions["menu"]->isChecked();
    profile.settings.setValue("ui/showMenu", visible);
    applyPanelVisibility();
  });
  actions["menu"]->setCheckable(true);
  actions["menu"]->setChecked(
      profile.settings.value("ui/showMenu", true).toBool());
  add("playlistAdd", uiTr("Добавить файлы…"), "Ctrl+Shift+O", [this] {
    addFiles(QFileDialog::getOpenFileNames(
        this, uiTr("Добавить в плейлист"),
        profile.settings.value("files/lastDirectory").toString(),
        uiTr("Медиафайлы (*)")));
    dock->show();
  });
  add("playlistRemove", uiTr("Удалить выделенные из списка"), "",
      [this] { removePlaylistSelection(); });
  add("playlistUp", uiTr("Переместить выше"), "",
      [this] { movePlaylistSelection(-1); });
  add("playlistDown", uiTr("Переместить ниже"), "",
      [this] { movePlaylistSelection(1); });
  add("playlistClear", uiTr("Очистить плейлист"), "",
      [this] { queuePanel->clearActive(); });
  add("playlistLoad", uiTr("Открыть плейлист…"), "Ctrl+Shift+L", [this] {
    const auto path = QFileDialog::getOpenFileName(
        this, uiTr("Открыть плейлист"),
        profile.settings.value("files/lastDirectory").toString(),
        uiTr("Плейлисты (*.m3u8 *.m3u *.mpcpl *.pls *.asx *.cue)"));
    if (!path.isEmpty())
      loadPlaylist(path);
  });
  add("playlistSave", uiTr("Сохранить плейлист…"), "Ctrl+Shift+S", [this] {
    QString selectedFilter;
    auto path = QFileDialog::getSaveFileName(
        this, uiTr("Сохранить плейлист"),
        profile.settings.value("files/lastDirectory").toString(),
        "M3U8 (*.m3u8);;MPCPL (*.mpcpl);;PLS (*.pls);;ASX (*.asx)",
        &selectedFilter);
    if (!path.isEmpty()) {
      if (QFileInfo(path).suffix().isEmpty()) {
        if (selectedFilter.startsWith("MPCPL"))
          path += ".mpcpl";
        else if (selectedFilter.startsWith("PLS"))
          path += ".pls";
        else if (selectedFilter.startsWith("ASX"))
          path += ".asx";
        else
          path += ".m3u8";
      }
      savePlaylist(path);
    }
  });
  add("shuffle", uiTr("Случайный порядок"), "", [this] {
    profile.settings.setValue("playlist/shuffle",
                              actions["shuffle"]->isChecked());
    resetShuffle();
  });
  actions["shuffle"]->setCheckable(true);
  actions["shuffle"]->setChecked(
      profile.settings.value("playlist/shuffle", false).toBool());
  for (auto entry :
       {qMakePair(QString("repeatNone"), QString(uiTr("Без повтора"))),
        qMakePair(QString("repeatOne"), QString(uiTr("Текущий файл"))),
        qMakePair(QString("repeatAll"), QString(uiTr("Весь список")))}) {
    add(entry.first, entry.second, "", [this, id = entry.first] {
      filePlays = playlistPlays = 0;
      profile.settings.setValue("playlist/repeat", id);
    });
    actions[entry.first]->setCheckable(true);
    actions[entry.first]->setChecked(
        profile.settings.value("playlist/repeat", "repeatNone").toString() ==
        entry.first);
  }
  add("historyManage", uiTr("История воспроизведения…"), "Ctrl+H",
      [this] { showHistory(); });
  add("favoriteAdd", uiTr("Добавить в избранное…"), "Ctrl+D",
      [this] { addFavoriteDialog(); });
  add("favoriteManage", uiTr("Управление избранным…"), "",
      [this] { manageFavorites(); });
  add("snapshot", uiTr("Сохранить кадр…"), "Ctrl+S",
      [this] { snapshotDialog(); });
  add("snapshotQuick", uiTr("Сохранить кадр без диалога"), "F12",
      [this] { quickSnapshot(); });
  add("snapshotClipboard", uiTr("Кадр в буфер обмена"), "Alt+C",
      [this] { captureImage({}, true); });
  add("snapshotDisplay", uiTr("Сохранить область видео…"), "",
      [this] { displaySnapshotDialog(); });
  add("thumbnailSheet", uiTr("Сохранить лист миниатюр…"), "",
      [this] { contactSheetDialog(); });
  add("captureSettings", uiTr("Настройки снимков и миниатюр…"), "",
      [this] { captureSettingsDialog(); });
  add("mediaProperties", uiTr("Свойства текущего источника…"), "Ctrl+Alt+I",
      [this] { showMediaProperties(); });
  add("saveMediaCopy", uiTr("Сохранить копию медиафайла…"), "",
      [this] { saveMediaCopyDialog(); });
  add("image", uiTr("Изображение и звук…"), "",
      [this] { showVideoSettings(); });
  add("videoOutputSettings", uiTr("Видеовывод и декодирование…"), "Ctrl+Alt+V",
      [this] { videoOutputDialog(); });
  add("renderStatistics", uiTr("Статистика видеовывода…"), "Ctrl+Alt+G",
      [this] { showRenderStatistics(); });
  add("associations", uiPlatformTr("Форматы и ассоциации Linux…", "Форматы и ассоциации Windows…"), "", [this] { showAssociations(profile,this); });
  add("profileManagement", uiTr("Профиль и резервные копии…"), "", [this] {
    auto *preferences = findChild<QDialog *>("preferencesDialog");
    showProfileManagement(profile, preferences && preferences->isVisible() ? static_cast<QWidget *>(preferences) : this, [this] {
      savePosition();
      syncPlaylist();
      if (profile.settings.value("playlist/rememberPanel", true).toBool()) {
        const bool hidden = isFullScreen() && profile.settings.value("playlist/hideFullscreen", true).toBool();
        if (hidden)
          dock->setVisible(restorePlaylist);
        profile.settings.setValue("playlist/dockState", saveState(1));
        profile.settings.setValue("playlist/dockGeometry", dock->saveGeometry());
        if (hidden)
          dock->hide();
      }
      if (profile.settings.value("ui/rememberGeometry", FoxicMPFamily::RememberGeometry).toBool())
        profile.settings.setValue("window/geometry", isFullScreen() && !beforeFullscreenGeometry.isEmpty()
            ? beforeFullscreenGeometry : saveGeometry());
      profile.settings.setValue("audio/volume", volume->value());
    }, [this](bool keep) {
      resetProfileOnExit = true;
      keepResetLibrary = keep;
      exitApplication();
    });
  });
  add("webControl", uiTr("Веб-управление…"), "", [this] { webControlDialog(); });
  add("checkUpdates", uiPlatformTr("Проверить обновления Linux/Qt…", "Проверить обновления Windows/Qt…"), "", [this] { maintenanceDialog(true); });
  add("maintenance", uiTr("Обслуживание и диагностика…"), "", [this] { maintenanceDialog(); });
  add("desktopSettings", uiTr("Запуск и системный трей…"), "", [this] { desktopSettingsDialog(); });
  add("onTop", uiTr("Поверх остальных окон"), "Ctrl+T", [this] {
    profile.settings.setValue("ui/onTop", actions["onTop"]->isChecked());
    applyWindowMode();
  });
  actions["onTop"]->setCheckable(true);
  actions["onTop"]->setChecked(
      profile.settings.value("ui/onTop", false).toBool());
  add("frameless", uiTr("Без рамки"), "Ctrl+B", [this] {
    profile.settings.setValue("ui/frameless",
                              actions["frameless"]->isChecked());
    applyWindowMode();
  });
  actions["frameless"]->setCheckable(true);
  actions["frameless"]->setChecked(
      profile.settings.value("ui/frameless", false).toBool());
  buildWindowActions();
  buildInputActions();
  buildTransformActions();
  buildSubtitleActions();
  applyShortcuts();
}
void Window::applyShortcuts() {
  QMap<QString, QKeySequence> custom;
  for (const auto &key : profile.settings.allKeys())
    if (key.startsWith("keys/")) custom[key.mid(5)] = QKeySequence(profile.settings.value(key).toString(), QKeySequence::PortableText);
  const auto conflictsWithCustom = [&custom](const QString &id, const QKeySequence &sequence) {
    if (sequence.isEmpty()) return false;
    for (auto it = custom.begin(); it != custom.end(); ++it)
      if (it.key() != id && !it.value().isEmpty() &&
          (sequence.matches(it.value()) != QKeySequence::NoMatch || it.value().matches(sequence) != QKeySequence::NoMatch)) return true;
    return false;
  };
  for (auto it = actions.begin(); it != actions.end(); ++it) {
    QString key =
        profile.settings.value("keys/" + it.key(), defaultKeys[it.key()])
            .toString();
    if (!custom.contains(it.key()) && conflictsWithCustom(it.key(), QKeySequence(key, QKeySequence::PortableText))) key.clear();
    it.value()->setShortcut(QKeySequence(key, QKeySequence::PortableText));
    if (it.key() == "fullscreen" &&
        (!custom.contains(it.key()) || custom.value(it.key()) == QKeySequence(defaultKeys.value(it.key()), QKeySequence::PortableText)) &&
        !conflictsWithCustom(it.key(), QKeySequence("F11")))
      it.value()->setShortcuts({QKeySequence(key, QKeySequence::PortableText), QKeySequence("F11")});
    const QString id = it.key();
    it.value()->setAutoRepeat(
        id.startsWith("back") || id.startsWith("forward") ||
        id.startsWith("volume") || id == "frameNext" || id == "frameBack" ||
        id.startsWith("width") || id.startsWith("height") ||
        id.startsWith("scale") || id.startsWith("zoom") ||
        id.startsWith("pan") || id.startsWith("speed") ||
        id.endsWith("DelayUp") || id.endsWith("DelayDown"));
#ifdef Q_OS_WIN
    if (id == "frameNext") {
      // Classic labels the fifth toolbar button "Step | Jump To" and does
      // not append the Ctrl+Right accelerator to that two-action hint.
      it.value()->setToolTip(uiTr("Шаг / Перейти"));
    } else
#endif
      it.value()->setToolTip(it.value()->text() +
                             (key.isEmpty() ? ""
                                            : " (" +
                                                  it.value()->shortcut().toString(
                                                      QKeySequence::NativeText) +
                                                  ")"));
  }
}
void Window::goToTime() {
  if (stopped || duration <= 0)
    return;
  QDialog dialog(this);
  dialog.setObjectName("gotoDialog");
  dialog.setWindowTitle(uiTr("Перейти к времени"));
  auto *layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QLabel(uiTr("Секунды, ММ:СС или ЧЧ:ММ:СС.мс")));
  auto *input = new QLineEdit(clockText(pos));
  input->setObjectName("gotoTime");
  input->selectAll();
  layout->addWidget(input);
  auto *message = new QLabel(uiTr("Длительность: ") + clockText(duration));
  layout->addWidget(message);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    double target;
    if (!parsePlaybackTime(input->text(), &target) || target > duration) {
      message->setText(uiTr("Введите время от 0 до ") + clockText(duration));
      input->setFocus();
      return;
    }
    if (!seekTo(target)) {
      message->setText(uiTr("Выбранное время ещё не загружено"));
      return;
    }
    dialog.accept();
  });
  showFullscreenControls();
  dialog.exec();
}
void Window::resizeVideo(double scale) {
  if (isFullScreen())
    toggleFullscreen();
  const auto video = videoSize();
  if (!video.isValid())
    return;
  showNormal();
  resizeVideoArea(video * scale);
}
void Window::showFullscreenControls() {
  applyPanelVisibility();
  player->unsetCursor();
  if (isFullScreen() && hideControls && hideControls->interval() > 0)
    hideControls->start();
  else if (hideControls)
    hideControls->stop();
}
bool Window::eventFilter(QObject *object, QEvent *event) {
  auto *widget = qobject_cast<QWidget *>(object);
  if (!widget || (widget != this && !isAncestorOf(widget)))
    return false;
  if (auto *button = qobject_cast<QToolButton *>(object);
      button && event->type() == QEvent::MouseButtonPress) {
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() == Qt::RightButton) {
      const QString id = button->objectName().mid(QString("control_").size());
      if (id == "open") {
        refreshLibraryMenus();
        historyMenu->popup(button->mapToGlobal(QPoint(0, button->height())));
        return true;
      }
      if (id == "stop") {
        actions["closeFile"]->trigger();
        return true;
      }
      if (id == "previous" || id == "next") {
        const bool forward = id == "next";
        if (!advancePlaylist(forward) && !advanceDirectory(forward))
          showOsd(forward ? uiTr("Последний файл") : uiTr("Первый файл"));
        return true;
      }
      if (id == "frameNext") {
        chapterMenu->popup(button->mapToGlobal(QPoint(0, button->height())));
        return true;
      }
      if (id == "audioQuick") {
        findChild<QMenu *>("classicAudioOptionsMenu")->popup(button->mapToGlobal(QPoint(0, button->height())));
        return true;
      }
      if (id == "subtitleQuick") {
        findChild<QMenu *>("classicSubtitleOptionsMenu")->popup(button->mapToGlobal(QPoint(0, button->height())));
        return true;
      }
    }
  }
  if (object == time && event->type() == QEvent::MouseButtonPress &&
      static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
    profile.settings.setValue("ui/remainingTime", !profile.settings.value("ui/remainingTime", false).toBool());
    updateClock(); return true;
  }
  if (object == time && event->type() == QEvent::ContextMenu) {
    auto *menu = new QMenu(time); menu->setObjectName("classicClockMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto *milliseconds = menu->addAction(uiTr("Показывать миллисекунды"));
    milliseconds->setObjectName("classicClockMilliseconds"); milliseconds->setCheckable(true);
    milliseconds->setChecked(profile.settings.value("ui/showMilliseconds", false).toBool());
    connect(milliseconds, &QAction::triggered, this, [this](bool checked) {
      profile.settings.setValue("ui/showMilliseconds", checked); updateClock();
    });
    menu->popup(static_cast<QContextMenuEvent *>(event)->globalPos()); return true;
  }
  if (object == player && event->type() == QEvent::Resize && osd)
    positionOverlays();
  return inputEvent(object, event);
}
Window::~Window() {
  cancelProgressiveOpening();
  delete webControl;
  webControl = nullptr;
  qApp->removeEventFilter(this);
  cancelMouseHold();
  delete mediaRemote;
  mediaRemote = nullptr;
}

bool Window::openStdin(int descriptor, const QJsonObject &options) {
  emit playbackIntentChanged();
  closePlaybackFile();
  QString error;
  const auto source = player->attachStdin(descriptor, &error);
  if (source.isEmpty()) { state->setText(error); return false; }
  stdinPlayback = true;
  pendingLaunchOptions = options;
  launchOptionsGeneration = playbackGeneration;
  expectedPlaybackSource = source;
  pendingTrackSelections.clear();
  watchedSubtitle.clear();
  audioUserSelection = subtitleUserSelection = false;
  holdAfterLoad = options.value("pause").toBool();
  lastError.clear();
  pos = duration = 0;
  timeline->position = timeline->duration = 0;
  timeline->chapters.clear();
  timeline->setEnabled(false);
  actions["frameBack"]->setEnabled(false);
  state->setText(uiTr("Ожидание медиапотока stdin…"));
  if (options.contains("volume")) webCommand("volume", options.value("volume").toDouble());
  if (options.contains("after")) onceAfterPlayback = options.value("after").toString();
  applyLaunchWindowOptions(options);
  return player->loadSource(source, {{"pause", holdAfterLoad ? "yes" : "no"},
      {"sub-auto", "no"}, {"audio-file-auto", "no"}, {"cache", "no"},
      {"demuxer-readahead-secs", "1"}, {"demuxer-max-bytes", "16MiB"},
      {"demuxer-max-back-bytes", "0"}, {"force-seekable", "no"}});
}
void Window::addFiles(const QStringList &files, QListWidget *target) {
  QStringList sources;
  QString error;
  for (const QString &file : files) {
    if (QFileInfo(file).isDir()) {
      sources.append(MediaSource::filesInDirectory(file));
      continue;
    }
    const QString source = MediaSource::normalize(file, &error);
    if (source.isEmpty()) {
      state->setText(uiTr("Источник: ") + error);
      state->setToolTip(error);
      continue;
    }
    sources.append(source);
  }
  if (!target) target = queuePanel->activeList();
  queuePanel->append(target, sources);
  if (target == playlist && !progressive && !currentItem && stopped &&
      playlist->count()) {
    currentItem = playlist->item(0);
    currentId = currentItem->data(Qt::UserRole + 1).toString();
    current = currentItem->data(Qt::UserRole).toString();
    playlist->setCurrentItem(currentItem);
  }
  if (!sources.isEmpty() && !MediaSource::isUrl(sources.first()))
    profile.settings.setValue("files/lastDirectory",
                              QFileInfo(sources.first()).absolutePath());
  syncPlaylist();
}
void Window::openFiles(const QStringList &files, bool usePlayingList) {
  if (files.isEmpty())
    return;
  if (files.size() == 1 && PlaylistFile::isPlaylist(files.first())) {
    loadPlaylist(files.first());
    return;
  }
  QStringList sources;
  QString error;
  for (const QString &file : files) {
    if (PlaylistFile::isPlaylist(file)) {
      state->setText(uiTr(
          "Откройте один плейлист или добавьте несколько в панель плейлиста"));
      return;
    }
    if (QFileInfo(file).isDir()) {
      const QStringList expanded = MediaSource::filesInDirectory(file);
      if (expanded.isEmpty()) {
        state->setText(uiTr("В папке нет поддерживаемых медиафайлов"));
        state->setToolTip(QDir::cleanPath(file));
        return;
      }
      sources.append(expanded);
    } else {
      const QString source = MediaSource::normalize(file, &error);
      if (source.isEmpty()) {
        state->setText(uiTr("Источник: ") + error);
        state->setToolTip(error);
        return;
      }
      if (DeviceSource::isDevice(source)) {
        const auto plan = DeviceSource::resolve(
            source, player->get("protocol-list").toStringList(),
            player->get("demuxer-lavf-list").toStringList(), profile.directory);
        if (!plan.error.isEmpty()) {
          state->setText(uiTr("Устройство: ") + plan.error);
          state->setToolTip(plan.error);
          return;
        }
      }
      sources.append(source);
    }
  }
  if (sources.isEmpty())
    return;
  selectPlaybackList(usePlayingList && playlist ? playlist : queuePanel->activeList());
  savePosition();
  stopped = true;
  currentItem = nullptr;
  currentId.clear();
  currentIndex = -1;
  playlist->clear();
  addFiles(sources, usePlayingList ? playlist : nullptr);
  resetShuffle();
  playIndex(0);
}
bool Window::openDirectory(const QString &directory) {
  const QStringList files = MediaSource::filesInDirectory(directory);
  if (files.isEmpty()) {
    state->setText(uiTr("В папке нет поддерживаемых медиафайлов"));
    state->setToolTip(QDir::cleanPath(directory));
    return false;
  }
  openFiles(files);
  return true;
}
bool Window::loadExternalAudio(const QString &file, bool select) {
  if (stopped || (current.isEmpty() && !stdinPlayback)) {
    state->setText(uiTr("Сначала откройте видео"));
    return false;
  }
  QString error;
  const QString source = MediaSource::normalize(file, &error);
  if (source.isEmpty() || MediaSource::isUrl(source)) {
    state->setText(
        uiTr("Аудиодорожка: ") +
        (error.isEmpty() ? uiTr("требуется локальный файл") : error));
    return false;
  }
  const bool queued = player->addExternalTrack("audio", source, select);
  if (queued) {
    if (select) {
      audioUserSelection = true;
      pendingTrackSelections.remove("aid");
    }
    if (currentItem &&
        profile.settings.value("playlist/rememberTracks", true).toBool()) {
      auto data = currentItem->data(PlaylistPanel::Metadata).toMap();
      auto files = data.value("audioFiles").toStringList();
      if (!files.contains(source))
        files.append(source);
      data["audioFiles"] = files;
      if (select) {
        data["selectedAudioFile"] = source;
        data.remove("aid");
      }
      currentItem->setData(PlaylistPanel::Metadata, data);
      syncPlaylist();
    }
    profile.settings.setValue("files/lastDirectory",
                              QFileInfo(source).absolutePath());
    showOsd(uiTr("Внешняя аудиодорожка добавлена"));
  }
  return queued;
}
void Window::reopenCurrent() {
  if (stdinPlayback) {
    showOsd(uiTr("Поток stdin одноразовый. Запустите передающую программу снова."));
    return;
  }
  if (current.isEmpty()) {
    state->setText(uiTr("Нет открытого файла"));
    return;
  }
  const int row = currentItem ? playlist->row(currentItem) : currentIndex;
  if (row < 0)
    return;
  pendingResume = pos;
  playIndex(row, pendingResume);
}
void Window::closeCurrent() {
  emit playbackIntentChanged();
  if (current.isEmpty() && !progressive && !stdinPlayback)
    return;
  savePosition();
  closePlaybackFile();
}
void Window::openClipboard() {
  const QMimeData *mime = QApplication::clipboard()->mimeData();
  QStringList input;
  if (mime->hasUrls()) {
    for (const QUrl &url : mime->urls())
      input.append(url.isLocalFile() ? url.toLocalFile()
                                     : url.toString(QUrl::FullyEncoded));
  } else {
    for (const QString &line : mime->text().split(
             QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts))
      input.append(line.trimmed());
  }
  QStringList sources;
  QString error;
  for (const QString &value : input) {
    if (QFileInfo(value).isDir())
      sources.append(MediaSource::filesInDirectory(value));
    else {
      const QString source = MediaSource::normalize(value, &error);
      if (source.isEmpty() ||
          (!MediaSource::isUrl(source) && !QFileInfo::exists(source))) {
        state->setText(uiTr("Буфер обмена: ") + error);
        if (error.isEmpty())
          state->setText(
              uiTr("Буфер обмена не содержит существующего файла или URL"));
        state->setToolTip(error);
        return;
      }
      sources.append(source);
    }
  }
  if (sources.isEmpty()) {
    state->setText(uiTr("Буфер обмена не содержит медиаисточников"));
    return;
  }
  openFiles(sources);
}
bool Window::loadOnlinePlaylist(const QString &url) {
  if (onlineProcess) {
    state->setText(uiTr("Онлайн-плейлист уже загружается"));
    return false;
  }
  QString error;
  const QString source = MediaSource::normalize(url, &error);
  if (source.isEmpty() || !MediaSource::isNetwork(source) ||
      !source.startsWith("http", Qt::CaseInsensitive)) {
    state->setText(uiTr("Онлайн-плейлист: неверный HTTP(S)-адрес"));
    return false;
  }
  const QString executable = OnlineOptions::resolveYtDlp(profile.settings);
  if (executable.isEmpty()) {
    state->setText(uiTr("Онлайн-плейлист: yt-dlp не найден"));
    state->setToolTip(
        uiTr("Укажите исполняемый файл yt-dlp в настройках онлайн-видео"));
    return false;
  }
  onlineOutput.clear();
  onlineProcess = new QProcess(this);
  onlineProcess->setProgram(executable);
  onlineProcess->setArguments(
      OnlineOptions::playlistArguments(profile.settings, source));
  onlineProgress = new QProgressDialog(uiTr("Чтение онлайн-плейлиста…"),
                                       uiTr("Отмена"), 0, 0, this);
  onlineProgress->setObjectName("onlinePlaylistProgress");
  onlineProgress->setWindowModality(Qt::WindowModal);
  onlineProgress->setMinimumDuration(0);
  connect(onlineProgress, &QProgressDialog::canceled, onlineProcess, [this] {
    if (onlineProcess) {
      onlineProcess->setProperty("cancelled", true);
      onlineProcess->kill();
    }
  });
  connect(onlineProcess, &QProcess::readyReadStandardOutput, this, [this] {
    onlineOutput += onlineProcess->readAllStandardOutput();
    if (onlineOutput.size() > 8 * 1024 * 1024) {
      state->setText(uiTr("Онлайн-плейлист слишком большой"));
      onlineProcess->kill();
    }
  });
  const auto finish = [this](int exitCode, QProcess::ExitStatus exitStatus) {
    if (!onlineProcess)
      return;
    QProcess *finished = onlineProcess;
    onlineOutput += finished->readAllStandardOutput();
    QString message;
    QList<OnlineEntry> entries;
    bool success = exitStatus == QProcess::NormalExit && exitCode == 0 &&
                   !finished->property("cancelled").toBool() &&
                   onlineOutput.size() <= 8 * 1024 * 1024;
    if (success)
      success = OnlineOptions::parsePlaylist(onlineOutput, &entries, &message);
    if (!success && message.isEmpty()) {
      message = QString::fromUtf8(finished->readAllStandardError())
                    .trimmed()
                    .left(500);
      if (message.isEmpty())
        message = finished->property("cancelled").toBool()
                      ? uiTr("Загрузка отменена")
                      : finished->errorString();
    }
    if (onlineProgress) {
      onlineProgress->close();
      onlineProgress->deleteLater();
      onlineProgress = nullptr;
    }
    onlineProcess = nullptr;
    finished->deleteLater();
    if (success) {
      QStringList urls;
      for (const auto &entry : entries)
        urls.append(entry.url);
      openFiles(urls);
      for (int i = 0; i < entries.size() && i < playlist->count(); ++i)
        if (!entries[i].title.isEmpty())
          playlist->item(i)->setText(entries[i].title);
      dock->show();
      message = QString(uiTr("Добавлено записей: %1")).arg(entries.size());
      state->setText(message);
    } else {
      state->setText(uiTr("Онлайн-плейлист: ") + message);
      state->setToolTip(message);
    }
    emit onlinePlaylistFinished(success, message);
  };
  connect(onlineProcess, &QProcess::finished, this, finish);
  connect(onlineProcess, &QProcess::errorOccurred, this,
          [finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
              finish(-1, QProcess::CrashExit);
          });
  auto *deadline = new QTimer(onlineProcess);
  deadline->setSingleShot(true);
  connect(deadline, &QTimer::timeout, onlineProcess, [this] {
    if (onlineProcess) {
      onlineProcess->setProperty("cancelled", true);
      onlineProcess->kill();
    }
  });
  deadline->start(120000);
  state->setText(uiTr("Чтение онлайн-плейлиста…"));
  onlineProcess->start();
  return true;
}
bool Window::loadPlaylist(const QString &path) {
  QList<PlaylistEntry> entries;
  QString error;
  if (!PlaylistFile::readEntries(
          path, &entries, &error,
          profile.settings.value("playlist/skipInvalidSources", false)
              .toBool())) {
    state->setText(uiTr("Плейлист: ") + error);
    state->setToolTip(error);
    return false;
  }
  selectPlaybackList(queuePanel->activeList());
  savePosition();
  stopped = true;
  currentItem = nullptr;
  currentId.clear();
  currentIndex = -1;
  changingPlaylist = true;
  playlist->clear();
  QStringList sources;
  for (const auto &entry : entries)
    sources.append(entry.source);
  queuePanel->append(playlist, sources, false);
  for (int i = 0; i < entries.size(); ++i) {
    auto *item = playlist->item(i);
    if (!entries[i].title.isEmpty())
      item->setText(entries[i].title);
    item->setData(PlaylistPanel::Metadata, entries[i].metadata);
    if (entries[i].duration >= 0)
      item->setData(PlaylistPanel::Duration, entries[i].duration);
  }
  changingPlaylist = false;
  resetShuffle();
  playIndex(0);
  queuePanel->updateSummary();
  dock->show();
  return true;
}
bool Window::savePlaylist(const QString &path) {
  QList<PlaylistEntry> entries;
  auto *list = queuePanel->activeList();
  for (int i = 0; i < list->count(); ++i) {
    auto *item = list->item(i);
    entries.append({item->data(PlaylistPanel::Source).toString(), item->text(),
                    item->data(PlaylistPanel::Duration).toDouble(),
                    item->data(PlaylistPanel::Metadata).toMap()});
  }
  QString error;
  if (!PlaylistFile::writeEntries(path, entries, &error)) {
    state->setText(uiTr("Плейлист: ") + error);
    state->setToolTip(error);
    return false;
  }
  return true;
}
void Window::selectPlaybackList(QListWidget *list) {
  if (playlist == list)
    return;
  savePosition();
  clearAbLoop();
  completionArmed = false;
  releaseProgressive();
  player->command({"stop"});
  stopped = true;
  currentItem = nullptr;
  currentId.clear();
  current.clear();
  currentIndex = -1;
  playlist = list;
  resetShuffle();
}
void Window::syncPlaylist() {
  if (changingPlaylist || !playlist)
    return;
  currentItem = nullptr;
  for (int i = 0; i < playlist->count(); ++i)
    if (playlist->item(i)->data(Qt::UserRole + 1).toString() == currentId)
      currentItem = playlist->item(i);
  currentIndex = currentItem ? playlist->row(currentItem) : -1;
  QStringList files;
  for (int i = 0; i < playlist->count(); ++i)
    files.append(playlist->item(i)->data(Qt::UserRole).toString());
  for (auto *list : queuePanel->lists())
    for (int i = 0; i < list->count(); ++i) {
      auto *item = list->item(i);
      QFont font = item->font();
      if (font.bold() != (item == currentItem)) {
        font.setBold(item == currentItem);
        item->setFont(font);
      }
    }
  profile.settings.setValue("files/playlist", files);
  profile.settings.setValue("files/currentIndex", currentIndex);
  queuePanel->save(playlist, currentId);
}
void Window::resetShuffle() {
  shuffled.clear();
  shuffleTrail.clear();
  shuffleCursor = -1;
  if (currentItem) {
    const auto id = currentItem->data(Qt::UserRole + 1).toString();
    shuffled.insert(id);
    shuffleTrail.append(id);
    shuffleCursor = 0;
  }
}
void Window::playIndex(int index, double start, bool resetOrder,
                       bool preservePasses, bool hold) {
  cancelMouseHold();
  if (index < 0 || index >= playlist->count())
    return;
  emit playbackIntentChanged();
  ++playbackGeneration;
  pendingLaunchOptions = openingLaunchOptions;
  openingLaunchOptions = {};
  launchOptionsGeneration = playbackGeneration;
  if (!preservePasses && !advancingAfterError)
    failedEntries.clear();
  savePosition();
  clearAbLoop();
  pausedByMinimize = false;
  holdAfterLoad = hold || pendingLaunchOptions.value("pause").toBool();
  repeatSameSource =
      preservePasses &&
      currentId == playlist->item(index)->data(Qt::UserRole + 1).toString();
  if (!preservePasses)
    filePlays = playlistPlays = 0;
  else if (currentId !=
           playlist->item(index)->data(Qt::UserRole + 1).toString())
    filePlays = 0;
  stopped = true;
  stdinPlayback = false;
  releaseProgressive();
  currentIndex = index;
  currentItem = playlist->item(index);
  currentId = currentItem->data(Qt::UserRole + 1).toString();
  current = currentItem->data(Qt::UserRole).toString();
  titleMetadata.clear();
  updateWindowTitle();
  audioUserSelection = false;
  subtitleUserSelection = false;
  watchedSubtitle.clear();
  QString sourceError;
  QMap<QString, QString> deviceOptions;
  QString playbackSource;
  if (DeviceSource::isDevice(current)) {
    const auto plan = DeviceSource::resolve(
        current, player->get("protocol-list").toStringList(),
        player->get("demuxer-lavf-list").toStringList(), profile.directory);
    playbackSource = plan.url;
    sourceError = plan.error;
    deviceOptions = plan.options;
  } else
    playbackSource =
        OnlineOptions::playbackUrl(profile.settings, current, &sourceError);
  if (playbackSource.isEmpty()) {
    stopped = true;
    player->command({"stop"});
    completionArmed = false;
    state->setText(uiTr("Источник: ") + sourceError);
    state->setToolTip(sourceError);
    if (resetOrder)
      resetShuffle();
    handlePlaylistFailure();
    return;
  }
  playlist->setCurrentItem(currentItem);
  const auto entryOptions = currentItem->data(PlaylistPanel::Metadata).toMap();
  expectedPlaybackSource = playbackSource;
  pendingTrackSelections.clear();
  for (const auto &key : {"aid", "sid"}) {
    deviceOptions[key] = "auto";
    if (profile.settings.value("playlist/rememberTracks", true).toBool() &&
        entryOptions.contains(key) &&
        entryOptions
            .value(QString(key) == "aid" ? "selectedAudioFile"
                                         : "selectedSubtitleFile")
            .toString()
            .isEmpty()) {
      deviceOptions[key] = entryOptions.value(key).toString();
      pendingTrackSelections[key] = entryOptions.value(key);
    }
  }
  pendingResume = pendingLaunchOptions.contains("start")
      ? pendingLaunchOptions.value("start").toDouble() : start;
  if (pendingLaunchOptions.contains("start"))
    deviceOptions["start"] = QString::number(pendingResume, 'f', 6);
  if (pendingLaunchOptions.contains("audio-delay"))
    deviceOptions["audio-delay"] = QString::number(pendingLaunchOptions.value("audio-delay").toDouble(), 'f', 6);
  if (holdAfterLoad) deviceOptions["pause"] = "yes";
  if (resetOrder)
    resetShuffle();
  pos = duration = 0;
  hoverStatus.clear();
  lastError.clear();
  state->setToolTip({});
  state->setText(uiTr("Открытие…"));
  applyAudioSourceOptions();
  if (deviceOptions.isEmpty())
    player->command({"loadfile", playbackSource, "replace"});
  else
    player->loadSource(playbackSource, deviceOptions);
  syncPlaylist();
}
bool Window::advancePlaylist(bool forward, bool natural) {
  currentIndex = currentItem ? playlist->row(currentItem) : -1;
  const int count = playlist->count();
  if (count == 0)
    return false;
  const QString mode =
      profile.settings.value("playlist/repeat", "repeatNone").toString();
  const int limit = std::clamp(
      profile.settings.value("playlist/repeatCount", 0).toInt(), 0, 9999);
  const bool repeatPass = !natural || !limit || playlistPlays < limit;
  if (natural && mode == "repeatOne" && currentIndex >= 0 &&
      (!limit || filePlays < limit)) {
    playIndex(currentIndex, 0, false, true);
    return true;
  }
  int next = -1;
  int eligibleCount = 0;
  for (int i = 0; i < count; ++i)
    if (!playlistItemSkipped(i))
      ++eligibleCount;
  if (profile.settings.value("playlist/shuffle", false).toBool()) {
    auto rowOf = [&](const QString &id) {
      for (int i = 0; i < count; ++i)
        if (!playlistItemSkipped(i) &&
            playlist->item(i)->data(Qt::UserRole + 1).toString() == id)
          return i;
      return -1;
    };
    int cursor = shuffleCursor + (forward ? 1 : -1);
    while (cursor >= 0 && cursor < shuffleTrail.size()) {
      next = rowOf(shuffleTrail[cursor]);
      if (next >= 0) {
        shuffleCursor = cursor;
        break;
      }
      cursor += forward ? 1 : -1;
    }
    if (next < 0) {
      if (!forward)
        return false;
      QList<int> candidates;
      for (int i = 0; i < count; ++i)
        if (!playlistItemSkipped(i) &&
            !shuffled.contains(
                playlist->item(i)->data(Qt::UserRole + 1).toString()))
          candidates.append(i);
      if (candidates.isEmpty() && mode == "repeatAll" && repeatPass) {
        shuffled.clear();
        for (int i = 0; i < count; ++i)
          if (!playlistItemSkipped(i) &&
              (i != currentIndex || eligibleCount == 1))
            candidates.append(i);
      }
      if (candidates.isEmpty())
        return false;
      next = candidates[QRandomGenerator::global()->bounded(candidates.size())];
      const auto id = playlist->item(next)->data(Qt::UserRole + 1).toString();
      shuffled.insert(id);
      while (shuffleTrail.size() > shuffleCursor + 1)
        shuffleTrail.removeLast();
      shuffleTrail.append(id);
      shuffleCursor = shuffleTrail.size() - 1;
      if (shuffleTrail.size() > 1000) {
        shuffleTrail.removeFirst();
        --shuffleCursor;
      }
    }
  } else {
    for (int step = 1; step <= count; ++step) {
      int row = currentIndex + (forward ? step : -step);
      if (mode == "repeatAll" && repeatPass)
        row = (row % count + count) % count;
      else if (row < 0 || row >= count)
        break;
      if (!playlistItemSkipped(row)) {
        next = row;
        break;
      }
    }
  }
  if (next < 0 || next >= count)
    return false;
  playIndex(next, natural ? 0 : -1, false, natural);
  return true;
}
bool Window::advanceDirectory(bool forward) {
  const QFileInfo currentFile(current);
  if (!currentFile.isFile() || !playlist)
    return false;
  const QStringList files =
      MediaSource::filesInDirectory(currentFile.absolutePath());
  const int index = files.indexOf(currentFile.absoluteFilePath());
  if (index < 0 || files.size() < 2)
    return false;
  const int row = index + (forward ? 1 : -1);
  if (row < 0 || row >= files.size())
    return false;
  for (int i = 0; i < playlist->count(); ++i) {
    if (playlist->item(i)->data(PlaylistPanel::Source).toString() == files[row]) {
      playIndex(i, 0);
      return true;
    }
  }
  queuePanel->append(playlist, {files[row]});
  playIndex(playlist->count() - 1, 0);
  return true;
}
void Window::navigateSkip(int direction) {
  if (stopped || stdinPlayback || DeviceSource::isLive(current))
    return;
  // Match Classic: toolbar buttons first move between chapters, then files.
  if (!timeline->chapters.isEmpty() && changeChapter(direction, 3.))
    return;
  if (advancePlaylist(direction > 0) || advanceDirectory(direction > 0))
    return;
  showOsd(direction > 0 ? uiTr("Последний файл") : uiTr("Первый файл"));
}
void Window::removePlaylistSelection() { queuePanel->removeSelected(); }
void Window::movePlaylistSelection(int delta) {
  queuePanel->moveSelected(delta);
}
void Window::savePosition() {
  if (progressive || stdinPlayback)
    return;
  if (!stopped) {
    // A seek may finish before its queued property notification reaches Qt.
    const auto time = player->get("time-pos");
    profile.savePosition(current, time.isValid() ? time.toDouble() : pos,
                         duration);
  }
}
void Window::refreshTracks() {
  restorePlaylistTracks();
  enforceAudioPolicy();
  enforceSubtitlePolicy();
  applySubtitleStyle();
  audioMenu->clear();
  subtitleMenu->clear();
  subtitleMenu->addAction(uiTr("Выключены"), this,
                          [this] { rememberTrack("sid", "no"); });
  for (const auto &v : player->get("track-list").toList()) {
    auto t = v.toMap();
    QString type = t.value("type").toString();
    if (type != "audio" && type != "sub")
      continue;
    int id = t.value("id").toInt();
    if(progressive && type=="audio"){
      const auto path=t.value("external-filename").toString();
      const auto media=progressive->playbackDetails().value("mediaTracks").toArray();
      for(const auto& value:media){const auto track=value.toObject();const int index=track.value("index").toInt();
        if(index>0 && (path.endsWith(index==1?"/audio":"/track/"+QString::number(index)))){t["title"]=track.value("label").toString();t["lang"]=track.value("language").toString();break;}
      }
    }
    QString label =
        QString("%1: %2 %3 %4")
            .arg(id)
            .arg(t.value("lang").toString(), t.value("title").toString(),
                 t.value("codec").toString())
            .trimmed();
    auto *a = (type == "audio" ? audioMenu : subtitleMenu)
                  ->addAction(label, this, [this, type, id] {
                    rememberTrack(type == "audio" ? "aid" : "sid",
                                  QString::number(id));
                  });
    a->setData(id);
    a->setEnabled(subtitleTrackAllowed(t));
    a->setCheckable(true);
    a->setChecked(t.value("selected").toBool());
  }
  if (audioMenu->isEmpty()) {
    auto *empty = audioMenu->addAction(uiTr("Аудиодорожек нет"));
    empty->setEnabled(false);
  }
}
void Window::refreshChapters() {
  chapterMenu->clear();
  for (int i = 0; i < timeline->chapters.size(); ++i) {
    auto t = timeline->chapters[i].toMap();
    chapterMenu->addAction(
        QString("%1  %2").arg(clockText(t.value("time").toDouble()),
                              t.value("title").toString()),
        this, [this, i] {
          seekTo(timeline->chapters.value(i).toMap().value("time").toDouble());
        });
  }
  if (chapterMenu->isEmpty()) chapterMenu->addAction(uiTr("Глав нет"))->setEnabled(false);
}
void Window::setIntegration(bool enabled) {
  QString error;
  bool ok = enabled ? Integration::installForProfile(
                          QCoreApplication::applicationFilePath(), profile.directory, &error)
                    : Integration::remove(&error);
  if (!ok)
    QMessageBox::warning(this, uiTr("Интеграция"), error);
  else {
    profile.settings.setValue("integration/enabled", enabled);
    Integration::refreshDatabase(this);
  }
}
void Window::integrationPrompt() {
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
  if (!profile.settings.value("integration/prompted", false).toBool()) {
    profile.settings.setValue("integration/prompted", true);
    if (QMessageBox::question(
            this, "FoxiqMP qt",
            uiPlatformTr("Добавить FoxiqMP qt в «Открыть с помощью» и контекстное меню Nemo для видеофайлов?", "Добавить FoxiqMP qt в «Открыть с помощью» и контекстное меню Windows для видеофайлов?")) ==
        QMessageBox::Yes) {
      setIntegration(true);
      profile.settings.setValue("integration/checkOnStartup", true);
    }
  } else if (profile.settings.value("integration/enabled", false).toBool() &&
             profile.settings.value("integration/checkOnStartup", false)
                 .toBool())
    setIntegration(true);
#endif
}
void Window::preferences() {
  if (auto *existing = findChild<QDialog *>("preferencesDialog")) {
    if (existing->isVisible()) { existing->raise(); existing->activateWindow(); }
    return;
  }
  const auto requestedPage = property("foxicmpPreferencePage").toString();
  setProperty("foxicmpPreferencePage", {});
  QDialog dialog(this);
  dialog.setWindowTitle(uiTr("Настройки"));
  dialog.setObjectName("preferencesDialog");
  Appearance::stylePreferences(dialog);
  dialog.resize(900, 720);
  auto *outer = new QVBoxLayout(&dialog);
  ClassicPreferences::beginLoading(&dialog);
  if (!ClassicPreferences::loadingStep(&dialog)) return;
  auto *tabs = new QTabWidget;
  outer->addWidget(tabs); tabs->hide();
  auto *general = new QWidget;
  auto *l = new QVBoxLayout(general);
  tabs->addTab(general, uiTr("Общие"));
  auto check = [&](const QString &text, const char *key, bool def) {
    auto *c = new QCheckBox(text);
    c->setObjectName(key);
    c->setChecked(profile.settings.value(key, def).toBool());
    l->addWidget(c);
    return c;
  };
  auto *history = check(uiTr("Запоминать историю открытых файлов"),
                        "history/enabled", true);
  auto *remember =
      check(uiTr("Запоминать позицию"), "playback/rememberPosition", FoxicMPFamily::RememberPosition);
  auto *hw = check(uiTr("Аппаратное декодирование (при доступности)"),
                   "playback/hardwareDecoding", true);
  hw->setObjectName("playback/hardwareDecoding");
  auto *integration = check(uiPlatformTr("Пункт меню для видеофайлов (Nemo)", "Пункт меню для видеофайлов (Windows)"),
                            "integration/enabled", false);
  auto *verify = check(uiTr("Проверять путь в меню при запуске"),
                       "integration/checkOnStartup", false);
  auto *formats = new QPushButton(uiPlatformTr("Форматы и ассоциации Linux…", "Форматы и ассоциации Windows…"));
  formats->setObjectName("openAssociations");
  l->addWidget(formats);
  connect(formats, &QPushButton::clicked, &dialog, [this,integration] {
    showAssociations(profile,this);
    integration->setChecked(profile.settings.value("integration/enabled",false).toBool());
  });
  auto *form = new QFormLayout;
  auto *audioLanguages =
      new QLineEdit(profile.settings.value("audio/languages").toString());
  audioLanguages->setObjectName("audio/languages");
  auto *subtitleLanguages =
      new QLineEdit(profile.settings.value("subtitles/languages").toString());
  subtitleLanguages->setObjectName("subtitles/languages");
  audioLanguages->setPlaceholderText("ru,en");
  subtitleLanguages->setPlaceholderText("ru,en");
  auto *output = new QComboBox;
  output->setObjectName("video/output");
  output->addItem(uiTr("Программный"), "software");
  output->addItem(uiTr("OpenGL (экспериментальный)"), "opengl");
  output->setCurrentIndex(
      profile.settings.value("video/output", "software").toString() == "opengl"
          ? 1
          : 0);
  output->setToolTip(
      uiTr("Изменение применяется при следующем запуске плеера"));
  form->addRow(uiTr("Вывод изображения:"), output);
  form->addRow(uiTr("Языки звука:"), audioLanguages);
  form->addRow(uiTr("Языки субтитров:"), subtitleLanguages);
  l->addLayout(form);
  auto *profileLocation = new QLabel(QDir::toNativeSeparators(profile.settings.fileName()));
  profileLocation->setObjectName("profileLocation");
  profileLocation->setWordWrap(true);
  l->addWidget(profileLocation);
  l->addStretch();
  if (!ClassicPreferences::loadingStep(&dialog)) return;
  auto *onlinePage = new QWidget;
  auto *onlineLayout = new QVBoxLayout(onlinePage);
  auto *onlineEnabled =
      new QCheckBox(uiTr("Включить yt-dlp для адресов веб-сайтов"));
  onlineEnabled->setObjectName("online/enabled");
  onlineEnabled->setChecked(
      profile.settings.value("online/enabled", true).toBool());
  onlineLayout->addWidget(onlineEnabled);
  auto *onlineForm = new QFormLayout;
  auto *ytdlpRow = new QWidget;
  auto *ytdlpLayout = new QHBoxLayout(ytdlpRow);
  ytdlpLayout->setContentsMargins(0, 0, 0, 0);
  auto *ytdlpPath =
      new QLineEdit(profile.settings.value("online/ytdlpPath").toString());
  ytdlpPath->setObjectName("online/ytdlpPath");
  ytdlpPath->setPlaceholderText(uiTr("Автоматически из PATH"));
  auto *browseYtdlp = new QPushButton(uiTr("Обзор…"));
  ytdlpLayout->addWidget(ytdlpPath, 1);
  ytdlpLayout->addWidget(browseYtdlp);
  connect(browseYtdlp, &QPushButton::clicked, &dialog, [&] {
    const QString path = QFileDialog::getOpenFileName(
        &dialog, uiTr("Выберите yt-dlp"),
        QFileInfo(ytdlpPath->text()).absolutePath());
    if (!path.isEmpty())
      ytdlpPath->setText(path);
  });
  onlineForm->addRow(uiTr("Исполняемый файл yt-dlp:"), ytdlpRow);
  auto combo = [](const QList<QPair<QString, QVariant>> &values,
                  const QVariant &selected, const QString &name) {
    auto *box = new QComboBox;
    box->setObjectName(name);
    for (const auto &value : values)
      box->addItem(value.first, value.second);
    int index = box->findData(selected);
    box->setCurrentIndex(index < 0 ? 0 : index);
    return box;
  };
  auto *height = combo({{uiTr("Без ограничения"), 0},
                        {"720p", 720},
                        {"1080p", 1080},
                        {"1440p", 1440},
                        {"2160p (4K)", 2160},
                        {"4320p (8K)", 4320}},
                       profile.settings.value("online/maxHeight", 1080),
                       "online/maxHeight");
  auto *fps =
      combo({{uiTr("Любая"), 0},
             {uiTr("До 30"), 30},
             {uiTr("До 50"), 50},
             {uiTr("До 60"), 60}},
            profile.settings.value("online/maxFps", 60), "online/maxFps");
  auto *videoCodec = combo({{uiTr("Любой"), "any"},
                            {"H.264 / AVC", "h264"},
                            {"VP9", "vp9"},
                            {"AV1", "av1"}},
                           profile.settings.value("online/videoCodec", "any"),
                           "online/videoCodec");
  auto *hdr =
      combo({{uiTr("Разрешить"), "allow"},
             {uiTr("Предпочитать"), "prefer"},
             {uiTr("Предпочитать SDR"), "avoid"}},
            profile.settings.value("online/hdr", "allow"), "online/hdr");
  auto *audioCodec = combo(
      {{uiTr("Любой"), "any"}, {"AAC", "aac"}, {"Opus", "opus"}},
      profile.settings.value("online/audioCodec", "any"), "online/audioCodec");
  auto *audioLanguage =
      new QLineEdit(profile.settings.value("online/audioLanguage").toString());
  audioLanguage->setObjectName("online/audioLanguage");
  audioLanguage->setPlaceholderText(uiTr("ru, en или пусто (один язык)"));
  auto *bitrate = new QSpinBox;
  bitrate->setObjectName("online/maxVideoBitrate");
  bitrate->setRange(0, 100000);
  bitrate->setSpecialValueText(uiTr("Без ограничения"));
  bitrate->setSuffix(uiTr(" кбит/с"));
  bitrate->setValue(
      profile.settings.value("online/maxVideoBitrate", 0).toInt());
  auto *networkTimeout = new QSpinBox;
  networkTimeout->setObjectName("online/networkTimeout");
  networkTimeout->setRange(5, 300);
  networkTimeout->setSuffix(uiTr(" с"));
  networkTimeout->setValue(
      profile.settings.value("online/networkTimeout", 30).toInt());
  auto *userAgent = new QLineEdit(
      profile.settings.value("online/userAgent", OnlineOptions::defaultUserAgent())
          .toString());
  userAgent->setObjectName("online/userAgent");
  auto *aceEndpoint =
      new QLineEdit(profile.settings
                        .value("online/aceStreamEndpoint",
                               "http://127.0.0.1:6878/ace/getstream")
                        .toString());
  aceEndpoint->setObjectName("online/aceStreamEndpoint");
  auto *torrEndpoint = new QLineEdit(
      profile.settings
          .value("online/torrServerEndpoint", "http://127.0.0.1:8090/stream")
          .toString());
  torrEndpoint->setObjectName("online/torrServerEndpoint");
  onlineForm->addRow(uiTr("Максимальное разрешение:"), height);
  onlineForm->addRow(uiTr("Частота кадров:"), fps);
  onlineForm->addRow(uiTr("Видеокодек:"), videoCodec);
  onlineForm->addRow("HDR:", hdr);
  onlineForm->addRow(uiTr("Аудиокодек:"), audioCodec);
  onlineForm->addRow(uiTr("Язык аудио:"), audioLanguage);
  onlineForm->addRow(uiTr("Максимальный видеобитрейт:"), bitrate);
  onlineForm->addRow(uiTr("Сетевой тайм-аут:"), networkTimeout);
  onlineForm->addRow("User-Agent:", userAgent);
  onlineForm->addRow("Ace Stream endpoint:", aceEndpoint);
  onlineForm->addRow("TorrServer endpoint:", torrEndpoint);
  onlineLayout->addLayout(onlineForm);
  auto *onlineStatus = new QLabel;
  onlineStatus->setObjectName("online/status");
  onlineStatus->setWordWrap(true);
  const QString detected = OnlineOptions::resolveYtDlp(profile.settings);
  onlineStatus->setText(detected.isEmpty()
                            ? uiTr("yt-dlp не найден. Укажите путь вручную.")
                            : uiTr("Обнаружен: ") + detected);
  onlineLayout->addWidget(onlineStatus);
  onlineLayout->addWidget(new QLabel(
      uiTr("Параметры движка применяются после перезапуска FoxiqMP qt.")));
  onlineLayout->addStretch();
  tabs->addTab(onlinePage, uiTr("Онлайн"));
  if (!ClassicPreferences::loadingStep(&dialog)) return;
  auto *keysPage = new QWidget(tabs);
  auto *keysLayout = new QVBoxLayout(keysPage);
  auto *table = new QTableWidget(actions.size(), 2);
  table->setObjectName("shortcutTable");
  table->setHorizontalHeaderLabels({uiTr("Команда"), uiTr("Клавиши")});
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  table->verticalHeader()->hide();
  auto *keyFilter = new QLineEdit;
  keyFilter->setObjectName("classicKeyFilter"); keyFilter->setPlaceholderText(uiTr("Фильтр:"));
  keysLayout->addWidget(keyFilter);
  keysLayout->addWidget(table);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  connect(keyFilter, &QLineEdit::textChanged, table, [table](const QString &filter) {
    for (int row = 0; row < table->rowCount(); ++row) {
      auto *edit = qobject_cast<QKeySequenceEdit *>(table->cellWidget(row, 1));
      table->setRowHidden(row, !table->item(row, 0)->text().contains(filter, Qt::CaseInsensitive) &&
          !edit->keySequence().toString(QKeySequence::NativeText).contains(filter, Qt::CaseInsensitive));
    }
  });
  QMap<QString, QKeySequenceEdit *> editors;
  int row = 0;
  for (auto it = actions.begin(); it != actions.end(); ++it, ++row) {
    if (row % 20 == 0 && !ClassicPreferences::loadingStep(&dialog)) return;
    auto *label = new QTableWidgetItem(it.value()->property("classicLabel").isValid() ? it.value()->property("classicLabel").toString().remove('&') : it.value()->text());
    label->setFlags(Qt::ItemIsEnabled);
    table->setItem(row, 0, label);
    auto *edit = new QKeySequenceEdit(it.value()->shortcut());
    edit->setObjectName("key_" + it.key());
    edit->setAccessibleName(label->text());
    table->setCellWidget(row, 1, edit);
    editors[it.key()] = edit;
  }
  auto *reset = new QPushButton(uiTr("Вернуть стандартные клавиши"));
  keysLayout->addWidget(reset);
  connect(reset, &QPushButton::clicked, &dialog, [&] {
    for (auto it = editors.begin(); it != editors.end(); ++it)
      it.value()->setKeySequence(
          QKeySequence(defaultKeys[it.key()], QKeySequence::PortableText));
  });
  auto *conflict = new QLabel;
  conflict->setWordWrap(true);
  keysLayout->addWidget(conflict);
  tabs->addTab(keysPage, uiTr("Клавиши"));
  QList<std::function<void()>> mediaApply;
  attachInputPreferences(tabs, &mediaApply);
  if (!ClassicPreferences::loadingStep(&dialog)) return;
  attachMediaPreferences(tabs, &mediaApply);
  if (!ClassicPreferences::loadingStep(&dialog)) return;
  attachPlaybackPreferences(tabs, &mediaApply);
  if (!ClassicPreferences::loadingStep(&dialog)) return;
  attachPlaylistPreferences(tabs, &mediaApply);
  if (!ClassicPreferences::loadingStep(&dialog)) return;
  attachLibraryPreferences(tabs, &mediaApply);
  if (!ClassicPreferences::loadingStep(&dialog)) return;
  attachDesktopPreferences(tabs, &mediaApply);
  if (!ClassicPreferences::loadingStep(&dialog)) return;
  auto *formatsPage = createAssociationsPage(profile, &dialog, true);
  tabs->addTab(formatsPage, uiTr("Форматы"));
  if (!ClassicPreferences::loadingStep(&dialog)) return;
  // These integration actions explicitly apply immediately; keep the general
  // draft in sync so a later Apply cannot undo them.
  connect(formatsPage->findChild<QCheckBox *>("integration/checkOnStartup"), &QCheckBox::toggled,
          verify, &QCheckBox::setChecked);
  for (auto name : {"installNemoMenu", "removeNemoMenu"})
    connect(formatsPage->findChild<QPushButton *>(name), &QPushButton::clicked, integration, [this, integration] {
      integration->setChecked(profile.settings.value("integration/enabled", false).toBool());
    });
  l->removeWidget(integration); integration->hide();
  l->removeWidget(verify); verify->hide();
  l->removeWidget(formats); formats->hide();
  auto *webPage = new QWidget;
  auto *webForm = new QFormLayout(webPage);
  auto *webEnabled = new QCheckBox(uiTr("Включить Web-интерфейс"));
  webEnabled->setObjectName("classicWebEnabled");
  webEnabled->setChecked(profile.settings.value("web/enabled", false).toBool());
  auto *webPort = new QSpinBox; webPort->setRange(1024, 65535);
  webPort->setObjectName("classicWebPort"); webPort->setValue(profile.settings.value("web/port", 13579).toInt());
  webForm->addRow(webEnabled); webForm->addRow(uiTr("Порт:"), webPort);
  auto *webDetails = new QPushButton(uiTr("Настроить доступ и открыть в браузере…"));
  webForm->addRow(webDetails);
  webDetails->setObjectName("classicWebDetails");
  const auto showPreferencesTool = [this, &dialog](const QString &name) -> QDialog * {
    auto *tool = findChild<QDialog *>(name);
    if (tool) {
      tool->setParent(&dialog, tool->windowFlags());
      tool->setWindowModality(Qt::WindowModal); tool->show(); tool->raise();
    }
    return tool;
  };
  connect(webDetails, &QPushButton::clicked, &dialog, [this, &dialog, webEnabled, webPort, showPreferencesTool] {
    webControlDialog();
    if (auto *tool = showPreferencesTool("webControlDialog"))
      connect(tool, &QDialog::finished, &dialog, [this, webEnabled, webPort] {
        webEnabled->setChecked(profile.settings.value("web/enabled", false).toBool());
        webPort->setValue(profile.settings.value("web/port", 13579).toInt());
      });
  });
  tabs->addTab(webPage, uiTr("Web-интерфейс"));
  mediaApply.append([this, webEnabled, webPort] {
    profile.settings.setValue("web/enabled", webEnabled->isChecked());
    profile.settings.setValue("web/port", webPort->value());
  });
  auto *misc = new QWidget;
  auto *miscForm = new QFormLayout(misc);
  auto *zeroHours = new QCheckBox(uiTr("Показывать нулевые часы в статусной строке"));
  zeroHours->setObjectName("ui/showZeroHours"); zeroHours->setChecked(profile.settings.value("ui/showZeroHours", false).toBool());
  miscForm->addRow(zeroHours);
  mediaApply.append([this, zeroHours] { profile.settings.setValue("ui/showZeroHours", zeroHours->isChecked()); });
  auto *profileButton = new QPushButton(uiTr("Управление настройками…"));
  profileButton->setObjectName("settingsProfileManagement");
  connect(profileButton, &QPushButton::clicked, &dialog, [this] { actions["profileManagement"]->trigger(); });
  miscForm->addRow(profileButton);
  auto *maintenanceButton = new QPushButton(uiTr("Обслуживание и диагностика…"));
  maintenanceButton->setObjectName("classicMaintenanceDetails");
  connect(maintenanceButton, &QPushButton::clicked, &dialog, [this, &dialog, showPreferencesTool] {
    maintenanceDialog();
    if (auto *tool = showPreferencesTool("maintenanceDialog"))
      connect(tool->findChild<QPushButton *>("maintenanceSavePreferences"), &QPushButton::clicked, &dialog, [this, &dialog] {
        if (auto *choice = dialog.findChild<QComboBox *>("maintenance/priority"))
          choice->setCurrentIndex(choice->findData(profile.settings.value("maintenance/priority", "normal")));
        if (auto *check = dialog.findChild<QCheckBox *>("maintenance/updatePreviews"))
          check->setChecked(profile.settings.value("maintenance/updatePreviews", true).toBool());
      });
  });
  miscForm->addRow(maintenanceButton);
  tabs->addTab(misc, uiTr("Разное"));
  auto *capturePage = new QWidget;
  auto *captureForm = new QFormLayout(capturePage);
  auto *captureFormat = new QComboBox;
  captureFormat->setObjectName("capture/format");
  for (const auto &format : MediaExports::formats()) captureFormat->addItem(format.toUpper());
  captureFormat->setCurrentText(profile.settings.value("capture/format", "png").toString().toUpper());
  captureForm->addRow(uiTr("Формат изображения:"), captureFormat);
  mediaApply.append([this, captureFormat] { profile.settings.setValue("capture/format", captureFormat->currentText().toLower()); });
  for (const auto &entry : QList<QPair<QString, QString>>{
         {"capture/quality", uiTr("Качество (1–100):")},
         {"capture/columns", uiTr("Столбцов миниатюр:")},
         {"capture/rows", uiTr("Строк миниатюр:")},
         {"capture/thumbWidth", uiTr("Ширина миниатюры, пикселей:")}}) {
    auto *spin = new QSpinBox; spin->setObjectName(entry.first);
    const bool width = entry.first.endsWith("thumbWidth");
    spin->setRange(width ? 80 : 1, width ? 480 : entry.first.endsWith("quality") ? 100 : entry.first.endsWith("columns") ? 6 : 10);
    spin->setValue(profile.settings.value(entry.first, width ? 240 : entry.first.endsWith("quality") ? 90 : 4).toInt());
    captureForm->addRow(entry.second, spin);
    mediaApply.append([this, spin, key = entry.first] { profile.settings.setValue(key, spin->value()); });
  }
  tabs->addTab(capturePage, uiTr("Снимки и миниатюры"));
  auto *servicePage = new QWidget;
  auto *serviceForm = new QFormLayout(servicePage);
  auto *previews = new QCheckBox(uiTr("Учитывать предварительные выпуски Qt"));
  previews->setObjectName("maintenance/updatePreviews");
  previews->setChecked(profile.settings.value("maintenance/updatePreviews", true).toBool());
  serviceForm->addRow(previews);
  auto *checkUpdates = new QPushButton(uiPlatformTr("Проверить обновления Linux/Qt", "Проверить обновления Windows/Qt"));
  checkUpdates->setObjectName("settingsCheckUpdates");
  serviceForm->addRow(checkUpdates);
  connect(checkUpdates, &QPushButton::clicked, &dialog, [this, previews, &dialog, showPreferencesTool] {
    // The explicit check uses the draft channel without saving it.
    maintenanceDialog();
    if (auto *tool = showPreferencesTool("maintenanceDialog")) {
      tool->findChild<QCheckBox *>("maintenancePreviews")->setChecked(previews->isChecked());
      tool->findChild<QPushButton *>("maintenanceCheckUpdates")->click();
      connect(tool->findChild<QPushButton *>("maintenanceSavePreferences"), &QPushButton::clicked, &dialog, [this, previews] {
        previews->setChecked(profile.settings.value("maintenance/updatePreviews", true).toBool());
      });
    }
  });
  auto *priority = new QComboBox;
  priority->setObjectName("maintenance/priority");
  priority->addItem(uiTr("Обычный (nice 0)"), "normal");
  priority->addItem(uiTr("Выше обычного (nice −5)"), "above");
  priority->addItem(uiTr("Ниже обычного (nice 5)"), "low");
  priority->setCurrentIndex(std::max(0, priority->findData(profile.settings.value("maintenance/priority", "normal"))));
  serviceForm->addRow(uiTr("Приоритет со следующего запуска:"), priority);
  mediaApply.append([this, previews, priority] {
    profile.settings.setValue("maintenance/updatePreviews", previews->isChecked());
    profile.settings.setValue("maintenance/priority", priority->currentData());
  });
  tabs->addTab(servicePage, uiTr("Обслуживание"));
  if (!ClassicPreferences::loadingStep(&dialog)) return;
  ClassicPreferences::organize(&dialog, tabs, requestedPage.isEmpty()
      ? profile.settings.value("ui/lastPreferencePage").toString() : requestedPage);
  if (!ClassicPreferences::loadingStep(&dialog)) return;
  auto *b =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
  auto *buttonStyle = new ClassicButtonStyle; buttonStyle->setParent(b); b->setStyle(buttonStyle);
  for (auto *button : b->buttons()) { button->setIcon({}); button->setLayoutDirection(Qt::LeftToRight); }
  b->button(QDialogButtonBox::Ok)->setText("OK");
  b->button(QDialogButtonBox::Cancel)->setText(uiTr("Отмена"));
  b->button(QDialogButtonBox::Apply)->setText(uiTr("Применить"));
  outer->addWidget(b); b->hide();
  auto *applyButton = b->button(QDialogButtonBox::Apply);
  applyButton->setEnabled(false);
  const auto dirty = [applyButton] { applyButton->setEnabled(true); };
  for (auto *box : dialog.findChildren<QCheckBox *>()) connect(box, &QCheckBox::toggled, &dialog, dirty);
  for (auto *box : dialog.findChildren<QRadioButton *>()) connect(box, &QRadioButton::toggled, &dialog, dirty);
  for (auto *box : dialog.findChildren<QComboBox *>()) connect(box, &QComboBox::currentIndexChanged, &dialog, dirty);
  for (auto *edit : dialog.findChildren<QLineEdit *>())
    if (!edit->property("settingsNavigation").toBool()) connect(edit, &QLineEdit::textChanged, &dialog, dirty);
  for (auto *spin : dialog.findChildren<QSpinBox *>()) connect(spin, &QSpinBox::valueChanged, &dialog, dirty);
  for (auto *spin : dialog.findChildren<QDoubleSpinBox *>()) connect(spin, &QDoubleSpinBox::valueChanged, &dialog, dirty);
  for (auto *slider : dialog.findChildren<QSlider *>()) connect(slider, &QSlider::valueChanged, &dialog, dirty);
  for (auto *edit : dialog.findChildren<QPlainTextEdit *>()) connect(edit, &QPlainTextEdit::textChanged, &dialog, dirty);
  for (auto *edit : dialog.findChildren<QKeySequenceEdit *>()) connect(edit, &QKeySequenceEdit::keySequenceChanged, &dialog, dirty);
  const auto validate = [&]() -> bool {
    const QString customYtdlp = ytdlpPath->text().trimmed();
    if (onlineEnabled->isChecked() && !customYtdlp.isEmpty()) {
      const QFileInfo executable(customYtdlp);
      if (!executable.isFile() || !executable.isExecutable()) {
        ClassicPreferences::selectPage(tabs, onlinePage);
        onlineStatus->setText(
            uiTr("Выбранный файл yt-dlp не существует или не исполняемый"));
        return false;
      }
    }
    if (userAgent->text().contains(',') || userAgent->text().contains('\n') ||
        userAgent->text().contains('\r')) {
      ClassicPreferences::selectPage(tabs, onlinePage);
      onlineStatus->setText(
          uiTr("User-Agent не должен содержать запятые или перевод строки"));
      return false;
    }
    const QRegularExpression languagePattern("^[A-Za-z0-9_-]{0,16}$");
    if (!languagePattern.match(audioLanguage->text().trimmed()).hasMatch()) {
      ClassicPreferences::selectPage(tabs, onlinePage);
      onlineStatus->setText(
          uiTr("Язык аудио: один код до 16 символов, например ru или en"));
      return false;
    }
    for (auto first = editors.begin(); first != editors.end(); ++first) {
      if (first.value()->keySequence().isEmpty())
        continue;
      auto second = first;
      for (++second; second != editors.end(); ++second) {
        auto a = first.value()->keySequence(),
             b = second.value()->keySequence();
        if (!b.isEmpty() && (a.matches(b) != QKeySequence::NoMatch ||
                             b.matches(a) != QKeySequence::NoMatch)) {
          ClassicPreferences::selectPage(tabs, keysPage);
          conflict->setText(uiTr("Совпадающие клавиши: «") +
                            actions[first.key()]->text() + uiTr("» и «") +
                            actions[second.key()]->text() + "»");
          return false;
        }
      }
    }
    if (validateAudioFilterPreferences(&dialog) &&
        validateVideoShaderPreferences(&dialog) &&
        validateVideoDisplayPreferences(&dialog)) {
      if (webEnabled->isChecked()) {
        QString error;
        const auto address = profile.settings.value("web/lanEnabled", false).toBool()
          ? QHostAddress(profile.settings.value("web/lanAddress").toString()) : QHostAddress(QHostAddress::LocalHost);
        if (!replaceWebControl(quint16(webPort->value()), address, &error)) {
          ClassicPreferences::selectPage(tabs, webPage);
          QMessageBox::warning(&dialog, uiTr("Web-интерфейс"), error);
          return false;
        }
      } else if (webControl) webControl->stop();
      return true;
    }
    return false;
  };
  connect(b, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  const auto savePreferences = [&] {
    for (auto &save : mediaApply)
      save();
    filePlays = playlistPlays = 0;
    refreshPlaybackControls();
    updateMinimizePause();
    applyMediaOptions();
    preview->setFilters(previewVideoFilters());
    timeline->previewEnabled =
        profile.settings.value("ui/previews", true).toBool();
    applyAppearance();
    applyWindowPreferences();
    actions["onTop"]->setChecked(
        profile.settings.value("ui/onTop", false).toBool());
    actions["frameless"]->setChecked(
        profile.settings.value("ui/frameless", false).toBool());
    if (!profile.settings.value("ui/osd", FoxicMPFamily::Osd).toBool())
      osd->hide();
    updateClock();
    for (auto it = editors.begin(); it != editors.end(); ++it)
      profile.settings.setValue(
          "keys/" + it.key(),
          it.value()->keySequence().toString(QKeySequence::PortableText));
    applyShortcuts();
    profile.settings.setValue("history/enabled", history->isChecked());
    profile.settings.setValue("playback/rememberPosition",
                              remember->isChecked());
    profile.settings.setValue("video/output", output->currentData().toString());
    profile.settings.setValue("audio/languages", audioLanguages->text());
    profile.settings.setValue("subtitles/languages", subtitleLanguages->text());
    player->set("alang", audioLanguages->text());
    player->set("slang", subtitleLanguages->text());
    profile.settings.setValue("playback/hardwareDecoding", hw->isChecked());
    VideoOutput::apply(player, profile.settings);
    if (integration->isChecked() !=
        profile.settings.value("integration/enabled", false).toBool())
      setIntegration(integration->isChecked());
    profile.settings.setValue("integration/checkOnStartup",
                              verify->isChecked());
    profile.settings.setValue("online/enabled", onlineEnabled->isChecked());
    profile.settings.setValue("online/ytdlpPath", ytdlpPath->text().trimmed());
    profile.settings.setValue("online/maxHeight", height->currentData());
    profile.settings.setValue("online/maxFps", fps->currentData());
    profile.settings.setValue("online/videoCodec", videoCodec->currentData());
    profile.settings.setValue("online/hdr", hdr->currentData());
    profile.settings.setValue("online/audioCodec", audioCodec->currentData());
    profile.settings.setValue("online/audioLanguage",
                              audioLanguage->text().trimmed());
    profile.settings.setValue("online/maxVideoBitrate", bitrate->value());
    profile.settings.setValue("online/networkTimeout", networkTimeout->value());
    profile.settings.setValue("online/userAgent", userAgent->text().trimmed());
    profile.settings.setValue("online/aceStreamEndpoint",
                              aceEndpoint->text().trimmed());
    profile.settings.setValue("online/torrServerEndpoint",
                              torrEndpoint->text().trimmed());
    if (tabs->currentWidget()) profile.settings.setValue("ui/lastPreferencePage", tabs->currentWidget()->objectName());
    applyButton->setEnabled(false);
  };
  connect(b, &QDialogButtonBox::accepted, &dialog, [&] {
    if (validate()) dialog.accept();
  });
  connect(b->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&] {
    if (validate()) savePreferences();
  });
  ClassicPreferences::finishLoading(&dialog);
  if (dialog.exec() == QDialog::Accepted) savePreferences();
}
bool Window::requestedProfileReset(bool *keepLibrary) const {
  if (keepLibrary) *keepLibrary = keepResetLibrary;
  return resetProfileOnExit;
}
void Window::closeEvent(QCloseEvent *e) {
  if (!forceExit && !qApp->isSavingSession() && profile.settings.value("desktop/closeToTray", false).toBool() && hideToTray()) {
    e->ignore(); return;
  }
  cancelProgressiveOpening();
  cancelMouseHold();
  ++playbackGeneration;
  if (profile.settings.value("playlist/rememberPanel", true).toBool()) {
    const bool hidden =
        isFullScreen() &&
        profile.settings.value("playlist/hideFullscreen", true).toBool();
    if (hidden)
      dock->setVisible(restorePlaylist);
    profile.settings.setValue("playlist/dockState", saveState(1));
    profile.settings.setValue("playlist/dockGeometry", dock->saveGeometry());
    if (hidden)
      dock->hide();
  }
  savePosition();
  releaseProgressive();
  syncPlaylist();
  if (profile.settings.value("ui/rememberGeometry", FoxicMPFamily::RememberGeometry).toBool())
    profile.settings.setValue(
        "window/geometry", isFullScreen() && !beforeFullscreenGeometry.isEmpty()
                               ? beforeFullscreenGeometry
                               : saveGeometry());
  restoreDisplayMode();
  profile.settings.setValue("audio/volume", volume->value());
  profile.settings.sync();
  if (profile.settings.status() != QSettings::NoError)
    QMessageBox::warning(
        this, uiTr("Настройки"),
        uiTr("Не удалось сохранить профиль в папку программы."));
  e->accept();
}
void Window::dragEnterEvent(QDragEnterEvent *e) {
  if (e->mimeData()->hasUrls())
    e->acceptProposedAction();
}
void Window::dropEvent(QDropEvent *e) {
  QStringList files;
  for (const auto &u : e->mimeData()->urls())
    files.append(u.isLocalFile() ? u.toLocalFile()
                                 : u.toString(QUrl::FullyEncoded));
  openFiles(files);
  e->acceptProposedAction();
}
