#include "progressiveapi.h"
#include "progressivesource.h"
#include "player.h"
#include "profile.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QtEndian>
#include <future>
#include <cstring>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>

class ProgressiveApiTest : public QObject {
  Q_OBJECT
  QTemporaryDir temporary;
  QByteArray data[2];
  QString paths[2];
  QJsonObject snapshot;
  bool indexed = false;
  qint64 boundary(int track, int seconds) {
    const auto &bytes = data[track];
    if (indexed) {
      IndexedMp4 index;
      index.open([&](uint64_t at, void *out, size_t count) {
        if (!harvester::RangeReady(at, count, bytes.size())) return false;
        std::memcpy(out, bytes.constData() + at, count); return true;
      }, bytes.size(), bytes.size(), track, 0);
      uint64_t end = 0;
      for (const auto &sample : index.samples())
        if (sample.decode < int64_t(seconds) * 10000000) end = sample.offset + sample.size;
      return qint64(end);
    }
    qint64 at = 0;
    int fragments = 0;
    while (at + 8 <= bytes.size()) {
      auto count = quint64(qFromBigEndian<quint32>(bytes.constData() + at));
      if (count == 1) count = qFromBigEndian<quint64>(bytes.constData() + at + 8);
      const auto kind = bytes.mid(at + 4, 4);
      if (count < 8 || count > quint64(bytes.size() - at)) return -1;
      at += qint64(count);
      if (kind == "mdat" && ++fragments == seconds) return at;
    }
    return bytes.size();
  }
  void writePrefixes(int seconds, bool complete = false, bool renamed = false) {
    auto tracks = snapshot["tracks"].toObject();
    for (int i = 0; i < 2; ++i) {
      const auto length = complete ? data[i].size() : boundary(i, seconds);
      QVERIFY(length > 0);
      QFile file(paths[i] + (renamed ? ".done" : ""));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
      const auto old = file.size(); QVERIFY(length >= old);
      QCOMPARE(file.write(data[i].mid(old, length - old)), length - old);
      file.close();
      const QString kind = i ? "audio" : "video";
      auto track = tracks[kind].toObject();
      track["availablePrefixBytes"] = length;
      track["expectedTotalBytes"] = complete || indexed ? QJsonValue(qint64(data[i].size())) : QJsonValue(QJsonValue::Null);
      track["state"] = complete ? "complete" : "downloading";
      tracks[kind] = track;
    }
    snapshot["tracks"] = tracks;
    snapshot["state"] = complete ? "complete" : "downloading";
    snapshot["revision"] = snapshot["revision"].toInt() + 1;
  }
  QJsonObject message(const QString &action) {
    QJsonObject out{{"version", 1}, {"action", action}};
    if (action != "start") out["sessionId"] = snapshot["sessionId"];
    if (action == "start" || action == "update" || action == "complete") out["session"] = snapshot;
    if (action == "start") { out["activate"] = false; out["title"] = "Тест загрузки YouTube / Rutube / VK"; }
    return out;
  }
  void prepare(Profile &profile) {
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("playback/hardwareDecoding", false);
    profile.settings.setValue("video/output", qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
  }
  QJsonObject wire(const QString &endpoint, const QByteArray &raw) {
    QLocalSocket socket;
    socket.connectToServer(endpoint);
    if (!socket.waitForConnected(1000)) return {};
    socket.write(raw); socket.flush();
    QByteArray response;
    QElapsedTimer timer; timer.start();
    while (!response.contains('\n') && timer.elapsed() < 6000) {
      QCoreApplication::processEvents(); response += socket.readAll(); QTest::qWait(1);
    }
    return QJsonDocument::fromJson(response).object();
  }
private slots:
  void init() {
    QVERIFY(temporary.isValid());
    indexed = QString::fromLatin1(QTest::currentDataTag()).contains("indexed");
    const auto dir = temporary.filePath(QUuid::createUuid().toString(QUuid::Id128));
    QVERIFY(QDir().mkpath(dir));
    QJsonObject tracks;
    for (int i = 0; i < 2; ++i) {
      const auto name = i ? "audio.m4a" : "video.mp4";
      QFile file(QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(
          QString(indexed ? "progressive-indexed/" : "progressive/") + name));
      QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
      data[i] = file.readAll();
      paths[i] = QDir(dir).filePath(QString::fromUtf8(i ? "звук.part" : "видео.part"));
      tracks[i ? "audio" : "video"] = QJsonObject{
        {"path", paths[i]}, {"container", i ? "m4a" : "mp4"}, {"fragmented", !indexed},
        {"initializationReady", true}, {"availablePrefixBytes", 0},
        {"expectedTotalBytes", QJsonValue(QJsonValue::Null)}, {"state", "downloading"}};
    }
    snapshot = {{"schemaVersion", 1}, {"sessionId", QUuid::createUuid().toString(QUuid::Id128)},
                {"generation", 1}, {"revision", 1}, {"duration100ns", 200000000},
                {"state", "downloading"}, {"tracks", tracks}};
    writePrefixes(5);
  }
  void growingFilesWaitForUpdatesAndSurviveRename_data() {
    QTest::addColumn<bool>("unused"); QTest::newRow("fmp4") << false; QTest::newRow("indexed") << true;
  }
  void growingFilesWaitForUpdatesAndSurviveRename() {
    auto source = ProgressiveSource::openPublished(snapshot);
    QVERIFY(source->status().available > 4 && source->status().available < 6);
    const auto at = snapshot["tracks"].toObject()["audio"].toObject()["availablePrefixBytes"].toInteger();
    auto read = std::async(std::launch::async, [source, at] { char byte; return source->read(1, at, &byte, 1); });
    const auto cleanup = qScopeGuard([&] { source->cancel(); });
    QVERIFY(read.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout);
    writePrefixes(12);
    QString error;
    QVERIFY2(source->publish(snapshot, &error), qPrintable(error));
    QVERIFY(read.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    QCOMPARE(read.get(), int64_t(1));
    QVERIFY(!source->publish(snapshot, &error)); // Stale revisions never rewind.
    for (const auto &path : paths) QVERIFY(QFile::rename(path, path + ".done"));
    writePrefixes(20, true, true);
    QVERIFY2(source->publish(snapshot, &error), qPrintable(error));
    QVERIFY(source->status().complete);
    char byte;
    QCOMPARE(source->read(1, data[1].size(), &byte, 1), int64_t(0));
    QFile truncated(paths[0] + ".done"); QVERIFY(truncated.open(QIODevice::WriteOnly)); truncated.close();
    QTRY_VERIFY(!source->status().error.isEmpty());
  }
  void invalidUpdatesDoNotAdvanceAndUnsafeFilesAreRejected() {
    auto source = ProgressiveSource::openPublished(snapshot);
    QString error;
    auto bad = snapshot; bad["generation"] = 2; bad["revision"] = 99;
    QVERIFY(!source->publish(bad, &error));
    auto tracks = snapshot["tracks"].toObject();
    auto video = tracks["video"].toObject(); video["availablePrefixBytes"] = data[0].size() + 1;
    tracks["video"] = video; bad = snapshot; bad["tracks"] = tracks; bad["revision"] = 99;
    QVERIFY(!source->publish(bad, &error));
    QVERIFY(source->status().available > 4 && source->status().error.isEmpty());
    const auto link = paths[0] + ".link";
    QVERIFY(QFile::link(paths[0], link));
    video = snapshot["tracks"].toObject()["video"].toObject(); video["path"] = link;
    tracks = snapshot["tracks"].toObject(); tracks["video"] = video; bad = snapshot; bad["tracks"] = tracks;
    QVERIFY_EXCEPTION_THROWN(ProgressiveSource::openPublished(bad), std::runtime_error);
    const auto fifo = QFile::encodeName(paths[0] + ".fifo"); QCOMPARE(::mkfifo(fifo.constData(), 0600), 0);
    video["path"] = QString::fromLocal8Bit(fifo); tracks["video"] = video; bad["tracks"] = tracks;
    QVERIFY_EXCEPTION_THROWN(ProgressiveSource::openPublished(bad), std::runtime_error);
  }
  void missingHeartbeatWaitsAndReconnectsWithoutEof() {
    auto source = ProgressiveSource::openPublished(snapshot);
    const auto at = snapshot["tracks"].toObject()["audio"].toObject()["availablePrefixBytes"].toInteger();
    auto read = std::async(std::launch::async, [source, at] { char byte; return source->read(1, at, &byte, 1); });
    const auto cleanup = qScopeGuard([&] { source->cancel(); });
    QTRY_VERIFY_WITH_TIMEOUT(!source->status().connected, 17000);
    QVERIFY(read.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    source->publishedHeartbeat(); QVERIFY(source->status().connected);
    writePrefixes(12); QString error;
    QVERIFY2(source->publish(snapshot, &error), qPrintable(error));
    QVERIFY(read.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    QCOMPARE(read.get(), int64_t(1));
  }
  void windowBufferingPauseAndStaleCancel_data() {
    QTest::addColumn<bool>("unused"); QTest::newRow("fmp4") << false; QTest::newRow("indexed") << true;
  }
  void windowBufferingPauseAndStaleCancel() {
    Profile profile(temporary.filePath("profile-" + snapshot["sessionId"].toString())); prepare(profile);
    Window w(profile); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    ProgressiveApi api(&w);
    auto *player = w.findChild<Player *>(); QSignalSpy loaded(player, &Player::loaded);
    auto reply = api.request(message("start")); QVERIFY2(reply["ok"].toBool(), qPrintable(reply["error"].toString()));
    QCOMPARE(reply["state"].toString(), QString("buffering"));
    auto original = w.progressiveSession();
    QVERIFY(api.request(message("start"))["ok"].toBool()); QCOMPARE(w.progressiveSession(), original);
    auto replacement = message("start");
    auto bad = snapshot; bad["sessionId"] = QUuid::createUuid().toString(QUuid::Id128);
    auto badTracks = bad["tracks"].toObject(); auto badVideo = badTracks["video"].toObject();
    badVideo["initializationReady"] = false; badTracks["video"] = badVideo; bad["tracks"] = badTracks;
    replacement["session"] = bad;
    QCOMPARE(api.request(replacement)["code"].toString(), QString("session_busy"));
    replacement["replace"] = true;
    QCOMPARE(api.request(replacement)["code"].toString(), QString("source_not_ready"));
    QCOMPARE(w.progressiveSession(), original);
    auto staleCancel = message("cancel"); staleCancel["sessionId"] = bad["sessionId"];
    QVERIFY(!api.request(staleCancel)["ok"].toBool()); QCOMPARE(w.progressiveSession(), original);
    QTest::qWait(200); QCOMPARE(loaded.size(), 0);
    writePrefixes(12); QVERIFY(api.request(message("update"))["ok"].toBool());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(player->get("time-pos").toDouble() > .3, 5000);
    QVERIFY(player->get("audio-codec-name").toString().contains("aac", Qt::CaseInsensitive));
    QVERIFY(player->get("video-codec").toString().contains("264"));
    QTRY_VERIFY(w.windowTitle().contains("Тест загрузки"));
    QVERIFY(!w.seekTo(18));
    w.findChild<QAction *>("action_play")->trigger(); QTRY_VERIFY(player->get("pause").toBool());
    writePrefixes(20, true); QVERIFY(api.request(message("complete"))["ok"].toBool());
    QTest::qWait(300); QVERIFY(player->get("pause").toBool());
    w.findChild<QAction *>("action_play")->trigger();
    QTRY_VERIFY(w.seekTo(19));
    QTRY_COMPARE_WITH_TIMEOUT(api.request(message("status"))["state"].toString(), QString("finished"), 5000);
    QTRY_VERIFY(!api.request(message("status"))["active"].toBool());
    QVERIFY(api.request(message("cancel"))["ok"].toBool());
    QCOMPARE(api.request(message("status"))["state"].toString(), QString("finished"));
    QTRY_VERIFY(player->get("idle-active").toBool());
    w.openFiles({QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("Русский H264.mp4")});
    QTRY_COMPARE(loaded.size(), 2);
    QVERIFY(api.request(message("cancel"))["ok"].toBool());
    QVERIFY(!player->get("idle-active").toBool());
    QVERIFY(!api.request(message("update"))["ok"].toBool());
  }
  void downloaderErrorRemainsAvailableAfterPlaybackCloses() {
    Profile profile(temporary.filePath("error-profile")); prepare(profile);
    Window w(profile); w.show(); ProgressiveApi api(&w);
    QVERIFY(api.request(message("start"))["ok"].toBool());
    auto failure = message("error"); failure["message"] = "Download failed: test";
    QVERIFY(api.request(failure)["ok"].toBool());
    QTRY_VERIFY(!w.progressiveSession());
    const auto status = api.request(message("status"));
    QCOMPARE(status["state"].toString(), QString("error"));
    QCOMPARE(status["error"].toString(), QString("Download failed: test"));
  }
  void socketProtocolAndExternalPythonClient() {
    Profile profile(temporary.filePath("socket-profile")); prepare(profile);
    Window w(profile); w.show();
    ProgressiveApi api(&w); QVERIFY(api.listen(profile.directory));
    const auto endpoint = ProgressiveApi::endpointFor(profile.directory);
    auto reply = wire(endpoint, "{\"version\":1,\"action\":\"capabilities\"}\n");
    QVERIFY(reply["ok"].toBool());
    QVERIFY(!wire(endpoint, "{\"version\":1,\"version\":1,\"action\":\"capabilities\"}\n")["ok"].toBool());
    QVERIFY(!wire(endpoint, QByteArray(65537, 'x'))["ok"].toBool());
    QProcess client;
    client.start("python3", {FOXICMP_CLIENT_SCRIPT, "--player", QCoreApplication::applicationDirPath() + "/foxicmp-qt",
                              "--profile", profile.directory, "capabilities"});
    QVERIFY(client.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == QProcess::NotRunning, 10000);
    QVERIFY2(client.exitCode() == 0, client.readAllStandardError().constData());
    QVERIFY(QJsonDocument::fromJson(client.readAllStandardOutput()).object()["ok"].toBool());
    writePrefixes(12);
    const auto snapshotPath = temporary.filePath("external-request.json");
    QFile document(snapshotPath); QVERIFY(document.open(QIODevice::WriteOnly));
    document.write(QJsonDocument(snapshot).toJson()); document.close();
    auto *player = w.findChild<Player *>(); QSignalSpy loaded(player, &Player::loaded);
    client.start("python3", {FOXICMP_CLIENT_SCRIPT, "--player", QCoreApplication::applicationDirPath() + "/foxicmp-qt",
                              "--profile", profile.directory, "start", "--snapshot", snapshotPath,
                              "--title", "Внешний загрузчик"});
    QVERIFY(client.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == QProcess::NotRunning, 10000);
    QVERIFY2(client.exitCode() == 0, client.readAllStandardError().constData());
    QVERIFY(QJsonDocument::fromJson(client.readAllStandardOutput()).object()["ok"].toBool());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(player->get("time-pos").toDouble() > .2, 5000);
    reply = wire(endpoint, QJsonDocument(message("cancel")).toJson(QJsonDocument::Compact) + '\n');
    QVERIFY(reply["ok"].toBool());
    QCOMPARE(reply["state"].toString(), QString("cancelled"));
    QVERIFY(!reply["active"].toBool());
  }
  void discoverWithoutProfileAndStartPlayerFromClient() {
    const auto profilePath = temporary.filePath("autostart-profile");
    const auto executable = QCoreApplication::applicationDirPath() + "/foxicmp-qt";
    QProcess query;
    query.start(executable, {"--profile", profilePath, "--progressive-api-info"});
    QVERIFY(query.waitForFinished(5000)); QCOMPARE(query.exitCode(), 0);
    QVERIFY(!QFileInfo::exists(profilePath));
    const auto info = QJsonDocument::fromJson(query.readAllStandardOutput()).object();
    QCOMPARE(info["version"].toInt(), 1); QVERIFY(info["endpoint"].isString());
    Profile profile(profilePath); prepare(profile); profile.settings.sync();
    const auto endpoint = ProgressiveApi::endpointFor(profilePath);
    const auto cleanup = qScopeGuard([&] {
      QLockFile lease(endpoint + ".lock");
      qint64 pid = 0; QString host, application;
      if (lease.getLockInfo(&pid, &host, &application) && pid > 0 && pid != getpid())
        ::kill(pid_t(pid), SIGTERM);
    });
    QProcess client;
    client.start("python3", {FOXICMP_CLIENT_SCRIPT, "--player", executable,
                             "--profile", profilePath, "--ensure", "capabilities"});
    QVERIFY(client.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == QProcess::NotRunning, 15000);
    QVERIFY2(client.exitCode() == 0, client.readAllStandardError().constData());
    const auto reply = QJsonDocument::fromJson(client.readAllStandardOutput()).object();
    QVERIFY(reply["ok"].toBool()); QVERIFY(reply["pid"].toInteger() != getpid());
  }
};
QTEST_MAIN(ProgressiveApiTest)
#include "progressiveapi_test.moc"
