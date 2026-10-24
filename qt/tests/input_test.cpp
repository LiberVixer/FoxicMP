#include "preferencesdriver.h"
#include "mediaremote.h"
#include "player.h"
#include "playlistpanel.h"
#include "profile.h"
#include "theme.h"
#include "window.h"
#include "classicpreferences.h"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QProcess>
#include <QScopeGuard>
#include <QtTest>
#include <QtWidgets>
#include <clocale>
#include <cmath>
class RemoteObserver : public QObject {
  Q_OBJECT
public:
  QList<QVariantMap> changes;
  QList<qlonglong> jumps;
public slots:
  void changed(const QString &interface, const QVariantMap &values,
               const QStringList &) {
    if (interface == "org.mpris.MediaPlayer2.Player")
      changes.append(values);
  }
  void seeked(qlonglong position) { jumps.append(position); }
};
class InputTest : public QObject {
  Q_OBJECT
  QProcess busProcess;
  QByteArray originalBus;
  QString address;
  std::unique_ptr<QDBusConnection> client;
  const QString path = "/org/mpris/MediaPlayer2";
  const QString playerInterface = "org.mpris.MediaPlayer2.Player";
  QString media(const QString &name) const {
    return QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(name);
  }
  void prepare(Profile &profile) {
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("playback/hardwareDecoding", false);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
  }
  QAction *action(Window &window, const QString &id) {
    return window.findChild<QAction *>("action_" + id);
  }
  QDBusMessage call(const QString &service, const QString &interface,
                    const QString &method, const QVariantList &arguments = {}) {
    auto request =
        QDBusMessage::createMethodCall(service, path, interface, method);
    request.setArguments(arguments);
    QDBusPendingCallWatcher watcher(client->asyncCall(request, 3000));
    if (!watcher.isFinished()) {
      QEventLoop loop;
      connect(&watcher, &QDBusPendingCallWatcher::finished, &loop,
              &QEventLoop::quit);
      QTimer::singleShot(3500, &loop, &QEventLoop::quit);
      loop.exec();
    }
    return watcher.reply();
  }
  QVariant property(const QString &service, const QString &name,
                    bool root = false) {
    const auto reply =
        call(service, "org.freedesktop.DBus.Properties", "Get",
             {root ? "org.mpris.MediaPlayer2" : playerInterface, name});
    return reply.arguments().value(0).value<QDBusVariant>().variant();
  }
  QDBusMessage setProperty(const QString &service, const QString &name,
                           const QVariant &value, bool root = false) {
    return call(service, "org.freedesktop.DBus.Properties", "Set",
                {root ? "org.mpris.MediaPlayer2" : playerInterface, name,
                 QVariant::fromValue(QDBusVariant(value))});
  }
  QVariantMap map(const QVariant &value) {
    return value.metaType() == QMetaType::fromType<QDBusArgument>()
               ? qdbus_cast<QVariantMap>(value)
               : value.toMap();
  }
  void wheel(Player *player, int y,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier, int x = 0) {
    QWheelEvent event(QPointF(40, 40), player->mapToGlobal(QPoint(40, 40)), {},
                      QPoint(x, y), Qt::NoButton, modifiers, Qt::NoScrollPhase,
                      false);
    QApplication::sendEvent(player, &event);
  }
  void key(Window &window, QEvent::Type type, int code, bool repeat = false) {
    QKeyEvent event(type, code, Qt::NoModifier, {}, repeat);
    QApplication::sendEvent(&window, &event);
  }
private slots:
  void initTestCase() {
    originalBus = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    busProcess.start("dbus-daemon", {"--session", "--nofork", "--nopidfile",
                                     "--print-address=1"});
    QVERIFY(busProcess.waitForStarted(3000));
    QVERIFY(busProcess.waitForReadyRead(3000));
    address = QString::fromUtf8(busProcess.readLine()).trimmed();
    QVERIFY(address.startsWith("unix:"));
    qputenv("DBUS_SESSION_BUS_ADDRESS", address.toUtf8());
    client = std::make_unique<QDBusConnection>(
        QDBusConnection::connectToBus(address, "foxicmp-input-tests-client"));
    QVERIFY(client->isConnected());
  }
  void cleanupTestCase() {
    client.reset();
    QDBusConnection::disconnectFromBus("foxicmp-input-tests-client");
    busProcess.terminate();
    QVERIFY(busProcess.waitForFinished(3000));
    if (originalBus.isNull())
      qunsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
      qputenv("DBUS_SESSION_BUS_ADDRESS", originalBus);
  }
  void preferencesMigrationCancelApplyAndReset() {
    QTemporaryDir directory;
    Profile profile(directory.path());
    prepare(profile);
    profile.settings.setValue("mouse/clickPause", false);
    profile.settings.setValue("mouse/doubleClickFullscreen", false);
    profile.settings.setValue("mouse/wheelVolume", false);
    Window window(profile);
    window.show();
    for (bool accept : {false, true}) {
      bool handled = false;
      QTimer watchdog;
      watchdog.setSingleShot(true);
      connect(&watchdog, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("preferencesDialog");
        if (dialog && dialog->isVisible()) {
          for (auto *label : dialog->findChildren<QLabel *>())
            if (label->text().contains("Совпадающие клавиши"))
              qWarning() << label->text();
          dialog->reject();
        }
      });
      watchdog.start(30000);
      afterPreferencesReady(&window, [&] {
        auto *dialog = window.findChild<QDialog *>("preferencesDialog");
        QVERIFY(dialog);
        auto *left = dialog->findChild<QComboBox *>("mouse/window/left");
        QCOMPARE(left->currentData().toString(), QString("none"));
        auto *wheel = dialog->findChild<QComboBox *>("mouse/window/wheelUp");
        QCOMPARE(wheel->currentData().toString(), QString("none"));
        left->setCurrentIndex(left->findData("volumeUp"));
        auto *full = dialog->findChild<QComboBox *>("mouse/fullscreen/left");
        full->setCurrentIndex(full->findData("volumeDown"));
        dialog->findChild<QCheckBox *>("mouse/openLastWhenIdle")
            ->setChecked(true);
        dialog->findChild<QCheckBox *>("input/accelerateSeek")
            ->setChecked(true);
        dialog->findChild<QCheckBox *>("mouse/holdSpeed")->setChecked(true);
        dialog->findChild<QSpinBox *>("mouse/holdSpeedRate")->setValue(3);
        dialog->findChild<QSpinBox *>("mouse/holdSpeedDelayMs")->setValue(400);
        auto *modifiers =
            dialog->findChild<QComboBox *>("mouseBindingModifiers");
        QVERIFY(modifiers);
        modifiers->setCurrentIndex(3);
        auto *modified =
            dialog->findChild<QComboBox *>("mouse/window/ctrl_shift_middle");
        modified->setCurrentIndex(modified->findData("mute"));
        modifiers->setCurrentIndex(0);
        dialog->findChild<QSpinBox *>("input/audioDelayStepMs")->setValue(25);
        dialog->findChild<QKeySequenceEdit *>("key_audioNext")
            ->setKeySequence(QKeySequence("F7"));
        const auto screenshots =
            qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        if (accept && !screenshots.isEmpty()) {
          auto *tabs = dialog->findChild<QTabWidget *>();
          for (const auto &title : {QString("Мышь"), QString("Шаги команд")}) {
            for (int i = 0; i < tabs->count(); ++i)
              if (tabs->tabText(i) == title)
                ClassicPreferences::selectPage(tabs, tabs->widget(i));
            dialog->grab().save(screenshots + (title == "Мышь"
                                                   ? "/mouse-settings.png"
                                                   : "/input-settings.png"));
          }
        }
        handled = true;
        dialog->findChild<QDialogButtonBox *>()
            ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
      });
      action(window, "preferences")->trigger();
      QVERIFY(handled);
      QCOMPARE(profile.settings.contains("mouse/window/left"), accept);
      QCOMPARE(action(window, "audioNext")->shortcut(),
               QKeySequence(accept ? "F7" : "A"));
    }
    QCOMPARE(profile.settings.value("mouse/fullscreen/left").toString(),
             QString("volumeDown"));
    QCOMPARE(profile.settings.value("input/audioDelayStepMs").toInt(), 25);
    QVERIFY(profile.settings.value("mouse/holdSpeed").toBool());
    QCOMPARE(profile.settings.value("mouse/holdSpeedRate").toInt(), 3);
    QCOMPARE(
        profile.settings.value("mouse/window/ctrl_shift_middle").toString(),
        QString("mute"));
    bool reset = false;
    afterPreferencesReady(&window, [&] {
      auto *dialog = window.findChild<QDialog *>("preferencesDialog");
      QVERIFY(dialog);
      for (auto *button : dialog->findChildren<QPushButton *>())
        if (button->text() == "Вернуть стандартные действия мыши") {
          button->click();
          reset = true;
        }
      QCOMPARE(dialog->findChild<QComboBox *>("mouse/window/left")
                   ->currentData()
                   .toString(),
               QString("play"));
      dialog->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Ok)
          ->click();
    });
    action(window, "preferences")->trigger();
    QVERIFY(reset);
    profile.settings.sync();
    Profile restored(directory.path());
    QCOMPARE(restored.settings.value("mouse/window/left").toString(),
             QString("play"));
    QCOMPARE(restored.settings.value("mouse/fullscreen/wheelDown").toString(),
             QString("volumeDown"));
    QCOMPARE(
        restored.settings.value("mouse/window/ctrl_shift_middle").toString(),
        QString("none"));
    QVERIFY(!action(window, "play")->autoRepeat());
    QVERIFY(action(window, "forward")->autoRepeat());
  }
  void tracksDelaysInformationAndPan() {
    QTemporaryDir directory;
    Profile profile(directory.path());
    prepare(profile);
    profile.settings.setValue("input/audioDelayStepMs", 25);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({media("chapters.mkv")});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    const auto original = player->get("aid").toString();
    action(window, "audioNext")->trigger();
    QVERIFY(player->get("aid").toString() != original);
    action(window, "audioPrevious")->trigger();
    QCOMPARE(player->get("aid").toString(), original);
    auto *queue = window.findChild<PlaylistPanel *>();
    QCOMPARE(queue->savedPlayingList()
                 ->item(0)
                 ->data(PlaylistPanel::Metadata)
                 .toMap()
                 .value("aid")
                 .toString(),
             original);
    // A subtitle cycle includes the disabled state and preserves visibility.
    player->set("sid", "no");
    action(window, "subtitleNext")->trigger();
    QVERIFY(player->get("sid").toInt() > 0);
    QVERIFY(player->get("sub-visibility").toBool());
    action(window, "subtitlePrevious")->trigger();
    QVERIFY(player->get("sid").toString() == "no" ||
            !player->get("sid").toBool());
    action(window, "subtitleToggle")->trigger();
    QVERIFY(!player->get("sub-visibility").toBool());
    action(window, "audioDelayUp")->trigger();
    QVERIFY(std::abs(player->get("audio-delay").toDouble() - .025) < .0001);
    action(window, "subtitleDelayDown")->trigger();
    QVERIFY(std::abs(player->get("sub-delay").toDouble() + .1) < .0001);
    action(window, "delayReset")->trigger();
    QCOMPARE(player->get("sub-delay").toDouble(), 0.);
    QCOMPARE(player->get("audio-delay").toDouble(), 0.);
    action(window, "panLeft")->trigger();
    action(window, "panDown")->trigger();
    QVERIFY(player->get("video-pan-x").toDouble() < 0);
    QVERIFY(player->get("video-pan-y").toDouble() > 0);
    action(window, "zoomIn")->trigger();
    action(window, "panReset")->trigger();
    QCOMPARE(player->get("video-pan-x").toDouble(), 0.);
    QCOMPARE(player->get("video-pan-y").toDouble(), 0.);
    QCOMPARE(player->get("video-zoom").toDouble(), 0.);
    bool seen = false;
    QTimer::singleShot(100, &window, [&] {
      auto *dialog = window.findChild<QMessageBox *>("playbackInformation");
      QVERIFY(dialog);
      QVERIFY(dialog->text().contains("audio-delay"));
      seen = true;
      dialog->accept();
    });
    action(window, "information")->trigger();
    QVERIFY(seen);
    window.closeCurrent();
  }
  void mouseBindingsDoubleClickAndWheel() {
    QTemporaryDir directory;
    Profile profile(directory.path());
    prepare(profile);
    profile.settings.setValue("mouse/window/left", "volumeUp");
    profile.settings.setValue("mouse/fullscreen/left", "volumeDown");
    profile.settings.setValue("mouse/window/middle", "mute");
    profile.settings.setValue("mouse/window/back", "volumeDown");
    profile.settings.setValue("mouse/window/forward", "volumeUp");
    profile.settings.setValue("mouse/window/ctrl_wheelUp", "volumeDown");
    profile.settings.setValue("mouse/window/wheelRight", "volumeUp");
    profile.settings.setValue("mouse/window/shift_back", "volumeUp");
    profile.settings.setValue("mouse/window/ctrl_shift_wheelDown",
                              "volumeDown");
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QTest::mouseClick(player, Qt::LeftButton);
    QCOMPARE(player->get("volume").toDouble(), 75.);
    QTest::qWait(QApplication::doubleClickInterval() + 80);
    QCOMPARE(player->get("volume").toDouble(), 80.);
    QTest::mouseDClick(player, Qt::LeftButton);
    QVERIFY(window.isFullScreen());
    QTest::qWait(QApplication::doubleClickInterval() + 80);
    QCOMPARE(player->get("volume").toDouble(), 80.);
    QTest::mouseClick(player, Qt::LeftButton);
    QTest::qWait(QApplication::doubleClickInterval() + 80);
    QCOMPARE(player->get("volume").toDouble(), 75.);
    action(window, "escape")->trigger();
    QVERIFY(!window.isFullScreen());
    QTest::mouseClick(player, Qt::MiddleButton);
    QTRY_VERIFY(!player->get("mute").toBool());
    QTest::mouseClick(player, Qt::BackButton);
    QCOMPARE(player->get("volume").toDouble(), 70.);
    QTest::mouseClick(player, Qt::ForwardButton);
    QCOMPARE(player->get("volume").toDouble(), 75.);
    wheel(player, 60);
    QCOMPARE(player->get("volume").toDouble(), 75.);
    wheel(player, 60);
    QCOMPARE(player->get("volume").toDouble(), 80.);
    wheel(player, 240, Qt::ControlModifier);
    QCOMPARE(player->get("volume").toDouble(), 70.);
    wheel(player, 60, Qt::ControlModifier);
    wheel(player, 60);
    QCOMPARE(player->get("volume").toDouble(), 70.);
    wheel(player, 60);
    QCOMPARE(player->get("volume").toDouble(), 75.);
    wheel(player, 0, Qt::NoModifier, 120);
    QCOMPARE(player->get("volume").toDouble(), 80.);
    QTest::mouseClick(player, Qt::BackButton, Qt::ShiftModifier);
    QCOMPARE(player->get("volume").toDouble(), 85.);
    wheel(player, -120, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(player->get("volume").toDouble(), 80.);
    QTest::mouseClick(player, Qt::RightButton);
    auto *menu = window.findChild<QMenu *>("videoContextMenu");
    QTRY_VERIFY(menu->isVisible());
    menu->hide();
    profile.settings.setValue("mouse/window/right", "volumeDown");
    QTest::mouseClick(player, Qt::RightButton);
    QCOMPARE(player->get("volume").toDouble(), 75.);
    QVERIFY(!menu->isVisible());
    QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(40, 40),
                              player->mapToGlobal(QPoint(40, 40)));
    QApplication::sendEvent(player, &context);
    QVERIFY(!menu->isVisible());
    profile.settings.setValue("mouse/window/middle", "volumeUp");
    profile.settings.setValue("mouse/window/double_middle", "volumeDown");
    QTest::mouseDClick(player, Qt::MiddleButton);
    QTest::qWait(QApplication::doubleClickInterval() + 80);
    QCOMPARE(player->get("volume").toDouble(), 70.);
  }
  void dragWithoutClickAndLastFileBossAndMediaKeys() {
    QTemporaryDir directory;
    Profile profile(directory.path());
    prepare(profile);
    profile.settings.setValue("mouse/openLastWhenIdle", true);
    profile.recordRecent(media("progressive-indexed/video.mp4"));
    Window window(profile);
    window.show();
    window.activateWindow();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    QTest::mouseClick(player, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 4000);
    QTRY_VERIFY(player->get("time-pos").toDouble() > .1);
    const QPoint oldPosition = window.QWidget::pos(), local(100, 100),
                 global = player->mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(player, &press);
    QMouseEvent move(QEvent::MouseMove, local + QPoint(55, 25),
                     global + QPoint(55, 25), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(player, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, local,
                        global + QPoint(55, 25), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(player, &release);
    QTRY_COMPARE(window.QWidget::pos(), oldPosition + QPoint(55, 25));
    QTest::qWait(QApplication::doubleClickInterval() + 80);
    QVERIFY(!player->get("pause").toBool());
    key(window, QEvent::KeyPress, Qt::Key_MediaPause);
    key(window, QEvent::KeyRelease, Qt::Key_MediaPause);
    QTRY_VERIFY(player->get("pause").toBool());
    key(window, QEvent::KeyPress, Qt::Key_MediaPause);
    key(window, QEvent::KeyRelease, Qt::Key_MediaPause);
    QVERIFY(player->get("pause").toBool());
    key(window, QEvent::KeyPress, Qt::Key_MediaPlay);
    key(window, QEvent::KeyRelease, Qt::Key_MediaPlay);
    QTRY_VERIFY(!player->get("pause").toBool());
    action(window, "bossKey")->trigger();
    QTRY_VERIFY(window.isMinimized());
    QTRY_VERIFY(player->get("pause").toBool());
    const double saved = player->get("time-pos").toDouble();
    action(window, "bossKey")->trigger();
    QTRY_VERIFY(!window.isMinimized());
    QTest::qWait(200);
    QVERIFY(player->get("pause").toBool());
    QVERIFY(std::abs(player->get("time-pos").toDouble() - saved) < .05);
    window.closeCurrent();
    action(window, "openLast")->trigger();
    QTRY_COMPARE(loaded.size(), 2);
  }
  void mouseHoldSpeedRestoreAndDragCancel() {
    QTemporaryDir directory;
    Profile profile(directory.path());
    prepare(profile);
    profile.settings.setValue("mouse/holdSpeed", true);
    profile.settings.setValue("mouse/holdSpeedRate", 3);
    profile.settings.setValue("mouse/holdSpeedDelayMs", 100);
    profile.settings.setValue("mouse/window/left", "volumeUp");
    profile.settings.setValue("mouse/window/double_left", "none");
    profile.settings.setValue("mouse/windowDrag", "left");
    profile.settings.setValue("playback/keepSpeed", true);
    Window window(profile);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({media("progressive-indexed/video.mp4")});
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(player->get("time-pos").toDouble() > .1);
    player->set("speed", 1.25);
    QTest::mousePress(player, Qt::LeftButton);
    QTRY_COMPARE(player->get("speed").toDouble(), 3.);
    QTest::mouseRelease(player, Qt::LeftButton);
    QTRY_COMPARE(player->get("speed").toDouble(), 1.25);
    QTest::qWait(QApplication::doubleClickInterval() + 50);
    QCOMPARE(player->get("volume").toDouble(), 75.);
    // A normal short click retains its assigned action.
    QTest::mouseClick(player, Qt::LeftButton);
    QCOMPARE(player->get("volume").toDouble(), 80.);
    const QPoint local(100, 100), global = player->mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(player, &press);
    QMouseEvent move(QEvent::MouseMove, local + QPoint(40, 20),
                     global + QPoint(40, 20), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(player, &move);
    QTest::qWait(180);
    QCOMPARE(player->get("speed").toDouble(), 1.25);
    QMouseEvent release(QEvent::MouseButtonRelease, local,
                        global + QPoint(40, 20), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(player, &release);
    QCOMPARE(player->get("volume").toDouble(), 80.);
    QTest::mousePress(player, Qt::LeftButton);
    QTRY_COMPARE(player->get("speed").toDouble(), 3.);
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(&window, &deactivate);
    QCOMPARE(player->get("speed").toDouble(), 1.25);
    QTest::mouseRelease(player, Qt::LeftButton);
    QCOMPARE(player->get("volume").toDouble(), 80.);
    QTest::mousePress(player, Qt::LeftButton);
    QTRY_COMPARE(player->get("speed").toDouble(), 3.);
    window.openFiles({media("chapters.mkv")});
    QTRY_COMPARE(loaded.size(), 2);
    QCOMPARE(player->get("speed").toDouble(), 1.25);
    QTest::mouseRelease(player, Qt::LeftButton);
    QCOMPARE(player->get("volume").toDouble(), 80.);
    player->set("pause", true);
    QTest::mousePress(player, Qt::LeftButton);
    QTest::qWait(180);
    QCOMPARE(player->get("speed").toDouble(), 1.25);
    QVERIFY(player->get("pause").toBool());
    QTest::mouseRelease(player, Qt::LeftButton);
    window.closeCurrent();
  }
  void seekHoldAccelerationAndRelease() {
    QTemporaryDir directory;
    Profile profile(directory.path());
    prepare(profile);
    profile.settings.setValue("input/accelerateSeek", true);
    profile.settings.setValue("playback/seekMediumMs", 100);
    Window window(profile);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({media("progressive-indexed/video.mp4")});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    QVERIFY(window.seekTo(2));
    QTRY_VERIFY(!player->get("seeking").toBool() &&
                std::abs(player->get("time-pos").toDouble() - 2) < .05);
    const auto step = [&](double seconds, bool repeat = false) {
      // Exact seeks land on a decoded frame. Measure each step from the
      // settled position, rather than accumulating 25 FPS rounding errors.
      const double target = player->get("time-pos").toDouble() + seconds;
      key(window, QEvent::KeyPress, Qt::Key_Right, repeat);
      QTRY_VERIFY(!player->get("seeking").toBool() &&
                  std::abs(player->get("time-pos").toDouble() - target) < .045);
    };
    step(.1);
    QTest::qWait(800);
    step(.2, true);
    QTest::qWait(750);
    step(.4, true);
    QTest::qWait(1500);
    step(.8, true);
    key(window, QEvent::KeyRelease, Qt::Key_Right);
    step(.1);
    key(window, QEvent::KeyRelease, Qt::Key_Right);
    profile.settings.setValue("input/accelerateSeek", false);
    step(.1);
    QTest::qWait(800);
    step(.1, true);
    key(window, QEvent::KeyRelease, Qt::Key_Right);
    profile.settings.setValue("input/accelerateSeek", true);
    step(.1);
    QTest::qWait(800);
    QFocusEvent focusOut(QEvent::FocusOut);
    QApplication::sendEvent(&window, &focusOut);
    step(.1, true);
    key(window, QEvent::KeyRelease, Qt::Key_Right);
    window.closeCurrent();
  }
  void mprisUnavailableAndRetry() {
    const auto saved = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    auto restore =
        qScopeGuard([&] { qputenv("DBUS_SESSION_BUS_ADDRESS", saved); });
    qputenv("DBUS_SESSION_BUS_ADDRESS",
            "unix:path=/nonexistent/foxicmp-input-test-bus");
    MediaRemote remote([] { return QVariantMap{}; },
                       [](const QString &, const QVariantList &) {});
    remote.setEnabled(true);
    QVERIFY(remote.serviceName().isEmpty());
    QVERIFY(!remote.errorString().isEmpty());
    qputenv("DBUS_SESSION_BUS_ADDRESS", saved);
    remote.setEnabled(true);
    const auto name = remote.serviceName();
    QVERIFY2(!name.isEmpty(), qPrintable(remote.errorString()));
    QVERIFY(client->interface()->isServiceRegistered(name).value());
    remote.setEnabled(false);
    QVERIFY(!client->interface()->isServiceRegistered(name).value());
  }
  void mprisWireControlIsolationAndLifetime() {
    QTemporaryDir directory, secondDirectory;
    Profile profile(directory.path());
    prepare(profile);
    profile.settings.setValue("input/mpris", true);
    Window window(profile);
    window.show();
    auto *remote = window.findChild<MediaRemote *>();
    QVERIFY(remote);
    const auto service = remote->serviceName();
    QVERIFY2(!service.isEmpty(), qPrintable(remote->errorString()));
    QVERIFY(client->interface()->isServiceRegistered(service));
    QCOMPARE(property(service, "Identity", true).toString(),
             QString("FoxiqMP qt"));
    const auto commandsReply =
        call(service, "org.freedesktop.DBus.Properties", "Get",
             {"org.foxicmp.Control1", "Commands"});
    const auto commands =
        map(commandsReply.arguments().value(0).value<QDBusVariant>().variant());
    QVERIFY(commands.contains("subtitleNext"));
    QVERIFY(commands.contains("bossKey"));
    QCOMPARE(call(service, "org.foxicmp.Control1", "Trigger", {"shell-command"})
                 .arguments()
                 .value(0)
                 .toBool(),
             false);
    auto reply =
        call(service, "org.freedesktop.DBus.Introspectable", "Introspect");
    QVERIFY(reply.arguments().value(0).toString().contains(playerInterface));
    RemoteObserver observer;
    QVERIFY(client->connect(service, path, "org.freedesktop.DBus.Properties",
                            "PropertiesChanged", &observer,
                            SLOT(changed(QString, QVariantMap, QStringList))));
    QVERIFY(client->connect(service, path, playerInterface, "Seeked", &observer,
                            SLOT(seeked(qlonglong))));
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles(
        {media("progressive-indexed/video.mp4"), media("chapters.mkv")});
    QTRY_COMPARE(loaded.size(), 1);
    QCOMPARE(call(service, playerInterface, "Pause").type(),
             QDBusMessage::ReplyMessage);
    QTRY_VERIFY(player->get("pause").toBool());
    QCOMPARE(property(service, "PlaybackStatus").toString(), QString("Paused"));
    QTRY_VERIFY(!observer.changes.isEmpty());
    QVERIFY(std::any_of(observer.changes.begin(), observer.changes.end(),
                        [](const QVariantMap &v) {
                          return v.value("PlaybackStatus") == "Paused";
                        }));
    for (const auto &change : observer.changes)
      QVERIFY(!change.contains("Position"));
    QCOMPARE(setProperty(service, "Rate", 1.25).type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(player->get("speed").toDouble(), 1.25);
    QCOMPARE(setProperty(service, "Volume", .37).type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(player->get("volume").toDouble(), 37.);
    QVERIFY(!player->get("mute").toBool());
    auto metadata = map(property(service, "Metadata"));
    const auto track = metadata.value("mpris:trackid").value<QDBusObjectPath>();
    QVERIFY(track.path().startsWith("/org/foxicmp/track/"));
    QVERIFY(metadata.value("mpris:length").toLongLong() > 10000000);
    QCOMPARE(call(service, playerInterface, "SetPosition",
                  {QVariant::fromValue(QDBusObjectPath("/old/track")),
                   qlonglong(6000000)})
                 .type(),
             QDBusMessage::ReplyMessage);
    QVERIFY(player->get("time-pos").toDouble() < 1);
    QCOMPARE(call(service, playerInterface, "SetPosition",
                  {QVariant::fromValue(track), qlonglong(3000000)})
                 .type(),
             QDBusMessage::ReplyMessage);
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 3) < .05);
    QTRY_VERIFY(!observer.jumps.isEmpty());
    QCOMPARE(
        call(service, playerInterface, "Seek", {qlonglong(-1000000)}).type(),
        QDBusMessage::ReplyMessage);
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 2) < .05);
    QCOMPARE(call(service, playerInterface, "Next").type(),
             QDBusMessage::ReplyMessage);
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(player->get("pause").toBool());
    QVERIFY(map(property(service, "Metadata"))
                .value("mpris:trackid")
                .value<QDBusObjectPath>()
                .path() != track.path());
    QCOMPARE(setProperty(service, "Shuffle", true).type(),
             QDBusMessage::ReplyMessage);
    QVERIFY(profile.settings.value("playlist/shuffle").toBool());
    QCOMPARE(setProperty(service, "LoopStatus", "Track").type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(profile.settings.value("playlist/repeat").toString(),
             QString("repeatOne"));
    QCOMPARE(setProperty(service, "Fullscreen", true, true).type(),
             QDBusMessage::ReplyMessage);
    QTRY_VERIFY(window.isFullScreen());
    QCOMPARE(setProperty(service, "Fullscreen", false, true).type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(call(service, playerInterface, "OpenUri", {"command://touch-bad"})
                 .type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(call(service, playerInterface, "Stop").type(),
             QDBusMessage::ReplyMessage);
    QTRY_COMPARE(property(service, "PlaybackStatus").toString(),
                 QString("Stopped"));
    QCOMPARE(call(service, playerInterface, "Play").type(),
             QDBusMessage::ReplyMessage);
    QTRY_COMPARE(loaded.size(), 3);
    QVERIFY(player->get("time-pos").toDouble() < .7);
    QCOMPARE(call(service, playerInterface, "OpenUri",
                  {QUrl::fromLocalFile(media("Русский H264.mp4"))
                       .toString(QUrl::FullyEncoded)})
                 .type(),
             QDBusMessage::ReplyMessage);
    QTRY_COMPARE(loaded.size(), 4);
    Profile secondProfile(secondDirectory.path());
    prepare(secondProfile);
    secondProfile.settings.setValue("input/mpris", true);
    {
      Window second(secondProfile);
      second.show();
      const auto other = second.findChild<MediaRemote *>()->serviceName();
      QVERIFY(!other.isEmpty());
      QVERIFY(other != service);
      QVERIFY(client->interface()->isServiceRegistered(other));
      second.close();
    }
    QCOMPARE(call(service, playerInterface, "Pause").type(),
             QDBusMessage::ReplyMessage);
    QTRY_VERIFY(player->get("pause").toBool());
    QCOMPARE(call(service, "org.foxicmp.Control1", "Trigger", {"bossKey"})
                 .arguments()
                 .value(0)
                 .toBool(),
             true);
    QTRY_VERIFY(window.isMinimized());
    QCOMPARE(call(service, "org.mpris.MediaPlayer2", "Raise").type(),
             QDBusMessage::ReplyMessage);
    QTRY_VERIFY(!window.isMinimized());
    QVERIFY(player->get("pause").toBool());
    // Disable through the real settings dialog, then re-enable without restart.
    for (bool enabled : {false, true}) {
      afterPreferencesReady(&window, [&] {
        auto *dialog = window.findChild<QDialog *>("preferencesDialog");
        QVERIFY(dialog);
        dialog->findChild<QCheckBox *>("input/mpris")->setChecked(enabled);
        dialog->findChild<QDialogButtonBox *>()
            ->button(QDialogButtonBox::Ok)
            ->click();
      });
      action(window, "preferences")->trigger();
      QCOMPARE(!remote->serviceName().isEmpty(), enabled);
    }
    window.close();
  }
};
int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  std::setlocale(LC_NUMERIC, "C");
  app.setQuitOnLastWindowClosed(false);
  Q_INIT_RESOURCE(resources);
  applyTheme(app);
  InputTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "input_test.moc"
