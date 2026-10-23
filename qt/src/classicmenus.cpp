#include "window.h"
#include "i18n.h"
#include "player.h"
#include "videoshaders.h"
#include "videodisplay.h"
#include "devicesource.h"
#include "timeline.h"
#include <cmath>
#include <QtWidgets>

// Classic command captions and aliases come from the checked-in resources.
// Keep the QAction objects: keyboard, mouse, web and menu commands share state.
void Window::applyClassicMenus() {
  QFile file(":/classic-menus.json");
  if (!file.open(QIODevice::ReadOnly)) return;
  const bool english = profile.settings.value("appearance/language", "ru").toString() == "en";
  const auto reference = QJsonDocument::fromJson(file.readAll()).object()
                             .value(english ? "en" : "ru").toObject();
  QMap<QString, QString> ids{
    {"ID_FILE_OPENFILE", "open"}, {"ID_FILE_OPENFILEURL", "openUrl"},
    {"ID_FILE_OPENDVD", "openDisc"}, {"ID_FILE_OPENDEVICE", "openCapture"},
    {"ID_FILE_OPENDIRECTORY", "openDirectory"}, {"ID_FILE_CLOSEPLAYLIST", "closeFile"},
    {"ID_FILE_SAVE_COPY", "saveMediaCopy"}, {"ID_FILE_SAVE_IMAGE", "snapshot"},
    {"ID_FILE_SAVE_THUMBNAILS", "thumbnailSheet"}, {"ID_FILE_LOAD_SUBTITLE", "loadSubtitles"},
    {"ID_FILE_PROPERTIES", "mediaProperties"}, {"ID_FILE_EXIT", "quit"},
    {"ID_VIEW_CAPTIONMENU", "classicCaptionMenu"}, {"ID_VIEW_SEEKER", "showTimeline"},
    {"ID_VIEW_CONTROLS", "showControls"}, {"ID_VIEW_INFORMATION", "classicInformation"},
    {"ID_VIEW_STATISTICS", "classicStatistics"}, {"ID_VIEW_STATUS", "showStatus"},
    {"ID_VIEW_SUBRESYNC", "subtitleEditor"}, {"ID_VIEW_PLAYLIST", "playlist"},
    {"ID_VIEW_SHADEREDITOR", "classicShaderEditor"},
    {"ID_VIEW_NAVIGATION", "openTelevision"},
    {"ID_VIEW_PRESETS_MINIMAL", "window_minimal"}, {"ID_VIEW_PRESETS_COMPACT", "window_compact"},
    {"ID_VIEW_PRESETS_NORMAL", "window_normal"}, {"ID_VIEW_FULLSCREEN", "fullscreen"},
    {"ID_VIEW_ZOOM_50", "size50"}, {"ID_VIEW_ZOOM_100", "size100"}, {"ID_VIEW_ZOOM_200", "size200"},
    {"ID_VIEW_OPTIONS", "preferences"}, {"ID_PLAY_PLAYPAUSE", "play"}, {"ID_PLAY_STOP", "stop"},
    {"ID_PLAY_FRAMESTEP", "frameNext"}, {"ID_PLAY_GOTO", "goto"},
    {"ID_REPEAT_FOREVER", "repeatAll"}, {"ID_PLAY_REPEAT_AB", "abClear"},
    {"ID_PLAY_REPEAT_AB_MARK_A", "abA"}, {"ID_PLAY_REPEAT_AB_MARK_B", "abB"},
    {"ID_PLAY_INCRATE", "speedUp"}, {"ID_PLAY_DECRATE", "speedDown"}, {"ID_PLAY_RESETRATE", "speedReset"},
    {"ID_VOLUME_UP", "volumeUp"}, {"ID_VOLUME_DOWN", "volumeDown"}, {"ID_VOLUME_MUTE", "mute"},
    {"ID_HELP_CHECKFORUPDATE", "checkUpdates"}, {"ID_HELP_ABOUT", "classicAbout"},
    {"ID_VIEW_DISPLAYSTATS", "renderStatistics"},
    {"ID_VIEW_INCSIZE", "scaleUp"}, {"ID_VIEW_DECSIZE", "scaleDown"},
    {"ID_VIEW_INCWIDTH", "widthUp"}, {"ID_VIEW_DECWIDTH", "widthDown"},
    {"ID_VIEW_INCHEIGHT", "heightUp"}, {"ID_VIEW_DECHEIGHT", "heightDown"},
    {"ID_PANSCAN_MOVERIGHT", "panRight"}, {"ID_PANSCAN_MOVELEFT", "panLeft"},
    {"ID_PANSCAN_MOVEUP", "panUp"}, {"ID_PANSCAN_MOVEDOWN", "panDown"},
    {"ID_PANSCAN_CENTER", "panCenter"}, {"ID_PANSCAN_ROTATE_CW", "rotateClockwise"},
    {"ID_PANSCAN_ROTATE_CCW", "rotateCounterclockwise"}, {"ID_PANSCAN_FLIP", "mirrorHorizontal"},
    {"ID_VIEW_RESET", "transformReset"}, {"ID_VIEW_VF_FROMINSIDE", "fit"},
    {"ID_VIEW_VF_FROMOUTSIDE", "fill"}, {"ID_VIEW_VF_ZOOM1", "zoomIn"}, {"ID_VIEW_VF_ZOOM2", "zoomOut"},
    {"ID_STEREO3D_AUTO", "stereoOutput_pass"}, {"ID_STEREO3D_MONO", "stereoOutput_ml"},
    {"ID_STEREO3D_ROW_INTERLEAVED", "stereoOutput_irl"}, {"ID_STEREO3D_ROW_INTERLEAVED_2X", "stereoOutput_irl2"},
    {"ID_STEREO3D_HALFOVERUNDER", "stereoOutput_ab2l"}, {"ID_STEREO3D_OVERUNDER", "stereoOutput_abl"},
    {"ID_STEREO3D_SWAP_LEFTRIGHT", "stereoSwap"}
  };
  auto extra = [this](const QString &name, std::function<void()> fn) {
    auto *a = new QAction(this); a->setObjectName("action_" + name);
    connect(a, &QAction::triggered, this, std::move(fn));
    actions[name] = a; defaultKeys[name] = {}; addAction(a); return a;
  };
  const auto captionMode = [this] {
    return profile.settings.value("ui/frameless", false).toBool() ? 3
      : profile.settings.value("ui/frameStyle", "system").toString() == "thin" ? 2
      : profile.settings.value("ui/showMenu", true).toBool() ? 0 : 1;
  };
  QStringList captionLabels;
  for (const auto &value : reference.value("captionModes").toArray()) captionLabels.append(value.toString());
  extra("classicCaptionMenu", [this, captionMode, captionLabels] {
    const int next = (captionMode() + 1) % 4;
    profile.settings.setValue("ui/showMenu", next == 0);
    profile.settings.setValue("ui/frameless", next == 3);
    profile.settings.setValue("ui/frameStyle", next == 2 ? "thin" : "system");
    applyWindowPreferences(); actions["classicCaptionMenu"]->setText(captionLabels.value(captionMode()));
  });
  const auto updateCaption = [this, captionMode, captionLabels] {
    actions["classicCaptionMenu"]->setText(captionLabels.value(captionMode()));
  };
  updateCaption();
  connect(actions["menu"], &QAction::changed, this, updateCaption);
  connect(actions["frameless"], &QAction::changed, this, updateCaption);
  for (const auto &entry : QList<QPair<QString, QString>>{{"classicInformation", "ui/showInformation"}, {"classicStatistics", "ui/showStatistics"}}) {
    const auto name = entry.first, key = entry.second;
    auto *panel = new QFrame(centralWidget()); panel->setObjectName(name + "Panel");
    panel->setFrameShape(QFrame::StyledPanel);
    auto *layout = new QVBoxLayout(panel); layout->setContentsMargins(8, 3, 8, 3);
    auto *label = new QLabel; label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(label);
    auto *root = qobject_cast<QVBoxLayout *>(centralWidget()->layout());
    root->insertWidget(root->indexOf(timeline), panel);
    auto *a = extra(name, [this, name, key] {
      profile.settings.setValue(key, actions[name]->isChecked()); applyPanelVisibility();
    });
    a->setCheckable(true); a->setChecked(profile.settings.value(key, false).toBool());
    panel->setVisible(a->isChecked());
    auto *timer = new QTimer(panel); timer->setInterval(500);
    auto update = [this, label, name, panel] {
      if (!panel->isVisible()) return;
      if (name == "classicInformation") {
        const auto size = videoSize();
        label->setText(uiTr("Файл:") + " " + (stopped ? QString("—") : QFileInfo(current).fileName()) + "\n" +
          uiTr("Видео:") + " " + player->get("video-codec").toString() +
          (size.isValid() ? QString("  %1 × %2").arg(size.width()).arg(size.height()) : QString()) + "\n" +
          uiTr("Аудио:") + " " + player->get("audio-codec-name").toString() + "\n" +
          uiTr("Длительность:") + " " + clockText(duration));
      } else {
        const auto stats = player->renderStatistics();
        label->setText(uiTr("Статистика") + ": " + stats.value("renderer").toString() +
          QString("  FPS: %1  A/V: %2 s  Dropped: %3").arg(player->get("estimated-vf-fps").toDouble(), 0, 'f', 3)
          .arg(player->get("avsync").toDouble(), 0, 'f', 3).arg(player->get("frame-drop-count").toLongLong()));
      }
    };
    connect(timer, &QTimer::timeout, panel, update); connect(a, &QAction::triggered, panel, update);
    timer->start();
  }
  extra("classicShaderEditor", [this] { setProperty("foxicmpPreferencePage", "videoShadersPage"); preferences(); });
  extra("classicAbout", [this] {
    QMessageBox::about(this, uiTr("О программе"),
                       QString("FoxicMP %1\nQt 6 + libmpv\nGPL v3").arg(QCoreApplication::applicationVersion()));
  });
  extra("classicCommandLine", [this] {
    QDialog d(this); d.setWindowTitle(uiTr("Параметры командной строки")); d.resize(720, 520);
    auto *l = new QVBoxLayout(&d); auto *text = new QPlainTextEdit;
    text->setReadOnly(true); QProcess proc;
    proc.start(QCoreApplication::applicationFilePath(), {"--help"});
    if (proc.waitForFinished(5000)) text->setPlainText(QString::fromUtf8(proc.readAllStandardOutput()));
    l->addWidget(text); auto *b = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject); l->addWidget(b); d.exec();
  });
  ids["ID_HELP_SHOWCOMMANDLINESWITCHES"] = "classicCommandLine";
  extra("classicHome", [] { QDesktopServices::openUrl(QUrl("https://github.com/LiberVixer/FoxicMP")); });
  ids["ID_HELP_HOMEPAGE"] = "classicHome";
  ids["ID_FILE_OPENISO"] = "openDisc";
  extra("classicAutoFit", [this] {
    if (isFullScreen() || !screen()) return;
    const auto area = screen()->availableGeometry().size() - QSize(16, 40);
    const auto video = videoSize();
    if (video.isValid()) resizeVideoArea(video.scaled(area, Qt::KeepAspectRatio));
  });
  ids["ID_VIEW_ZOOM_AUTOFIT"] = "classicAutoFit";
  for (const auto &entry : QList<QPair<QString, double>>{
         {"ID_VIEW_VF_HALF", .5}, {"ID_VIEW_VF_NORMAL", 1.}, {"ID_VIEW_VF_DOUBLE", 2.}}) {
    extra(entry.first, [this, factor = entry.second] {
      const auto video = videoSize();
      if (!video.isValid() || player->width() <= 0 || player->height() <= 0) return;
      const auto fitted = video.scaled(player->size(), Qt::KeepAspectRatio);
      videoTransform.width = videoTransform.height = 1.; videoTransform.fill = 0;
      videoTransform.zoom = std::clamp(std::log2(factor * video.width() / fitted.width()), -3., 3.);
      player->set("keepaspect", true); transformChanged();
    });
    ids[entry.first] = entry.first;
  }
  auto *keepAspect = extra("classicKeepAspect", [this] {
    const bool enabled = actions["classicKeepAspect"]->isChecked();
    player->set("keepaspect", enabled); profile.settings.setValue("video/keepAspect", enabled);
  });
  keepAspect->setCheckable(true); keepAspect->setChecked(profile.settings.value("video/keepAspect", true).toBool());
  player->set("keepaspect", keepAspect->isChecked());
  ids["ID_VIEW_VF_KEEPASPECTRATIO"] = "classicKeepAspect";
  extra("classicStretch", [this] { videoTransform = VideoTransform(); transformChanged(); player->set("keepaspect", false); profile.settings.setValue("video/keepAspect", false); actions["classicKeepAspect"]->setChecked(false); });
  ids["ID_VIEW_VF_STRETCH"] = "classicStretch";
  for (auto id : {"fit", "fill"})
    connect(actions[id], &QAction::triggered, this, [this] { player->set("keepaspect", true); profile.settings.setValue("video/keepAspect", true); actions["classicKeepAspect"]->setChecked(true); });
  extra("classicRendererReset", [this] {
    const auto defaults = VideoDisplay::defaults();
    for (auto it = defaults.begin(); it != defaults.end(); ++it) profile.settings.setValue("video/" + it.key(), it.value());
    VideoDisplay::apply(player, profile.settings);
  });
  ids["ID_VIEW_RESET_DEFAULT"] = "classicRendererReset";

  const QMap<QString, QString> classicKeys{
    {"open", "Ctrl+Q"}, {"openUrl", "Ctrl+O"}, {"openDisc", "Ctrl+D"},
    {"quit", "Alt+X"}, {"closeFile", "Ctrl+C"}, {"snapshot", "Ctrl+F5"},
    {"snapshotQuick", "F5"}, {"snapshotDisplay", "Shift+F5"}, {"snapshotClipboard", ""},
    {"loadSubtitles", "Ctrl+L"}, {"playlist", "Ctrl+7"}, {"mediaProperties", "Shift+F10"},
    {"context", "Menu"}, {"menu", ""}, {"classicCaptionMenu", "Ctrl+0"}, {"fullscreen", "Ctrl+Return"}, {"showTimeline", "Ctrl+1"},
    {"showControls", "Ctrl+2"}, {"showStatus", "Ctrl+5"}, {"classicInformation", "Ctrl+3"}, {"classicStatistics", "Ctrl+4"},
    {"subtitleEditor", "Ctrl+6"}, {"classicShaderEditor", "Ctrl+9"}, {"favoriteAdd", ""},
    {"mute", "Ctrl+M"}, {"renderStatistics", "Ctrl+J"}};
  for (auto it = classicKeys.begin(); it != classicKeys.end(); ++it) defaultKeys[it.key()] = it.value();
  applyShortcuts();
  ids["ID_NAVIGATE_SKIPBACK"] = "previous";
  ids["ID_NAVIGATE_SKIPFORWARD"] = "next";
  const QMap<QString, QString> afterIds{
    {"ID_AFTERPLAYBACK_CLOSE", "afterOnce_exit"}, {"ID_AFTERPLAYBACK_STANDBY", "afterOnce_suspend"},
    {"ID_AFTERPLAYBACK_HIBERNATE", "afterOnce_hibernate"}, {"ID_AFTERPLAYBACK_SHUTDOWN", "afterOnce_shutdown"},
    {"ID_AFTERPLAYBACK_LOGOFF", "afterOnce_logout"}, {"ID_AFTERPLAYBACK_LOCK", "afterOnce_lock"},
    {"ID_AFTERPLAYBACK_DONOTHING", "afterOnce_none"}, {"ID_AFTERPLAYBACK_EXIT", "afterAlways_exit"},
    {"ID_AFTERPLAYBACK_CLOSE_FILE", "afterAlways_close"}, {"ID_AFTERPLAYBACK_CLOSE_FILE_AND_MINIMIZE", "afterAlways_minimize"},
    {"ID_AFTERPLAYBACK_NEXT", "afterAlways_nextDirectory"}, {"ID_AFTERPLAYBACK_NEXT_LOOPED", "afterAlways_loopDirectory"},
    {"ID_AFTERPLAYBACK_EVERYTIMEDONOTHING", "afterAlways_none"}};
  for (auto it = afterIds.begin(); it != afterIds.end(); ++it) {
    auto *a = afterMenu->findChild<QAction *>(it.value());
    if (a) { actions[it.value()] = a; ids[it.key()] = it.value(); }
  }
  auto *language = new QMenu(this);
  for (const auto &entry : QList<QPair<QString, QString>>{{"Русский", "ru"}, {"English", "en"}}) {
    auto *a = language->addAction(entry.first, this, [this, value = entry.second] {
      profile.settings.setValue("appearance/language", value);
      showOsd(uiTr("Язык интерфейса изменится после перезапуска"));
    }); a->setCheckable(true); a->setChecked((english ? "en" : "ru") == entry.second);
  }
  for (const auto &entry : QList<QPair<QString, QString>>{{"ID_SHADERS_1_ENABLE", "MAIN"}, {"ID_SHADERS_2_ENABLE", "SCALED"}}) {
    auto *a = extra(entry.first, [this, stage = entry.second, key = entry.first] {
      auto chain = VideoShaders::read(profile.settings);
      for (auto &pass : chain) if (pass.stage == stage) pass.enabled = actions[key]->isChecked();
      profile.settings.setValue("video/shadersV1", VideoShaders::encode(chain));
      profile.settings.setValue("video/shadersEnabled", true);
      VideoShaders::apply(player, profile.settings);
    });
    a->setCheckable(true); bool active = false;
    for (const auto &pass : VideoShaders::read(profile.settings)) active |= pass.stage == entry.second && pass.enabled;
    a->setChecked(active && profile.settings.value("video/shadersEnabled", false).toBool()); ids[entry.first] = entry.first;
  }
  ids["ID_SHADERS_SELECT"] = "classicShaderEditor";
  auto *videoStreams = new QMenu(this); videoStreams->setObjectName("classicVideoStreamsMenu");
  connect(videoStreams, &QMenu::aboutToShow, this, [this, videoStreams] {
    videoStreams->clear();
    for (const auto &value : player->get("track-list").toList()) {
      const auto track = value.toMap();
      if (track.value("type") != "video" || track.value("albumart").toBool()) continue;
      const int id = track.value("id").toInt();
      auto *a = videoStreams->addAction(QString::number(id) + " — " + track.value("title", track.value("codec")).toString(), this, [this, id] { player->set("vid", id); });
      a->setCheckable(true); a->setChecked(track.value("selected").toBool());
    }
    if (videoStreams->isEmpty()) videoStreams->addAction(uiTr("Видеодорожек нет"))->setEnabled(false);
  });
  // Unavailable Classic aliases remain unbound and are omitted from menus.
  const auto unavailable = english ? QString("Unavailable with the Linux backend")
                                   : QString("Недоступно в Linux-версии");
  auto *onTop = new QActionGroup(this);
  for (auto id : {"ID_ONTOP_NEVER", "ID_ONTOP_ALWAYS", "ID_ONTOP_WHILEPLAYING", "ID_ONTOP_WHILEPLAYINGVIDEO"}) {
    const QString key(id);
    auto *a = extra(key, [this, key] {
      profile.settings.setValue("ui/onTop", key != "ID_ONTOP_NEVER");
      profile.settings.setValue("ui/onTopWhen", key == "ID_ONTOP_WHILEPLAYING" ? "playing" : key == "ID_ONTOP_WHILEPLAYINGVIDEO" ? "video" : "always");
      applyWindowPreferences();
    }); a->setCheckable(true); onTop->addAction(a); ids[key] = key;
  }
  auto updateTop = [this] {
    const auto when = profile.settings.value("ui/onTopWhen", "always").toString();
    const QString id = !profile.settings.value("ui/onTop", false).toBool() ? "ID_ONTOP_NEVER"
      : when == "playing" ? "ID_ONTOP_WHILEPLAYING" : when == "video" ? "ID_ONTOP_WHILEPLAYINGVIDEO" : "ID_ONTOP_ALWAYS";
    actions[id]->setChecked(true);
  };
  updateTop();
  auto *aspectGroup = new QActionGroup(this);
  const QMap<QString, QString> ratios{{"ID_ASPECTRATIO_SOURCE", "no"}, {"ID_ASPECTRATIO_4_3", "4:3"},
    {"ID_ASPECTRATIO_5_4", "5:4"}, {"ID_ASPECTRATIO_16_9", "16:9"},
    {"ID_ASPECTRATIO_235_100", "2.35:1"}, {"ID_ASPECTRATIO_185_100", "1.85:1"}};
  for (auto it = ratios.begin(); it != ratios.end(); ++it) {
    auto *a = extra(it.key(), [this, value = it.value()] {
      player->set("video-aspect-override", value); profile.settings.setValue("video/aspect", value);
      autoSizedGeneration = ~quint64(0); autoSizeVideo();
    }); a->setCheckable(true); a->setChecked(profile.settings.value("video/aspect", "no") == it.value());
    aspectGroup->addAction(a); ids[it.key()] = it.key();
  }
  // Keep Classic command labels and bindings as compatibility aliases. Menu
  // placement is owned by buildOrganizedMenus, independently of the resources.
  QSet<QString> registered;
  std::function<void(const QJsonArray &)> registerCommands;
  registerCommands = [&](const QJsonArray &entries) {
    for (const auto &value : entries) {
      const auto entry = value.toObject();
      if (entry.contains("children")) registerCommands(entry.value("children").toArray());
      const auto id = entry.value("id").toString();
      if (id.isEmpty() || id.startsWith("ID_SUBMENU_") || registered.contains(id)) continue;
      registered.insert(id);
      const auto text = entry.value("text").toString();
      auto *a = new QAction(this); a->setObjectName("classic_" + id);
      a->setProperty("classicCommand", id);
      auto *target = actions.value(ids.value(id));
      if (target) {
        a->setProperty("classicBinding", target->objectName());
        if (target->text().isEmpty()) target->setText(text);
        if (!target->property("classicLabel").isValid()) target->setProperty("classicLabel", text);
        const auto sync = [a, target, text, id] {
          const auto key = target->shortcut().toString(QKeySequence::NativeText).replace("Return", "Enter");
          a->setText((id == "ID_VIEW_CAPTIONMENU" ? target->text() : text) + (key.isEmpty() ? QString() : "\t" + key));
          a->setEnabled(target->isEnabled());
          a->setCheckable(target->isCheckable()); a->setChecked(target->isChecked());
        };
        connect(target, &QAction::changed, a, sync);
        connect(a, &QAction::triggered, target, &QAction::trigger);
        sync();
      } else {
        a->setText(text); a->setEnabled(false); a->setToolTip(unavailable);
      }
    }
  };
  for (auto it = reference.begin(); it != reference.end(); ++it)
    if (it.value().isArray()) registerCommands(it.value().toArray());
  buildOrganizedMenus(videoStreams, language);
  for (auto *menu : contextMenu->findChildren<QMenu *>()) {
    connect(menu, &QMenu::aboutToShow, this, updateTop);
    connect(menu, &QMenu::aboutToShow, this, updateCaption);
  }
}
