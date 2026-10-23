#include "player.h"
#include "profile.h"
#include "systemicc.h"
#include "videodisplay.h"
#include "window.h"
#include "preferencesdriver.h"
#include <QColorSpace>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QtTest>
#include <QtWidgets>
#include <unistd.h>

class FakeColord : public QDBusVirtualObject {
public:
  QMap<QString, QString> files;
  QStringList methods;
  QString delayed;
  int delay = 0;
  bool enabled = true, inhibited = false, wrongScreen = false;
  QString introspect(const QString &) const override { return {}; }
  static QString suffix(const QString &screen) {
    return QString::fromLatin1(screen.toUtf8().toHex());
  }
  bool handleMessage(const QDBusMessage &m,
                     const QDBusConnection &bus) override {
    methods << m.member();
    const auto error = [&] {
      bus.send(m.createErrorReply("org.freedesktop.ColorManager.NothingMatched",
                                  "No active profile"));
      return true;
    };
    if (m.member() == "FindDeviceByProperty") {
      const auto args = m.arguments();
      if (args.size() != 2 || args[0].toString() != "XRANDR_name" ||
          !files.contains(args[1].toString()))
        return error();
      const auto screen = args[1].toString();
      const auto reply = m.createReply(
          QVariant::fromValue(QDBusObjectPath("/device/" + suffix(screen))));
      if (screen == delayed && delay)
        QTimer::singleShot(delay, this, [bus, reply] { bus.send(reply); });
      else
        bus.send(reply);
      return true;
    }
    const auto key = QByteArray::fromHex(m.path().section('/', -1).toLatin1());
    const auto screen = QString::fromUtf8(key);
    if (!files.contains(screen))
      return error();
    if (m.member() == "GetProfileForQualifiers") {
      if (!enabled || inhibited ||
          qdbus_cast<QStringList>(m.arguments().value(0)) != QStringList{"*"})
        return error();
      bus.send(m.createReply(
          QVariant::fromValue(QDBusObjectPath("/profile/" + suffix(screen)))));
      return true;
    }
    if (m.member() == "GetAll") {
      QVariantMap props;
      if (m.path().startsWith("/device/"))
        props = {
            {"Enabled", enabled},
            {"Kind", "display"},
            {"Owner", uint(getuid())},
            {"Metadata", QVariant::fromValue(QMap<QString, QString>{
                             {"XRANDR_name", wrongScreen ? "OTHER" : screen}})},
            // Deliberately unrelated: clients must ask colord for the
            // default instead of selecting the first Profiles element.
            {"Profiles", QVariant::fromValue(QList<QDBusObjectPath>{
                             QDBusObjectPath("/profile/incorrect")})}};
      else
        props = {{"Filename", files.value(screen)}};
      bus.send(m.createReply(QVariant::fromValue(props)));
      return true;
    }
    return error(); // No mutation methods should ever be called.
  }
};

class SystemIccTest : public QObject {
  Q_OBJECT
  QTemporaryDir fixtures;
  QString a, b;
  QByteArray bytesA, bytesB;
  QDBusConnection bus = QDBusConnection::sessionBus();
  QDBusConnection serviceBus = QDBusConnection::connectToBus(
      QDBusConnection::SessionBus, "foxicmp-system-icc-service");
  FakeColord fake;
  QJsonObject measurements;
  void editDialog(Window &w, const std::function<void(QDialog *)> &edit,
                  const QString &action = "videoOutputSettings") {
    const auto objectName = action == "videoOutputSettings"
                                ? QStringLiteral("videoOutputDialog")
                                : action == "preferences"
                                      ? QStringLiteral("preferencesDialog")
                                      : QString();
    afterDialogReady(&w, objectName, [edit](QDialog *d) { edit(d); });
    auto *a = w.findChild<QAction *>("action_" + action);
    QVERIFY(a);
    a->trigger();
  }
private slots:
  void initTestCase() {
    QVERIFY(fixtures.isValid());
    qDBusRegisterMetaType<QMap<QString, QString>>();
    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    bytesA = QColorSpace(QColorSpace::SRgb).iccProfile();
    bytesB = QColorSpace(QColorSpace::DisplayP3).iccProfile();
    QVERIFY(bytesA != bytesB);
    QVERIFY(VideoDisplay::validateIcc(bytesA).isEmpty());
    QVERIFY(VideoDisplay::validateIcc(bytesB).isEmpty());
    a = fixtures.filePath("a.icc");
    b = fixtures.filePath("b.icc");
    for (const auto &entry :
         QList<QPair<QString, QByteArray>>{{a, bytesA}, {b, bytesB}}) {
      QFile f(entry.first);
      QVERIFY(f.open(QIODevice::WriteOnly));
      QCOMPARE(f.write(entry.second), entry.second.size());
    }
    QVERIFY(bus.isConnected());
    QVERIFY(serviceBus.isConnected());
    QVERIFY(serviceBus.registerService("org.freedesktop.ColorManager"));
    QVERIFY(
        serviceBus.registerVirtualObject("/", &fake, QDBusConnection::SubPath));
  }
  void init() {
    fake.files = {{"A", a}, {"B", b}};
    fake.methods.clear();
    fake.delayed.clear();
    fake.delay = 0;
    fake.enabled = true;
    fake.inhibited = fake.wrongScreen = false;
  }
  void exactOutputDefaultDedupAndRemoval() {
    QTemporaryDir dir;
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(480, 270);
    p.show();
    p.set("mute", true);
    p.set("hwdec", "no");
    const auto media = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                           .filePath(QString::fromUtf8("Русский H264.mp4"));
    QVERIFY2(QFile::exists(media), qPrintable(media));
    QSignalSpy loaded(&p, &Player::loaded);
    QVERIFY(p.loadSource(media, {{"pause", "yes"}}));
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(p.get("video-out-params").toMap().value("w").toInt() > 0);
    // A non-null buffer can still precede delivery of the first decoded
    // frame, especially just after the deferred OpenGL -> Software fallback.
    // Wait for the color bars of the known testsrc2 fixture, not allocation.
    QImage before;
    const auto decodedColorBars = [&] {
      before = p.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
      bool red = false, green = false, blue = false;
      for (int y = 0; y < before.height(); y += 8)
        for (int x = 0; x < before.width(); x += 8) {
          const auto c = before.pixelColor(x, y);
          red |= c.red() > 100 && c.green() < 50 && c.blue() < 50;
          green |= c.green() > 100 && c.red() < 50 && c.blue() < 50;
          blue |= c.blue() > 100 && c.red() < 50 && c.green() < 50;
        }
      return red && green && blue;
    };
    QTRY_VERIFY_WITH_TIMEOUT(decodedColorBars(), 3000);
    SystemIcc controller(&p, bus, "xcb", false);
    controller.configure(true, dir.path());
    controller.refresh("A");
    QTRY_VERIFY_WITH_TIMEOUT(
        !p.QObject::property("foxicmpSystemIccFile").toString().isEmpty(),
        3000);
    const auto savedA = p.QObject::property("foxicmpSystemIccFile").toString();
    QString error;
    QCOMPARE(VideoDisplay::readIcc(p.get("icc-profile").toString(), &error),
             bytesA);
    QVERIFY(fake.methods.contains("GetProfileForQualifiers"));
    controller.refresh("B");
    QCOMPARE(p.get("icc-profile").toString(), QString()); // Never reuse A on B.
    QTRY_COMPARE(p.QObject::property("foxicmpSystemIccOutput").toString(),
                 QString("B"));
    QTRY_VERIFY(
        p.QObject::property("foxicmpSystemIccFile").toString() != QString() &&
        p.QObject::property("foxicmpSystemIccFile").toString() != savedA);
    QCOMPARE(VideoDisplay::readIcc(p.get("icc-profile").toString(), &error),
             bytesB);
    QVERIFY(p.get("pause").toBool());
    if (p.outputName() == "Software")
      QCOMPARE(p.grabFramebuffer().convertToFormat(QImage::Format_RGB32),
               before);
    controller.refresh("A");
    QTRY_COMPARE(p.QObject::property("foxicmpSystemIccFile").toString(),
                 savedA);
    QCOMPARE(QDir(dir.filePath("ColorProfiles")).entryList(QDir::Files).size(),
             2);
    fake.files["A"] = b; // Same output, changed desktop assignment.
    controller.refresh("A");
    QTRY_VERIFY(p.QObject::property("foxicmpSystemIccFile").toString() !=
                savedA);
    fake.files.remove("A");
    controller.refresh("A");
    QTRY_COMPARE(p.get("icc-profile").toString(), QString());
    QVERIFY(!QFile::exists(dir.filePath("FoxicMPQt.ini")));
    QVERIFY(p.set("pause", false));
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > .2, 3000);
    measurements["pauseAndPlaybackPreserved"] = true;
    measurements["twoOutputProfilesAndAssignmentChangesVerified"] = true;
    measurements["defaultQualifierNotProfilesZeroVerified"] = true;
  }
  void disabledInhibitedWrongOutputAndInvalidProfile() {
    QTemporaryDir dir;
    Player p;
    SystemIcc controller(&p, bus, "xcb", false);
    controller.configure(true, dir.path());
    for (int scenario = 0; scenario < 5; ++scenario) {
      fake.enabled = scenario != 0;
      fake.inhibited = scenario == 1;
      fake.wrongScreen = scenario == 2;
      fake.files["A"] = scenario == 3   ? "relative.icc"
                        : scenario == 4 ? fixtures.filePath("missing.icc")
                                        : a;
      controller.refresh("A");
      QTRY_VERIFY(p.QObject::property("foxicmpIccStatus")
                      .toString()
                      .contains("ICC не применяется"));
      QCOMPARE(p.get("icc-profile").toString(), QString());
      QVERIFY(!QDir(dir.filePath("ColorProfiles")).exists());
    }
    measurements["disabledInhibitedWrongOutputInvalidFileVerified"] = true;
  }
  void windowTrackingAndProfileContentPolling() {
    QTemporaryDir dir;
    const auto changing = fixtures.filePath("changing.icc");
    const auto replace = [&](const QByteArray &bytes) {
      QFile f(changing);
      QVERIFY(f.open(QIODevice::WriteOnly));
      QCOMPARE(f.write(bytes), bytes.size());
    };
    replace(bytesA);
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(480, 270);
    p.show();
    QVERIFY(p.windowHandle() && p.windowHandle()->screen());
    // The window manager may move a newly shown window onto another monitor.
    // Keep the same fixture assigned to every real output and verify tracking
    // against the window's current screen after the asynchronous reply.
    for (auto *screen : QGuiApplication::screens())
      fake.files[screen->name()] = changing;
    SystemIcc controller(&p, bus, "xcb");
    controller.configure(true, dir.path());
    QTRY_VERIFY2_WITH_TIMEOUT(
        !p.QObject::property("foxicmpSystemIccFile").toString().isEmpty(),
        qPrintable(p.QObject::property("foxicmpIccStatus").toString() +
                   "; methods=" + fake.methods.join(',')),
        10000);
    QCOMPARE(p.QObject::property("foxicmpSystemIccOutput").toString(),
             p.windowHandle()->screen()->name());
    QString error;
    QCOMPARE(VideoDisplay::readIcc(p.get("icc-profile").toString(), &error),
             bytesA);
    const auto initial = p.get("icc-profile").toString();
    replace(bytesB); // Same filename: content changes must also be discovered.
    QTRY_VERIFY_WITH_TIMEOUT(p.get("icc-profile").toString() != initial, 8000);
    QCOMPARE(VideoDisplay::readIcc(p.get("icc-profile").toString(), &error),
             bytesB);
    QCOMPARE(QDir(dir.filePath("ColorProfiles")).entryList(QDir::Files).size(),
             2);
    measurements["realQtWindowAndFiveSecondContentPollingVerified"] = true;
  }
  void repeatedScreenEventsKeepPendingRequest() {
    QTemporaryDir dir;
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(480, 270);
    p.show();
    QVERIFY(QTest::qWaitForWindowExposed(&p));
    for (auto *screen : QGuiApplication::screens())
      fake.files[screen->name()] = a;
    fake.delayed = p.windowHandle()->screen()->name();
    fake.delay = 700;
    SystemIcc controller(&p, bus, "xcb");
    controller.configure(true, dir.path());
    QTRY_VERIFY(fake.methods.contains("FindDeviceByProperty"));
    const auto output = p.windowHandle()->screen()->name();
    QTimer repeated;
    connect(&repeated, &QTimer::timeout, &p, [&] {
      p.windowHandle()->screenChanged(p.windowHandle()->screen());
    });
    repeated.start(30);
    QTimer::singleShot(500, &repeated, &QTimer::stop);
    QTRY_VERIFY_WITH_TIMEOUT(
        !p.QObject::property("foxicmpSystemIccFile").toString().isEmpty(), 2000);
    repeated.stop();
    // A real move to another output legitimately starts another lookup.
    if (p.windowHandle()->screen()->name() == output)
      QCOMPARE(fake.methods.count("FindDeviceByProperty"), 1);
  }
  void staleRepliesModeChangeTimeoutAndWayland() {
    QTemporaryDir dir;
    Player p;
    SystemIcc controller(&p, bus, "xcb", false);
    controller.configure(true, dir.path());
    fake.delayed = "A";
    fake.delay = 250;
    controller.refresh("A");
    QTest::qWait(30);
    controller.refresh("B");
    QTRY_VERIFY(
        !p.QObject::property("foxicmpSystemIccFile").toString().isEmpty());
    const auto savedB = p.get("icc-profile").toString();
    QTest::qWait(300);
    QCOMPARE(p.get("icc-profile").toString(), savedB);
    controller.refresh("A");
    QTest::qWait(30);
    controller.configure(false, dir.path());
    QVERIFY(
        p.set("icc-profile", a)); // Switch back to manual while query pending.
    QTest::qWait(300);
    QCOMPARE(p.get("icc-profile").toString(), a);
    controller.configure(true, dir.path());
    fake.delay = 1900;
    int heartbeats = 0;
    QTimer timer;
    connect(&timer, &QTimer::timeout, &p, [&] { ++heartbeats; });
    timer.start(20);
    QElapsedTimer requestTime;
    requestTime.start();
    controller.refresh("A");
    QVERIFY(requestTime.elapsed() < 500);
    QTRY_VERIFY_WITH_TIMEOUT(p.QObject::property("foxicmpIccStatus")
                                 .toString()
                                 .contains("ICC не применяется"),
                             3000);
    QVERIFY(heartbeats > 20);
    QCOMPARE(p.get("icc-profile").toString(), QString());
    QTest::qWait(500); // The late successful reply must not undo the timeout.
    QCOMPARE(p.get("icc-profile").toString(), QString());
    controller.configure(false, dir.path());
    SystemIcc wayland(&p, bus, "wayland", false);
    wayland.configure(true, dir.path());
    const int calls = fake.methods.size();
    wayland.refresh("A");
    QVERIFY(
        p.QObject::property("foxicmpIccStatus").toString().contains("Wayland"));
    QCOMPARE(fake.methods.size(), calls);
    measurements["staleRepliesAndTimeoutNonblockingVerified"] = true;
    measurements["waylandAutoBypassedVerified"] = true;
  }
  void preferencesCancelApplyResetAndManualPreserved() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("video/iccProfile", "missing-manual.icc");
    QCOMPARE(
        VideoDisplay::values(profile.settings).value("iccSource").toString(),
        QString("manual"));
    Window w(profile);
    w.show();
    auto *p = w.findChild<Player *>();
    QVERIFY(p);
    const auto fill = [](QDialog *d) {
      d->findChild<QCheckBox *>("video/iccEnabled")->setChecked(true);
      auto *source = d->findChild<QComboBox *>("video/iccSource");
      source->setCurrentIndex(source->findData("system"));
      QVERIFY(!d->findChild<QPushButton *>("videoIccBrowse")->isEnabled());
    };
    editDialog(w, [&](QDialog *d) {
      fill(d);
      d->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Cancel)
          ->click();
    });
    QVERIFY(!profile.settings.contains("video/iccSource"));
    QVERIFY(!QDir(dir.filePath("ColorProfiles")).exists());
    for (const auto &action : {"videoOutputSettings", "preferences", "image"}) {
      editDialog(
          w,
          [&](QDialog *d) {
            fill(d);
            auto *tabs = d->findChild<QTabWidget *>();
            tabs->setCurrentWidget(d->findChild<QWidget *>("videoIccPage"));
            if (action == QString("videoOutputSettings")) {
              const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
              if (!out.isEmpty()) {
                QTest::qWait(40);
                QVERIFY(d->grab().save(out + "/video-system-icc.png"));
              }
            }
            d->findChild<QDialogButtonBox *>()
                ->button(QDialogButtonBox::Ok)
                ->click();
            QVERIFY(!d->isVisible());
          },
          action);
      QCOMPARE(profile.settings.value("video/iccSource").toString(),
               QString("system"));
      QCOMPARE(profile.settings.value("video/iccProfile").toString(),
               QString("missing-manual.icc"));
    }
    editDialog(w, [&](QDialog *d) {
      d->findChild<QPushButton *>("videoIccReset")->click();
      QCOMPARE(d->findChild<QComboBox *>("video/iccSource")
                   ->currentData()
                   .toString(),
               QString("manual"));
      d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    QCOMPARE(VideoDisplay::values(profile.settings), VideoDisplay::defaults());
    measurements["preferencesCancelResetAllDialogsAndManualPathPreserved"] =
        true;
  }
  void cleanupTestCase() {
    const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!out.isEmpty()) {
      QFile f(out + "/system-icc-measurements.json");
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QJsonDocument(measurements).toJson());
    }
    serviceBus.unregisterObject("/", QDBusConnection::UnregisterTree);
    serviceBus.unregisterService("org.freedesktop.ColorManager");
  }
};
QTEST_MAIN(SystemIccTest)
#include "systemicc_test.moc"
