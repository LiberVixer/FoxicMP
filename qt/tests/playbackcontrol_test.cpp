#include "preferencesdriver.h"
#include "player.h"
#include "profile.h"
#include "theme.h"
#include "timeline.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>

class PlaybackControlTest : public QObject {
  Q_OBJECT
  QString media(const QString &name) const {
    return QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(name);
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
private slots:
  void preferencesCancelAndApply() {
    QTemporaryDir temporary;
    Profile profile(temporary.path());
    prepare(profile);
    Window window(profile);
    window.show();
    for (bool accept : {false, true}) {
      bool handled = false;
      afterPreferencesReady(&window, [&] {
        auto *dialog = window.findChild<QDialog *>("preferencesDialog");
        QVERIFY(dialog);
        dialog->findChild<QSpinBox *>("playback/seekSmallMs")->setValue(200);
        dialog->findChild<QSpinBox *>("playlist/repeatCount")->setValue(3);
        dialog->findChild<QCheckBox *>("playback/pauseMinimized")
            ->setChecked(true);
        auto *speed = dialog->findChild<QComboBox *>("playback/speedStep");
        speed->setCurrentIndex(speed->findData(25));
        const auto screenshots =
            qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        if (accept && !screenshots.isEmpty()) {
          auto *tabs = dialog->findChild<QTabWidget *>();
          for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == "Воспроизведение")
              tabs->setCurrentIndex(i);
          dialog->grab().save(screenshots + "/playback-controls.png");
        }
        dialog->findChild<QDialogButtonBox *>()
            ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
        handled = true;
      });
      action(window, "preferences")->trigger();
      QVERIFY(handled);
      QCOMPARE(profile.settings.value("playback/seekSmallMs", 1000).toInt(),
               accept ? 200 : 1000);
      QCOMPARE(profile.settings.value("playlist/repeatCount", 0).toInt(),
               accept ? 3 : 0);
    }
    profile.settings.sync();
    Profile restored(temporary.path());
    QCOMPARE(restored.settings.value("playback/speedStep").toInt(), 25);
    QVERIFY(restored.settings.value("playback/pauseMinimized").toBool());
  }
  void seekingSpeedAndSessionReset() {
    QTemporaryDir temporary;
    Profile profile(temporary.path());
    prepare(profile);
    profile.settings.setValue("playback/seekSmallMs", 200);
    profile.settings.setValue("playback/seekMediumMs", 600);
    profile.settings.setValue("playback/seekLargeMs", 1500);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded), errors(player, &Player::error);
    window.openFiles({media("progressive-indexed/video.mp4")});
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(player->get("time-pos").toDouble() > .1);
    player->set("pause", true);
    QVERIFY(window.seekTo(3.3));
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 3.3) < .05);
    action(window, "forwardSmall")->trigger();
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 3.5) < .06);
    action(window, "back")->trigger();
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 2.9) < .06);
    action(window, "forwardLarge")->trigger();
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 4.4) < .06);
    QTRY_VERIFY(action(window, "keyframeNext")->isEnabled());
    action(window, "keyframeNext")->trigger();
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 5) < .06);
    action(window, "keyframeBack")->trigger();
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 4) < .06);
    action(window, "speedUp")->trigger();
    QCOMPARE(player->get("speed").toDouble(), 1.2);
    action(window, "speedDown")->trigger();
    QCOMPARE(player->get("speed").toDouble(), 1.);
    profile.settings.setValue("playback/speedStep", 25);
    action(window, "speedUp")->trigger();
    QCOMPARE(player->get("speed").toDouble(), 1.25);
    action(window, "speedUp")->trigger();
    QCOMPARE(player->get("speed").toDouble(), 1.5);
    profile.settings.setValue("playback/keepSpeed", true);
    window.openFiles({media("Русский H264.mp4")});
    QTRY_COMPARE(loaded.size(), 2);
    QCOMPARE(player->get("speed").toDouble(), 1.5);
    profile.settings.setValue("playback/keepSpeed", false);
    window.reopenCurrent();
    QTRY_COMPARE(loaded.size(), 3);
    QCOMPARE(player->get("speed").toDouble(), 1.);
    action(window, "speedUp")->trigger();
    action(window, "speedReset")->trigger();
    QCOMPARE(player->get("speed").toDouble(), 1.);
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
    QCOMPARE(errors.size(), 0);
    profile.settings.setValue("playback/keepSpeed", true);
    Window nextSession(profile);
    nextSession.show();
    QCOMPARE(nextSession.findChild<Player *>()->get("speed").toDouble(), 1.);
  }
  void abLoopCountsAndDialog() {
    QTemporaryDir temporary;
    Profile profile(temporary.path());
    prepare(profile);
    profile.settings.setValue("playback/after", "close");
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded), errors(player, &Player::error);
    window.openFiles({media("progressive-indexed/video.mp4")});
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(player->get("time-pos").toDouble() > .1);
    int wraps = 0;
    double previous = 0;
    connect(player, &Player::property, &window,
            [&](const QString &name, const QVariant &value) {
              if (name == "time-pos" && value.isValid()) {
                const double now = value.toDouble();
                if (previous > 1.4 && now < 1.2)
                  ++wraps;
                previous = now;
              }
            });
    QVERIFY(window.setAbLoop(1, 1.6, 3));
    QVERIFY(player->get("ab-loop-count").isValid());
    QCOMPARE(player->get("ab-loop-count").toInt(), 2);
    QVERIFY(!window.setAbLoop(2, 1, 3));
    QVERIFY(!window.setAbLoop(0, 25, 3));
    QCOMPARE(window.findChild<Timeline *>()->loopA, 1.);
    QCOMPARE(window.findChild<Timeline *>()->loopB, 1.6);
    QTRY_VERIFY_WITH_TIMEOUT(player->get("time-pos").toDouble() > 2, 6000);
    QCOMPARE(wraps, 2);
    QVERIFY(window.isVisible());
    QVERIFY(!player->get("idle-active").toBool());
    player->set("pause", true);
    window.clearAbLoop();
    QCOMPARE(player->get("ab-loop-a").toString(), QString("no"));
    bool accepted = false;
    QTimer::singleShot(100, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("abLoopDialog");
      QVERIFY(dialog);
      dialog->findChild<QDoubleSpinBox *>("abStart")->setValue(2);
      dialog->findChild<QDoubleSpinBox *>("abEnd")->setValue(3);
      dialog->findChild<QSpinBox *>("abPlays")->setValue(2);
      dialog->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Ok)
          ->click();
      accepted = true;
    });
    action(window, "abDialog")->trigger();
    QVERIFY(accepted);
    QCOMPARE(profile.settings.value("playback/abPlays").toInt(), 2);
    QCOMPARE(window.findChild<Timeline *>()->loopB, 3.);
    window.openFiles({media("Русский H264.mp4")});
    QTRY_COMPARE(loaded.size(), 2);
    QCOMPARE(player->get("ab-loop-a").toString(), QString("no"));
    QCOMPARE(window.findChild<Timeline *>()->loopA, -1.);
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
    QCOMPARE(errors.size(), 0);
  }
  void finiteRepeats_data() {
    QTest::addColumn<QString>("mode");
    QTest::addColumn<bool>("shuffle");
    QTest::addColumn<bool>("rewind");
    QTest::newRow("file-count") << QString("repeatOne") << false << false;
    QTest::newRow("list-count") << QString("repeatAll") << false << false;
    QTest::newRow("shuffle-count") << QString("repeatAll") << true << false;
    QTest::newRow("rewind") << QString("repeatOne") << false << true;
  }
  void finiteRepeats() {
    QFETCH(QString, mode);
    QFETCH(bool, shuffle);
    QFETCH(bool, rewind);
    QTemporaryDir temporary;
    Profile profile(temporary.path());
    prepare(profile);
    profile.settings.setValue("playlist/repeat", mode);
    profile.settings.setValue("playlist/repeatCount", 2);
    profile.settings.setValue("playlist/shuffle", shuffle);
    profile.settings.setValue("playback/rewindAtEnd", rewind);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded), errors(player, &Player::error);
    QStringList seen;
    connect(player, &Player::loaded, &window,
            [&] { seen.append(player->get("path").toString()); });
    const QString first = media("progressive-indexed/video.mp4"),
                  second = media("Русский H264.mp4");
    window.openFiles({first, second});
    for (int pass = 1; pass <= 4; ++pass) {
      QTRY_COMPARE(loaded.size(), pass);
      QTRY_VERIFY(player->get("time-pos").toDouble() > .04);
      if (mode == "repeatOne" && !rewind) {
        if (pass == 1)
          action(window, "speedUp")->trigger();
        if (pass == 2)
          QCOMPARE(player->get("speed").toDouble(), 1.2);
        if (pass == 3)
          QCOMPARE(player->get("speed").toDouble(), 1.);
      }
      QVERIFY(window.seekTo(player->get("duration").toDouble() - .1));
      if (pass == 4 && !rewind)
        QTRY_VERIFY(player->get("idle-active").toBool());
    }
    if (rewind) {
      QTRY_COMPARE(loaded.size(), 5);
      QTRY_VERIFY(player->get("pause").toBool());
      QTRY_VERIFY(player->get("time-pos").toDouble() < .1);
      seen.removeLast();
    } else {
      QTest::qWait(150);
      QCOMPARE(loaded.size(), 4);
    }
    QCOMPARE(seen.count(first), 2);
    QCOMPARE(seen.count(second), 2);
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
    QCOMPARE(errors.size(), 0);
  }
  void minimizingRespectsUserPauseAndAudio() {
    QTemporaryDir temporary;
    Profile profile(temporary.path());
    prepare(profile);
    profile.settings.setValue("playback/pauseMinimized", true);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded), errors(player, &Player::error);
    window.openFiles({media("progressive-indexed/video.mp4")});
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(player->get("time-pos").toDouble() > .2);
    window.showMinimized();
    QTRY_VERIFY(player->get("pause").toBool());
    const double time = player->get("time-pos").toDouble();
    QTest::qWait(200);
    QVERIFY(std::abs(player->get("time-pos").toDouble() - time) < .1);
    window.showNormal();
    QTRY_VERIFY(!player->get("pause").toBool());
    player->set("pause", true);
    window.showMinimized();
    window.showNormal();
    QTest::qWait(100);
    QVERIFY(player->get("pause").toBool());
    action(window, "play")->trigger();
    QTRY_VERIFY(!player->get("pause").toBool());
    window.showMinimized();
    QTRY_VERIFY(player->get("pause").toBool());
    action(window, "play")->trigger();
    action(window, "play")->trigger();
    window.showNormal();
    QTest::qWait(100);
    QVERIFY(player->get("pause").toBool());
    window.openFiles({media("progressive-indexed/audio.m4a")});
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(player->get("time-pos").toDouble() > .1);
    window.showMinimized();
    QTest::qWait(100);
    QVERIFY(!player->get("pause").toBool());
    window.showNormal();
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
    QCOMPARE(errors.size(), 0);
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  applyTheme(app);
  Q_INIT_RESOURCE(resources);
  PlaybackControlTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "playbackcontrol_test.moc"
