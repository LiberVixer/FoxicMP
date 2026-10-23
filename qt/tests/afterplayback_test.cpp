#include "player.h"
#include "profile.h"
#include "systemactions.h"
#include "theme.h"
#include "window.h"
#include <QDBusContext>
#include <QtTest>
#include <QtWidgets>

// Exported only on a private test bus: these methods never touch the OS.
class FakeLogin : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Manager")
public:
  QString capability = "yes";
  QStringList calls;
public slots:
  QString CanSuspend() { return capability; }
  QString CanHibernate() { return capability; }
  QString CanPowerOff() { return capability; }
  void Suspend(bool interactive) {
    calls.append(interactive ? "Suspend" : "wrong");
  }
  void Hibernate(bool) {
    sendErrorReply("org.freedesktop.DBus.Error.AccessDenied", "test denial");
  }
  void PowerOff(bool interactive) {
    calls.append(interactive ? "PowerOff" : "wrong");
  }
};
class FakeScreen : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.cinnamon.ScreenSaver")
public:
  QString message;
public slots:
  void Lock(const QString &text) { message = text; }
};
class FakeSession : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.gnome.SessionManager")
public:
  uint mode = 99;
public slots:
  void Logout(uint value) { mode = value; }
};

class AfterPlaybackTest : public QObject {
  Q_OBJECT
  QString fixture() const {
    return QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
        .filePath("chapters.mkv");
  }
  void prepare(Profile &p) {
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("audio/mute", true);
  }
  QAction *action(Window &w, const QString &id) {
    return w.findChild<QAction *>(id);
  }
  void endFile(Player *p) {
    p->set("pause", false);
    p->command({"seek", "7.85", "absolute+exact"});
  }
private slots:
  void closeKeepsQueue() {
    QTemporaryDir d;
    Profile profile(d.path());
    prepare(profile);
    Window w(profile);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded), ended(p, &Player::ended);
    action(w, "afterAlways_close")->trigger();
    const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty()) {
      QDir().mkpath(screenshots);
      auto *menu = w.findChild<QMenu *>("afterPlaybackMenu");
      for (int index = 0; index < 2; ++index) {
        auto *sub = menu->actions()[index]->menu();
        sub->popup(w.mapToGlobal(QPoint(80, 80)));
        QTest::qWait(100);
        QVERIFY(sub->isVisible());
        sub->grab().save(screenshots +
                         (index ? "/after-always.png" : "/after-once.png"));
        sub->hide();
      }
    }
    w.openFiles({fixture(), fixture()});
    QTRY_COMPARE(loaded.size(), 1);
    endFile(p);
    QTRY_COMPARE(loaded.size(),
                 2); // First EOF advances; action waits for the list.
    QVERIFY(w.isVisible());
    endFile(p);
    QTRY_COMPARE(ended.size(), 2);
    QTRY_COMPARE(profile.settings.value("files/currentIndex").toInt(), -1);
    QCOMPARE(w.findChild<QListWidget *>("playlist")->count(), 2);
    QTest::qWait(200);
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(profile.position(fixture()), 0.0);
    profile.settings.sync();
    Profile restored(d.path());
    QCOMPARE(restored.settings.value("playback/after").toString(),
             QString("close"));
  }
  void onceActionsAndStops() {
    QTemporaryDir d;
    Profile profile(d.path());
    prepare(profile);
    QStringList requests;
    Window w(profile, [&](const QString &id) { requests.append(id); });
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded);
    action(w, "afterOnce_shutdown")->trigger();
    QVERIFY(action(w, "afterOnce_shutdown")->isChecked());
    QVERIFY(!profile.settings.contains("playback/after"));
    w.openFiles({fixture()});
    QTRY_COMPARE(loaded.size(), 1);
    action(w, "action_stop")->trigger();
    QTest::qWait(200);
    QCOMPARE(requests.size(), 0);
    QVERIFY(action(w, "afterOnce_shutdown")->isChecked());
    w.openFiles({fixture() + ".missing"});
    QTRY_VERIFY(p->get("idle-active").toBool());
    QTest::qWait(200);
    QCOMPARE(requests.size(), 0);
    w.openFiles({fixture()});
    QTRY_COMPARE(loaded.size(), 2);
    endFile(p);
    QTRY_COMPARE(requests, QStringList{"shutdown"});
    QVERIFY(action(w, "afterOnce_none")->isChecked());
    w.openFiles({fixture()});
    QTRY_COMPARE(loaded.size(), 3);
    endFile(p);
    QTRY_VERIFY(p->get("idle-active").toBool());
    QTest::qWait(100);
    QCOMPARE(requests.size(), 1);
    int count = 3;
    for (const auto &id : {"suspend", "hibernate", "logout", "lock"}) {
      action(w, "afterOnce_" + QString(id))->trigger();
      w.openFiles({fixture()});
      ++count;
      QTRY_COMPARE(loaded.size(), count);
      endFile(p);
      QTRY_COMPARE(requests.last(), QString(id));
      QVERIFY(action(w, "afterOnce_none")->isChecked());
    }
    action(w, "afterOnce_lock")->trigger();
    action(w, "afterOnce_none")->trigger();
    QCOMPARE(requests.size(), 5);
  }
  void repeatAndShuffleBoundary() {
    QTemporaryDir d;
    Profile profile(d.path());
    prepare(profile);
    QStringList requests;
    Window w(profile, [&](const QString &id) { requests.append(id); });
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded);
    action(w, "action_shuffle")->trigger();
    action(w, "action_repeatAll")->trigger();
    action(w, "afterOnce_lock")->trigger();
    w.openFiles({fixture(), fixture(), fixture()});
    QTRY_COMPARE(loaded.size(), 1);
    for (int count = 2; count <= 3; ++count) {
      endFile(p);
      QTRY_COMPARE(loaded.size(), count);
      QCOMPARE(requests.size(), 0);
    }
    endFile(p);
    QTRY_COMPARE(requests, QStringList{"lock"});
    QTest::qWait(200);
    QCOMPARE(loaded.size(), 3); // Explicit action precedes repeat.
  }
  void directoryNavigation() {
    QTemporaryDir d;
    Profile profile(d.path());
    prepare(profile);
    QVERIFY(QFile::copy(fixture(), d.filePath("movie2.mkv")));
    QVERIFY(QFile::copy(fixture(), d.filePath("movie10.mkv")));
    QFile text(d.filePath("movie3.txt"));
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("not media");
    text.close();
    Window w(profile);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded);
    action(w, "afterAlways_nextDirectory")->trigger();
    w.openFiles({d.filePath("movie2.mkv")});
    QTRY_COMPARE(loaded.size(), 1);
    endFile(p);
    QTRY_COMPARE(loaded.size(), 2);
    QCOMPARE(profile.recentFiles().first(), d.filePath("movie10.mkv"));
    endFile(p);
    QTRY_VERIFY(p->get("idle-active").toBool());
    QTest::qWait(200);
    QCOMPARE(loaded.size(), 2);
    action(w, "afterAlways_loopDirectory")->trigger();
    action(w, "action_play")->trigger();
    QTRY_COMPARE(loaded.size(), 3);
    endFile(p);
    QTRY_COMPARE(loaded.size(), 4);
    QCOMPARE(profile.recentFiles().first(), d.filePath("movie2.mkv"));
    QCOMPARE(w.findChild<QListWidget *>("playlist")->count(), 2);
    action(w, "action_stop")->trigger();
  }
  void minimizeAndExit() {
    for (const auto &id : {"minimize", "exit"}) {
      QTemporaryDir d;
      Profile profile(d.path());
      prepare(profile);
      Window w(profile);
      w.show();
      QVERIFY(QTest::qWaitForWindowExposed(&w));
      auto *p = w.findChild<Player *>();
      QSignalSpy loaded(p, &Player::loaded);
      action(w, "afterAlways_" + QString(id))->trigger();
      w.openFiles({fixture()});
      QTRY_COMPARE(loaded.size(), 1);
      endFile(p);
      if (QString(id) == "minimize") {
        QTRY_VERIFY(w.isMinimized());
        QCOMPARE(profile.settings.value("files/currentIndex").toInt(), -1);
      } else
        QTRY_VERIFY(!w.isVisible());
    }
  }
  void isolatedSystemTransport() {
    QProcess daemon;
    daemon.start("dbus-daemon", {"--session", "--nofork", "--print-address=1"});
    QVERIFY(daemon.waitForStarted());
    QVERIFY(daemon.waitForReadyRead());
    const QString address = QString::fromUtf8(daemon.readLine()).trimmed();
    auto bus = QDBusConnection::connectToBus(address, "foxicmp-after-test");
    QVERIFY(bus.isConnected());
    FakeLogin login;
    FakeScreen screen;
    FakeSession session;
    QVERIFY(bus.registerService("org.freedesktop.login1"));
    QVERIFY(bus.registerObject("/org/freedesktop/login1", &login,
                               QDBusConnection::ExportAllSlots));
    QVERIFY(bus.registerService("org.cinnamon.ScreenSaver"));
    QVERIFY(bus.registerObject("/org/cinnamon/ScreenSaver", &screen,
                               QDBusConnection::ExportAllSlots));
    QVERIFY(bus.registerService("org.gnome.SessionManager"));
    QVERIFY(bus.registerObject("/org/gnome/SessionManager", &session,
                               QDBusConnection::ExportAllSlots));
    bool done = false;
    QString error;
    auto dispatch = [&](const QString &id) {
      done = false;
      error.clear();
      SystemActions::execute(
          id, this,
          [&](const QString &e) {
            error = e;
            done = true;
          },
          bus);
    };
    dispatch("suspend");
    QTRY_VERIFY(done);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(login.calls, QStringList{"Suspend"});
    login.capability = "no";
    dispatch("shutdown");
    QTRY_VERIFY(done);
    QVERIFY(!error.isEmpty());
    QCOMPARE(login.calls.size(), 1);
    login.capability = "challenge";
    dispatch("shutdown");
    QTRY_VERIFY(done);
    QVERIFY(error.isEmpty());
    QCOMPARE(login.calls.last(), QString("PowerOff"));
    dispatch("hibernate");
    QTRY_VERIFY(done);
    QVERIFY(error.contains("test denial"));
    dispatch("lock");
    QTRY_VERIFY(done);
    QVERIFY(error.isEmpty());
    QVERIFY(screen.message.contains("FoxicMP"));
    dispatch("logout");
    QTRY_VERIFY(done);
    QVERIFY(error.isEmpty());
    QCOMPARE(session.mode, 0U);
    bus.unregisterService("org.cinnamon.ScreenSaver");
    dispatch("lock");
    QTRY_VERIFY(done);
    QVERIFY(!error.isEmpty());
    QDBusConnection::disconnectFromBus("foxicmp-after-test");
    daemon.kill();
    QVERIFY(daemon.waitForFinished());
  }
  void systemRequestContracts() {
    for (const auto &pair :
         {qMakePair(QString("suspend"), QString("Suspend")),
          qMakePair(QString("hibernate"), QString("Hibernate")),
          qMakePair(QString("shutdown"), QString("PowerOff"))}) {
      const auto requests = SystemActions::requests(pair.first);
      QCOMPARE(requests.size(), 1);
      QVERIFY(requests.first().systemBus);
      QCOMPARE(requests.first().method, pair.second);
      QCOMPARE(requests.first().arguments, QVariantList{true});
    }
    QCOMPARE(SystemActions::requests("logout").first().arguments,
             QVariantList{QVariant::fromValue<uint>(0)});
    QVERIFY(SystemActions::requests("none").isEmpty());
    bool finished = false;
    QString error;
    SystemActions::execute("invalid", this, [&](const QString &e) {
      error = e;
      finished = true;
    });
    QTRY_VERIFY(finished);
    QVERIFY(!error.isEmpty());
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  Q_INIT_RESOURCE(resources);
  applyTheme(app);
  AfterPlaybackTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "afterplayback_test.moc"
