#include "instancebroker.h"
#include "player.h"
#include "profile.h"
#include "window.h"
#include <QLocalSocket>
#include <QtEndian>
#include <QtTest>
#include <QtWidgets>
#include <cmath>

class DesktopIntegrationTest : public QObject {
  Q_OBJECT
  QString media;
  QString exe(const QString &name) {
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
  }
  void prepare(Profile &p) {
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("input/mpris", false);
    p.settings.setValue("audio/mute", true);
    p.settings.setValue("playback/hardwareDecoding", false);
    p.settings.setValue("video/output", qEnvironmentVariable(
                                            "FOXICMP_TEST_OUTPUT", "software"));
  }
  void load(Window &w) {
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded);
    w.show();
    w.openFiles({media});
    QTRY_COMPARE(loaded.size(), 1);
    QVERIFY(p->set("pause", true));
    QVERIFY(p->command({"seek", "2", "absolute+exact"}));
    QTRY_VERIFY(std::abs(p->get("time-pos").toDouble() - 2) < .1);
  }
  void edit(Window &w, const std::function<void(QDialog *)> &fn) {
    QTimer::singleShot(50, &w, [&w, fn] {
      auto *d = w.findChild<QDialog *>("desktopSettingsDialog");
      QVERIFY(d);
      fn(d);
    });
    w.findChild<QAction *>("action_desktopSettings")->trigger();
  }
  QByteArray frame(const QByteArray &data) {
    QByteArray result(4, '\0');
    qToBigEndian<quint32>(data.size(), result.data());
    return result + data;
  }
  QString remote(const QString &interface, const QString &method,
                 const QStringList &args = {}) {
    QProcess p;
    p.start("gdbus", QStringList{"call", "--session", "--dest",
                                 "org.mpris.MediaPlayer2.FoxicMP",
                                 "--object-path", "/org/mpris/MediaPlayer2",
                                 "--method", interface + "." + method} +
                         args);
    if (!p.waitForFinished(4000) || p.exitCode())
      return {};
    return QString::fromUtf8(p.readAllStandardOutput());
  }
private slots:
  void initTestCase() {
    media = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                .filePath("chapters.mkv");
    QVERIFY(QFileInfo(media).isFile());
  }
  void strictRequestsAndSenderPaths() {
    QTemporaryDir dir;
    const auto old = QDir::currentPath();
    QVERIFY(QDir::setCurrent(dir.path()));
    LaunchRequest r;
    r.files = {"Тест с пробелами.mkv", "https://example.org/a.mp4"};
    QString error;
    QVERIFY(LaunchRequest::normalizeFiles(&r.files, &error));
    QCOMPARE(r.files[0], dir.filePath("Тест с пробелами.mkv"));
    QVERIFY(QDir::setCurrent(old));
    LaunchRequest decoded;
    QVERIFY(LaunchRequest::decode(r.encode(), &decoded));
    QCOMPARE(decoded.files, r.files);
    QVERIFY(!LaunchRequest::decode("{}", &decoded));
    r.session = dir.filePath("manifest.json");
    QVERIFY(!LaunchRequest::decode(r.encode(), &decoded));
    r.session.clear();
    r.files = {"relative.mkv"};
    QVERIFY(!LaunchRequest::decode(r.encode(), &decoded));
    r.files = {"gopher://invalid/x"};
    QVERIFY(!LaunchRequest::decode(r.encode(), &decoded));
    r.files = QStringList(257, media);
    QVERIFY(!LaunchRequest::decode(r.encode(), &decoded));
  }
  void processesForwardIsolationAndIndependent() {
    QTemporaryDir dir, other;
    LaunchRequest first;
    QList<LaunchRequest> received;
    InstanceBroker owner(dir.path());
    QCOMPARE(owner.start(first, true), InstanceBroker::Primary);
    owner.setReceiver([&](const LaunchRequest &r) { received << r; });
    QTRY_COMPARE(received.size(), 1);
    LaunchRequest next;
    next.files = {media};
    next.enqueue = true;
    next.activate = false;
    QProcess client;
    client.start(
        exe("foxicmp-instance-client"),
        {dir.path(), QString::fromLatin1(next.encode().toBase64()), "reuse"});
    QTRY_COMPARE(client.state(), QProcess::NotRunning);
    QCOMPARE(client.exitCode(), 0);
    QCOMPARE(client.readAllStandardOutput().trimmed(),
             QByteArray::number(InstanceBroker::Forwarded));
    QTRY_COMPARE(received.size(), 2);
    QCOMPARE(received.last().files, next.files);
    QVERIFY(received.last().enqueue);
    QVERIFY(!received.last().activate);
    InstanceBroker independent(dir.path());
    QCOMPARE(independent.start(next, false), InstanceBroker::Independent);
    QList<LaunchRequest> own;
    independent.setReceiver([&](const LaunchRequest &r) { own << r; });
    QTRY_COMPARE(own.size(), 1);
    QCOMPARE(received.size(), 2);
    InstanceBroker isolated(other.path());
    QCOMPARE(isolated.start(first, true), InstanceBroker::Primary);
    QVERIFY(isolated.serverName() != owner.serverName());
    const auto alias = other.filePath("profile-link");
    QVERIFY(QFile::link(dir.path(), alias));
    QCOMPARE(InstanceBroker::endpointFor(alias), owner.serverName());
  }
  void malformedAndFragmentedPeers() {
    QTemporaryDir dir;
    InstanceBroker owner(dir.path());
    LaunchRequest r;
    QCOMPARE(owner.start(r, true), InstanceBroker::Primary);
    QList<LaunchRequest> received;
    owner.setReceiver([&](const LaunchRequest &v) { received << v; });
    QTRY_COMPARE(received.size(), 1);
    QLocalSocket bad;
    bad.connectToServer(owner.serverName());
    QVERIFY(bad.waitForConnected());
    bad.write(frame("{}"));
    bad.flush();
    QTRY_VERIFY(bad.bytesAvailable() > 4);
    QVERIFY(!QJsonDocument::fromJson(bad.readAll().mid(4))
                 .object()
                 .value("accepted")
                 .toBool());
    QCOMPARE(received.size(), 1);
    QLocalSocket peer;
    peer.connectToServer(owner.serverName());
    QVERIFY(peer.waitForConnected());
    const auto packet = frame(r.encode());
    peer.write(packet.left(3));
    peer.flush();
    QTest::qWait(30);
    QCOMPARE(received.size(), 1);
    peer.write(packet.mid(3));
    peer.flush();
    QTRY_COMPARE(received.size(), 2);
    QLocalSocket huge;
    huge.connectToServer(owner.serverName());
    QVERIFY(huge.waitForConnected());
    QByteArray header(4, '\0');
    qToBigEndian<quint32>(262145, header.data());
    huge.write(header);
    huge.flush();
    QTRY_VERIFY(huge.bytesAvailable() > 4);
    QVERIFY(!QJsonDocument::fromJson(huge.readAll().mid(4))
                 .object()
                 .value("accepted")
                 .toBool());
    QLocalSocket stalled;
    stalled.connectToServer(owner.serverName());
    QVERIFY(stalled.waitForConnected());
    stalled.write("x");
    stalled.flush();
    QTRY_COMPARE_WITH_TIMEOUT(stalled.state(), QLocalSocket::UnconnectedState,
                              4500);
    QCOMPARE(received.size(), 2);
  }
  void nestedEventLoopKeepsDispatchSequential() {
    QTemporaryDir dir;
    LaunchRequest request;
    InstanceBroker owner(dir.path());
    QCOMPARE(owner.start(request, true), InstanceBroker::Primary);
    QProcess peer;
    bool inside = false, reentered = false, ackDuringHandler = false;
    int delivered = 0;
    owner.setReceiver([&](const LaunchRequest &) {
      if (inside)
        reentered = true;
      ++delivered;
      if (delivered != 1)
        return;
      inside = true;
      QEventLoop modal;
      connect(&peer, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
              &modal, [&] {
                ackDuringHandler = inside;
                modal.quit();
              });
      QTimer::singleShot(0, &modal, [&] {
        peer.start(exe("foxicmp-instance-client"),
                   {dir.path(),
                    QString::fromLatin1(request.encode().toBase64()), "reuse"});
      });
      QTimer::singleShot(6000, &modal, &QEventLoop::quit);
      modal.exec();
      inside = false;
    });
    QTRY_COMPARE_WITH_TIMEOUT(delivered, 2, 8000);
    QVERIFY(ackDuringHandler);
    QVERIFY(!reentered);
    QCOMPARE(peer.exitCode(), 0);
  }
  void hiddenWindowQuitExitsProcess() {
    QTemporaryDir dir;
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(),
                {"--hidden-quit-probe", dir.path()});
    QVERIFY(child.waitForFinished(8000));
    QCOMPARE(child.exitCode(), 0);
    QSettings saved(dir.filePath("FoxicMPQt.ini"), QSettings::IniFormat);
    QVERIFY(saved.contains("window/geometry"));
  }
  void crashReleasesProfileLease() {
    QTemporaryDir dir;
    LaunchRequest r;
    QProcess child;
    child.start(
        exe("foxicmp-instance-client"),
        {dir.path(), QString::fromLatin1(r.encode().toBase64()), "hold"});
    QTRY_VERIFY(child.bytesAvailable() > 0);
    QCOMPARE(child.readAllStandardOutput().trimmed(),
             QByteArray::number(InstanceBroker::Primary));
    child.kill();
    QVERIFY(child.waitForFinished());
    InstanceBroker recovered(dir.path());
    QCOMPARE(recovered.start(r, true), InstanceBroker::Primary);
  }
  void trayCloseRestoreLossAndExplicitExit() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    prepare(profile);
    profile.settings.setValue("desktop/tray", true);
    profile.settings.setValue("desktop/closeToTray", true);
    bool available = true;
    Window w(profile, {}, {}, [&] { return available; });
    load(w);
    auto *p = w.findChild<Player *>();
    QVERIFY(!w.close());
    QVERIFY(w.isHidden());
    QVERIFY(!profile.settings.contains("window/geometry"));
    QVERIFY(p->get("pause").toBool());
    // Existing external/input commands may show the window without using the
    // tray's Restore action. Closing it must hide the newly visible state.
    w.findChild<QAction *>("action_fullscreen")->trigger();
    QTRY_VERIFY(w.isVisible());
    QVERIFY(!w.close());
    QVERIFY(w.isHidden());
    w.restoreFromTray();
    w.findChild<QAction *>("action_fullscreen")->trigger();
    QVERIFY(!w.close());
    QVERIFY(w.isHidden());
    auto *menu = w.findChild<QMenu *>("desktopTrayMenu");
    QVERIFY(menu);
    menu->findChild<QAction *>("trayRestore")->trigger();
    QTRY_VERIFY(w.isVisible());
    QVERIFY(p->get("pause").toBool());
    QVERIFY(std::abs(p->get("time-pos").toDouble() - 2) < .1);
    w.showMaximized();
    QTRY_VERIFY(w.isMaximized());
    QVERIFY(!w.close());
    w.restoreFromTray();
    QTRY_VERIFY(w.isMaximized());
    w.showNormal();
    w.findChild<QAction *>("action_fullscreen")->trigger();
    QTRY_VERIFY(w.isFullScreen());
    QVERIFY(!w.close());
    w.restoreFromTray();
    QTRY_VERIFY(w.isFullScreen());
    w.findChild<QAction *>("action_fullscreen")->trigger();
    QVERIFY(!w.close());
    LaunchRequest enqueue;
    enqueue.files = {media};
    enqueue.enqueue = true;
    enqueue.activate = false;
    w.handleLaunchRequest(enqueue);
    QVERIFY(w.isHidden());
    QVERIFY(p->get("pause").toBool());
    available = false;
    QTRY_VERIFY_WITH_TIMEOUT(w.isVisible(), 1500);
    QVERIFY(w.close());
    QVERIFY(profile.settings.contains("window/geometry"));
    available = true;
    w.show();
    QVERIFY(!w.close());
    w.exitApplication();
    QVERIFY(!w.isVisible());
  }
  void unavailableTrayNeverHidesAndMinimizePause() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    prepare(profile);
    profile.settings.setValue("desktop/tray", true);
    profile.settings.setValue("desktop/closeToTray", true);
    Window unavailable(profile, {}, {}, [] { return false; });
    unavailable.show();
    QVERIFY(unavailable.close());
    profile.settings.setValue("desktop/minimizeToTray", true);
    profile.settings.setValue("playback/pauseMinimized", true);
    Window w(profile, {}, {}, [] { return true; });
    load(w);
    auto *p = w.findChild<Player *>();
    p->set("pause", false);
    QTRY_VERIFY(!p->get("pause").toBool());
    w.showMinimized();
    QTRY_VERIFY(w.isHidden());
    QTRY_VERIFY(p->get("pause").toBool());
    w.restoreFromTray();
    QTRY_VERIFY(w.isVisible());
    QTRY_VERIFY(!p->get("pause").toBool());
    p->set("pause", true);
    QTRY_VERIFY(p->get("pause").toBool());
    w.showMinimized();
    QTRY_VERIFY(w.isHidden());
    w.restoreFromTray();
    QVERIFY(p->get("pause").toBool());
    w.exitApplication();
  }
  void settingsCancelApplyResetAndHiddenDisable() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    prepare(profile);
    Window w(profile, {}, {}, [] { return true; });
    w.show();
    edit(w, [](QDialog *d) {
      d->findChild<QCheckBox *>("instance/reuseExisting")->setChecked(true);
      d->reject();
    });
    QVERIFY(!profile.settings.value("instance/reuseExisting", false).toBool());
    edit(w, [](QDialog *d) {
      d->findChild<QCheckBox *>("instance/reuseExisting")->setChecked(true);
      d->findChild<QCheckBox *>("desktop/tray")->setChecked(true);
      d->findChild<QCheckBox *>("desktop/closeToTray")->setChecked(true);
      const auto images = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      if (!images.isEmpty()) {
        QDir().mkpath(images);
        QVERIFY(d->grab().save(QDir(images).filePath("desktop-settings.png")));
      }
      d->accept();
    });
    QVERIFY(profile.settings.value("instance/reuseExisting").toBool());
    auto *menu = w.findChild<QMenu *>("desktopTrayMenu");
    QVERIFY(menu);
    QVERIFY(menu->actions().contains(w.findChild<QAction *>("action_play")));
    const auto images = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!images.isEmpty()) {
      menu->popup(QPoint(30, 30));
      QTest::qWait(50);
      QVERIFY(menu->grab().save(QDir(images).filePath("tray-menu.png")));
      menu->hide();
    }
    QVERIFY(!w.close());
    edit(w, [](QDialog *d) {
      for (auto *b : d->findChildren<QPushButton *>())
        if (b->text().contains("Сбросить"))
          b->click();
      d->accept();
    });
    QTRY_VERIFY(w.isVisible());
    QVERIFY(!profile.settings.value("desktop/tray").toBool());
    QVERIFY(!profile.settings.value("instance/reuseExisting").toBool());
    QVERIFY(profile.settings.value("instance/activateOnOpen").toBool());
    w.exitApplication();
  }
  void cliQueriesProfilesAndRealForwarding() {
    QTemporaryDir dir;
    const auto profileDir = dir.filePath("profile");
    for (const auto &flag : {"--help", "--version"}) {
      QProcess query;
      query.start(exe("foxicmp-qt"), {"--profile", profileDir, flag});
      QVERIFY(query.waitForFinished(8000));
      QCOMPARE(query.exitCode(), 0);
      QVERIFY(!QDir(profileDir).exists());
    }
    QProcess invalid;
    invalid.start(exe("foxicmp-qt"), {"--profile", profileDir, "--new-instance",
                                      "--reuse-instance"});
    QVERIFY(invalid.waitForFinished(8000));
    QVERIFY(invalid.exitCode() != 0);
    QVERIFY(!QDir(profileDir).exists());
    QVERIFY(QDir().mkpath(profileDir));
    {
      Profile profile(profileDir);
      prepare(profile);
      profile.settings.setValue("input/mpris", true);
      profile.settings.setValue("instance/reuseExisting", true);
      profile.settings.sync();
    }
    QProcess primary;
    primary.setProcessChannelMode(QProcess::MergedChannels);
    primary.start(exe("foxicmp-qt"), {"--profile", profileDir, media});
    // Ensure child is reaped even when a verification returns early.
    struct Reap {
      QProcess &p;
      ~Reap() {
        if (p.state() != QProcess::NotRunning) {
          p.kill();
          p.waitForFinished();
        }
      }
    } reap{primary};
    const QString props = "org.freedesktop.DBus.Properties",
                  player = "org.mpris.MediaPlayer2.Player";
    QTRY_VERIFY_WITH_TIMEOUT(
        remote(props, "Get", {player, "PlaybackStatus"}).contains("Playing"),
        12000);
    remote(player, "Pause");
    QVERIFY(
        remote(props, "Get", {player, "PlaybackStatus"}).contains("Paused"));
    const auto position = remote(props, "Get", {player, "Position"});
    const auto relative = "Тест с пробелами.mkv";
    QVERIFY(QFile::link(media, dir.filePath(relative)));
    QProcess client;
    client.setWorkingDirectory(dir.path());
    client.start(exe("foxicmp-qt"), {"--profile", profileDir, "--enqueue",
                                     "--no-activate", relative});
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QProcess::NotRunning, 10000);
    QCOMPARE(client.exitCode(), 0);
    QVERIFY(
        remote(props, "Get", {player, "PlaybackStatus"}).contains("Paused"));
    QCOMPARE(remote(props, "Get", {player, "Position"}), position);
    QProcess independent;
    independent.start(exe("foxicmp-qt"),
                      {"--profile", profileDir, "--new-instance"});
    QVERIFY(independent.waitForStarted());
    QTest::qWait(800);
    QCOMPARE(independent.state(), QProcess::Running);
    independent.kill();
    QVERIFY(independent.waitForFinished());
    remote("org.mpris.MediaPlayer2", "Quit");
    QTRY_COMPARE(primary.state(), QProcess::NotRunning);
    QCOMPARE(primary.exitCode(), 0);
    QSettings saved(QDir(profileDir).filePath("FoxicMPQt.ini"),
                    QSettings::IniFormat);
    const auto tabs = saved.value("playlist/tabsV1").toString();
    QVERIFY2(tabs.contains(relative), qPrintable(tabs));
    QVERIFY(!QFileInfo(QDir(dir.path()).filePath("FoxicMPQt.ini")).exists());
  }
};
int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  Q_INIT_RESOURCE(resources);
  const auto args = app.arguments();
  if (args.size() == 3 && args[1] == "--hidden-quit-probe") {
    Profile profile(args[2]);
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("desktop/tray", true);
    profile.settings.setValue("desktop/closeToTray", true);
    Window window(profile, {}, {}, [] { return true; });
    window.show();
    QTimer::singleShot(100, &window, [&] {
      window.close();
      if (!window.isHidden()) {
        app.exit(8);
        return;
      }
      QTimer::singleShot(100, &window, &Window::exitApplication);
    });
    QTimer::singleShot(3000, &app, [&] { app.exit(9); });
    return app.exec();
  }
  DesktopIntegrationTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "desktopintegration_test.moc"
