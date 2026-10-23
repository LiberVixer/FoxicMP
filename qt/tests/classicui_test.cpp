#include "preferencesdriver.h"
#include "window.h"
#include "player.h"
#include "onlineoptions.h"
#include "classicpreferences.h"
#include "timeline.h"
#include "playlistpanel.h"
#include <QtTest>
#include <QtWidgets>
#include <QTcpServer>
#include <QScopeGuard>

class ClassicUiTest : public QObject {
  Q_OBJECT
  QJsonObject reference, report;
  QString output;
  void prepare(Profile &p, bool english = false) {
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("ui/showMenu", false);
    p.settings.setValue("audio/mute", true);
    p.settings.setValue("input/mpris", false);
    p.settings.setValue("playback/hardwareDecoding", false);
    p.settings.setValue("video/output", "software");
    p.settings.setValue("appearance/language", english ? "en" : "ru");
  }
  static QString caption(QAction *a) { return a->text().section('\t', 0, 0); }
  QJsonArray dump(QMenu *menu) {
    QJsonArray rows;
    for (auto *a : menu->actions()) {
      QJsonObject row{{"text", caption(a)}, {"separator", a->isSeparator()},
                      {"enabled", a->isEnabled()}, {"checked", a->isChecked()},
                      {"command", a->property("classicCommand").toString()},
                      {"binding", a->property("classicBinding").toString()}};
      if (a->menu()) row["children"] = dump(a->menu());
      rows.append(row);
    }
    return rows;
  }
  static bool contains(QMenu *menu, QAction *action) {
    for (auto *entry : menu->actions())
      if (entry == action || (entry->menu() && contains(entry->menu(), action))) return true;
    return false;
  }
  void checkMenu(QMenu *menu) {
    QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
    QVERIFY(!menu->actions().isEmpty());
    QVERIFY(!menu->actions().first()->isSeparator());
    QVERIFY(!menu->actions().last()->isSeparator());
    bool separator = false;
    for (auto *action : menu->actions()) {
      QVERIFY(!(separator && action->isSeparator()));
      separator = action->isSeparator();
      if (auto *sub = action->menu()) checkMenu(sub);
      else QVERIFY2(action->toolTip() != "Недоступно в Linux-версии" && action->toolTip() != "Unavailable with the Linux backend", qPrintable(action->text()));
    }
  }
private slots:
  void initTestCase() {
    Q_INIT_RESOURCE(resources);
    qApp->setStyle("Fusion");
    QFile file(":/classic-menus.json"); QVERIFY(file.open(QIODevice::ReadOnly));
    reference = QJsonDocument::fromJson(file.readAll()).object();
    output = qEnvironmentVariable("FOXICMP_CLASSIC_REPORT_DIR");
    if (!output.isEmpty()) QVERIFY(QDir().mkpath(output));
  }
  void menus_data() { QTest::addColumn<bool>("english"); QTest::newRow("Russian") << false; QTest::newRow("English") << true; }
  void menus() {
    QFETCH(bool, english);
    QTemporaryDir temp; Profile p(temp.path()); prepare(p, english);
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    const QStringList labels = english
      ? QStringList{"File", "Playback", "Video", "Audio", "Subtitles", "Library", "View", "Tools", "Help"}
      : QStringList{"Файл", "Воспроизведение", "Видео", "Аудио", "Субтитры", "Библиотека", "Вид", "Сервис", "Справка"};
    const QStringList ids{"organizedFileMenu", "organizedPlaybackMenu", "organizedVideoMenu", "organizedAudioMenu",
      "organizedSubtitlesMenu", "organizedLibraryMenu", "organizedViewMenu", "organizedServiceMenu", "organizedHelpMenu"};
    QCOMPARE(w.menuBar()->actions().size(), labels.size());
    auto *context = w.findChild<QMenu *>("videoContextMenu"); QVERIFY(context);
    QCOMPARE(context->actions().size(), 18);
    QVERIFY(!w.findChild<QMenu *>("classicAdditionalMenu"));
    QJsonArray menus;
    for (int i = 0; i < labels.size(); ++i) {
      auto *a = w.menuBar()->actions()[i];
      QCOMPARE(caption(a), labels[i]); QVERIFY(a->menu());
      QCOMPARE(a->menu()->objectName(), ids[i]);
      QCOMPARE(context->actions()[i + 5], a); // Identical menu, not a copied hierarchy.
      checkMenu(a->menu());
      menus.append(QJsonObject{{"title", caption(a)}, {"children", dump(a->menu())}});
    }
    const QMap<QString, QStringList> placement{
      {"organizedFileMenu", {"open", "openUrl", "openClipboard", "saveMediaCopy", "mediaProperties", "quit"}},
      {"organizedPlaybackMenu", {"play", "stop", "previous", "next", "goto", "chapterNext", "frameBack", "speedReset", "repeatOne", "abDialog"}},
      {"organizedVideoMenu", {"fit", "fill", "zoomIn", "panLeft", "rotateClockwise", "mirrorVertical", "transformDialog", "videoOutputSettings", "classicShaderEditor", "stereoOutput_arcd"}},
      {"organizedAudioMenu", {"externalAudio", "audioNext", "mute", "volumeUp", "audioDelayDialog", "audioProcessingSettings"}},
      {"organizedSubtitlesMenu", {"loadSubtitles", "subtitleToggle", "subtitleNext", "subtitleDelayDialog", "subtitleEditor", "subtitleStyleSettings", "subtitleCopy", "subtitleReload"}},
      {"organizedLibraryMenu", {"playlist", "playlistAdd", "playlistSort_name", "playlistNewTab", "shuffle", "historyManage", "favoriteAdd"}},
      {"organizedViewMenu", {"fullscreen", "size100", "classicAutoFit", "frameless", "showTimeline", "classicInformation", "window_compact"}},
      {"organizedServiceMenu", {"snapshot", "snapshotQuick", "thumbnailSheet", "captureSettings", "webControl", "profileManagement", "associations", "maintenance", "renderStatistics", "preferences"}},
      {"organizedHelpMenu", {"classicCommandLine", "checkUpdates", "classicHome", "classicAbout"}}
    };
    for (auto it = placement.begin(); it != placement.end(); ++it) {
      auto *menu = w.findChild<QMenu *>(it.key()); QVERIFY(menu);
      for (const auto &id : it.value()) {
        auto *action = w.findChild<QAction *>("action_" + id); QVERIFY(action);
        QVERIFY2(contains(menu, action), qPrintable(it.key() + ": " + id));
      }
    }
    QCOMPARE(context->actions()[0], w.findChild<QAction *>("action_play"));
    QCOMPARE(context->actions()[17], w.findChild<QAction *>("action_quit"));
    const auto initial = context->actions();
    for (bool show : {true, false}) {
      w.menuBar()->setVisible(show);
      QMetaObject::invokeMethod(context, "aboutToShow", Qt::DirectConnection);
      QCOMPARE(context->actions(), initial);
    }
    w.findChild<QAction *>("action_fullscreen")->trigger(); QTRY_VERIFY(w.isFullScreen());
    QMetaObject::invokeMethod(context, "aboutToShow", Qt::DirectConnection);
    QCOMPARE(context->actions(), initial);
    w.findChild<QAction *>("action_fullscreen")->trigger(); QTRY_VERIFY(!w.isFullScreen());
    report[english ? "englishMenus" : "russianMenus"] = menus;
    report[english ? "englishContext" : "russianContext"] = dump(context);
    if (!output.isEmpty() && !english) {
      w.menuBar()->show();
      for (int i = 0; i < w.menuBar()->actions().size(); ++i) {
        auto *menu = w.menuBar()->actions()[i]->menu();
        menu->popup(w.mapToGlobal(QPoint(10, 30))); QTest::qWait(80);
        QVERIFY(menu->grab().save(output + QString("/menu-%1.png").arg(i))); menu->hide();
      }
      QVERIFY(w.grab().save(output + "/main.png"));
      context->popup(w.mapToGlobal(QPoint(40, 40))); QTest::qWait(80);
      QVERIFY(context->grab().save(output + "/context.png")); context->hide();
    }
    w.close();
  }
  void menuPreferenceLinks() {
    QTemporaryDir temp; Profile p(temp.path()); prepare(p);
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    const QMap<QString, QString> pages{{"menuVideoColor", "IDD_PPAGECOLOR"},
      {"menuAudioOptions", "IDD_PPAGEAUDIO"}, {"menuSubtitlePlacement", "settingsSubtitlePlacement"},
      {"menuShortcuts", "IDD_PPAGEACCELTBL"}};
    for (auto it = pages.begin(); it != pages.end(); ++it) {
      auto *action = w.findChild<QAction *>("action_" + it.key()); QVERIFY(action);
      bool visited = false;
      afterPreferencesReady(&w, [&] {
        auto *dialog = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(dialog);
        auto *tabs = dialog->findChild<QTabWidget *>("classicPreferencesPages"); QVERIFY(tabs);
        const auto close = qScopeGuard([&] { dialog->reject(); });
        QCOMPARE(tabs->currentWidget()->objectName(), it.value());
        visited = true;
      });
      action->trigger(); QVERIFY(visited);
      QVERIFY(!w.property("foxicmpPreferencePage").isValid());
    }
    w.close();
  }
  void sharedActionsAndState() {
    QTemporaryDir temp; Profile p(temp.path()); prepare(p);
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *up = w.findChild<QAction *>("classic_ID_VOLUME_UP");
    auto *volume = w.findChild<QAction *>("action_volumeUp"); QVERIFY(up); QVERIFY(volume);
    QSignalSpy triggered(volume, &QAction::triggered);
    up->trigger(); QCOMPARE(triggered.size(), 1);
    volume->setEnabled(false); QVERIFY(!up->isEnabled());
    volume->setShortcut(QKeySequence("Ctrl+Alt+F8")); QVERIFY(up->text().contains("Ctrl+Alt+F8"));
    QVERIFY(up->shortcut().isEmpty()); // Display shortcut without a second shortcut registration.
    auto *mute = w.findChild<QAction *>("classic_ID_VOLUME_MUTE"); QVERIFY(mute); QTRY_VERIFY(mute->isChecked());
    mute->trigger(); QTRY_VERIFY(!w.findChild<Player *>()->get("mute").toBool());
    QTRY_VERIFY(!mute->isChecked());
    for (auto id : {"classic_ID_VIEW_EXCLUSIVE_FULLSCREEN", "classic_ID_NAVIGATE_ROOTMENU"}) {
      auto *a = w.findChild<QAction *>(id); QVERIFY(a); QVERIFY(!a->isEnabled()); QVERIFY(!a->toolTip().isEmpty());
    }
    w.close();
  }
  void savedCustomShortcutsTakePriorityOverNewDefaults() {
    QTemporaryDir temp; Profile p(temp.path()); prepare(p);
    p.settings.setValue("keys/menu", "Ctrl+0");
    p.settings.setValue("keys/favoriteAdd", "F11");
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    QCOMPARE(w.findChild<QAction *>("action_menu")->shortcut(), QKeySequence("Ctrl+0"));
    QVERIFY(w.findChild<QAction *>("action_classicCaptionMenu")->shortcut().isEmpty());
    QVERIFY(!w.findChild<QAction *>("action_fullscreen")->shortcuts().contains(QKeySequence("F11")));
    w.close();
  }
  void captionMenuCyclesFourClassicModes() {
    QTemporaryDir temp; Profile p(temp.path()); prepare(p);
    p.settings.remove("ui/showMenu");
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    QVERIFY(w.menuBar()->isVisible());
    auto *a = w.findChild<QAction *>("action_classicCaptionMenu"); QVERIFY(a);
    const auto labels = reference.value("ru").toObject().value("captionModes").toArray();
    for (int i = 0; i < 4; ++i) {
      QCOMPARE(a->text(), labels[i].toString());
      a->trigger(); QCoreApplication::processEvents();
      QCOMPARE(w.menuBar()->isVisible(), i == 3);
      QCOMPARE(w.windowFlags().testFlag(Qt::FramelessWindowHint), i == 2);
      if (i == 1) QVERIFY(!w.windowFlags().testFlag(Qt::WindowTitleHint));
    }
    w.close();
  }
  void classicPanelsFollowVisibilityAndShortcuts() {
    QTemporaryDir temp; Profile p(temp.path()); prepare(p);
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *info = w.findChild<QAction *>("classic_ID_VIEW_INFORMATION");
    auto *stats = w.findChild<QAction *>("classic_ID_VIEW_STATISTICS");
    auto *panel = w.findChild<QFrame *>("classicInformationPanel");
    auto *statsPanel = w.findChild<QFrame *>("classicStatisticsPanel");
    QVERIFY(info); QVERIFY(stats); QVERIFY(panel); QVERIFY(statsPanel);
    QVERIFY(!panel->isVisible()); QVERIFY(!statsPanel->isVisible());
    QVERIFY(info->text().contains("Ctrl+3")); QVERIFY(stats->text().contains("Ctrl+4"));
    auto *clock = w.findChild<QLabel *>("playbackClock"); QVERIFY(clock);
    QVERIFY(!clock->text().contains(" / "));
    QTest::mouseClick(clock, Qt::LeftButton); QVERIFY(p.settings.value("ui/remainingTime").toBool());
    QContextMenuEvent clockMenu(QContextMenuEvent::Mouse, clock->rect().center(), clock->mapToGlobal(clock->rect().center()));
    QApplication::sendEvent(clock, &clockMenu);
    auto *milliseconds = w.findChild<QAction *>("classicClockMilliseconds"); QVERIFY(milliseconds);
    milliseconds->trigger(); QVERIFY(p.settings.value("ui/showMilliseconds").toBool());
    QVERIFY(clock->text().isEmpty()); // No timer without an open file.
    w.findChild<QMenu *>("classicClockMenu")->close();
    info->trigger(); stats->trigger();
    QVERIFY(panel->isVisible()); QVERIFY(statsPanel->isVisible());
    QVERIFY(info->isChecked()); QVERIFY(stats->isChecked());
    w.findChild<QAction *>("action_fullscreen")->trigger(); QTRY_VERIFY(w.isFullScreen());
    QVERIFY(!panel->isVisible()); QVERIFY(!statsPanel->isVisible());
    w.findChild<QAction *>("action_fullscreen")->trigger(); QTRY_VERIFY(!w.isFullScreen());
    QVERIFY(panel->isVisible()); QVERIFY(statsPanel->isVisible());
    info->trigger(); QVERIFY(!panel->isVisible()); QVERIFY(!info->isChecked());
    QVERIFY(!p.settings.value("ui/showInformation").toBool());
    w.close();
  }
  void nestedSettingsToolsRemainUsable() {
    QTemporaryDir temp; Profile p(temp.path()); prepare(p);
    const auto previousVersion = QCoreApplication::applicationVersion();
    QCoreApplication::setApplicationVersion("ui-test-version");
    QCOMPARE(OnlineOptions::mpvOptions(p.settings).value("user-agent"), QString("FoxicMPQt/ui-test-version"));
    QVERIFY(OnlineOptions::playlistArguments(p.settings, "https://example.com").contains("FoxicMPQt/ui-test-version"));
    const auto restoreVersion = qScopeGuard([previousVersion] { QCoreApplication::setApplicationVersion(previousVersion); });
    QTcpServer probe; QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const int port = probe.serverPort(); probe.close();
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    bool seen = false;
    afterPreferencesReady(&w, [&] {
      auto *dialog = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(dialog);
      const auto closeOnExit = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
      auto *tabs = dialog->findChild<QTabWidget *>("classicPreferencesPages"); QVERIFY(tabs);
      auto *openWeb = dialog->findChild<QPushButton *>("classicWebDetails"); QVERIFY(openWeb);
      ClassicPreferences::selectPage(tabs, openWeb); openWeb->click();
      auto *web = dialog->findChild<QDialog *>("webControlDialog"); QVERIFY(web);
      QCOMPARE(web->parentWidget(), dialog); QVERIFY(web->isModal());
      QTRY_COMPARE(QApplication::activeModalWidget(), web);
      web->findChild<QSpinBox *>("webPort")->setValue(port);
      web->findChild<QPushButton *>("webStart")->click();
      QVERIFY(p.settings.value("web/enabled").toBool());
      web->close(); QCoreApplication::processEvents();
      QVERIFY(dialog->findChild<QCheckBox *>("classicWebEnabled")->isChecked());
      QCOMPARE(dialog->findChild<QSpinBox *>("classicWebPort")->value(), port);
      auto *openMaintenance = dialog->findChild<QPushButton *>("classicMaintenanceDetails"); QVERIFY(openMaintenance);
      ClassicPreferences::selectPage(tabs, openMaintenance); openMaintenance->click();
      auto *maintenance = dialog->findChild<QDialog *>("maintenanceDialog"); QVERIFY(maintenance);
      QCOMPARE(maintenance->parentWidget(), dialog); QVERIFY(maintenance->isModal());
      QTRY_COMPARE(QApplication::activeModalWidget(), maintenance);
      QVERIFY(maintenance->findChild<QLabel *>("maintenanceVersion")->text().contains("ui-test-version"));
      maintenance->close(); QCoreApplication::processEvents();
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
      QVERIFY(p.settings.value("web/enabled").toBool()); QCOMPARE(p.settings.value("web/port").toInt(), port);
      dialog->reject(); seen = true;
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QVERIFY(seen); w.close();
  }
  void logoModesPreserveDraftsAndLegacyPaths() {
    QTemporaryDir temp; Profile p(temp.path()); prepare(p);
    QImage externalImage(100, 50, QImage::Format_RGB32); externalImage.fill(Qt::green);
    QVERIFY(externalImage.save(temp.filePath("custom.png")));
    p.settings.setValue("appearance/logoPath", "custom.png");
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    QCOMPARE(w.findChild<QLabel *>("playerLogo")->pixmap().toImage().pixelColor(50, 25), QColor(Qt::green));
    bool seen = false;
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(d);
      const auto closeOnExit = qScopeGuard([d] { if (d->isVisible()) d->reject(); });
      auto *tabs = d->findChild<QTabWidget *>("classicPreferencesPages");
      auto *internal = d->findChild<QRadioButton *>("classicLogoInternal");
      auto *external = d->findChild<QRadioButton *>("classicLogoExternal");
      auto *preview = d->findChild<QLabel *>("classicLogoPreview");
      auto *choice = d->findChild<QComboBox *>("appearance/logoId");
      auto *previous = d->findChild<QToolButton *>("classicLogoPrevious");
      auto *next = d->findChild<QToolButton *>("classicLogoNext");
      auto *buttons = d->findChild<QDialogButtonBox *>();
      QVERIFY(tabs); QVERIFY(internal); QVERIFY(external); QVERIFY(preview); QVERIFY(choice); QVERIFY(previous); QVERIFY(next);
      ClassicPreferences::selectPage(tabs, preview);
      QVERIFY(external->isChecked()); QVERIFY(!previous->isEnabled());
      QCOMPARE(preview->pixmap().toImage().pixelColor(50, 25), QColor(Qt::green));
      internal->click(); QVERIFY(previous->isEnabled());
      QVERIFY(buttons->button(QDialogButtonBox::Apply)->isEnabled());
      QCOMPARE(preview->pixmap().size(), QSize(256, 256));
      previous->click(); QCOMPARE(choice->currentData().toInt(), 0);
      QVERIFY(preview->pixmap().isNull());
      QVERIFY(!p.settings.contains("appearance/logoId"));
      buttons->button(QDialogButtonBox::Apply)->click();
      QVERIFY(!p.settings.value("appearance/logoExternal").toBool());
      QCOMPARE(p.settings.value("appearance/logoPath").toString(), QString("custom.png"));
      QVERIFY(w.findChild<QLabel *>("playerLogo")->pixmap().isNull());
      next->click(); QCOMPARE(choice->currentData().toInt(), 1);
      buttons->button(QDialogButtonBox::Apply)->click();
      QCOMPARE(w.findChild<QLabel *>("playerLogo")->pixmap().size(), QSize(256, 256));
      QTest::qWait(30);
      auto *author = d->findChild<QLabel *>("classicLogoAuthor"); QVERIFY(author);
      QVERIFY(author->width() >= author->fontMetrics().horizontalAdvance(author->text()));
      if (!output.isEmpty()) QVERIFY(d->grab().save(output + "/logo-refined.png"));
      external->click();
      QCOMPARE(preview->pixmap().toImage().pixelColor(50, 25), QColor(Qt::green));
      d->reject(); seen = true;
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QVERIFY(seen);
    QVERIFY(!p.settings.value("appearance/logoExternal").toBool());
    QCOMPARE(p.settings.value("appearance/logoId").toInt(), 1);
    seen = false;
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(d);
      const auto closeOnExit = qScopeGuard([d] { if (d->isVisible()) d->reject(); });
      QVERIFY(d->findChild<QRadioButton *>("classicLogoInternal")->isChecked());
      QCOMPARE(d->findChild<QLineEdit *>("appearance/logoPath")->text(), QString("custom.png"));
      d->reject(); seen = true;
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QVERIFY(seen); w.close();
  }
  void colorSlidersApplyResetAndCancel() {
    QTemporaryDir temp; Profile p(temp.path()); prepare(p);
    p.settings.setValue("video/contrast", 20);
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    bool seen = false;
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(d);
      const auto closeOnExit = qScopeGuard([d] { if (d->isVisible()) d->reject(); });
      auto *tabs = d->findChild<QTabWidget *>("classicPreferencesPages");
      auto *buttons = d->findChild<QDialogButtonBox *>();
      auto *brightness = d->findChild<QSlider *>("video/brightnessSlider"); QVERIFY(brightness);
      ClassicPreferences::selectPage(tabs, brightness);
      auto *page = qobject_cast<QScrollArea *>(tabs->currentWidget()); QVERIFY(page);
      QCOMPARE(page->objectName(), QString("IDD_PPAGECOLOR"));
      QCOMPARE(page->horizontalScrollBar()->maximum(), 0);
      for (const auto &key : {"video/brightness", "video/contrast", "video/hue", "video/saturation"}) {
        auto *number = d->findChild<QSpinBox *>(key);
        auto *slider = d->findChild<QSlider *>(QString(key) + "Slider");
        QVERIFY(number); QVERIFY(slider);
        QCOMPARE(number->value(), slider->value());
        number->setValue(-25); QCOMPARE(slider->value(), -25);
        slider->setValue(40); QCOMPARE(number->value(), 40);
      }
      QCOMPARE(p.settings.value("video/contrast").toInt(), 20);
      buttons->button(QDialogButtonBox::Apply)->click();
      QCOMPARE(p.settings.value("video/contrast").toInt(), 40);
      if (!output.isEmpty()) QVERIFY(d->grab().save(output + "/color-refined.png"));
      auto *reset = d->findChild<QPushButton *>("classicColorReset"); QVERIFY(reset); reset->click();
      QCOMPARE(brightness->value(), 0);
      QCOMPARE(p.settings.value("video/contrast").toInt(), 40);
      d->reject(); seen = true;
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QVERIFY(seen);
    QCOMPARE(p.settings.value("video/contrast").toInt(), 40);
    seen = false;
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(d);
      const auto closeOnExit = qScopeGuard([d] { if (d->isVisible()) d->reject(); });
      auto *reset = d->findChild<QPushButton *>("classicColorReset"); QVERIFY(reset);
      ClassicPreferences::selectPage(d->findChild<QTabWidget *>("classicPreferencesPages"), reset);
      reset->click(); d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click(); seen = true;
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QVERIFY(seen);
    QCOMPARE(p.settings.value("video/contrast").toInt(), 0); w.close();
  }
  void interfaceGroupsApplyCancelAndDependencies_data() {
    QTest::addColumn<bool>("english");
    QTest::newRow("Russian") << false;
    QTest::newRow("English") << true;
  }
  void interfaceGroupsApplyCancelAndDependencies() {
    QFETCH(bool, english);
    QTemporaryDir temp; Profile p(temp.path()); prepare(p, english);
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    const QColor initialColor = qApp->palette().color(QPalette::Window);
    bool seen = false;
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(d);
      const auto closeOnExit = qScopeGuard([d] { if (d->isVisible()) d->reject(); });
      auto *tabs = d->findChild<QTabWidget *>("classicPreferencesPages"); QVERIFY(tabs);
      auto *dark = d->findChild<QCheckBox *>("classicDarkTheme"); QVERIFY(dark);
      ClassicPreferences::selectPage(tabs, dark); QCoreApplication::processEvents();
      auto *page = qobject_cast<QScrollArea *>(tabs->currentWidget()); QVERIFY(page);
      QCOMPARE(page->objectName(), QString("IDD_PPAGEINTERFACE"));
      QCOMPARE(page->horizontalScrollBar()->maximum(), 0);
      QCOMPARE(page->verticalScrollBar()->maximum(), 0);
      auto *theme = d->findChild<QComboBox *>("appearance/theme"); QVERIFY(theme);
      auto *brightness = d->findChild<QSlider *>("appearance/themeBrightness"); QVERIFY(brightness);
      auto *red = d->findChild<QSlider *>("appearance/themeRed"); QVERIFY(red);
      auto *previews = d->findChild<QCheckBox *>("ui/previews"); QVERIFY(previews);
      auto *time = d->findChild<QCheckBox *>("ui/timeTooltip"); QVERIFY(time);
      auto *position = d->findChild<QComboBox *>("ui/timeTooltipBelow"); QVERIFY(position);
      auto *online = d->findChild<QCheckBox *>("preview/online"); QVERIFY(online);
      auto *previewSize = d->findChild<QSpinBox *>("preview/screenPercent"); QVERIFY(previewSize);
      auto *fontPercent = d->findChild<QSpinBox *>("appearance/playlistFontPercent"); QVERIFY(fontPercent);
      auto *chapters = d->findChild<QCheckBox *>("ui/chapterMarkers"); QVERIFY(chapters);
      auto *fill = d->findChild<QLineEdit *>("appearance/volumeFill"); QVERIFY(fill);
      auto *reset = d->findChild<QPushButton *>("classicVolumeColorsReset"); QVERIFY(reset);
      auto *buttons = d->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
      QVERIFY(!buttons->button(QDialogButtonBox::Apply)->isEnabled());
      brightness->setValue(30);
      QVERIFY(buttons->button(QDialogButtonBox::Apply)->isEnabled());
      QVERIFY(dark->isChecked()); QVERIFY(!time->isEnabled()); QVERIFY(online->isEnabled());
      previews->click(); QVERIFY(time->isEnabled()); QVERIFY(position->isEnabled());
      QVERIFY(!online->isEnabled()); QVERIFY(!previewSize->isEnabled());
      time->click(); QVERIFY(!position->isEnabled()); time->click();
      position->setCurrentIndex(1); chapters->click();
      dark->click(); QCOMPARE(theme->currentData().toString(), QString("light")); QVERIFY(!brightness->isEnabled());
      theme->setCurrentIndex(theme->findData("dark")); QVERIFY(dark->isChecked()); QVERIFY(brightness->isEnabled());
      brightness->setValue(30); red->setValue(128); fill->setText("#12ab34"); fontPercent->setValue(125);
      QCOMPARE(qApp->palette().color(QPalette::Window), initialColor);
      QVERIFY(!p.settings.contains("appearance/volumeFill"));
      buttons->button(QDialogButtonBox::Apply)->click();
      QCOMPARE(qApp->palette().color(QPalette::Window), QColor(28, 61, 65));
      QCOMPARE(p.settings.value("appearance/volumeFill").toString(), QString("#12ab34"));
      QVERIFY(w.findChild<QSlider *>("volumeSlider")->styleSheet().contains("#12ab34"));
      auto *timeline = w.findChild<Timeline *>(); QVERIFY(timeline);
      QVERIFY(!timeline->previewEnabled); QVERIFY(timeline->timeTooltipEnabled);
      QVERIFY(timeline->tooltipBelow); QVERIFY(!timeline->chapterMarkers);
      auto *panel = w.findChild<PlaylistPanel *>(); QVERIFY(panel);
      QCOMPARE(panel->activeList()->font().pointSizeF(), qApp->font().pointSizeF() * 1.25);
      QCOMPARE(panel->createList("Scaled")->font().pointSizeF(), qApp->font().pointSizeF() * 1.25);
      QTest::qWait(80); // Finish stylesheet repolishing before capturing checkbox indicators.
      if (!output.isEmpty()) QVERIFY(d->grab().save(output +
          (english ? "/interface-refined-en.png" : "/interface-refined.png")));
      auto *details = d->findChild<QToolButton *>("classicInterfaceDetails"); QVERIFY(details);
      QVERIFY(!theme->isVisible()); details->click(); QVERIFY(theme->isVisible());
      fill->setText("#ff0000"); reset->click(); QCOMPARE(fill->text(), QString("#ffffff"));
      dark->click(); chapters->click(); d->reject(); seen = true;
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QVERIFY(seen);
    QCOMPARE(p.settings.value("appearance/theme").toString(), QString("dark"));
    QCOMPARE(p.settings.value("appearance/volumeFill").toString(), QString("#12ab34"));
    QVERIFY(!w.findChild<Timeline *>()->chapterMarkers);
    seen = false;
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(d);
      const auto closeOnExit = qScopeGuard([d] { if (d->isVisible()) d->reject(); });
      QCOMPARE(d->findChild<QSlider *>("appearance/themeBrightness")->value(), 30);
      QCOMPARE(d->findChild<QSlider *>("appearance/themeRed")->value(), 128);
      QCOMPARE(d->findChild<QLineEdit *>("appearance/volumeFill")->text(), QString("#12ab34"));
      QVERIFY(!d->findChild<QCheckBox *>("ui/chapterMarkers")->isChecked());
      d->reject(); seen = true;
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QVERIFY(seen); w.close();
  }
  void windowSizeRadiosCurrentDimensionsAndDrafts() {
    QTemporaryDir temp; Profile p(temp.path()); prepare(p);
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    bool seen = false;
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(d);
      const auto closeOnExit = qScopeGuard([d] { if (d->isVisible()) d->reject(); });
      auto *tabs = d->findChild<QTabWidget *>("classicPreferencesPages"); QVERIFY(tabs);
      auto *startup = d->findChild<QComboBox *>("ui/startSize"); QVERIFY(startup);
      auto *width = d->findChild<QSpinBox *>("ui/startWidth"); QVERIFY(width);
      auto *height = d->findChild<QSpinBox *>("ui/startHeight"); QVERIFY(height);
      auto *current = d->findChild<QPushButton *>("classicCurrentWindowSize"); QVERIFY(current);
      auto *standard = d->findChild<QRadioButton *>("classicStartup_default"); QVERIFY(standard);
      auto *remember = d->findChild<QRadioButton *>("classicStartup_remember"); QVERIFY(remember);
      auto *fixed = d->findChild<QRadioButton *>("classicStartup_window"); QVERIFY(fixed);
      auto *video = d->findChild<QRadioButton *>("classicStartup_video"); QVERIFY(video);
      ClassicPreferences::selectPage(tabs, current); QCoreApplication::processEvents();
      auto *page = qobject_cast<QScrollArea *>(tabs->currentWidget()); QVERIFY(page);
      QCOMPARE(page->objectName(), QString("IDD_PPAGEWINDOWSIZE"));
      QCOMPARE(page->horizontalScrollBar()->maximum(), 0);
      QCOMPARE(page->verticalScrollBar()->maximum(), 0);
      QVERIFY(remember->isChecked()); QVERIFY(!current->isEnabled());
      QVERIFY(d->findChild<QCheckBox *>("ui/rememberGeometry")->isChecked());
      QVERIFY(!d->findChild<QCheckBox *>("ui/keepVideoAspect")->isChecked());
      standard->click(); QCOMPARE(startup->currentData().toString(), QString("default"));
      fixed->click(); QVERIFY(current->isEnabled()); current->click();
      QCOMPARE(QSize(width->value(), height->value()), w.size());
      video->click(); current->click();
      const auto area = w.findChild<Player *>()->size();
      QCOMPARE(QSize(width->value(), height->value()), area.expandedTo(QSize(240, 135)));
      auto *scale = d->findChild<QComboBox *>("ui/autoScale"); QVERIFY(scale);
      auto *percent = d->findChild<QComboBox *>("classicAutoScalePercent"); QVERIFY(percent);
      auto *off = d->findChild<QRadioButton *>("classicAutoScaleOff"); QVERIFY(off);
      auto *on = d->findChild<QRadioButton *>("classicAutoScaleOn"); QVERIFY(on);
      QVERIFY(off->isChecked()); QVERIFY(!percent->isEnabled());
      on->click(); QVERIFY(percent->isEnabled()); QCOMPARE(scale->currentData().toInt(), 100);
      percent->setCurrentIndex(percent->findData(200)); QCOMPARE(scale->currentData().toInt(), 200);
      scale->setCurrentIndex(0); QVERIFY(off->isChecked()); QVERIFY(!percent->isEnabled());
      scale->setCurrentIndex(scale->findData(50)); QVERIFY(on->isChecked()); QCOMPARE(percent->currentData().toInt(), 50);
      auto *buttons = d->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
      buttons->button(QDialogButtonBox::Apply)->click();
      QCOMPARE(p.settings.value("ui/startSize").toString(), QString("video"));
      QCOMPARE(p.settings.value("ui/autoScale").toInt(), 50);
      if (!output.isEmpty()) QVERIFY(d->grab().save(output + "/window-size-refined.png"));
      auto *details = d->findChild<QToolButton *>("classicWindowDetails"); QVERIFY(details);
      details->click(); QVERIFY(d->findChild<QComboBox *>("ui/frameStyle")->isVisible());
      standard->click(); off->click(); d->reject(); seen = true;
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QVERIFY(seen);
    QCOMPARE(p.settings.value("ui/startSize").toString(), QString("video"));
    QCOMPARE(p.settings.value("ui/autoScale").toInt(), 50);
    seen = false;
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(d);
      const auto closeOnExit = qScopeGuard([d] { if (d->isVisible()) d->reject(); });
      QVERIFY(d->findChild<QRadioButton *>("classicStartup_video")->isChecked());
      QCOMPARE(d->findChild<QComboBox *>("classicAutoScalePercent")->currentData().toInt(), 50);
      d->reject(); seen = true;
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QVERIFY(seen); w.close();
  }
  void loadingAppearsBeforePagesAndCanCancel() {
    QTemporaryDir temp; Profile p(temp.path()); prepare(p);
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    QVariantMap before;
    for (const auto &key : p.settings.allKeys()) before[key] = p.settings.value(key);
    w.setProperty("foxicmpPreferencePage", "videoIccPage");
    QElapsedTimer elapsed; elapsed.start();
    bool seen = false;
    QTimer::singleShot(0, &w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(d);
      QVERIFY(d->isVisible()); QVERIFY(d->property("settingsLoading").toBool());
      QVERIFY(!d->property("settingsReady").toBool()); QVERIFY(elapsed.elapsed() < 1000);
      QVERIFY(!d->findChild<QTreeWidget *>("classicPreferencesTree"));
      auto *spinner = d->findChild<QWidget *>("settingsLoadingSpinner"); QVERIFY(spinner);
      const auto frame = spinner->grab().toImage();
      if (!output.isEmpty()) QVERIFY(d->grab().save(output + "/settings-loading.png"));
      QTimer::singleShot(110, d, [&, d, frame] {
        if (d->property("settingsLoading").toBool()) {
          auto *indicator = d->findChild<QWidget *>("settingsLoadingSpinner"); QVERIFY(indicator);
          QVERIFY(indicator->grab().toImage() != frame);
          d->findChild<QPushButton *>("settingsCancelLoading")->click();
        } else d->reject();
        seen = true;
      });
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QVERIFY(seen);
    QVariantMap after;
    for (const auto &key : p.settings.allKeys()) after[key] = p.settings.value(key);
    QCOMPARE(after, before); QVERIFY(!w.findChild<QDialog *>("preferencesDialog"));
    QVERIFY(w.property("foxicmpPreferencePage").toString().isEmpty());
    seen = false;
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(d);
      QVERIFY(!d->property("settingsLoading").toBool());
      QVERIFY(d->findChild<QTreeWidget *>("classicPreferencesTree")->isVisible());
      QVERIFY(d->findChild<QTabWidget *>("classicPreferencesPages")->tabBar()->isVisible());
      d->reject(); seen = true;
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QVERIFY(seen); w.close();
  }
  void categoriesSearchAndDrafts_data() {
    QTest::addColumn<bool>("english");
    QTest::newRow("Russian") << false;
    QTest::newRow("English") << true;
  }
  void categoriesSearchAndDrafts() {
    QFETCH(bool, english);
    QTemporaryDir temp; Profile p(temp.path()); prepare(p, english);
    p.settings.setValue("capture/quality", 90);
    p.settings.setValue("audio/normalize", false);
    p.settings.setValue("ui/lastPreferencePage", "videoIccPage");
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    int seen = 0;
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(d);
      const auto closeOnExit = qScopeGuard([d] { if (d->isVisible()) d->reject(); });
      auto *tabs = d->findChild<QTabWidget *>("classicPreferencesPages");
      auto *tree = d->findChild<QTreeWidget *>("classicPreferencesTree");
      auto *search = d->findChild<QLineEdit *>("settingsSearch");
      auto *results = d->findChild<QListWidget *>("settingsSearchResults");
      auto *buttons = d->findChild<QDialogButtonBox *>();
      QVERIFY(tabs); QVERIFY(tree); QVERIFY(search); QVERIFY(results); QVERIFY(buttons);
      d->activateWindow(); QApplication::setActiveWindow(d);
      auto *apply = buttons->button(QDialogButtonBox::Apply);
      auto *icc = d->findChild<QLineEdit *>("video/iccProfile"); QVERIFY(icc);
      QCOMPARE(tabs->currentWidget()->objectName(), QString("IDD_PPAGECOLOR"));
      QVERIFY(icc->isVisible()); // A saved legacy page also expands its section.
      QVERIFY(!apply->isEnabled());
      QTest::keyClick(search, Qt::Key_Return); QVERIFY(d->isVisible());
      for (const auto &query : {QString("ui/rememberGeometry"), QString("playback/rememberPosition"),
                                QString("video/iccProfile"), QString("ui/chapterMarkers")}) {
        search->setFocus(); search->setText(query);
        QCOMPARE(results->count(), 1); QVERIFY(results->isVisible());
        QVERIFY(!apply->isEnabled());
        auto *target = qobject_cast<QWidget *>(results->item(0)->data(Qt::UserRole).value<QObject *>());
        QVERIFY(target); QCOMPARE(target->objectName(), query);
        QTest::keyClick(search, Qt::Key_Return);
        QVERIFY(d->isVisible()); QVERIFY(search->text().isEmpty());
        QVERIFY(target->isVisible()); QVERIFY(!apply->isEnabled());
      }
      search->setText(english ? "remember window" : "запоминать положение окна");
      QVERIFY(results->count() > 0);
      QVERIFY(results->item(0)->data(Qt::UserRole).value<QObject *>());
      if (!output.isEmpty()) QVERIFY(d->grab().save(output + (english ? "/settings-search-en.png" : "/settings-search-ru.png")));
      QTest::keyClick(search, Qt::Key_Escape);
      QVERIFY(d->isVisible()); QVERIFY(search->text().isEmpty());
      search->setText("capture/quality"); search->setFocus();
      QTest::keyClick(search, Qt::Key_Down); QTRY_VERIFY(results->hasFocus());
      QTest::keyClick(results, Qt::Key_Return);
      QVERIFY(d->isVisible()); QVERIFY(search->text().isEmpty());
      QCOMPARE(tabs->currentWidget()->objectName(), QString("settingsCaptures"));
      search->setText("no-such-setting-xyz"); QCOMPARE(results->count(), 1);
      QTest::keyClick(search, Qt::Key_Return); QVERIFY(d->isVisible());
      QVERIFY(!apply->isEnabled()); search->clear();
      auto *quality = d->findChild<QSpinBox *>("capture/quality"); QVERIFY(quality);
      ClassicPreferences::selectPage(tabs, quality); quality->setValue(73);
      QCOMPARE(tabs->currentWidget()->objectName(), QString("settingsCaptures"));
      auto *thumbnail = d->findChild<QSpinBox *>("capture/columns"); QVERIFY(thumbnail);
      ClassicPreferences::selectPage(tabs, thumbnail); thumbnail->setValue(5);
      auto *captureCategory = tree->currentItem();
      tree->setCurrentItem(tree->topLevelItem(0));
      tree->setCurrentItem(captureCategory);
      QCOMPARE(tabs->currentWidget()->objectName(), QString("settingsThumbnails"));
      QCOMPARE(quality->value(), 73); QVERIFY(apply->isEnabled());
      QCOMPARE(p.settings.value("capture/quality").toInt(), 90);
      QCheckBox *normalize = nullptr;
      for (auto *box : d->findChildren<QCheckBox *>("audio/normalize"))
        if (!d->findChild<QWidget *>("settingsInactiveSources")->isAncestorOf(box)) normalize = box;
      QVERIFY(normalize); ClassicPreferences::selectPage(tabs, normalize); normalize->setChecked(true);
      apply->click();
      QCOMPARE(p.settings.value("capture/quality").toInt(), 73);
      QCOMPARE(p.settings.value("capture/columns").toInt(), 5);
      QVERIFY(p.settings.value("audio/normalize").toBool());
      QVERIFY(!apply->isEnabled());
      quality->setValue(35); normalize->setChecked(false);
      d->reject(); ++seen;
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QCOMPARE(seen, 1);
    QCOMPARE(p.settings.value("capture/quality").toInt(), 73);
    QVERIFY(p.settings.value("audio/normalize").toBool());
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(d);
      const auto closeOnExit = qScopeGuard([d] { if (d->isVisible()) d->reject(); });
      QCOMPARE(d->findChild<QSpinBox *>("capture/quality")->value(), 73);
      for (auto *box : d->findChildren<QCheckBox *>("audio/normalize")) QVERIFY(box->isChecked());
      d->reject(); ++seen;
    });
    w.findChild<QAction *>("action_preferences")->trigger(); QCOMPARE(seen, 2); w.close();
  }
  void treeApplyCancelAndValidation() {
    QTemporaryDir temp; Profile p(temp.path()); prepare(p);
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    int seen = 0;
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog");
      if (!d) { QFAIL("No settings dialog"); return; }
      const auto closeOnExit = qScopeGuard([d] { if (d->isVisible()) d->reject(); });
      auto *tree = d->findChild<QTreeWidget *>("classicPreferencesTree");
      auto *tabs = d->findChild<QTabWidget *>("classicPreferencesPages");
      auto *buttons = d->findChild<QDialogButtonBox *>();
      QVERIFY(tree); QVERIFY(tabs); QVERIFY(buttons); QVERIFY(tabs->tabBar()->isVisible());
      QVERIFY(buttons->button(QDialogButtonBox::Ok)->x() < buttons->button(QDialogButtonBox::Cancel)->x());
      QVERIFY(buttons->button(QDialogButtonBox::Cancel)->x() < buttons->button(QDialogButtonBox::Apply)->x());
      QVERIFY(!buttons->button(QDialogButtonBox::Apply)->isEnabled());
      QCOMPARE(tree->topLevelItemCount(), 10);
      QCOMPARE(tabs->count(), 40);
      QVERIFY(tree->rootIsDecorated());
      int leaves = 0;
      for (int group = 0; group < tree->topLevelItemCount(); ++group) {
        auto *category = tree->topLevelItem(group);
        QVERIFY(category->childCount() > 0); leaves += category->childCount();
        for (int child = 0; child < category->childCount(); ++child) {
          auto *item = category->child(child);
          const int index = item->data(0, Qt::UserRole + 2).toInt();
          QCOMPARE(item->text(0), tabs->tabText(index));
          tree->setCurrentItem(item);
          QCOMPARE(tabs->currentIndex(), index); QCOMPARE(tree->currentItem(), item);
        }
      }
      QCOMPARE(leaves, tabs->count());
      QVERIFY(d->property("settingsUnassignedSources").toStringList().isEmpty());
      const QStringList legacyBindings{"playback/hardwareDecoding", "integration/enabled",
                                       "integration/checkOnStartup", "audio/normalize"};
      auto *inactive = d->findChild<QWidget *>("settingsInactiveSources"); QVERIFY(inactive);
      for (auto *control : inactive->findChildren<QWidget *>())
        if (control->objectName().contains('/') &&
            (qobject_cast<QAbstractButton *>(control) || qobject_cast<QAbstractSpinBox *>(control) ||
             qobject_cast<QComboBox *>(control) || qobject_cast<QLineEdit *>(control) ||
             qobject_cast<QPlainTextEdit *>(control)))
          QVERIFY2(legacyBindings.contains(control->objectName()), qPrintable(control->objectName()));
      QJsonArray pages;
      for (int i = 0; i < tabs->count(); ++i) {
        ClassicPreferences::selectPage(tabs, tabs->widget(i)); QCoreApplication::processEvents();
        QCOMPARE(tree->currentItem()->data(0, Qt::UserRole).toString(), tabs->widget(i)->property("settingsCategory").toString());
        QVERIFY(tree->currentItem()->parent());
        QCOMPARE(tree->currentItem()->data(0, Qt::UserRole + 2).toInt(), i);
        for (int j = 0; j < tabs->count(); ++j)
          QCOMPARE(tabs->isTabVisible(j), tabs->widget(i)->property("settingsCategory") == tabs->widget(j)->property("settingsCategory"));
        pages.append(QJsonObject{{"id", tabs->widget(i)->objectName()}, {"category", tree->currentItem()->parent()->text(0)}, {"tab", tabs->tabText(i)}});
        QTest::qWait(25);
        if (!output.isEmpty()) QVERIFY(d->grab().save(output + QString("/settings-%1.png").arg(i, 2, 10, QChar('0'))));
      }
      report["settingsPages"] = pages;
      QVERIFY(d->findChild<QTableWidget *>("associationTypes"));
      QVERIFY(d->findChild<QComboBox *>("appearance/language"));
      tabs->setCurrentIndex(0); QCoreApplication::processEvents();
      auto *playerPage = qobject_cast<QScrollArea *>(tabs->widget(0)); QVERIFY(playerPage);
      QCOMPARE(playerPage->horizontalScrollBar()->maximum(), 0);
      auto *sourceHistory = d->findChild<QCheckBox *>("history/enabled"); QVERIFY(sourceHistory);
      ClassicPreferences::selectPage(tabs, sourceHistory);
      QCOMPARE(tabs->currentWidget()->objectName(), QString("settingsHistory"));
      QCOMPARE(d->findChildren<QCheckBox *>("history/enabled").size(), 1);
      auto *filter = d->findChild<QLineEdit *>("classicKeyFilter"); QVERIFY(filter);
      filter->setText("no-such-command-xyz");
      auto *keyTable = d->findChild<QTableWidget *>("shortcutTable"); QVERIFY(keyTable);
      for (int row = 0; row < keyTable->rowCount(); ++row) QVERIFY(keyTable->isRowHidden(row));
      filter->clear();
      auto *snap = d->findChild<QCheckBox *>("ui/snapEdges"); QVERIFY(snap);
      ClassicPreferences::selectPage(tabs, snap);
      QCOMPARE(tabs->currentWidget()->objectName(), QString("IDD_PPAGEWINDOWSIZE"));
      snap->setChecked(true);
      QVERIFY(!p.settings.value("ui/snapEdges", false).toBool());
      buttons->button(QDialogButtonBox::Apply)->click();
      QVERIFY(d->isVisible()); QVERIFY(p.settings.value("ui/snapEdges").toBool());
      QVERIFY(w.findChild<QAction *>("action_fullscreen")->shortcuts().contains(QKeySequence("F11")));
      snap->setChecked(false);
      // A validation failure selects the visible, reorganized online page.
      auto *agent = d->findChild<QLineEdit *>("online/userAgent"); QVERIFY(agent);
      agent->setText("invalid,agent");
      buttons->button(QDialogButtonBox::Ok)->click(); QVERIFY(d->isVisible());
      QCOMPARE(tabs->currentWidget()->objectName(), QString("IDD_PPAGEYOUTUBE"));
      QVERIFY(p.settings.value("ui/snapEdges").toBool());
      buttons->button(QDialogButtonBox::Cancel)->click(); ++seen;
    });
    w.findChild<QAction *>("action_preferences")->trigger();
    QCOMPARE(seen, 1); QVERIFY(p.settings.value("ui/snapEdges").toBool());
    QVERIFY(!p.settings.value("online/userAgent").toString().contains(','));
    w.close();
  }
  void cleanupTestCase() {
    if (output.isEmpty()) return;
    QSaveFile f(output + "/runtime.json"); QVERIFY(f.open(QIODevice::WriteOnly));
    const auto bytes = QJsonDocument(report).toJson(); QCOMPARE(f.write(bytes), bytes.size()); QVERIFY(f.commit());
  }
};
QTEST_MAIN(ClassicUiTest)
#include "classicui_test.moc"
