#include "stdinstream.h"
#include "instancebroker.h"
#include "mediasource.h"
#include "player.h"
#include "profile.h"
#include "timeline.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <QtDBus>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

class StdinTest : public QObject {
  Q_OBJECT
  QString media;
  QString executable() const {
    return qEnvironmentVariable("FOXICMP_TEST_EXECUTABLE",
        QDir(QCoreApplication::applicationDirPath()).filePath("foxicmp-qt"));
  }
  void prepare(Profile &p, bool remote = false) {
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("input/mpris", remote);
    p.settings.setValue("audio/mute", true);
    p.settings.setValue("playback/hardwareDecoding", false);
    p.settings.setValue("video/output", qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.settings.setValue("playback/after", "none");
    p.settings.sync();
  }
  bool finished(QProcess &process, int timeout = 12000) {
    QElapsedTimer time; time.start();
    while (process.state() != QProcess::NotRunning && time.elapsed() < timeout) QTest::qWait(10);
    return process.state() == QProcess::NotRunning;
  }
  QVariant property(const QString &key) {
    auto call = QDBusMessage::createMethodCall("org.mpris.MediaPlayer2.FoxicMP",
        "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "Get");
    call.setArguments({"org.mpris.MediaPlayer2.Player", key});
    const auto result = QDBusConnection::sessionBus().call(call, QDBus::Block, 500);
    return result.type() == QDBusMessage::ReplyMessage && !result.arguments().isEmpty()
        ? qvariant_cast<QDBusVariant>(result.arguments().first()).variant() : QVariant();
  }
  void remote(const QString &name, bool root = false) {
    const auto call = QDBusMessage::createMethodCall("org.mpris.MediaPlayer2.FoxicMP",
        "/org/mpris/MediaPlayer2", root ? "org.mpris.MediaPlayer2" : "org.mpris.MediaPlayer2.Player", name);
    QDBusConnection::sessionBus().asyncCall(call);
  }
private slots:
  void initTestCase() {
    media = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv");
    QVERIFY(QFileInfo(media).isFile());
  }
  void descriptorAndIpcValidation() {
    QString error;
    QVERIFY(!StdinStream::supported(-1, &error)); QVERIFY(!error.isEmpty());
    int channel[2]; QVERIFY(pipe2(channel, O_CLOEXEC) == 0);
    QVERIFY(StdinStream::supported(channel[0])); QVERIFY(!StdinStream::supported(channel[1]));
    close(channel[0]); close(channel[1]);
    const int terminalLike = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    QVERIFY(terminalLike >= 0); QVERIFY(!StdinStream::supported(terminalLike)); close(terminalLike);
    QVERIFY(MediaSource::normalize("foxicmp-stdin://untrusted").isEmpty());
    LaunchRequest request; request.files = {"foxicmp-stdin://untrusted"}; LaunchRequest out;
    QVERIFY(!LaunchRequest::decode(request.encode(), &out));
    request.files = {}; request.options = {{"stdin", true}};
    QVERIFY(!LaunchRequest::decode(request.encode(), &out));
  }
  void filePlaybackIsTransientAndCannotReopen() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    Window w(profile); w.show(); auto *player = w.findChild<Player *>();
    w.addFiles({media}); const auto savedQueue = profile.settings.value("files/playlist");
    QFile input(media); QVERIFY(input.open(QIODevice::ReadOnly));
    QSignalSpy loaded(player, &Player::loaded), ended(player, &Player::ended);
    QVERIFY(w.openStdin(input.handle(), {{"pause", true}})); input.close();
    QTRY_COMPARE(loaded.size(), 1); QTRY_VERIFY(player->get("pause").toBool());
    QVERIFY(player->get("path").toString().startsWith("foxicmp-stdin://"));
    QVERIFY(player->get("video-params").toMap().value("w").toInt() > 0);
    QVERIFY(!w.seekTo(1)); QVERIFY(!w.setAbLoop(1, 2));
    QVERIFY(!w.webState().value("canSeek").toBool());
    QVERIFY(!w.findChild<Timeline *>()->isEnabled());
    w.reopenCurrent(); QTest::qWait(100); QCOMPARE(loaded.size(), 1);
    QVERIFY(profile.recentFiles().isEmpty());
    QCOMPARE(profile.settings.value("files/playlist"), savedQueue);
    player->set("speed", 8); player->set("pause", false);
    const auto eof = [&] { for (const auto &e : ended) if (e[0].toBool()) return true; return false; };
    QTRY_VERIFY_WITH_TIMEOUT(eof(), 12000);
    QTest::qWait(100); QCOMPARE(loaded.size(), 1); // Never auto-play saved queue or repeat stdin.
    QVERIFY(!w.webCommand("play", {}));
    for (const auto &key : profile.settings.allKeys()) {
      QVERIFY(!key.contains("foxicmp-stdin"));
      QVERIFY(!profile.settings.value(key).toString().contains("foxicmp-stdin://"));
    }
    w.openFiles({media}); QTRY_COMPARE(loaded.size(), 2);
    QVERIFY(w.findChild<Timeline *>()->isEnabled()); QVERIFY(w.seekTo(1));
    QCOMPARE(profile.recentFiles(), QStringList{media});
  }
  void waitingPipeCancellationAndNextFile() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    Window w(profile); w.show(); auto *player = w.findChild<Player *>();
    int channel[2]; QVERIFY(pipe2(channel, O_CLOEXEC) == 0);
    const auto cleanup = qScopeGuard([&] { close(channel[1]); });
    int ticks = 0; QTimer timer; connect(&timer, &QTimer::timeout, [&] { ++ticks; }); timer.start(10);
    QSignalSpy loaded(player, &Player::loaded);
    QVERIFY(w.openStdin(channel[0])); close(channel[0]);
    QTest::qWait(350); QCOMPARE(loaded.size(), 0); QVERIFY(ticks >= 5);
    QVERIFY(w.webCommand("stop", {}));
    const auto released = [&] { pollfd fd{channel[1], POLLOUT, 0}; return poll(&fd, 1, 0) > 0 && (fd.revents & POLLERR); };
    QTRY_VERIFY_WITH_TIMEOUT(released(), 3000);
    w.openFiles({media}); QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(player->get("path").toString() == media);
    QVERIFY(w.seekTo(1));
  }
  void emptyAndInvalidStreamsReleaseDescriptor_data() {
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("invalid") << QByteArray("not a media stream");
  }
  void emptyAndInvalidStreamsReleaseDescriptor() {
    QFETCH(QByteArray, bytes);
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    Window w(profile); w.show(); auto *player = w.findChild<Player *>();
    int channel[2]; QVERIFY(pipe2(channel, O_CLOEXEC) == 0);
    QSignalSpy failed(player, &Player::fileFailed), loaded(player, &Player::loaded);
    QVERIFY(w.openStdin(channel[0])); close(channel[0]);
    if (!bytes.isEmpty()) QCOMPARE(::write(channel[1], bytes.constData(), bytes.size()), bytes.size());
    close(channel[1]);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000); QCOMPARE(loaded.size(), 0);
    QVERIFY(profile.recentFiles().isEmpty());
    w.openFiles({media}); QTRY_COMPARE(loaded.size(), 1);
  }
  void invalidCliNeverCreatesProfile() {
    QTemporaryDir dir;
    const auto absent = dir.filePath("absent");
    for (const auto &args : QList<QStringList>{{"--stdin", media}, {"-", media},
        {"--stdin", "--reuse-instance"}, {"--stdin", "--enqueue"}, {"--stdin", "--start", "2"},
        {"--stdin", "--clipboard"}, {"--stdin", "--command", "pause"},
        {"--stdin", "--stdin"}, {"--stdin", "--after", "shutdown"}}) {
      QProcess child; child.start(executable(), QStringList{"--profile", absent} + args);
      QVERIFY(finished(child)); QCOMPARE(child.exitCode(), 1); QVERIFY(!QFileInfo::exists(absent));
    }
  }
  void cliPipe_data() {
    QTest::addColumn<QString>("flag"); QTest::addColumn<bool>("redirect");
    QTest::newRow("stdin-option") << QString("--stdin") << false;
    QTest::newRow("single-dash") << QString("-") << false;
    QTest::newRow("file-redirect") << QString("--stdin") << true;
  }
  void cliPipe() {
    QFETCH(QString, flag); QFETCH(bool, redirect);
    QTemporaryDir dir;
    { Profile profile(dir.path()); prepare(profile); }
    QFile input(media); QVERIFY(input.open(QIODevice::ReadOnly)); const auto bytes = input.readAll();
    QProcess child;
    if (redirect) child.setStandardInputFile(media);
    child.start(executable(), {"--profile", dir.path(), flag, "--after", "exit", "--rate", "8"});
    QVERIFY(child.waitForStarted());
    if (!redirect) { QCOMPARE(child.write(bytes), bytes.size()); child.closeWriteChannel(); }
    QVERIFY2(finished(child, 18000), child.readAllStandardError().constData());
    QCOMPARE(child.exitStatus(), QProcess::NormalExit); QCOMPARE(child.exitCode(), 0);
    QSettings ini(dir.filePath("FoxicMPQt.ini"), QSettings::IniFormat);
    QVERIFY(ini.value("history/files").toStringList().isEmpty());
    QVERIFY(ini.value("files/playlist").toStringList().isEmpty());
    QVERIFY(!ini.value("playback/after").toString().contains("exit"));
  }
  void stdinDoesNotForwardToExistingOwner() {
    QTemporaryDir dir;
    { Profile profile(dir.path()); prepare(profile); profile.settings.setValue("instance/reuseExisting", true); }
    InstanceBroker owner(dir.path()); LaunchRequest initial;
    QCOMPARE(owner.start(initial, false), InstanceBroker::Primary);
    int received = 0;
    owner.setReceiver([&](const LaunchRequest &) { ++received; });
    QTRY_COMPARE(received, 1); received = 0;
    QProcess child; child.setStandardInputFile(media);
    child.start(executable(), {"--profile", dir.path(), "--stdin", "--after", "exit", "--rate", "8"});
    QVERIFY(finished(child, 18000)); QCOMPARE(child.exitCode(), 0); QCOMPARE(received, 0);
  }
  void cliDelayedProducerAndCloseWhileWaiting() {
    QTemporaryDir dir;
    { Profile profile(dir.path()); prepare(profile, true); }
    QProcess child; child.start(executable(), {"--profile", dir.path(), "--stdin", "--pause"});
    QVERIFY(child.waitForStarted());
    const auto cleanup = qScopeGuard([&] { if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(); } });
    QTest::qWait(300); QCOMPARE(child.state(), QProcess::Running);
    QFile input(media); QVERIFY(input.open(QIODevice::ReadOnly));
    QCOMPARE(child.write(input.readAll()), QFileInfo(media).size());
    // Keep the producer pipe open. Absence of bytes is not EOF.
    QTRY_COMPARE_WITH_TIMEOUT(property("PlaybackStatus").toString(), QString("Paused"), 10000);
    QCOMPARE(property("CanSeek").toBool(), false);
    QTest::qWait(250); QCOMPARE(child.state(), QProcess::Running);
    remote("Quit", true); QVERIFY(finished(child, 5000)); QCOMPARE(child.exitCode(), 0);
  }
  void cliQuitBeforeFirstByte() {
    QTemporaryDir dir;
    { Profile profile(dir.path()); prepare(profile, true); }
    QProcess child; child.start(executable(), {"--profile", dir.path(), "--stdin"});
    QVERIFY(child.waitForStarted());
    const auto cleanup = qScopeGuard([&] { if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(); } });
    QTRY_VERIFY_WITH_TIMEOUT(property("CanControl").isValid(), 5000);
    remote("Quit", true); QVERIFY(finished(child, 5000)); QCOMPARE(child.exitCode(), 0);
  }
};
QTEST_MAIN(StdinTest)
#include "stdin_test.moc"
