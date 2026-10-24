#include "preferencesdriver.h"
#include "associationoptions.h"
#include "instancebroker.h"
#include "platformpaths.h"
#include "player.h"
#include "profile.h"
#include "profilearchive.h"
#include "profilelaunch.h"
#include "subtitlerender.h"
#include "window.h"
#include "windowsruntime.h"
#include <QtTest>
#include <QtWidgets>
#include <QScopeGuard>
#include <QSslSocket>
#include <cmath>

class WindowsPortTest : public QObject {
  Q_OBJECT
  void prepare(Profile &profile) {
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("playback/hardwareDecoding", false);
    profile.settings.setValue("video/output", "software");
  }
private slots:
  void initTestCase() { Q_INIT_RESOURCE(resources); qApp->setStyle("Fusion"); }
  void commandLineMetadataAndTls() {
    QVERIFY(QSslSocket::supportsSsl());
    QProcess child;
    const auto exe = QDir(QCoreApplication::applicationDirPath()).filePath("FoxicMPq.exe");
    child.start(exe, {"--version"}); QVERIFY(child.waitForFinished(10000));
    QCOMPARE(child.exitCode(), 0); QVERIFY(child.readAllStandardOutput().startsWith("FoxicMPq 0."));
    child.start(exe, {"--help"}); QVERIFY(child.waitForFinished(10000));
    QCOMPARE(child.exitCode(), 0); QVERIFY(child.readAllStandardOutput().contains("--profile"));
    QTemporaryDir dir; const auto absent = dir.filePath("not-created");
    child.start(exe, {"--profile", absent, "--progressive-api-info"});
    QVERIFY(child.waitForFinished(10000)); QCOMPARE(child.exitCode(), 0);
    const auto json = QJsonDocument::fromJson(child.readAllStandardOutput()).object();
    QCOMPARE(json.value("supported"), QJsonValue(false)); QVERIFY(!QDir(absent).exists());
  }
  void portableIsolationAndLeases() {
    QTemporaryDir first, second;
    QVERIFY(first.isValid() && second.isValid());
    {
      Profile a(first.path()), b(second.path());
      QVERIFY(a.lease.valid() && b.lease.valid());
      a.settings.setValue("audio/volume", 37); a.settings.sync();
      QCOMPARE(b.settings.value("audio/volume").toInt(), 75);
      QVERIFY(InstanceBroker::endpointFor(first.path()) != InstanceBroker::endpointFor(second.path()));
      QCOMPARE(InstanceBroker::endpointFor(first.path()), InstanceBroker::endpointFor(first.path().toUpper()));
      {
        ProfileLease other(first.path()); QVERIFY(other.valid());
        QVERIFY(!a.lease.exclusive()); a.lease.shared();
      }
      QVERIFY(a.lease.exclusive()); a.lease.shared();
    }
    Profile reopened(first.path()); QCOMPARE(reopened.settings.value("audio/volume").toInt(), 37);
  }
  void archiveAndShortcutNoOverwrite() {
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.settings.setValue("audio/volume", 41);
    QString error;
    const auto archive = dir.filePath("test.foxicmp-profile");
    QVERIFY2(ProfileArchive::save(profile, archive, &error), qPrintable(error));
    ProfileArchive::Bundle bundle;
    QVERIFY2(ProfileArchive::read(archive, &bundle, &error), qPrintable(error));
    const auto restored = dir.filePath("Профиль с пробелами");
    QVERIFY2(ProfileArchive::restore(bundle, restored, &error), qPrintable(error));
    QVERIFY(!ProfileArchive::restore(bundle, restored, &error));
    Profile copy(restored); QCOMPARE(copy.settings.value("audio/volume").toInt(), 41);
    const auto shortcut = dir.filePath("Ярлык FoxicMP Qt.lnk");
    QVERIFY2(ProfileLaunch::launcher(QCoreApplication::applicationFilePath(), restored, shortcut, &error), qPrintable(error));
    QFile f(shortcut); QVERIFY(f.open(QIODevice::ReadOnly)); const auto original = f.readAll(); f.close();
    QVERIFY(original.size() > 76);
    QVERIFY(!ProfileLaunch::launcher(QCoreApplication::applicationFilePath(), restored, shortcut, &error));
    QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), original);
  }
  void fileOperationsNeverReplace() {
    QTemporaryDir dir;
    QFile a(dir.filePath("a")), b(dir.filePath("b"));
    QVERIFY(a.open(QIODevice::WriteOnly)); a.write("a"); a.close();
    QVERIFY(b.open(QIODevice::WriteOnly)); b.write("b"); b.close();
    QVERIFY(!PlatformPaths::moveNoReplace(a.fileName(), b.fileName()));
    QVERIFY(a.exists() && b.exists());
    QVERIFY(PlatformPaths::moveNoReplace(a.fileName(), dir.filePath("c")));
  }
  void playback_data() {
    QTest::addColumn<QString>("file");
    for (const auto &file : {"Русский H264.mp4", "chapters.mkv", "VP9.webm", "AV1.mkv", "HEVC.mov", "MPEG4.avi"})
      QTest::newRow(file) << QString::fromUtf8(file);
  }
  void playback() {
    QFETCH(QString, file);
    const auto path = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(file);
    QVERIFY(QFileInfo(path).isFile());
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(640, 360); p.show();
    QTRY_VERIFY_WITH_TIMEOUT(p.ready(), 15000);
    QVERIFY(p.set("hwdec", "no")); QVERIFY(p.set("mute", true));
    QSignalSpy loaded(&p, &Player::loaded), failed(&p, &Player::fileFailed);
    QVERIFY(p.loadSource(path, {{"pause", "no"}}));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > .2, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!p.grabFramebuffer().isNull(), 10000);
    QVERIFY(!p.get("video-codec").toString().isEmpty());
    QVERIFY(p.set("pause", true)); QTest::qWait(200);
    const auto before = p.get("time-pos").toDouble(); QTest::qWait(250);
    QVERIFY(std::abs(p.get("time-pos").toDouble() - before) < .1);
    QVERIFY(p.command({"seek", "1", "absolute+exact"}));
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(p.get("time-pos").toDouble() - 1) < .2, 10000);
    QVERIFY(p.set("pause", false));
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > 1.2, 10000);
    QCOMPARE(failed.size(), 0);
    if (!qEnvironmentVariableIsEmpty("FOXICMP_WINDOWS_REPORT"))
      QVERIFY(p.grabFramebuffer().save(QDir(qEnvironmentVariable("FOXICMP_WINDOWS_REPORT")).filePath(file + ".png")));
  }
  void subtitleCompatibility() {
    QTemporaryDir dir; Profile profile(dir.path());
    Player p; p.show(); QTRY_VERIFY_WITH_TIMEOUT(p.ready(), 15000);
    QSignalSpy errors(&p, &Player::error);
    QVERIFY(SubtitleRender::apply(&p, profile.settings));
    QCOMPARE(errors.size(), 0);
    QCOMPARE(p.get("sub-ass-use-video-data").toString(), QString("all"));
    profile.settings.setValue("subtitles/render/blurCompat", false);
    QVERIFY(SubtitleRender::apply(&p, profile.settings));
    QCOMPARE(p.get("sub-ass-use-video-data").toString(), QString("aspect-ratio"));
    profile.settings.setValue("subtitles/render/aspectCompat", false);
    QVERIFY(SubtitleRender::apply(&p, profile.settings));
    QCOMPARE(p.get("sub-ass-use-video-data").toString(), QString("none"));
    QCOMPARE(errors.size(), 0);
  }
  void settingsAndNativeAssociationsPage() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    Window window(profile); window.show();
    auto *player = window.findChild<Player *>(); QVERIFY(player);
    QTRY_VERIFY_WITH_TIMEOUT(player->ready(), 15000);
    bool checked = false;
    afterPreferencesReady(&window, [&] {
      auto *dialog = window.findChild<QDialog *>("preferencesDialog");
      const auto close = qScopeGuard([&] { dialog->reject(); });
      QVERIFY(dialog->findChild<QTreeWidget *>());
      QVERIFY(dialog->findChild<QTabWidget *>());
      QVERIFY(dialog->findChild<QPushButton *>("windowsDefaultApps"));
      QVERIFY(!dialog->findChild<QPushButton *>("assignAssociations"));
      const auto report = qEnvironmentVariable("FOXICMP_WINDOWS_REPORT");
      if (!report.isEmpty()) QVERIFY(dialog->grab().save(QDir(report).filePath("settings.png")));
      checked = true;
    });
    auto *action = window.findChild<QAction *>("action_preferences"); QVERIFY(action); action->trigger();
    QVERIFY(checked);
    const auto report = qEnvironmentVariable("FOXICMP_WINDOWS_REPORT");
    if (!report.isEmpty()) QVERIFY(window.grab().save(QDir(report).filePath("interface.png")));
  }
  void commandLineUnicodeAndStdin_data() {
    QTest::addColumn<bool>("standardInput");
    QTest::newRow("Unicode file argument") << false;
    QTest::newRow("Binary stdin pipe") << true;
  }
  void commandLineUnicodeAndStdin() {
    QFETCH(bool, standardInput);
    QTemporaryDir dir;
    { Profile profile(dir.path()); prepare(profile); profile.settings.sync(); }
    const auto media = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(standardInput ? "chapters.mkv" : "Русский H264.mp4");
    QStringList arguments{"--profile", dir.path(), "--volume", "0", "--after", "exit"};
    if (standardInput) arguments << "--stdin";
    else arguments << "--" << media;
    QProcess child;
    const auto cleanup = qScopeGuard([&] { if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(); } });
    child.start(QDir(QCoreApplication::applicationDirPath()).filePath("FoxicMPq.exe"), arguments);
    QVERIFY(child.waitForStarted());
    if (standardInput) {
      QFile file(media); QVERIFY(file.open(QIODevice::ReadOnly));
      const auto bytes = file.readAll(); QCOMPARE(child.write(bytes), bytes.size()); child.closeWriteChannel();
    }
    QTRY_VERIFY_WITH_TIMEOUT(child.state() == QProcess::NotRunning, 25000);
    QCOMPARE(child.exitStatus(), QProcess::NormalExit); QCOMPARE(child.exitCode(), 0);
    Profile result(dir.path());
    if (!standardInput) QVERIFY(result.recentFiles().contains(QDir::fromNativeSeparators(media)));
    else QVERIFY(result.recentFiles().isEmpty());
  }
  void namedPipeCommandForwarding() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    InstanceBroker broker(dir.path());
    LaunchRequest initial; QCOMPARE(broker.start(initial, true), InstanceBroker::Primary);
    QList<LaunchRequest> received; broker.setReceiver([&](const LaunchRequest &r) { received.append(r); });
    QTRY_COMPARE(received.size(), 1);
    QProcess child;
    child.start(QDir(QCoreApplication::applicationDirPath()).filePath("FoxicMPq.exe"),
        {"--profile", dir.path(), "--command", "pause", "--no-activate"});
    QVERIFY(child.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(child.state() == QProcess::NotRunning, 10000);
    QCOMPARE(child.exitStatus(), QProcess::NormalExit); QCOMPARE(child.exitCode(), 0);
    QTRY_COMPARE(received.size(), 2); QCOMPARE(received.last().options.value("command").toString(), QString("pause"));
  }
};
int main(int argc, char **argv) {
  WindowsRuntime runtime;
  if (!runtime.valid()) return 1;
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  WindowsPortTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "windows_port_test.moc"
