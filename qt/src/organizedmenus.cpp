#include "window.h"
#include "i18n.h"
#include "player.h"
#include "playlistpanel.h"
#include <QtWidgets>

void Window::buildOrganizedMenus(QMenu *videoStreams, QMenu *language) {
  const auto submenu = [](QMenu *parent, const QString &title, const char *name) {
    auto *menu = parent->addMenu(title);
    menu->setObjectName(name);
    menu->setToolTipsVisible(true);
    return menu;
  };
  const auto add = [this](QMenu *menu, std::initializer_list<const char *> ids) {
    for (const auto *id : ids) {
      auto *action = actions.value(id);
      Q_ASSERT(action);
      if (action) menu->addAction(action);
    }
  };
  const auto page = [this](QMenu *menu, const char *id, const QString &title, const char *target) {
    auto *action = menu->addAction(title, this, [this, target] {
      setProperty("foxicmpPreferencePage", target); preferences();
    });
    action->setObjectName(QString("action_") + id);
    action->setProperty("preferencePage", target);
  };
  const auto section = [this, &submenu](const QString &title, const char *name) {
    auto *menu = submenu(contextMenu, title, name);
    menuBar()->addMenu(menu);
    return menu;
  };
  // The same menus and actions are used by the menu bar and video popup.
  // Visibility of the menu bar and fullscreen never change their hierarchy.
  contextMenu->clear();
  add(contextMenu, {"play", "stop", "previous", "next"});
  contextMenu->addSeparator();

  auto *file = section(uiTr("Файл"), "organizedFileMenu");
  add(file, {"open", "openDirectory", "openUrl", "onlinePlaylist", "openClipboard"});
  auto *sources = submenu(file, uiTr("Диски и устройства"), "organizedSourcesMenu");
  add(sources, {"openDisc", "openCapture", "openTelevision"});
  file->addSeparator();
  file->addMenu(historyMenu);
  add(file, {"openLast", "reopen", "closeFile"});
  file->addSeparator();
  add(file, {"saveMediaCopy", "mediaProperties"});
  file->addSeparator();
  add(file, {"quit"});

  auto *playback = section(uiTr("Воспроизведение"), "organizedPlaybackMenu");
  add(playback, {"play", "stop", "previous", "next"});
  playback->addSeparator();
  auto *seek = submenu(playback, uiTr("Перемотка"), "organizedSeekMenu");
  add(seek, {"restart", "backSmall", "forwardSmall", "back", "forward", "backLarge", "forwardLarge", "goto"});
  auto *chapters = submenu(playback, uiTr("Главы"), "organizedChaptersMenu");
  add(chapters, {"chapterPrevious", "chapterNext"});
  chapters->addMenu(chapterMenu);
  add(chapters, {"discTitle"});
  auto *frames = submenu(playback, uiTr("Покадровый просмотр"), "organizedFramesMenu");
  add(frames, {"frameBack", "frameNext", "keyframeBack", "keyframeNext"});
  auto *speed = submenu(playback, uiTr("Скорость"), "organizedSpeedMenu");
  add(speed, {"speedUp", "speedDown", "speedReset"});
  speed->addSeparator();
  auto *rates = new QActionGroup(speed);
  for (double value : {.5, .75, 1., 1.25, 1.5, 2.}) {
    auto *action = speed->addAction(QString::number(value) + "×", this, [this, value] { setSpeed(value); });
    action->setCheckable(true); action->setData(value); rates->addAction(action);
  }
  connect(speed, &QMenu::aboutToShow, this, [this, rates] {
    const double rate = player->get("speed").toDouble();
    for (auto *action : rates->actions()) action->setChecked(qAbs(action->data().toDouble() - rate) < .00001);
  });
  auto *repeat = submenu(playback, uiTr("Повтор"), "organizedRepeatMenu");
  add(repeat, {"repeatNone", "repeatOne", "repeatAll"});
  auto *repeatGroup = new QActionGroup(repeat);
  for (auto *action : repeat->actions()) repeatGroup->addAction(action);
  auto *ab = submenu(repeat, uiTr("Отрезок A–B"), "abLoopMenu");
  add(ab, {"abA", "abB", "abClear", "abDialog"});
  playback->addSeparator();
  playback->addMenu(afterMenu);

  auto *video = section(uiTr("Видео"), "organizedVideoMenu");
  videoStreams->setParent(video, videoStreams->windowFlags());
  videoStreams->setTitle(uiTr("Видеодорожка")); video->addMenu(videoStreams);
  auto *aspect = submenu(video, uiTr("Пропорции"), "organizedAspectMenu");
  add(aspect, {"ID_ASPECTRATIO_SOURCE", "ID_ASPECTRATIO_4_3", "ID_ASPECTRATIO_5_4", "ID_ASPECTRATIO_16_9", "ID_ASPECTRATIO_235_100", "ID_ASPECTRATIO_185_100"});
  aspect->addSeparator();
  add(aspect, {"classicKeepAspect"});
  auto *fit = submenu(video, uiTr("Размещение кадра"), "organizedFitMenu");
  add(fit, {"fit", "fill", "classicStretch"});
  auto *zoom = submenu(video, uiTr("Масштаб изображения"), "organizedZoomMenu");
  add(zoom, {"ID_VIEW_VF_HALF", "ID_VIEW_VF_NORMAL", "ID_VIEW_VF_DOUBLE"});
  zoom->addSeparator();
  add(zoom, {"zoomIn", "zoomOut", "panReset"});
  auto *transform = submenu(video, uiTr("Положение и обрезка"), "transformMenu");
  add(transform, {"transformDialog"});
  auto *dimensions = submenu(transform, uiTr("Размер изображения"), "organizedDimensionsMenu");
  add(dimensions, {"scaleUp", "scaleDown", "widthUp", "widthDown", "heightUp", "heightDown"});
  auto *position = submenu(transform, uiTr("Смещение"), "organizedPositionMenu");
  add(position, {"panLeft", "panRight", "panUp", "panDown", "panUpLeft", "panUpRight", "panDownLeft", "panDownRight", "panCenter"});
  auto *presets = submenu(transform, uiTr("Пресеты преобразований"), "transformPresetsMenu");
  connect(presets, &QMenu::aboutToShow, this, [this, presets] { refreshTransformPresets(presets); });
  refreshTransformPresets(presets);
  transform->addSeparator();
  add(transform, {"transformReset"});
  auto *rotate = submenu(video, uiTr("Поворот и отражение"), "organizedRotationMenu");
  add(rotate, {"rotateClockwise", "rotateCounterclockwise", "mirrorHorizontal", "mirrorVertical"});
  video->addSeparator();
  page(video, "menuVideoColor", uiTr("Цвет и ICC…"), "videoColorPage");
  auto *shaders = submenu(video, uiTr("Шейдеры"), "organizedShadersMenu");
  add(shaders, {"ID_SHADERS_1_ENABLE", "ID_SHADERS_2_ENABLE", "classicShaderEditor"});
  auto *stereo = submenu(video, uiTr("Стерео 3D"), "organizedStereoMenu");
  auto *stereoGroup = new QActionGroup(stereo);
  for (const auto *id : {"pass", "ml", "mr", "irl", "irl2", "abl", "ab2l", "sbsl", "sbs2l", "arcd", "arcc", "icl", "chl"}) {
    auto *action = actions.value("stereoOutput_" + QString(id));
    stereo->addAction(action); stereoGroup->addAction(action);
  }
  stereo->addSeparator(); add(stereo, {"stereoSwap"});
  connect(stereo, &QMenu::aboutToShow, this, [this, stereoGroup] {
    for (auto *action : stereoGroup->actions()) action->setChecked(action->objectName().mid(20) == videoTransform.stereoOutput);
  });
  video->addSeparator();
  actions["classicRendererReset"]->setText(uiTr("Сбросить ICC и синхронизацию видео"));
  add(video, {"videoOutputSettings", "classicRendererReset"});

  auto *audio = section(uiTr("Аудио"), "organizedAudioMenu");
  audio->addMenu(audioMenu);
  add(audio, {"audioPrevious", "audioNext", "externalAudio"});
  audio->addSeparator();
  auto *volume = submenu(audio, uiTr("Громкость"), "organizedVolumeMenu");
  add(volume, {"volumeUp", "volumeDown", "mute"});
  auto *audioDelay = submenu(audio, uiTr("Задержка звука"), "organizedAudioDelayMenu");
  add(audioDelay, {"audioDelayDown", "audioDelayUp", "audioDelayDialog"});
  auto *devices = submenu(audio, uiTr("Устройство звука"), "organizedAudioDevicesMenu");
  connect(devices, &QMenu::aboutToShow, this, [this, devices] {
    devices->clear();
    const auto active = player->get("audio-device").toString();
    for (const auto &value : player->get("audio-device-list").toList()) {
      const auto entry = value.toMap(); const auto name = entry.value("name").toString();
      if (name.startsWith("pulse/foxicmp_dual_")) continue;
      auto *action = devices->addAction(entry.value("description").toString(), this, [this, name] {
        profile.settings.setValue("audio/device", name); applyMediaOptions();
        showOsd(uiTr("Устройство звука изменено"));
      });
      action->setData(name); action->setCheckable(true); action->setChecked(active == name);
    }
  });
  audio->addSeparator(); add(audio, {"audioProcessingSettings"});
  page(audio, "menuAudioOptions", uiTr("Параметры аудио…"), "IDD_PPAGEAUDIO");

  auto *subtitles = section(uiTr("Субтитры"), "organizedSubtitlesMenu");
  subtitles->addMenu(subtitleMenu);
  add(subtitles, {"loadSubtitles", "subtitleToggle", "subtitlePrevious", "subtitleNext"});
  subtitles->addSeparator();
  auto *subDelay = submenu(subtitles, uiTr("Задержка субтитров"), "organizedSubtitleDelayMenu");
  add(subDelay, {"subtitleDelayDown", "subtitleDelayUp", "subtitleDelayDialog", "subtitleSyncPrevious", "subtitleSyncNext"});
  auto *cues = submenu(subtitles, uiTr("Реплики"), "organizedSubtitleCuesMenu");
  add(cues, {"subtitleCuePrevious", "subtitleCueNext", "subtitleCopy"});
  add(subtitles, {"subtitleStyleSettings"});
  page(subtitles, "menuSubtitlePlacement", uiTr("Положение субтитров…"), "settingsSubtitlePlacement");
  subtitles->addSeparator();
  add(subtitles, {"subtitleEditor", "subtitleReload", "subtitleExternalEditor"});

  auto *audioOptions = new QMenu(this); audioOptions->setObjectName("classicAudioOptionsMenu");
  page(audioOptions, "quickAudioOptions", uiTr("Параметры…"), "IDD_PPAGEAUDIO");
  add(audioOptions, {"externalAudio"});
  auto *subtitleOptions = new QMenu(this); subtitleOptions->setObjectName("classicSubtitleOptionsMenu");
  page(subtitleOptions, "quickSubtitleOptions", uiTr("Параметры…"), "IDD_PPAGESUBTITLES");
  add(subtitleOptions, {"loadSubtitles", "subtitleStyleSettings", "subtitleReload"});
  subtitleOptions->addSeparator(); add(subtitleOptions, {"subtitleToggle"});
  auto *defaultStyle = subtitleOptions->addAction(uiTr("Использовать стиль по умолчанию"));
  defaultStyle->setCheckable(true);
  connect(defaultStyle, &QAction::triggered, this, [this](bool enabled) {
    profile.settings.setValue("subtitles/overrideAss", enabled); applySubtitleStyle();
  });
  auto *forcedOnly = subtitleOptions->addAction(uiTr("Только принудительные субтитры"));
  forcedOnly->setCheckable(true);
  connect(forcedOnly, &QAction::triggered, this, [this](bool enabled) {
    if (player->set("sub-forced-events-only", enabled)) profile.settings.setValue("subtitles/forcedOnly", enabled);
  });
  connect(subtitleOptions, &QMenu::aboutToShow, this, [this, defaultStyle, forcedOnly] {
    defaultStyle->setChecked(profile.settings.value("subtitles/overrideAss", false).toBool());
    forcedOnly->setEnabled(player->get("option-info/sub-forced-events-only").isValid());
    forcedOnly->setChecked(profile.settings.value("subtitles/forcedOnly", false).toBool());
  });
  auto *library = section(uiTr("Библиотека"), "organizedLibraryMenu");
  add(library, {"playlist"});
  auto *queue = submenu(library, uiTr("Плейлист"), "organizedPlaylistMenu");
  add(queue, {"playlistPlaySelected", "playlistAdd", "playlistRemove", "playlistUp", "playlistDown"});
  queue->addSeparator(); add(queue, {"playlistLoad", "playlistSave", "playlistClear", "shuffle"});
  queue->addMenu(queuePanel->operationsMenu());
  library->addSeparator(); library->addMenu(historyMenu); library->addMenu(favoritesMenu);

  auto *view = section(uiTr("Вид"), "organizedViewMenu");
  add(view, {"fullscreen"});
  auto *size = submenu(view, uiTr("Размер окна"), "organizedWindowSizeMenu");
  add(size, {"size50", "size100", "size200", "classicAutoFit"});
  auto *top = submenu(view, uiTr("Поверх остальных окон"), "organizedOnTopMenu");
  add(top, {"ID_ONTOP_NEVER", "ID_ONTOP_ALWAYS", "ID_ONTOP_WHILEPLAYING", "ID_ONTOP_WHILEPLAYINGVIDEO"});
  view->addSeparator(); add(view, {"classicCaptionMenu", "frameless"});
  buildWindowMenu(view);
  auto *panels = view->findChild<QMenu *>("windowPanelsMenu");
  add(panels, {"classicInformation", "classicStatistics"});
  view->addSeparator();
  language->setParent(view, language->windowFlags()); language->setTitle(uiTr("Язык (Language)")); view->addMenu(language);
  add(view, {"bossKey"});

  auto *service = section(uiTr("Сервис"), "organizedServiceMenu");
  auto *capture = submenu(service, uiTr("Снимки и миниатюры"), "organizedCaptureMenu");
  add(capture, {"snapshot", "snapshotQuick", "snapshotClipboard", "snapshotDisplay", "thumbnailSheet", "captureSettings"});
  add(service, {"webControl", "profileManagement", "associations", "desktopSettings"});
  service->addSeparator();
  auto *diagnostics = submenu(service, uiTr("Диагностика"), "organizedDiagnosticsMenu");
  add(diagnostics, {"information", "renderStatistics", "maintenance"});
  add(service, {"image", "delayReset"});
  service->addSeparator();
  actions["preferences"]->setText(uiTr("Настройки…")); add(service, {"preferences"});

  auto *help = section(uiTr("Справка"), "organizedHelpMenu");
  page(help, "menuShortcuts", uiTr("Горячие клавиши…"), "IDD_PPAGEACCELTBL");
  add(help, {"classicCommandLine", "checkUpdates", "classicHome"});
  help->addSeparator(); add(help, {"classicAbout"});

  contextMenu->addSeparator();
  add(contextMenu, {"fullscreen", "preferences", "quit"});
}
