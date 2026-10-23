#include "preferencesdriver.h"
#include "displaymode.h"
#include "player.h"
#include "profile.h"
#include "theme.h"
#include "timeline.h"
#include "window.h"
#include "classicpreferences.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>
class WindowTest : public QObject {
  Q_OBJECT
  QString media(const QString &file) {
    return QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(file);
  }
  void prepare(Profile &profile) {
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("playback/hardwareDecoding", false);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
  }
  QAction *action(Window &window, const QString &id) {
    return window.findChild<QAction *>("action_" + id);
  }
  QString modesText(const QString &output = "HDMI-1") {
    return output +
           " connected primary 1920x1080+0+0 (normal left inverted right x "
           "axis y axis) 520mm x 290mm\n"
           "   1920x1080 60.00*+ 59.94 24.00\n   1280x720 60.00 50.00\n"
           "DP-2 disconnected (normal left inverted right x axis y axis)\n   "
           "640x480 60.00\n"
           "DP-3 connected (normal left inverted right x axis y axis)\n   "
           "1280x720 60.00\n"
           "DP-4 connected 1080x1920+1920+0 left (normal left inverted right x "
           "axis y axis)\n   1920x1080 60.00*\n";
  }
private slots:
  void displayModeValidationAndRestoration() {
    const auto parsed = DisplayMode::parse(modesText());
    QCOMPARE(parsed.size(), 5);
    QCOMPARE(parsed[0].output, QString("HDMI-1"));
    QVERIFY(parsed[0].current);
    QCOMPARE(parsed[2].rate, QString("24.00"));
    QList<QStringList> calls;
    bool failSet = false, failRestore = false;
    {
      DisplayMode mode([&](const QStringList &args) -> DisplayMode::Result {
        calls.append(args);
        if (args == QStringList{"--query"})
          return {true, modesText()};
        if (args.last() == "24.00" && failSet)
          return {false, "test apply failure"};
        if (args.last() == "60.00" && failRestore)
          return {false, "test restore failure"};
        return {true, {}};
      });
      QString error;
      QVERIFY(!mode.enter("HDMI-1", "bad", "24.00", &error));
      QVERIFY(!error.isEmpty());
      QCOMPARE(calls.size(), 1);
      QVERIFY(mode.enter("HDMI-1", "1920x1080", "60.00"));
      QCOMPARE(calls.size(), 2); // Current mode is a no-op.
      QVERIFY(mode.enter("HDMI-1", "1920x1080", "24.00"));
      QCOMPARE(calls.last(), QStringList({"--output", "HDMI-1", "--mode",
                                          "1920x1080", "--rate", "24.00"}));
      failRestore = true;
      QVERIFY(!mode.restore(&error));
      failRestore = false;
      QVERIFY(mode.restore());
      QCOMPARE(calls.last().last(), QString("60.00"));
      const int before = calls.size();
      QVERIFY(mode.restore());
      QCOMPARE(calls.size(), before);
      failSet = true;
      QVERIFY(!mode.enter("HDMI-1", "1920x1080", "24.00"));
      QCOMPARE(calls.last().last(),
               QString("60.00")); // Failed apply rolls back.
      failSet = false;
      QVERIFY(mode.enter("HDMI-1", "1280x720", "50.00"));
    }
    QCOMPARE(calls.last(), QStringList({"--output", "HDMI-1", "--mode",
                                        "1920x1080", "--rate", "60.00"}));
  }
  void presetsPanelsAndFullscreenRestore() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *bar = window.findChild<QWidget *>("controls");
    auto *timeline = window.findChild<Timeline *>();
    auto *clock = window.findChild<QLabel *>("playbackClock");
    action(window, "window_minimal")->trigger();
    QVERIFY(!bar->isVisible());
    QVERIFY(!timeline->isVisible());
    QVERIFY(!window.menuBar()->isVisible());
    QVERIFY(window.windowFlags().testFlag(Qt::FramelessWindowHint));
    auto *menu = window.findChild<QMenu *>("videoContextMenu");
    QVERIFY(menu->findChild<QMenu *>("windowModesMenu"));
    action(window, "fullscreen")->trigger();
    QTRY_VERIFY(window.isFullScreen());
    QVERIFY(bar->isVisible());
    QVERIFY(timeline->isVisible());
    action(window, "escape")->trigger();
    QTRY_VERIFY(!window.isFullScreen());
    QVERIFY(!bar->isVisible());
    QVERIFY(!timeline->isVisible());
    action(window, "window_compact")->trigger();
    QVERIFY(bar->isVisible());
    QVERIFY(timeline->isVisible());
    QVERIFY(!clock->isVisible());
    QVERIFY(!window.windowFlags().testFlag(Qt::WindowTitleHint));
    action(window, "window_normal")->trigger();
    QVERIFY(window.menuBar()->isVisible());
    QVERIFY(clock->isVisible());
    window.showMaximized();
    QTRY_VERIFY(window.isMaximized());
    action(window, "fullscreen")->trigger();
    QTRY_VERIFY(window.isFullScreen());
    action(window, "escape")->trigger();
    QTRY_VERIFY(window.isMaximized());
    action(window, "onTop")->trigger();
    QTRY_VERIFY(window.isMaximized());
    window.showNormal();
    action(window, "showTimeline")->trigger();
    QVERIFY(!timeline->isVisible());
    QVERIFY(bar->isVisible());
    action(window, "showControls")->trigger();
    QVERIFY(!bar->isVisible());
    action(window, "showTimeline")->trigger();
    QVERIFY(timeline->isVisible());
    window.close();
    Profile restored(temp.path());
    Window next(restored);
    next.show();
    QVERIFY(QTest::qWaitForWindowExposed(&next));
    QVERIFY(next.findChild<QWidget *>("controls")->isHidden());
    QVERIFY(next.findChild<Timeline *>()->isVisible());
    next.close();
  }
  void startupSizeGeometryAndAutoscale() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("ui/startSize", "video");
    profile.settings.setValue("ui/startWidth", 640);
    profile.settings.setValue("ui/startHeight", 360);
    profile.settings.setValue("ui/showMenu", true);
    profile.settings.setValue("ui/restoreAfterClose", true);
    profile.settings.setValue("ui/autoScale", 200);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    QTRY_COMPARE(player->size(), QSize(640, 360));
    window.resize(1000, 650);
    QCoreApplication::processEvents();
    const auto original = window.geometry();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({media("Русский H264.mp4")});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    QTRY_VERIFY(!player->get("video-out-params").toMap().isEmpty());
    QTRY_COMPARE(player->size(), QSize(640, 360));
    window.resize(1000, 650);
    QCoreApplication::processEvents();
    window.closeCurrent();
    QTRY_COMPARE(window.geometry(), original);
    profile.settings.setValue("ui/startSize", "remember");
    window.setGeometry(120, 90, 880, 540);
    QCoreApplication::processEvents();
    const auto saved = window.geometry();
    action(window, "fullscreen")->trigger();
    QTRY_VERIFY(window.isFullScreen());
    window.close();
    Window next(profile);
    next.show();
    QVERIFY(QTest::qWaitForWindowExposed(&next));
    QTRY_VERIFY(!next.isFullScreen());
    QTRY_COMPARE(next.geometry(), saved);
    next.close();
  }
  void aspectResizeAndSnap() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("ui/keepVideoAspect", true);
    profile.settings.setValue("ui/snapEdges", true);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({media("Русский H264.mp4")});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    window.resize(900, 700);
    QTRY_VERIFY(
        std::abs(double(player->width()) / player->height() - 16. / 9.) < .015);
    window.move(window.screen()->availableGeometry().topLeft() + QPoint(7, 8));
    QTRY_COMPARE(window.frameGeometry().topLeft(),
                 window.screen()->availableGeometry().topLeft());
    const auto before = window.size();
    profile.settings.setValue("ui/keepVideoAspect", false);
    window.resize(before.width(), before.height() + 80);
    QCoreApplication::processEvents();
    QCOMPARE(window.height(), before.height() + 80);
    window.close();
  }
  void conditionalOnTop() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("ui/onTop", true);
    profile.settings.setValue("ui/onTopWhen", "video");
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(!window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({media("Русский H264.mp4")});
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    window.showMinimized();
    player->set("pause", true);
    QTRY_VERIFY(!window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    QVERIFY(window.isMinimized());
    window.showNormal();
    player->set("pause", false);
    QTRY_VERIFY(window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    window.openFiles({media("progressive-indexed/audio.m4a")});
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(!window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    afterPreferencesReady(&window, [&] {
      auto *dialog = window.findChild<QDialog *>("preferencesDialog");
      QVERIFY(dialog);
      auto *condition = dialog->findChild<QComboBox *>("ui/onTopWhen");
      condition->setCurrentIndex(condition->findData("playing"));
      dialog->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Ok)
          ->click();
    });
    action(window, "preferences")->trigger();
    QTRY_VERIFY(window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    action(window, "stop")->trigger();
    QTRY_VERIFY(!window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    window.close();
  }
  void videoSurvivesPresentationChanges() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({media("Русский H264.mp4")});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    QVERIFY(window.seekTo(2.25));
    const auto hasColor = [player] {
      const auto image = player->grab().toImage();
      int color = 0;
      for (int y = 20; y < image.height() - 20; y += 13)
        for (int x = 20; x < image.width() - 20; x += 13) {
          const auto c = image.pixelColor(x, y);
          if (std::max({c.red(), c.green(), c.blue()}) -
                  std::min({c.red(), c.green(), c.blue()}) >
              50)
            ++color;
        }
      return color > 20;
    };
    QTRY_VERIFY(hasColor());
    const auto geometry = window.geometry();
    action(window, "fullscreen")->trigger();
    QTRY_VERIFY(window.isFullScreen());
    QTRY_VERIFY(hasColor());
    action(window, "escape")->trigger();
    QTRY_COMPARE(window.geometry(), geometry);
    QTRY_VERIFY(hasColor());
    for (const auto &mode : {"compact", "minimal", "normal"}) {
      action(window, "window_" + QString(mode))->trigger();
      QTRY_VERIFY(hasColor());
      const auto output = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      if (!output.isEmpty())
        QVERIFY(window.grab().save(output + "/window-" + mode + ".png"));
    }
    action(window, "onTop")->trigger();
    QTRY_VERIFY(hasColor());
    QVERIFY(player->get("pause").toBool());
    QVERIFY(std::abs(player->get("time-pos").toDouble() - 2.25) < .1);
    window.close();
  }
  void fullscreenModeExitAndAutohide() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("ui/changeDisplayMode", true);
    profile.settings.setValue("ui/displayMode", "1920x1080");
    profile.settings.setValue("ui/displayRate", "24.00");
    profile.settings.setValue("ui/fullscreenScreen", "missing monitor");
    profile.settings.setValue("ui/startFullscreen", true);
    profile.settings.setValue("ui/fullscreenHideMs", 150);
    QList<QStringList> calls;
    Window window(
        profile, {}, [&](const QStringList &args) -> DisplayMode::Result {
          calls.append(args);
          return {true,
                  args == QStringList{"--query"}
                      ? modesText(QGuiApplication::primaryScreen()->name())
                      : QString()};
        });
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(window.isFullScreen());
    QCOMPARE(calls.last().last(), QString("24.00"));
    auto *player = window.findChild<Player *>();
    auto *bar = window.findChild<QWidget *>("controls");
    QTest::mouseMove(player, QPoint(30, 30));
    QTRY_VERIFY_WITH_TIMEOUT(!bar->isVisible(), 2000);
    QTest::mouseMove(player, QPoint(40, 40));
    QTRY_VERIFY(bar->isVisible());
    profile.settings.setValue("ui/exitFullscreenOnFocusLoss", true);
    QDialog modal(&window);
    modal.setModal(true);
    modal.show();
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(&window, &deactivate);
    QTest::qWait(250);
    QVERIFY(window.isFullScreen());
    modal.hide();
    profile.settings.setValue("ui/exitFullscreenOnFocusLoss", false);
    action(window, "escape")->trigger();
    QTRY_VERIFY(!window.isFullScreen());
    QCOMPARE(calls.last().last(), QString("60.00"));
    profile.settings.setValue("ui/fullscreenHideMs", 0);
    action(window, "preferences")->setEnabled(true);
    // Apply through the public settings dialog; zero must disable the timer.
    afterPreferencesReady(&window, [&] {
      auto *dialog = window.findChild<QDialog *>("preferencesDialog");
      QVERIFY(dialog);
      dialog->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Ok)
          ->click();
    });
    action(window, "preferences")->trigger();
    action(window, "fullscreen")->trigger();
    QTest::mouseMove(player, QPoint(50, 50));
    QTest::qWait(400);
    QVERIFY(bar->isVisible());
    window.close();
    QCOMPARE(calls.last().last(), QString("60.00"));
  }
  void naturalEndAndFocusPolicies() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("ui/exitFullscreenAtEnd", true);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({media("Русский H264.mp4"), media("VP9.webm")});
    QTRY_COMPARE(loaded.size(), 1);
    action(window, "fullscreen")->trigger();
    QVERIFY(window.isFullScreen());
    QVERIFY(window.seekTo(7.8));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 4000);
    QVERIFY(window.isFullScreen());
    QVERIFY(window.seekTo(7.8));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 4000);
    profile.settings.setValue("ui/exitFullscreenOnFocusLoss", true);
    action(window, "fullscreen")->trigger();
    // No window manager is needed: explicitly deactivate this top level while
    // another application-level window owns focus.
    QWidget other;
    other.show();
    QApplication::setActiveWindow(&other);
    QTest::qWait(50);
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(&window, &deactivate);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 2000);
    other.close();
    window.close();
  }
  void repeatStopAndFailureKeepFullscreen() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("ui/exitFullscreenAtEnd", true);
    profile.settings.setValue("playlist/repeat", "repeatOne");
    profile.settings.setValue("playlist/repeatCount", 2);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({media("Русский H264.mp4")});
    QTRY_COMPARE(loaded.size(), 1);
    action(window, "fullscreen")->trigger();
    QVERIFY(window.isFullScreen());
    QVERIFY(window.seekTo(7.8));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 4000);
    QVERIFY(window.isFullScreen());
    QVERIFY(window.seekTo(7.8));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 4000);
    action(window, "fullscreen")->trigger();
    action(window, "stop")->trigger();
    QTRY_VERIFY(player->get("idle-active").toBool());
    QVERIFY(window.isFullScreen());
    QSignalSpy failed(player, &Player::fileFailed);
    window.openFiles({temp.filePath("missing.mp4")});
    QTRY_VERIFY(!failed.isEmpty());
    QVERIFY(window.isFullScreen());
    action(window, "escape")->trigger();
    window.close();
  }
  void defaultStartupSizeOverridesRememberedSize() {
    QTemporaryDir temp; Profile profile(temp.path()); prepare(profile);
    {
      Window window(profile); window.show();
      QVERIFY(QTest::qWaitForWindowExposed(&window));
      window.resize(840, 500); QCoreApplication::processEvents(); window.close();
    }
    profile.settings.setValue("ui/startSize", "default");
    profile.settings.setValue("ui/startWidth", 700);
    profile.settings.setValue("ui/startHeight", 400);
    Window restored(profile); restored.show();
    QVERIFY(QTest::qWaitForWindowExposed(&restored));
    QTRY_COMPARE(restored.size(), QSize(1060, 680));
    QCOMPARE(profile.settings.value("ui/startWidth").toInt(), 700);
    QCOMPARE(profile.settings.value("ui/startHeight").toInt(), 400);
    restored.close();
  }
  void preferencesCancelApplyAndScreenshots() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("ui/displayMode", "800x600");
    profile.settings.setValue("ui/displayRate", "75.00");
    profile.settings.setValue("ui/fullscreenScreen", "disconnected");
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    for (bool accept : {false, true}) {
      afterPreferencesReady(&window, [&, accept] {
        auto *dialog = window.findChild<QDialog *>("preferencesDialog");
        QVERIFY(dialog);
        dialog->findChild<QCheckBox *>("ui/snapEdges")->setChecked(true);
        dialog->findChild<QCheckBox *>("ui/keepVideoAspect")->setChecked(true);
        dialog->findChild<QCheckBox *>("ui/showStatus")->setChecked(false);
        dialog->findChild<QSpinBox *>("ui/fullscreenHideMs")->setValue(4200);
        auto *size = dialog->findChild<QComboBox *>("ui/startSize");
        size->setCurrentIndex(size->findData("video"));
        auto *tabs = dialog->findChild<QTabWidget *>();
        const auto path = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        if (accept && !path.isEmpty())
          for (int index = 0; index < tabs->count(); ++index) {
            const auto title = tabs->tabText(index);
            if (title == "Окно" || title == "Полный экран") {
              ClassicPreferences::selectPage(tabs, tabs->widget(index));
              QVERIFY(dialog->grab().save(path +
                                          (title == "Полный экран"
                                               ? "/fullscreen-preferences.png"
                                               : "/window-preferences.png")));
            }
          }
        dialog->findChild<QDialogButtonBox *>()
            ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
      });
      action(window, "preferences")->trigger();
      QCOMPARE(profile.settings.value("ui/snapEdges", false).toBool(), accept);
      QCOMPARE(profile.settings.value("ui/fullscreenHideMs", 2500).toInt(),
               accept ? 4200 : 2500);
      QCOMPARE(window.findChild<QLabel *>("playbackClock")->isVisible(),
               !accept);
    }
    QCOMPARE(profile.settings.value("ui/displayMode").toString(),
             QString("800x600"));
    QCOMPARE(profile.settings.value("ui/displayRate").toString(),
             QString("75.00"));
    QCOMPARE(profile.settings.value("ui/fullscreenScreen").toString(),
             QString("disconnected"));
    window.close();
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  applyTheme(app);
  Q_INIT_RESOURCE(resources);
  WindowTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "window_test.moc"
