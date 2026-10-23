#include "window.h"
#include "player.h"
#include <QtTest>
#include <QtWidgets>

class MonitorTest : public QObject {
  Q_OBJECT
  QJsonArray results;
  QJsonObject active;
  void prepare(Profile &p) {
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("integration/enabled", false);
    p.settings.setValue("integration/checkOnStartup", false);
    p.settings.setValue("input/mpris", false);
    p.settings.setValue("audio/mute", true);
    p.settings.setValue("playback/hardwareDecoding", false);
    p.settings.setValue("video/output", qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
  }
  static QJsonArray rect(const QRect &r) { return {r.x(), r.y(), r.width(), r.height()}; }
  static bool frame(Player *p) {
    auto image = p->grabFramebuffer();
    for (int y = 0; y < image.height(); y += 12)
      for (int x = 0; x < image.width(); x += 12) {
        auto c = image.pixelColor(x, y);
        if (std::max({c.red(), c.green(), c.blue()}) -
            std::min({c.red(), c.green(), c.blue()}) > 30) return true;
      }
    return false;
  }
private slots:
  void initTestCase() {
    Q_INIT_RESOURCE(resources);
    qApp->setStyle("Fusion");
    QCOMPARE(QGuiApplication::platformName(), QString("xcb"));
    QVERIFY(QFile::exists(qEnvironmentVariable("FOXICMP_MATRIX_DIR") + "/Русский H264.mp4"));
  }
  void monitorsFullscreenAndRestore() {
    active = {{"case", "monitorsFullscreenAndRestore"}};
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("ui/fullscreenHideMs", 0);
    Window w(profile);
    w.resize(640, 400);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *p = w.findChild<Player *>();
    QVERIFY(p);
    LaunchRequest request;
    request.files = {qEnvironmentVariable("FOXICMP_MATRIX_DIR") + "/Русский H264.mp4"};
    request.options = {{"pause", true}, {"start", 1}};
    w.handleLaunchRequest(request);
    QTRY_VERIFY_WITH_TIMEOUT(p->get("pause").toBool(), 10000);
    QTRY_VERIFY(!p->get("seeking").toBool() && std::abs(p->get("time-pos").toDouble() - 1) < .15);
    QTRY_VERIFY(frame(p));
    if (qEnvironmentVariableIsSet("FOXICMP_MONITOR_REQUIRE_GL"))
      QCOMPARE(p->outputName(), QString("OpenGL"));
    QJsonArray samples;
    for (auto *s : QGuiApplication::screens()) {
      const auto area = s->availableGeometry();
      w.setScreen(s);
      w.resize(QSize(640, 400).boundedTo(area.size()));
      w.move(area.center() - QPoint(w.width() / 2, w.height() / 2));
      QTRY_COMPARE(w.screen(), s);
      QTest::qWait(150);
      const auto before = w.geometry();
      // Use the same selected-monitor preference as the settings dialog.
      profile.settings.setValue("ui/fullscreenScreen", s->name() + "|" + s->manufacturer() + "|" + s->model() + "|" + s->serialNumber());
      w.findChild<QAction *>("action_fullscreen")->trigger();
      QTRY_VERIFY(w.isFullScreen());
      QTRY_COMPARE(w.screen(), s);
      QTRY_COMPARE(w.geometry(), s->geometry());
      QTRY_VERIFY(frame(p));
      auto *bar = w.findChild<QWidget *>("controls");
      for (auto *button : bar->findChildren<QToolButton *>())
        if (button->isVisible())
          QVERIFY(bar->rect().contains(button->geometry()));
      w.findChild<QAction *>("action_escape")->trigger();
      QTRY_VERIFY(!w.isFullScreen());
      if (area.contains(before))
        QTRY_COMPARE(w.geometry(), before);
      else
        QTRY_VERIFY(area.intersects(w.frameGeometry())); // Minimum normal width may exceed a portrait screen.
      for (auto mode : {"minimal", "compact", "normal"}) {
        w.findChild<QAction *>(QString("action_window_") + mode)->trigger();
        QTRY_VERIFY(frame(p));
      }
      w.findChild<QAction *>("action_onTop")->trigger();
      QVERIFY(w.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
      w.findChild<QAction *>("action_onTop")->trigger();
      w.showMinimized();
      QTRY_VERIFY(w.isMinimized());
      w.restoreFromTray();
      QTRY_VERIFY(!w.isMinimized() && w.isVisible());
      QTRY_VERIFY(frame(p));
      QVERIFY(p->get("pause").toBool());
      QVERIFY(std::abs(p->get("time-pos").toDouble() - 1) < .15);
      const auto capture = w.grab();
      QCOMPARE(capture.devicePixelRatio(), w.devicePixelRatioF());
      samples.append(QJsonObject{{"name", s->name()}, {"geometry", rect(s->geometry())},
                                 {"workArea", rect(area)}, {"devicePixelRatio", w.devicePixelRatioF()},
                                 {"capturePixels", QJsonArray{capture.width(), capture.height()}},
                                 {"renderer", p->outputName()}, {"pausedPosition", p->get("time-pos").toDouble()}});
    }
    active["screens"] = samples;
    w.close();
  }
  void resolutionShrink_data() {
    QTest::addColumn<QString>("state");
    for (auto state : {"normal", "minimized", "maximized", "fullscreen"})
      QTest::newRow(state) << QString(state);
  }
  void resolutionShrink() {
    if (!qEnvironmentVariableIsSet("FOXICMP_MONITOR_ISOLATED"))
      QSKIP("Resolution changes are allowed only on the isolated nested X server");
    QFETCH(QString, state);
    active = {{"case", "resolutionShrink"}, {"state", state}};
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    Window w(profile);
    w.resize(700, 450);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.move(550, 400);
    QTest::qWait(200);
    if (state == "minimized") w.showMinimized();
    if (state == "maximized") w.showMaximized();
    if (state == "fullscreen") w.findChild<QAction *>("action_fullscreen")->trigger();
    auto *s = QGuiApplication::primaryScreen();
    const auto original = s->geometry();
    QProcess proc;
    proc.start("xrandr", {"--output", s->name(), "--mode", "800x600"});
    QVERIFY(proc.waitForFinished(5000));
    QCOMPARE(proc.exitCode(), 0);
    QTRY_COMPARE(s->geometry().size(), QSize(800, 600));
    QTest::qWait(700);
    if (state == "minimized" || state == "maximized") w.showNormal();
    if (state == "fullscreen") {
      QCOMPARE(w.geometry(), s->geometry());
      w.findChild<QAction *>("action_escape")->trigger();
    }
    QTest::qWait(500);
    active["windowAfterShrink"] = rect(w.frameGeometry());
    active["screenAfterShrink"] = rect(s->availableGeometry());
    const bool accessible = s->availableGeometry().contains(w.frameGeometry());
    proc.start("xrandr", {"--output", s->name(), "--mode", QString("%1x%2").arg(original.width()).arg(original.height())});
    QVERIFY(proc.waitForFinished(5000));
    QCOMPARE(proc.exitCode(), 0);
    QTRY_COMPARE(s->geometry(), original);
    QVERIFY2(accessible, "After a screen shrink the complete player frame must remain accessible");
  }
  void cleanup() {
    if (active.isEmpty()) return;
    active["passed"] = !QTest::currentTestFailed();
    results.append(active);
    active = {};
  }
  void cleanupTestCase() {
    QSaveFile file(qEnvironmentVariable("FOXICMP_MONITOR_REPORT"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    auto bytes = QJsonDocument(QJsonObject{{"platform", QGuiApplication::platformName()},
                                          {"scale", qEnvironmentVariable("QT_SCALE_FACTOR", "1")},
                                          {"results", results}}).toJson();
    QCOMPARE(file.write(bytes), bytes.size());
    QVERIFY(file.commit());
  }
};
QTEST_MAIN(MonitorTest)
#include "monitor_test.moc"
