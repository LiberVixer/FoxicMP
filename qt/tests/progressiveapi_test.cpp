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
#include "windowsruntime.h"
#include "platformpaths.h"
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#endif

static QString playerExecutable() {
#ifdef Q_OS_WIN
 return QCoreApplication::applicationDirPath() + "/FoxicMPq.exe";
#else
 return QCoreApplication::applicationDirPath() + "/foxicmp-qt";
#endif
}
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
#ifndef Q_OS_WIN
    const auto fifo = QFile::encodeName(paths[0] + ".fifo"); QCOMPARE(::mkfifo(fifo.constData(), 0600), 0);
    video["path"] = QString::fromLocal8Bit(fifo); tracks["video"] = video; bad["tracks"] = tracks;
    QVERIFY_EXCEPTION_THROWN(ProgressiveSource::openPublished(bad), std::runtime_error);
#endif
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
    // The progressive session's close notification is asynchronous on Windows;
    // explicitly finish the close before starting the next queue item.
    w.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
    w.openFiles({QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("Русский H264.mp4")});
    // Windows may reuse the existing mpv item without emitting a second
    // loaded signal; assert that playback resumed instead of counting signals.
    QTRY_VERIFY_WITH_TIMEOUT(!player->get("idle-active").toBool(), 15000);
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
    QCOMPARE(reply["schemaVersion"].toInt(), 1);
    QVERIFY(reply["actions"].toArray().contains(QJsonValue("probe")));
    auto relative = snapshot;
    auto relativeTracks = relative["tracks"].toObject();
    for (const auto &kind : {QStringLiteral("video"), QStringLiteral("audio")}) {
      auto track = relativeTracks[kind].toObject();
      track["path"] = QFileInfo(track["path"].toString()).fileName();
      relativeTracks[kind] = track;
    }
    relative["tracks"] = relativeTracks;
    QJsonObject probe{{"version", 1}, {"action", "probe"}, {"session", relative},
                      {"sessionDirectory", QFileInfo(paths[0]).absolutePath()}};
    reply = api.request(probe);
    QVERIFY(reply["ok"].toBool()); QVERIFY(reply["supported"].toBool());
    QCOMPARE(reply["mode"].toString(), QString("native-progressive"));
    QVERIFY(!wire(endpoint, "{\"version\":1,\"version\":1,\"action\":\"capabilities\"}\n")["ok"].toBool());
    QVERIFY(!wire(endpoint, QByteArray(65537, 'x'))["ok"].toBool());
    QProcess client;
    client.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE), {FOXICMP_CLIENT_SCRIPT, "--player", playerExecutable(),
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
    client.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE), {FOXICMP_CLIENT_SCRIPT, "--player", playerExecutable(),
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
  void commonRelativeSessionDirectoryContract() {
    Profile profile(temporary.filePath("relative-contract-profile")); prepare(profile);
    Window w(profile); w.show();
    ProgressiveApi api(&w); QVERIFY(api.listen(profile.directory));
    QProcess client;
    client.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE), {
        FOXICMP_CONFORMANCE_SCRIPT, "--player", playerExecutable(),
        "--profile", profile.directory, "--fixtures", qEnvironmentVariable("FOXICMP_TEST_MEDIA"),
        "--work", temporary.filePath("relative-contract"),
        "--expected-pid", QString::number(QCoreApplication::applicationPid())});
    QVERIFY(client.waitForStarted());
    const auto cleanup = qScopeGuard([&] {
      if (client.state() != QProcess::NotRunning) { client.kill(); client.waitForFinished(); }
    });
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == QProcess::NotRunning, 90000);
    QVERIFY2(client.exitCode() == 0, client.readAllStandardError().constData());
    const auto results = QJsonDocument::fromJson(client.readAllStandardOutput()).array();
    QCOMPARE(results.size(), 2);
    for (const auto &entry : results)
      QCOMPARE(entry.toObject()["terminal"].toObject()["state"].toString(), QString("finished"));
  }
  void schemaTwoValidationDoesNotOpenOrReplaceMedia() {
    Profile profile(temporary.filePath("v2-validation-profile")); prepare(profile);
    Window w(profile); w.show(); ProgressiveApi api(&w); QVERIFY(api.listen(profile.directory));
    QVERIFY(api.request(message("start"))["ok"].toBool());
    const auto active = w.progressiveSession(); QVERIFY(active);
    QProcess client;
    client.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE), {
      FOXICMP_V2_CONFORMANCE_SCRIPT, "--player", playerExecutable(), "--profile", profile.directory,
      "--expected-pid", QString::number(QCoreApplication::applicationPid())});
    QVERIFY(client.waitForStarted());
    const auto cleanup = qScopeGuard([&] { if (client.state() != QProcess::NotRunning) { client.kill(); client.waitForFinished(); } });
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == QProcess::NotRunning, 20000);
    QVERIFY2(client.exitCode() == 0, client.readAllStandardError().constData());
    QVERIFY(QJsonDocument::fromJson(client.readAllStandardOutput()).object()["passed"].toInt() >= 40);
    QCOMPARE(w.progressiveSession(), active); // Metadata checks never replace a working v1 source.
  }
  void readerLeasesFollowActualHandlesAcrossWindowsAndReplacement() {
    const auto firstDir=temporary.filePath("reader-first"), secondDir=temporary.filePath("reader-second");
    const auto cleanup=qScopeGuard([&] {
      QDir(ProgressiveApi::readerRegistryFor(firstDir)).removeRecursively();
      QDir(ProgressiveApi::readerRegistryFor(secondDir)).removeRecursively();
    });
    Profile p1(firstDir),p2(secondDir);prepare(p1);prepare(p2);
    Window w1(p1),w2(p2);ProgressiveApi api1(&w1),api2(&w2);QVERIFY(api1.listen(p1.directory));QVERIFY(api2.listen(p2.directory));
    auto start=message("start");start["trackReader"]=true;
    auto r1=api1.request(start),r2=api2.request(start);
    QVERIFY2(r1["ok"].toBool(),QJsonDocument(r1).toJson().constData());QVERIFY(r2["ok"].toBool());
    const auto id1=r1["readerId"].toString(),id2=r2["readerId"].toString();QVERIFY(id1.size()==32);QVERIFY(id1!=id2);
    auto query=[](const QString& action,const QString& id){return QJsonObject{{"version",1},{"action",action},{"readerId",id}};};
    auto receipt=[&](ProgressiveApi& api,const QString& id){return api.request(query("readerStatus",id))["reader"].toObject();};
    QVERIFY(!receipt(api1,id1)["released"].toBool());QVERIFY(!receipt(api2,id2)["released"].toBool());
    QCOMPARE(api1.request(query("readerForget",id1))["code"].toString(),QString("reader_in_use"));
    auto retained=w1.progressiveSession();QVERIFY(retained);
    api1.request(query("readerRelease",id1));
    QVERIFY(!receipt(api1,id1)["released"].toBool()); // Source reference still pins real media handles.
    retained.reset();
    QTRY_VERIFY(receipt(api1,id1)["released"].toBool());
    QVERIFY(!receipt(api2,id2)["released"].toBool());
    auto replacement=snapshot;replacement["generation"]=2;start["session"]=replacement;start["replace"]=true;
    const auto next=api1.request(start);QVERIFY(next["ok"].toBool());const auto nextId=next["readerId"].toString();
    api1.request(query("readerRelease",id1));QVERIFY(w1.progressiveSession());QCOMPARE(w1.progressiveSession()->registeredReaderId(),nextId);
    api1.request(query("readerRelease",nextId));api2.request(query("readerRelease",id2));
    QTRY_VERIFY(receipt(api1,nextId)["released"].toBool());QTRY_VERIFY(receipt(api2,id2)["released"].toBool());
    QVERIFY(api1.request(query("readerForget",id1))["ok"].toBool());
    QVERIFY(api1.request(query("readerForget",nextId))["ok"].toBool());QVERIFY(api2.request(query("readerForget",id2))["ok"].toBool());
    QCOMPARE(api1.request(query("readerStatus",id1))["code"].toString(),QString("unknown_reader"));
    // A restarted API can query the same on-disk registry, without a media open.
    const auto last=api1.request(start);QVERIFY(last["ok"].toBool());const auto lastId=last["readerId"].toString();
    w1.closeCurrent();
    QTRY_VERIFY(receipt(api1,lastId)["released"].toBool());
    ProgressiveApi restarted(&w1);
    // request handling is tied to the profile by listen; the original listener owns it.
    QVERIFY(!restarted.listen(p1.directory));
    QVERIFY(restarted.request(query("readerStatus",lastId))["reader"].toObject()["released"].toBool());
    QVERIFY(restarted.request(query("readerForget",lastId))["ok"].toBool());
  }
  void trackedReaderPublicClientContract() {
    const auto dir=temporary.filePath("reader-wire-profile");
    const auto cleanup=qScopeGuard([&]{QDir(ProgressiveApi::readerRegistryFor(dir)).removeRecursively();});
    Profile profile(dir);prepare(profile);Window w(profile);ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    QProcess client;client.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),{
      FOXICMP_READER_CONFORMANCE_SCRIPT,"--player",playerExecutable(),"--profile",profile.directory,
      "--fixtures",qEnvironmentVariable("FOXICMP_TEST_MEDIA"),"--work",temporary.filePath("reader-wire-media"),
      "--expected-pid",QString::number(QCoreApplication::applicationPid())});
    QVERIFY(client.waitForStarted());
    const auto guard=qScopeGuard([&]{if(client.state()!=QProcess::NotRunning){client.kill();client.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(client.state()==QProcess::NotRunning,30000);
    QVERIFY2(client.exitCode()==0,client.readAllStandardError().constData());
    QCOMPARE(QJsonDocument::fromJson(client.readAllStandardOutput()).object()["result"].toString(),QString("PASS"));
  }
  void sdkWriterPlaysTenSecondsBeforeComplete() {
    Profile profile(temporary.filePath("sdk-writer-profile")); prepare(profile);
    Window w(profile); w.show(); ProgressiveApi api(&w); QVERIFY(api.listen(profile.directory));
    const auto folder = temporary.filePath("sdk-writer");
    const auto media = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"));
    QProcess writer;
    writer.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE), {
      FOXICMP_WRITER_SCRIPT, "--player", playerExecutable(), "--profile", profile.directory,
      "--video", media.filePath("progressive/video.mp4"), "--audio", media.filePath("progressive/audio.m4a"),
      "--directory", folder});
    QVERIFY(writer.waitForStarted());
    const auto cleanup = qScopeGuard([&] { if (writer.state() != QProcess::NotRunning) { writer.kill(); writer.waitForFinished(); } });
    QTRY_VERIFY_WITH_TIMEOUT(writer.state() == QProcess::NotRunning, 60000);
    QVERIFY2(writer.exitCode() == 0, writer.readAllStandardError().constData());
    const auto report = QJsonDocument::fromJson(writer.readAllStandardOutput()).object();
    QVERIFY(report["maxPositionBeforeComplete"].toDouble() >= 10);
    QVERIFY(report["terminal"].toObject()["released"].toBool());
    QCOMPARE(report["networkRequests"].toInt(), 0);
  }
  void schemaTwoPlaysThroughResourceIoWhileFilesAreIncomplete() {
    const auto dir=temporary.filePath("v2-media-profile");
    const auto cleanup=qScopeGuard([&]{QDir(ProgressiveApi::readerRegistryFor(dir)).removeRecursively();});
    Profile profile(dir);prepare(profile);Window w(profile);w.show();ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    QProcess writer;writer.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),{
      FOXICMP_V2_PLAYBACK_SCRIPT,"--player",playerExecutable(),"--profile",profile.directory,
      "--fixtures",qEnvironmentVariable("FOXICMP_TEST_MEDIA"),"--work",temporary.filePath("v2-media-bytes"),
      "--expected-pid",QString::number(QCoreApplication::applicationPid())});
    QVERIFY(writer.waitForStarted());const auto guard=qScopeGuard([&]{if(writer.state()!=QProcess::NotRunning){writer.kill();writer.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(writer.state()==QProcess::NotRunning,60000);
    QVERIFY2(writer.exitCode()==0,writer.readAllStandardError().constData());
    const auto result=QJsonDocument::fromJson(writer.readAllStandardOutput()).object();QCOMPARE(result["result"].toString(),QString("PASS"));
    qInfo().noquote()<<"Schema-2 media report:"<<QJsonDocument(result).toJson(QJsonDocument::Compact);
    QVERIFY(result["playingWhilePhysicallyIncomplete"].toObject()["positionSeconds"].toDouble()>=10);
  }
  void resourceIoProbeDoesNotReplaceActivePlayback() {
    const auto dir=temporary.filePath("resource-io-profile");
    const auto cleanup=qScopeGuard([&]{QDir(ProgressiveApi::readerRegistryFor(dir)).removeRecursively();});
    Profile profile(dir);prepare(profile);Window w(profile);ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    QVERIFY(api.request(message("start"))["ok"].toBool());const auto retained=w.progressiveSession();QVERIFY(retained);
    QProcess client;client.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),{
      FOXICMP_RESOURCE_CONFORMANCE_SCRIPT,"--player",playerExecutable(),"--profile",profile.directory,
      "--work",temporary.filePath("resource-io-bytes"),"--expected-pid",QString::number(QCoreApplication::applicationPid())});
    QVERIFY(client.waitForStarted());const auto guard=qScopeGuard([&]{if(client.state()!=QProcess::NotRunning){client.kill();client.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(client.state()==QProcess::NotRunning,30000);
    QVERIFY2(client.exitCode()==0,client.readAllStandardError().constData());
    const auto result=QJsonDocument::fromJson(client.readAllStandardOutput()).object();QCOMPARE(result["passed"].toInt(),11);
    QCOMPARE(result["playbackSupported"],QJsonValue(false));QCOMPARE(w.progressiveSession(),retained);
  }
  void legacyProviderPublishesWriterStateAndReleases_data() {
    QTest::addColumn<bool>("failed"); QTest::addColumn<bool>("restart");
    QTest::newRow("complete") << false << false; QTest::newRow("failed") << true << false;
#ifdef Q_OS_WIN
    QTest::newRow("provider-restart") << false << true;
#endif
  }
  void legacyProviderPublishesWriterStateAndReleases() {
    QFETCH(bool, failed); QFETCH(bool, restart);
    const auto root = QFileInfo(paths[0]).absolutePath();
    auto legacy = snapshot; auto tracks = legacy["tracks"].toObject();
    for (const auto &kind : {QStringLiteral("video"), QStringLiteral("audio")}) {
      auto track = tracks[kind].toObject(); track["path"] = QFileInfo(track["path"].toString()).fileName(); tracks[kind] = track;
    }
    legacy["tracks"] = tracks;
    const auto manifest = QDir(root).filePath("session.json");
    QFile file(manifest); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(legacy).toJson()); file.close();
    QProcess provider;
    provider.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE), {
      FOXICMP_LEGACY_FIXTURE, "--manifest", manifest, "--fixtures", qEnvironmentVariable("FOXICMP_TEST_MEDIA")});
    QVERIFY(provider.waitForStarted());
    const auto cleanup = qScopeGuard([&] {
      QFile stop(QDir(root).filePath("fixture-stop")); if (stop.open(QIODevice::WriteOnly)) stop.close();
      if (!provider.waitForFinished(4000)) { provider.kill(); provider.waitForFinished(); }
    });
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(root).filePath("fixture-ready.json")), 6000);
    Profile profile(QDir(root).filePath("legacy-test-profile")); prepare(profile);
    Window w(profile); w.show();
    auto *player = w.findChild<Player *>();
    QSignalSpy errors(player, &Player::error);
    QSignalSpy diagnostics(player, &Player::diagnosticMessage);
    QVERIFY(w.openProgressiveSession(manifest));
    auto source = w.progressiveSession(); QVERIFY(source);
    auto receipt = source->releaseReceipt();
    QVERIFY(source->status().available > 4); QVERIFY(!receipt->load());
    auto control = [&](const QString &stage) {
      QFile command(QDir(root).filePath("fixture-control"));
      if (!command.open(QIODevice::WriteOnly)) return false;
      return command.write(stage.toUtf8()) == stage.size();
    };
    QVERIFY(control("grow"));
    QTRY_VERIFY_WITH_TIMEOUT(source->status().available > 10, 6000);
    auto playbackDiagnostic = [&] {
      QJsonObject report = w.progressivePlaybackStatus();
      report["sourceError"] = source->status().error;
      report["pause"] = player->get("pause").toBool();
      report["idle"] = player->get("idle-active").toBool();
      QStringList messages;
      for (const auto &event : errors) messages << event.front().toString();
      for (const auto &event : diagnostics) messages << event.front().toString();
      report["messages"] = messages.join('\n');
      return QJsonDocument(report).toJson(QJsonDocument::Compact);
    };
    QTRY_VERIFY2_WITH_TIMEOUT(player->get("time-pos").toDouble() > .2, playbackDiagnostic().constData(), 10000);
    if (restart) {
      QVERIFY(control("disconnect"));
      QTRY_VERIFY_WITH_TIMEOUT(!source->status().connected, 6000);
      QVERIFY(source->status().error.isEmpty()); QVERIFY(!source->status().complete);
      QVERIFY(control("resume"));
      QTRY_VERIFY2_WITH_TIMEOUT(source->status().connected,
          (source->status().error + QString::fromUtf8(provider.readAllStandardError())).toUtf8().constData(), 8000);
    }
    QVERIFY(control(failed ? "failed" : "complete"));
    if (failed) QTRY_VERIFY_WITH_TIMEOUT(!source->status().error.isEmpty(), 6000);
    else QTRY_VERIFY_WITH_TIMEOUT(source->status().complete, 6000);
    w.closeCurrent();
    QVERIFY(!receipt->load()); // The test still holds a real reader reference.
    source.reset();
    QTRY_VERIFY_WITH_TIMEOUT(receipt->load(), 6000);
    auto noLease = [&] {
      QFile leases(QDir(root).filePath("leases.json")); if (!leases.open(QIODevice::ReadOnly)) return false;
      return QJsonDocument::fromJson(leases.readAll()).object()["leases"].toObject().isEmpty();
    };
    QTRY_VERIFY_WITH_TIMEOUT(noLease(), 6000);
  }
  void discoverWithoutProfileAndStartPlayerFromClient() {
    const auto profilePath = temporary.filePath("autostart-profile");
    const auto executable = playerExecutable();
    QProcess query;
    query.start(executable, {"--profile", profilePath, "--progressive-api-info"});
    QVERIFY(query.waitForFinished(5000)); QCOMPARE(query.exitCode(), 0);
    QVERIFY(!QFileInfo::exists(profilePath));
    const auto info = QJsonDocument::fromJson(query.readAllStandardOutput()).object();
    QCOMPARE(info["version"].toInt(), 1); QVERIFY(info["endpoint"].isString());
    Profile profile(profilePath); prepare(profile); profile.settings.sync();
    const auto endpoint = ProgressiveApi::endpointFor(profilePath);
    const auto cleanup = qScopeGuard([&] {
      QLockFile lease(
#ifdef Q_OS_WIN
        QDir(PlatformPaths::runtimeDirectory()).filePath(endpoint + ".lock")
#else
        endpoint + ".lock"
#endif
      );
      qint64 pid = 0; QString host, application;
      if (lease.getLockInfo(&pid, &host, &application) && pid > 0 && pid != QCoreApplication::applicationPid()) {
#ifdef Q_OS_WIN
        HANDLE child = OpenProcess(PROCESS_TERMINATE, FALSE, DWORD(pid));
        if (child) {TerminateProcess(child, 0); CloseHandle(child);}
#else
        ::kill(pid_t(pid), SIGTERM);
#endif
      }
    });
    QProcess client;
    client.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE), {FOXICMP_CLIENT_SCRIPT, "--player", executable,
                             "--profile", profilePath, "--ensure", "capabilities"});
    QVERIFY(client.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == QProcess::NotRunning, 15000);
    QVERIFY2(client.exitCode() == 0, client.readAllStandardError().constData());
    const auto reply = QJsonDocument::fromJson(client.readAllStandardOutput()).object();
    QVERIFY(reply["ok"].toBool()); QVERIFY(reply["pid"].toInteger() != QCoreApplication::applicationPid());
  }
};
int main(int argc, char **argv) {
#ifdef Q_OS_WIN
  WindowsRuntime runtime; if (!runtime.valid()) return 1;
#endif
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv); ProgressiveApiTest test; return QTest::qExec(&test, argc, argv);
}
#include "progressiveapi_test.moc"
