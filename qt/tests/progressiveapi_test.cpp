#include "progressiveapi.h"
#include "progressivesource.h"
#include "../../shared/progressiveretirement.h"
#include "player.h"
#include "profile.h"
#include "window.h"
#include "timeline.h"
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
    if (action != "start" && action != "probe") out["sessionId"] = snapshot["sessionId"];
    if (action == "start" || action == "probe" || action == "update" || action == "complete") out["session"] = snapshot;
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
    QSet<QString> audioCommands;
    QTimer controls;controls.setInterval(100);
    connect(&controls,&QTimer::timeout,this,[&]{
      const auto source=w.progressiveSession();
      if(!source || w.pos<1 || audioCommands.contains(source->id()))return;
      for(auto* action:w.audioMenu->actions())if(action->isEnabled() && action->data().toInt()==1) {
        action->trigger();w.cycleTrack(true,1);audioCommands.insert(source->id());break;
      }
    });controls.start();
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
    QCOMPARE(audioCommands.size(),2);
    for (const auto &entry : results)
      QCOMPARE(entry.toObject()["terminal"].toObject()["state"].toString(), QString("finished"));
  }
  void mediaPreparationKeepsUiResponsive_data() {
    QTest::addColumn<QString>("action");QTest::addColumn<QString>("interruption");
    for(const auto &action:{"start","probe"})for(const auto &interrupt:{"none","close","stop","disconnect"})
      QTest::newRow(qPrintable(QString(action)+"-"+interrupt))<<QString(action)<<QString(interrupt);
  }
  void mediaPreparationKeepsUiResponsive() {
    QFETCH(QString,action);QFETCH(QString,interruption);
    Profile profile(temporary.filePath("async-"+action+"-"+interruption));prepare(profile);
    Window w(profile);ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    auto entered=std::make_shared<std::promise<void>>();auto enteredFuture=entered->get_future();
    auto release=std::make_shared<std::promise<void>>();const auto gate=release->get_future().share();
    api.preparationHook=[entered,gate]{entered->set_value();gate.wait();};
    const auto cleanup=qScopeGuard([release]{try{release->set_value();}catch(const std::future_error&){};});
    QLocalSocket socket;const auto endpoint=ProgressiveApi::endpointFor(profile.directory);
    socket.connectToServer(endpoint);QVERIFY(socket.waitForConnected(1000));
    socket.write(QJsonDocument(message(action)).toJson(QJsonDocument::Compact)+'\n');socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(enteredFuture.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,3000);
    int ticks=0;QTimer tick;connect(&tick,&QTimer::timeout,&tick,[&]{++ticks;});tick.start(5);
    QTRY_VERIFY_WITH_TIMEOUT(ticks>=5,1000);
    const auto caps=wire(endpoint,"{\"version\":1,\"action\":\"capabilities\"}\n");QVERIFY(caps["ok"].toBool());
    const auto busy=wire(endpoint,QJsonDocument(message("probe")).toJson(QJsonDocument::Compact)+'\n');
    QCOMPARE(busy["code"].toString(),QString("preparation_busy"));
    if(interruption=="close")w.closeCurrent();
    if(interruption=="stop")w.findChild<QAction*>("action_stop")->trigger();
    if(interruption=="disconnect"){socket.abort();QTest::qWait(20);}
    release->set_value();
    QTRY_VERIFY_WITH_TIMEOUT(!api.preparation,3000);
    if(interruption!="disconnect") {
      QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable()>0,3000);
      const auto reply=QJsonDocument::fromJson(socket.readAll()).object();
      if(interruption=="none")QVERIFY2(reply["ok"].toBool(),qPrintable(reply["error"].toString()));
      else QCOMPARE(reply["code"].toString(),QString("preparation_cancelled"));
    }
    QCOMPARE(bool(w.progressiveSession()),action=="start" && interruption=="none");
    w.closeCurrent();
  }
  void deletingApiDoesNotWaitForPreparationWorker() {
    Profile profile(temporary.filePath("async-delete"));prepare(profile);Window w(profile);
    auto api=std::make_unique<ProgressiveApi>(&w);QVERIFY(api->listen(profile.directory));
    auto entered=std::make_shared<std::promise<void>>();auto future=entered->get_future();
    auto release=std::make_shared<std::promise<void>>();auto gate=release->get_future().share();
    api->preparationHook=[entered,gate]{entered->set_value();gate.wait();};
    const auto cleanup=qScopeGuard([release]{release->set_value();});
    QLocalSocket socket;socket.connectToServer(ProgressiveApi::endpointFor(profile.directory));QVERIFY(socket.waitForConnected(1000));
    socket.write(QJsonDocument(message("start")).toJson(QJsonDocument::Compact)+'\n');socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,3000);
    QElapsedTimer timer;timer.start();api.reset();QVERIFY(timer.elapsed()<200);QVERIFY(!w.progressiveSession());
  }
  void preparationWorkerFailureDoesNotPoisonSlot() {
    Profile profile(temporary.filePath("async-failure"));prepare(profile);Window w(profile);
    ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));const auto endpoint=ProgressiveApi::endpointFor(profile.directory);
    for(const auto &action:{"probe","start"}) {
      api.preparationHook=[]{throw std::runtime_error("Test preparation failure");};
      const auto reply=wire(endpoint,QJsonDocument(message(action)).toJson(QJsonDocument::Compact)+'\n');
      QVERIFY(!reply["ok"].toBool());QCOMPARE(reply["code"].toString(),QString("source_not_ready"));
      QVERIFY(!w.progressiveSession());api.preparationHook={};
      const auto retry=wire(endpoint,QJsonDocument(message("probe")).toJson(QJsonDocument::Compact)+'\n');
      QVERIFY(retry["ok"].toBool());QVERIFY(retry["supported"].toBool());
    }
  }
  void preparationDeadlineCannotStartPlaybackLater() {
    Profile profile(temporary.filePath("async-timeout"));prepare(profile);Window w(profile);
    ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    auto entered=std::make_shared<std::promise<void>>();auto future=entered->get_future();
    auto release=std::make_shared<std::promise<void>>();const auto gate=release->get_future().share();
    api.preparationHook=[entered,gate]{entered->set_value();gate.wait();};
    const auto cleanup=qScopeGuard([release]{try{release->set_value();}catch(const std::future_error&){};});
    QLocalSocket socket;socket.connectToServer(ProgressiveApi::endpointFor(profile.directory));QVERIFY(socket.waitForConnected(1000));
    socket.write(QJsonDocument(message("start")).toJson(QJsonDocument::Compact)+'\n');socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,3000);
    QTRY_COMPARE_WITH_TIMEOUT(socket.state(),QLocalSocket::UnconnectedState,7000);
    QVERIFY(!w.progressiveSession());release->set_value();QTRY_VERIFY_WITH_TIMEOUT(!api.preparation,3000);
    QVERIFY(!w.progressiveSession());
  }
  void publishedUpdatesKeepUiResponsive_data() {
    QTest::addColumn<bool>("schemaTwo");QTest::addColumn<QString>("action");QTest::addColumn<QString>("interruption");
    for(const auto &format:{"fmp4","indexed","v2"})for(const auto &action:{"update","complete"})for(const auto &interrupt:{"none","close","disconnect"}) {
      if(QString(interrupt)=="disconnect" && QString(format)!="v2")continue;
      QTest::newRow(qPrintable(QString(format)+"-"+action+"-"+interrupt))<<(QString(format)=="v2")<<QString(action)<<QString(interrupt);
    }
  }
  void publishedUpdatesKeepUiResponsive() {
    QFETCH(bool,schemaTwo);QFETCH(QString,action);QFETCH(QString,interruption);
    const auto dir=temporary.filePath("publication-"+QString::fromLatin1(QTest::currentDataTag()));
    const auto cleanupRegistry=qScopeGuard([&]{QDir(ProgressiveApi::readerRegistryFor(dir)).removeRecursively();});
    Profile profile(dir);prepare(profile);Window w(profile);ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    const auto manifest=[&]{
      if(!schemaTwo)return snapshot;
      QJsonArray resources,tracks;
      for(const auto &kind:{"video","audio"}) {
        const auto track=snapshot["tracks"].toObject()[kind].toObject();
        resources.append(QJsonObject{{"id",kind},{"name",QFileInfo(track["path"].toString()).fileName()},
          {"length",track["expectedTotalBytes"].isNull()?QJsonValue(QJsonValue::Null):QJsonValue(QString::number(track["expectedTotalBytes"].toInteger()))},
          {"ranges",QJsonArray{QJsonObject{{"offset","0"},{"length",QString::number(track["availablePrefixBytes"].toInteger())}}}},{"state",snapshot["state"]}});
        tracks.append(QJsonObject{{"id",kind},{"kind",kind},{"required",true},{"containerHint","fmp4"},{"codecHint",QString(kind)=="video"?"h264":"aac-lc"},
          {"initializationReady",true},{"state",snapshot["state"]},{"source",QJsonObject{{"resourceId",kind}}}});
      }
      return QJsonObject{{"schemaVersion",2},{"sessionId",snapshot["sessionId"]},{"generation","18446744073709551615"},
        {"revision",QString::number(9007199254740990ULL+quint64(snapshot["revision"].toInt()))},
        {"writer",QJsonObject{{"instanceId",QString(32,'b')},{"state",snapshot["state"]}}},
        {"timeline",QJsonObject{{"timebase",QJsonObject{{"numerator",1},{"denominator",10000000}}},{"live",false},{"duration","200000000"},{"timestampOffset","0"}}},
        {"resources",resources},{"tracks",tracks}};
    };
    auto first=manifest();auto start=message("start");start["session"]=first;start["trackReader"]=true;
    if(schemaTwo)start["sessionDirectory"]=QFileInfo(paths[0]).absolutePath();
    auto reply=api.request(start);QVERIFY2(reply["ok"].toBool(),qPrintable(reply["error"].toString()));
    auto active=w.progressiveSession();QVERIFY(active);const auto oldStatus=active->status();
    auto entered=std::make_shared<std::promise<void>>();auto future=entered->get_future();
    auto release=std::make_shared<std::promise<void>>();const auto gate=release->get_future().share();
    active->publicationIoHook=[entered,gate]{entered->set_value();gate.wait();};
    const auto cleanup=qScopeGuard([release]{try{release->set_value();}catch(const std::future_error&){};});
    writePrefixes(action=="complete"?20:12,action=="complete");const auto next=manifest();
    auto update=message(action);update["session"]=next;update["readerId"]=reply["readerId"];
    const auto endpoint=ProgressiveApi::endpointFor(profile.directory);
    QLocalSocket socket;socket.connectToServer(endpoint);QVERIFY(socket.waitForConnected(1000));
    socket.write(QJsonDocument(update).toJson(QJsonDocument::Compact)+'\n');socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,3000);
    int ticks=0;QTimer tick;connect(&tick,&QTimer::timeout,&tick,[&]{++ticks;});tick.start(5);QTRY_VERIFY_WITH_TIMEOUT(ticks>=4,1000);
    QCOMPARE(active->status().available,oldStatus.available);
    auto status=wire(endpoint,QJsonDocument(message("status")).toJson(QJsonDocument::Compact)+'\n');QVERIFY(status["ok"].toBool());
    if(schemaTwo)QCOMPARE(status["revision"],first["revision"]);
    QVERIFY(wire(endpoint,"{\"version\":1,\"action\":\"capabilities\"}\n")["ok"].toBool());
    QVERIFY(wire(endpoint,QJsonDocument(message("heartbeat")).toJson(QJsonDocument::Compact)+'\n')["ok"].toBool());
    QCOMPARE(wire(endpoint,QJsonDocument(update).toJson(QJsonDocument::Compact)+'\n')["code"].toString(),QString("preparation_busy"));
    if(interruption=="close"){QElapsedTimer timer;timer.start();w.closeCurrent();QVERIFY(timer.elapsed()<200);}
    if(interruption=="disconnect"){socket.abort();QTest::qWait(20);}
    release->set_value();QTRY_VERIFY_WITH_TIMEOUT(!api.preparation,5000);active->publicationIoHook={};
    if(interruption!="disconnect") {
      QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable()>0,3000);reply=QJsonDocument::fromJson(socket.readAll()).object();
      if(interruption=="close")QCOMPARE(reply["code"].toString(),QString("preparation_cancelled"));
      else QVERIFY2(reply["ok"].toBool(),qPrintable(reply["error"].toString()));
    }
    if(interruption=="close"){QVERIFY(!w.progressiveSession());QCOMPARE(active->status().available,oldStatus.available);}
    else {
      QCOMPARE(w.progressiveSession(),active);QVERIFY(active->status().available>oldStatus.available);
      QCOMPARE(active->status().complete,action=="complete");
      if(schemaTwo)QCOMPARE(active->playbackDetails()["revision"],next["revision"]);
      const auto stale=wire(endpoint,QJsonDocument(update).toJson(QJsonDocument::Compact)+'\n');QVERIFY(!stale["ok"].toBool());
      QVERIFY(active->status().error.isEmpty());
    }
    w.closeCurrent();active.reset();
  }
  void readerRegistryIoDoesNotHoldUi_data() {
    QTest::addColumn<QString>("action");
    for(const auto &action:{"readerStatus","readerRelease","readerForget","readers"})QTest::newRow(action)<<QString(action);
  }
  void readerRegistryIoDoesNotHoldUi() {
    QFETCH(QString,action);const auto dir=temporary.filePath("registry-"+action);
    const auto cleanupRegistry=qScopeGuard([&]{QDir(ProgressiveApi::readerRegistryFor(dir)).removeRecursively();});
    Profile profile(dir);prepare(profile);Window w(profile);ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    auto start=message("start");start["trackReader"]=true;const auto opened=api.request(start);QVERIFY(opened["ok"].toBool());
    const auto reader=opened["readerId"].toString();
    QJsonObject query{{"version",1},{"action",action},{"readerId",reader}};
    if(action=="readers"){query.remove("readerId");query["sessionId"]=snapshot["sessionId"];query["generation"]="1";}
    auto entered=std::make_shared<std::promise<void>>();auto future=entered->get_future();auto release=std::make_shared<std::promise<void>>();auto gate=release->get_future().share();
    api.readerIoHook=[entered,gate]{entered->set_value();gate.wait();};
    const auto cleanup=qScopeGuard([release]{try{release->set_value();}catch(const std::future_error&){};});
    const auto endpoint=ProgressiveApi::endpointFor(dir);QLocalSocket socket;socket.connectToServer(endpoint);QVERIFY(socket.waitForConnected(1000));
    socket.write(QJsonDocument(query).toJson(QJsonDocument::Compact)+'\n');socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,3000);
    int ticks=0;QTimer tick;connect(&tick,&QTimer::timeout,&tick,[&]{++ticks;});tick.start(5);QTRY_VERIFY_WITH_TIMEOUT(ticks>=4,1000);
    QVERIFY(wire(endpoint,"{\"version\":1,\"action\":\"capabilities\"}\n")["ok"].toBool());
    QVERIFY(wire(endpoint,QJsonDocument(message("status")).toJson(QJsonDocument::Compact)+'\n')["ok"].toBool());
    QCOMPARE(wire(endpoint,QJsonDocument(query).toJson(QJsonDocument::Compact)+'\n')["code"].toString(),QString("reader_registry_busy"));
    release->set_value();QTRY_VERIFY_WITH_TIMEOUT(!api.registryPreparation,3000);api.readerIoHook={};
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable()>0,3000);const auto reply=QJsonDocument::fromJson(socket.readAll()).object();
    if(action=="readerForget"){QVERIFY(!reply["ok"].toBool());QCOMPARE(reply["code"].toString(),QString("reader_in_use"));QVERIFY(w.progressiveSession());}
    else {QVERIFY(reply["ok"].toBool());if(action=="readerRelease")QVERIFY(!w.progressiveSession());}
    w.closeCurrent();QTRY_VERIFY_WITH_TIMEOUT(api.request(QJsonObject{{"version",1},{"action","readerStatus"},{"readerId",reader}})["reader"].toObject()["released"].toBool(),5000);
  }
  void sourceDestructionIsDeferredUntilActualFileClose() {
    const auto dir=temporary.filePath("deferred-close");const auto cleanupRegistry=qScopeGuard([&]{QDir(ProgressiveApi::readerRegistryFor(dir)).removeRecursively();});
    Profile profile(dir);prepare(profile);Window w(profile);ProgressiveApi api(&w);QVERIFY(api.listen(dir));auto start=message("start");start["trackReader"]=true;
    const auto opened=api.request(start);QVERIFY(opened["ok"].toBool());auto source=w.progressiveSession();QVERIFY(source);auto receipt=source->releaseReceipt();const auto id=opened["readerId"];
    auto entered=std::make_shared<std::promise<void>>();auto future=entered->get_future();auto release=std::make_shared<std::promise<void>>();auto gate=release->get_future().share();
    source->retirementHook=[entered,gate]{entered->set_value();gate.wait();};const auto cleanup=qScopeGuard([release]{try{release->set_value();}catch(const std::future_error&){};});
    QElapsedTimer time;time.start();w.closeCurrent();source.reset();QVERIFY(time.elapsed()<200);
    QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,3000);QVERIFY(!receipt->load());
    const auto endpoint=ProgressiveApi::endpointFor(dir);const auto query=[&](const char* action){return QJsonDocument(QJsonObject{{"version",1},{"action",action},{"readerId",id}}).toJson(QJsonDocument::Compact)+'\n';};
    QVERIFY(!wire(endpoint,query("readerStatus"))["reader"].toObject()["released"].toBool());
    QCOMPARE(wire(endpoint,query("readerForget"))["code"].toString(),QString("reader_in_use"));
    QVERIFY(wire(endpoint,"{\"version\":1,\"action\":\"capabilities\"}\n")["ok"].toBool());
    release->set_value();QTRY_VERIFY_WITH_TIMEOUT(receipt->load(),5000);
    QTRY_VERIFY_WITH_TIMEOUT(wire(endpoint,query("readerStatus"))["reader"].toObject()["released"].toBool(),5000);
    QVERIFY(wire(endpoint,query("readerForget"))["ok"].toBool());
  }
  void sourceCapacityRejectsWithoutOpeningMoreFiles() {
    using Pool=foxicmp::progressive::SourceRetirement;QTRY_COMPARE_WITH_TIMEOUT(Pool::occupied(),size_t(0),5000);
    std::vector<std::shared_ptr<int>> owned;for(size_t i=0;i<Pool::Capacity;++i)owned.push_back(Pool::create<int>([]{return new int(0);}));
    const auto cleanup=qScopeGuard([&]{owned.clear();Pool::waitEmpty(std::chrono::seconds(3));});
    Profile profile(temporary.filePath("source-limit"));prepare(profile);Window w(profile);ProgressiveApi api(&w);
    QCOMPARE(api.request(message("start"))["code"].toString(),QString("source_limit"));QVERIFY(!w.progressiveSession());
    owned.clear();QTRY_COMPARE_WITH_TIMEOUT(Pool::occupied(),size_t(0),3000);
    QVERIFY(api.request(message("start"))["ok"].toBool());w.closeCurrent();
  }
  void segmentProbeDoesNotReplacePlayback() {
    const auto directory=temporary.filePath("segment-probe-profile");
    const auto cleanupRegistry=qScopeGuard([&]{QDir(ProgressiveApi::readerRegistryFor(directory)).removeRecursively();});
    Profile profile(directory);prepare(profile);
    Window w(profile);w.show();ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    QVERIFY(api.request(message("start"))["ok"].toBool());const auto active=w.progressiveSession();QVERIFY(active);
    QProcess client;client.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),{
      QFileInfo(FOXICMP_RESOURCE_CONFORMANCE_SCRIPT).dir().filePath("progressive_segment_api_conformance.py"),
      "--player",playerExecutable(),"--profile",profile.directory,"--fixtures",qEnvironmentVariable("FOXICMP_TEST_MEDIA"),
      "--work",temporary.filePath("segment-probe-data")});
    QVERIFY(client.waitForStarted());const auto cleanup=qScopeGuard([&]{if(client.state()!=QProcess::NotRunning){client.kill();client.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(client.state()==QProcess::NotRunning,45000);
    QVERIFY2(client.exitCode()==0,client.readAllStandardError().constData());
    const auto report=client.readAllStandardOutput();qInfo().noquote()<<"Segment probe report:"<<report;
    QVERIFY(QJsonDocument::fromJson(report).object()["passed"].toInt()>=10);
    QCOMPARE(w.progressiveSession(),active);
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
  void schemaTwoPlaysThroughResourceIoWhileFilesAreIncomplete_data() {
    QTest::addColumn<QString>("mediaProfile");
    for(const auto* profile:{"h264","hevc8","hevc10","av1-8","av1-10","vp9-8","vp9-10","h264-opus","h264-opus-mono","hevc8-opus","hevc10-opus","av1-8-opus","av1-10-opus","vp9-8-opus","vp9-10-opus","webm-vp9-8","webm-vp9-10","webm-av1-8","webm-av1-10","mixed-h264-opus","mixed-hevc8-opus","mixed-hevc10-opus","mixed-av1-8-opus","mixed-av1-10-opus","mixed-vp9-8-opus","mixed-vp9-10-opus","mixed-webm-vp9-8-aac","mixed-webm-vp9-8-opus","mixed-webm-vp9-10-aac","mixed-webm-vp9-10-opus","mixed-webm-av1-8-aac","mixed-webm-av1-8-opus","mixed-webm-av1-10-aac","mixed-webm-av1-10-opus","webm-vp8-opus","webm-vp8-vorbis","webm-vp8-vorbis-mono","webm-vp9-8-vorbis","webm-vp9-10-vorbis","webm-av1-8-vorbis","webm-av1-10-vorbis","mixed-h264-vorbis","mixed-hevc8-vorbis","mixed-hevc10-vorbis","mixed-av1-8-vorbis","mixed-av1-10-vorbis","mixed-vp9-8-vorbis","mixed-vp9-10-vorbis","mixed-webm-vp8-aac","mixed-webm-vp8-opus"})QTest::newRow(profile)<<QString(profile);
  }
  void schemaTwoPlaysThroughResourceIoWhileFilesAreIncomplete() {
    QFETCH(QString,mediaProfile);
    const auto dir=temporary.filePath("v2-media-profile-"+mediaProfile);
    const auto cleanup=qScopeGuard([&]{QDir(ProgressiveApi::readerRegistryFor(dir)).removeRecursively();});
    Profile profile(dir);prepare(profile);Window w(profile);w.show();ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    auto* player=w.findChild<Player*>();QVERIFY(player);
    QByteArray firstFrame;bool changingDecodedVideo=false,seekRequested=false,seekObserved=false;QString decodedFormat,decodedPixelFormat,decodedAudio;int decodedAudioRate=0;
    QTimer decoderSample;decoderSample.setInterval(100);
    connect(&decoderSample,&QTimer::timeout,this,[&]{
      if(!w.progressiveSession() || w.progressiveSession()->status().complete)return;
      const auto seconds=player->get("time-pos").toDouble();
      if(seconds<1 || player->get("dwidth").toInt()!=320)return;
      if((mediaProfile.contains("-opus") || mediaProfile.startsWith("webm-") || mediaProfile.startsWith("mixed-"))) {
        if(seekRequested && seconds<3)seekObserved=true;
        if(!seekRequested && seconds>=4)seekRequested=w.seekTo(2);
      }
      decodedFormat=player->get("video-format").toString();
      decodedAudio=player->get("audio-codec-name").toString();decodedAudioRate=player->get("audio-params/samplerate").toInt();
      decodedPixelFormat=player->get("video-dec-params/pixelformat").toString();
      const auto frame=player->grabFramebuffer();if(frame.isNull())return;
      const auto bytes=QByteArray(reinterpret_cast<const char*>(frame.constBits()),frame.sizeInBytes());
      const auto digest=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
      if(firstFrame.isEmpty())firstFrame=digest;
      else if(seconds>=2 && digest!=firstFrame)changingDecodedVideo=true;
    });decoderSample.start();
    QProcess writer;writer.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),{
      FOXICMP_V2_PLAYBACK_SCRIPT,"--media-profile",mediaProfile,"--player",playerExecutable(),"--profile",profile.directory,
      "--fixtures",qEnvironmentVariable("FOXICMP_TEST_MEDIA"),"--work",temporary.filePath("v2-media-bytes-"+mediaProfile),
      "--expected-pid",QString::number(QCoreApplication::applicationPid())});
    QVERIFY(writer.waitForStarted());const auto guard=qScopeGuard([&]{if(writer.state()!=QProcess::NotRunning){writer.kill();writer.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(writer.state()==QProcess::NotRunning,60000);
    QVERIFY2(writer.exitCode()==0,writer.readAllStandardError().constData());
    auto result=QJsonDocument::fromJson(writer.readAllStandardOutput()).object();QCOMPARE(result["result"].toString(),QString("PASS"));
    if((mediaProfile.contains("-opus") || mediaProfile.startsWith("webm-") || mediaProfile.startsWith("mixed-")))result["backwardSeekWhileIncomplete"]=seekObserved;
    decoderSample.stop();
    QCOMPARE(decodedFormat,mediaProfile.contains("vp8")?QString("vp8"):mediaProfile.contains("vp9-")?QString("vp9"):mediaProfile.contains("av1-")?QString("av1"):mediaProfile.contains("h264")?QString("h264"):QString("hevc"));
    QVERIFY2(changingDecodedVideo,"No changing decoded video frames before writer complete");
    if(mediaProfile.contains("hevc10") || mediaProfile.contains("av1-10") || mediaProfile.contains("vp9-10"))QVERIFY2(decodedPixelFormat.contains("10"),qPrintable(decodedPixelFormat));
    QVERIFY2(decodedAudio.contains(mediaProfile.contains("-vorbis")?"vorbis":(mediaProfile.contains("-opus") || mediaProfile.startsWith("webm-"))?"opus":"aac",Qt::CaseInsensitive),qPrintable(decodedAudio));
    QCOMPARE(decodedAudioRate,48000);
    if((mediaProfile.contains("-opus") || mediaProfile.startsWith("webm-") || mediaProfile.startsWith("mixed-"))){QVERIFY(seekRequested);QVERIFY(seekObserved);}
    qInfo().noquote()<<"Decoded audio:"<<mediaProfile<<decodedAudio<<decodedAudioRate;
    qInfo().noquote()<<"Decoded video:"<<mediaProfile<<decodedFormat<<decodedPixelFormat<<"changing frames before complete";
    qInfo().noquote()<<"Schema-2 media report:"<<QJsonDocument(result).toJson(QJsonDocument::Compact);
    QVERIFY(result["playingWhilePhysicallyIncomplete"].toObject()["positionSeconds"].toDouble()>=10);
  }
  void multiAudioAndLateSubtitlesWhileIncomplete() {
    Profile profile(temporary.filePath("multi-audio-profile"));prepare(profile);Window w(profile);w.show();ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    auto* player=w.findChild<Player*>();QVERIFY(player);QSet<QString> audioDecoded;bool metadataObserved=false,lateText=false,seekDone=false,seekObserved=false,audioLabels=false,lateSelected=false,restoredSelections=false,restoredDecoded=false,unconfirmedSeekRejected=false,completeSeek=false,completeSeekObserved=false;int stage=0;
    QTimer sample;sample.setInterval(100);connect(&sample,&QTimer::timeout,this,[&]{
      if(!w.progressiveSession())return;
      if(w.progressiveSession()->status().complete){if(!completeSeek)completeSeek=w.seekTo(19);else if(player->get("time-pos").toDouble()>18 && player->get("aid").toInt()==3 && player->get("sid").toInt()==2)completeSeekObserved=true;return;}
      const auto seconds=player->get("time-pos").toDouble();const auto codec=player->get("audio-codec-name").toString();
      const auto metadata=w.progressivePlaybackStatus().value("metadata").toObject();metadataObserved|=metadata.value("revision").toString()=="1" && metadata.value("chapters").toArray().size()==2;
      if(seconds>1 && !codec.isEmpty())audioDecoded.insert(codec);if(player->get("sub-text").toString().contains(QString::fromUtf8("Поздние")))lateText=true;
      const auto tracks=player->get("track-list").toList();int audios=0,subs=0;for(const auto& v:tracks){const auto type=v.toMap().value("type").toString();if(type=="audio")++audios;if(type=="sub")++subs;}
      if(audios==3){QStringList labels;for(auto* action:w.audioMenu->actions())labels<<action->text();const auto all=labels.join(" ");audioLabels|=all.contains(QString::fromUtf8("Русский AAC")) && all.contains("English Opus") && all.contains("Commentary Vorbis");}
      if(audios==3 && stage==0 && seconds>=2){w.rememberTrack("aid","3");stage=1;}
      else if(stage==1 && codec.contains("vorbis") && seconds>=4){w.rememberTrack("aid","2");stage=2;}
      else if(stage==2 && codec.contains("opus") && seconds>=6){w.rememberTrack("aid","1");stage=3;}
      if(subs==2 && !lateSelected){w.rememberTrack("sid","2");lateSelected=true;}
      if(stage==3 && codec.contains("aac") && seconds>=8){w.rememberTrack("aid","3");stage=4;}
      if(stage==4 && codec.contains("vorbis") && !seekDone){unconfirmedSeekRejected=!w.seekTo(15);seekDone=w.seekTo(2);}
      if(seekDone && seconds<3){seekObserved=true;restoredSelections|=player->get("aid").toInt()==3 && player->get("sid").toInt()==2;}
      if(restoredSelections && seconds>=4 && codec=="vorbis" && player->get("aid").toInt()==3 && player->get("sid").toInt()==2)restoredDecoded=true;
    });sample.start();
    QProcess writer;writer.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),{QFileInfo(FOXICMP_V2_PLAYBACK_SCRIPT).dir().filePath("progressive_multitrack_conformance.py"),"--player",playerExecutable(),"--profile",profile.directory,"--fixtures",qEnvironmentVariable("FOXICMP_TEST_MEDIA"),"--work",temporary.filePath("multi-audio-bytes")});
    QVERIFY(writer.waitForStarted());const auto guard=qScopeGuard([&]{if(writer.state()!=QProcess::NotRunning){writer.kill();writer.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(writer.state()==QProcess::NotRunning,90000);if(writer.exitCode()!=0)qWarning().noquote()<<writer.readAllStandardError();QVERIFY(writer.exitCode()==0);sample.stop();
    qInfo()<<"Multi-track selections"<<audioDecoded<<stage<<seekDone<<seekObserved<<lateSelected<<restoredSelections;
    QVERIFY(metadataObserved);QVERIFY(audioDecoded.contains("aac"));QVERIFY(audioDecoded.contains("opus"));QVERIFY(audioDecoded.contains("vorbis"));QVERIFY(seekDone);QVERIFY(seekObserved);QVERIFY(lateText);QVERIFY(audioLabels);QVERIFY(restoredSelections);QVERIFY(restoredDecoded);QVERIFY(unconfirmedSeekRejected);QVERIFY(completeSeek);QVERIFY(completeSeekObserved);
    auto result=QJsonDocument::fromJson(writer.readAllStandardOutput()).object();result["backwardSeek"]=seekObserved;result["decodedAudioCodecs"]=QJsonArray::fromStringList(audioDecoded.values());result["lateSubtitleTextObserved"]=lateText;result["audioMenuLabelsObserved"]=audioLabels;result["selectedTracksRestoredAfterSeek"]=restoredSelections && restoredDecoded;result["unconfirmedSeekRejected"]=unconfirmedSeekRejected;result["completeFarSeekObserved"]=completeSeekObserved;QCOMPARE(result["result"].toString(),QString("PASS"));qInfo().noquote()<<"Multi-track report:"<<QJsonDocument(result).toJson(QJsonDocument::Compact);
  }
  void progressiveMetadataUpdatesActualUi() {
    Profile profile(temporary.filePath("metadata-ui"));prepare(profile);profile.settings.setValue("ui/titleMode","title");
    Window w(profile);w.resize(800,500);w.show();ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    auto* player=w.findChild<Player*>();QVERIFY(player);bool initial=false,late=false,cleared=false,restored=false,restarted=false,marker=false,noMarker=false,jumped=false,requested=false;
    QTimer sample;sample.setInterval(50);connect(&sample,&QTimer::timeout,this,[&]{
      if(!w.progressiveSession())return;
      const auto metadata=w.progressiveSession()->playbackDetails().value("metadata").toObject();
      const auto rev=metadata.value("revision").toString();const auto title=metadata.value("title").toString();
      const auto labels=[&]{QStringList out;for(auto* item:w.chapterMenu->actions())if(item->isEnabled())out<<item->text();return out;}();
      if(rev=="1" && title==QString::fromUtf8("Первый {app}"))initial|=w.timeline->chapters.size()==2 && labels.size()==2 && w.windowTitle().startsWith(title);
      if(rev=="2") {
        late|=w.timeline->chapters.size()==3 && labels.size()==3 && labels[1].contains(QString::fromUtf8("Выбор главы")) && w.windowTitle().startsWith(title);
        const auto image=w.timeline->grab().toImage();if(image.width()>10 && image.height()>5){const int x=qRound(.8*(image.width()-1));marker|=image.pixelColor(x,2)==QColor(94,99,104) && image.pixelColor(x,image.height()-3)==QColor(94,99,104);}
        if(!requested && late && w.pos>=1.6 && w.pos<3){requested=true;w.chapterMenu->actions()[1]->trigger();}
        if(requested && w.pos>=3.9 && w.pos<5)jumped=true;
      }
      if(rev=="3") {
        cleared|=w.timeline->chapters.isEmpty() && labels.isEmpty() && w.windowTitle().startsWith(title);
        const auto image=w.timeline->grab().toImage();if(image.width()>10){const int x=qRound(.8*(image.width()-1));noMarker|=image.pixelColor(x,2)!=QColor(94,99,104);}
      }
      if(rev=="5")restored|=w.timeline->chapters.size()==1 && labels.size()==1 && w.windowTitle().startsWith(title);
      if(rev=="1" && title==QString::fromUtf8("Второй сеанс"))restarted|=w.timeline->chapters.size()==2 && labels.size()==2 && w.windowTitle().startsWith(title);
    });sample.start();
    QProcess writer;writer.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),{QFileInfo(FOXICMP_V2_PLAYBACK_SCRIPT).dir().filePath("progressive_metadata_conformance.py"),"--player",playerExecutable(),"--profile",profile.directory,"--fixtures",qEnvironmentVariable("FOXICMP_TEST_MEDIA"),"--work",temporary.filePath("metadata-ui-bytes")});
    QVERIFY(writer.waitForStarted());const auto guard=qScopeGuard([&]{if(writer.state()!=QProcess::NotRunning){writer.kill();writer.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(writer.state()==QProcess::NotRunning,85000);if(writer.exitCode()!=0)qWarning().noquote()<<writer.readAllStandardError();QVERIFY(writer.exitCode()==0);sample.stop();
    QVERIFY(initial);QVERIFY(late);QVERIFY(cleared);QVERIFY(restored);QVERIFY(restarted);QVERIFY(marker);QVERIFY(noMarker);QVERIFY(jumped);
    qInfo().noquote()<<"Actual metadata UI report:"<<writer.readAllStandardOutput();
  }
  void segmentedFmp4Playback_data() {
    QTest::addColumn<QString>("mediaProfile");
    QStringList profiles{"h264","hevc8","hevc10","av1-8","av1-10","vp9-8","vp9-10"};
    const auto videoProfiles=profiles;for(const auto& name:videoProfiles)profiles<<name+"-opus";profiles<<"h264-opus-mono";
    for(const auto& name:profiles)QTest::newRow(qPrintable(name))<<name;
  }
  void segmentedFmp4Playback() {
    QFETCH(QString,mediaProfile);checkSegmentPlayback(mediaProfile,false);
  }
  void catalogFmp4Playback_data() {
    QTest::addColumn<QString>("mediaProfile");
    for(const auto& name:QStringList{"h264","hevc10-opus","av1-10"})QTest::newRow(qPrintable(name))<<name;
  }
  void catalogFmp4Playback() {
    QFETCH(QString,mediaProfile);checkSegmentPlayback(mediaProfile,true);
  }
  void segmentedWebmPlayback_data() {
    QTest::addColumn<QString>("mediaProfile");
    for(const auto& name:QStringList{"vp8","vp9-8","vp9-10","av1-8","av1-10"})QTest::newRow(qPrintable(name))<<name;
  }
  void segmentedWebmPlayback() {
    QFETCH(QString,mediaProfile);checkSegmentPlayback(mediaProfile,false,true);
  }
  void catalogWebmPlayback_data() {
    QTest::addColumn<QString>("mediaProfile");
    for(const auto& name:QStringList{"vp8","vp9-10","av1-10"})QTest::newRow(qPrintable(name))<<name;
  }
  void catalogWebmPlayback() {
    QFETCH(QString,mediaProfile);checkSegmentPlayback(mediaProfile,true,true);
  }
  void segmentedTsPlayback_data() {
    QTest::addColumn<QString>("mediaProfile");
    for(const auto& name:QStringList{"main-bframes","high-fractional","main-preroll"})QTest::newRow(qPrintable(name))<<name;
  }
  void segmentedTsPlayback() {QFETCH(QString,mediaProfile);checkSegmentPlayback(mediaProfile,false,false,true);}
  void catalogTsPlayback() {checkSegmentPlayback("main-bframes",true,false,true);}
  void segmentedMuxTsPlayback_data() {
    QTest::addColumn<QString>("mediaProfile");
    for(const auto& name:QStringList{"main-bframes","high-fractional","main-preroll"})QTest::newRow(qPrintable(name))<<name;
  }
  void segmentedMuxTsPlayback() {QFETCH(QString,mediaProfile);checkSegmentPlayback(mediaProfile,false,false,true,true);}
  void catalogMuxTsPlayback() {checkSegmentPlayback("main-bframes",true,false,true,true);}
  void growingTsPlayback_data() {segmentedMuxTsPlayback_data();}
  void growingTsPlayback() {QFETCH(QString,mediaProfile);checkSegmentPlayback(mediaProfile,false,false,true,true,true);}
  void muxedMp4Playback_data() {
    QTest::addColumn<QString>("mediaProfile");
    for(const auto& name:QStringList{"main-bframes","high-fractional","main-delayed"})QTest::newRow(qPrintable(name))<<name;
  }
  void muxedMp4Playback() {QFETCH(QString,mediaProfile);checkSegmentPlayback(mediaProfile,false,false,false,false,false,true);}
private:
  void checkSegmentPlayback(const QString& mediaProfile,bool catalogMode,bool webm=false,bool ts=false,bool muxed=false,bool growing=false,bool muxedMp4=false) {
    const QString expectedCodec=ts || muxedMp4?"h264":mediaProfile=="vp8"?"vp8":mediaProfile.startsWith("av1-")?"av1":mediaProfile.startsWith("vp9-")?"vp9":mediaProfile.startsWith("h264")?"h264":"hevc";
    const QString expectedAudio=webm || mediaProfile.contains("-opus")?"opus":"aac";
    const QString expectedMode=QString(muxedMp4?"growing-muxed-fmp4-":growing?"growing-muxed-mpegts-":muxed?"segmented-muxed-mpegts-":ts?"segmented-mpegts-":webm?"segmented-webm-":"segmented-fmp4-")+expectedCodec+"-"+(expectedAudio=="opus"?"opus":"aac-lc");
    const int expectedChannels=ts || muxedMp4?2:expectedAudio=="aac" || mediaProfile.endsWith("-mono")?1:2;
    const int expectedRate=(ts || muxedMp4) && mediaProfile=="high-fractional"?44100:48000;
    const auto prefix=QString(catalogMode?"catalog-":"segment-")+
        (muxedMp4?"mux-mp4-":growing?"growing-ts-":muxed?"mux-ts-":ts?"ts-":webm?"webm-":"fmp4-");
    Profile profile(temporary.filePath(prefix+"playback-profile-"+mediaProfile));prepare(profile);
    Window w(profile);w.show();ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    auto* player=w.findChild<Player*>();QVERIFY(player);
    const auto work=temporary.filePath(prefix+"playback-bytes-"+mediaProfile);
    int sequence=0;bool decoded=false,changingVideo=false;int decodedChannels=0;QByteArray firstFrame;QString decodedFormat,decodedPixelFormat;QMap<int,QByteArray> zoneFrames;QSet<int> changingZones;
    QTimer sample;sample.setInterval(50);connect(&sample,&QTimer::timeout,this,[&]{
      QFile control(work+"/control.json");
      if(control.open(QIODevice::ReadOnly)) {
        const auto c=QJsonDocument::fromJson(control.readAll()).object();control.close();
        if(c["sequence"].toInt()>sequence) {
          sequence=c["sequence"].toInt();const auto action=c["action"].toString();
          if(action=="pause")w.actions["play"]->trigger();
          else if(action=="seek")QVERIFY(w.seekTo(c["value"].toDouble()));
        }
      }
      if(!w.progressiveSession())return;
      const auto state=w.progressivePlaybackStatus();
      if(state["state"]=="playing" && state["mediaMode"]==expectedMode &&
         player->get("audio-codec-name")==expectedAudio && player->get("audio-params/samplerate").toInt()==expectedRate)decoded=true;
      if(decoded && state["state"]=="playing") {
        const auto channels=player->get("audio-params/channel-count").toInt();if(channels>0)decodedChannels=channels;
        const auto format=player->get("video-format").toString(),pixels=player->get("video-dec-params/pixelformat").toString();
        if(!format.isEmpty())decodedFormat=format;if(!pixels.isEmpty())decodedPixelFormat=pixels;
        const auto frame=player->grabFramebuffer();if(!frame.isNull()) {
          const auto digest=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(frame.constBits()),frame.sizeInBytes()),QCryptographicHash::Sha256);
          if(firstFrame.isEmpty())firstFrame=digest;else changingVideo|=digest!=firstFrame;
          if(catalogMode && !webm && !ts) {
            const auto position=state["positionSeconds"].toDouble();const int zone=position>=68?2:position>=35 && position<40?1:position<10?0:-1;
            if(zone>=0) {if(!zoneFrames.contains(zone))zoneFrames[zone]=digest;else if(zoneFrames[zone]!=digest)changingZones.insert(zone);}
          }
        }
      }
    });sample.start();
    QStringList writerArgs{QFileInfo(FOXICMP_V2_PLAYBACK_SCRIPT).dir().filePath(muxedMp4?"progressive_mux_mp4_playback.py":growing?"progressive_growing_ts_playback.py":muxed?"progressive_mux_ts_playback.py":ts?"progressive_ts_playback.py":webm?"progressive_webm_segment_playback.py":catalogMode?"progressive_catalog_playback.py":"progressive_segment_playback.py"),"--player",playerExecutable(),"--profile",profile.directory,"--fixtures",qEnvironmentVariable("FOXICMP_TEST_MEDIA"),"--work",work,"--media-profile",mediaProfile};
    if((webm || ts) && catalogMode)writerArgs<<"--catalog";
    QProcess writer;writer.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),writerArgs);
    QVERIFY(writer.waitForStarted());const auto guard=qScopeGuard([&]{if(writer.state()!=QProcess::NotRunning){writer.kill();writer.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(writer.state()==QProcess::NotRunning,150000);
    if(writer.exitCode()!=0)qWarning().noquote()<<writer.readAllStandardError();
    QCOMPARE(writer.exitCode(),0);sample.stop();QVERIFY(decoded);QVERIFY(changingVideo);
    if(catalogMode && !webm && !ts){QVERIFY(changingZones.contains(0));QVERIFY(changingZones.contains(1));QVERIFY(changingZones.contains(2));}
    QCOMPARE(decodedFormat,expectedCodec);QCOMPARE(decodedChannels,expectedChannels);
    if(mediaProfile.contains("hevc10") || mediaProfile.contains("-10"))QVERIFY2(decodedPixelFormat.contains("10"),qPrintable(decodedPixelFormat));
    qInfo().noquote()<<"Segment decoded audio:"<<mediaProfile<<expectedAudio<<decodedChannels<<expectedRate;
    qInfo().noquote()<<"Segment decoded video:"<<mediaProfile<<decodedFormat<<decodedPixelFormat;
    qInfo().noquote()<<"Segment playback report:"<<writer.readAllStandardOutput();
  }
private slots:
  void partialOptionalAudio_data() {
    QTest::addColumn<QString>("audioProfile");
    for(const auto* name:{"aac","opus-webm","vorbis","opus-fmp4","active-error","active-writer-error","active-init-error","inactive-error","paused-active-error","required-error","active-opus-error","active-vorbis-error","integrity-active-error","integrity-inactive-error","integrity-required-error","integrity-opus-error"})QTest::newRow(name)<<QString(name);
  }
  void partialOptionalAudio() {
    QFETCH(QString,audioProfile);
    Profile profile(temporary.filePath("partial-audio-"+audioProfile));prepare(profile);
    Window w(profile);w.show();ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    auto* player=w.findChild<Player*>();QVERIFY(player);
    const auto work=temporary.filePath("partial-audio-bytes-"+audioProfile);
    int sequence=0;bool decoded=false,changingVideo=false,subtitleRestored=false,activeFailure=false;QByteArray firstFrame;
    QTimer sample;sample.setInterval(50);connect(&sample,&QTimer::timeout,this,[&]{
      QFile control(work+"/control.json");
      if(control.open(QIODevice::ReadOnly)) {
        const auto c=QJsonDocument::fromJson(control.readAll()).object();control.close();
        if(c["sequence"].toInt()>sequence) {
          sequence=c["sequence"].toInt();const auto action=c["action"].toString();
          if(action=="audio")w.rememberTrack("aid",QString::number(c["value"].toInt()+1));
          else if(action=="pause")w.actions["play"]->trigger();
          else if(action=="seek")QVERIFY(w.seekTo(c["value"].toDouble()));
        }
      }
      if(!w.progressiveSession())return;
      const auto state=w.progressivePlaybackStatus();bool selected=false;
      for(const auto& value:state["mediaTracks"].toArray()){const auto t=value.toObject();selected|=t["id"]=="optional-audio" && t["selected"].toBool();}
      const bool failureProfile=audioProfile.endsWith("error");
      const auto codec=audioProfile.endsWith("opus-error")?"opus":audioProfile=="active-vorbis-error"?"vorbis":(audioProfile=="aac" || failureProfile)?"aac":audioProfile=="vorbis"?"vorbis":"opus";
      if(selected && state["state"]=="playing" && player->get("audio-codec-name")==codec && player->get("audio-params/samplerate").toInt()==48000)decoded=true;
      if(decoded && selected && state["state"]=="playing")subtitleRestored|=player->get("sub-text").toString().contains(QString::fromUtf8("Ранние subtitles"));
      if(failureProfile && !audioProfile.endsWith("required-error")) {
        for(const auto& value:state["mediaTracks"].toArray()) {const auto t=value.toObject();activeFailure|=t["id"]=="optional-audio" && t["state"]=="error" && state["audioSelection"].toObject()["index"].toInt()==0 && state["state"]=="playing" && player->get("audio-codec-name")=="aac" && player->get("audio-params/samplerate").toInt()==48000;}
      }
      if(decoded){const auto frame=player->grabFramebuffer();if(!frame.isNull()){const auto digest=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(frame.constBits()),frame.sizeInBytes()),QCryptographicHash::Sha256);if(firstFrame.isEmpty())firstFrame=digest;else changingVideo|=digest!=firstFrame;}}
    });sample.start();
    QProcess writer;writer.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),{QFileInfo(FOXICMP_V2_PLAYBACK_SCRIPT).dir().filePath("progressive_partial_audio_conformance.py"),"--player",playerExecutable(),"--profile",profile.directory,"--fixtures",qEnvironmentVariable("FOXICMP_TEST_MEDIA"),"--work",work,"--media-profile",audioProfile});
    QVERIFY(writer.waitForStarted());const auto guard=qScopeGuard([&]{if(writer.state()!=QProcess::NotRunning){writer.kill();writer.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(writer.state()==QProcess::NotRunning,180000);if(writer.exitCode()!=0)qWarning().noquote()<<writer.readAllStandardError();QVERIFY(writer.exitCode()==0);sample.stop();if(!audioProfile.endsWith("inactive-error"))QVERIFY(decoded);if(!audioProfile.endsWith("inactive-error"))QVERIFY(changingVideo);if(audioProfile.endsWith("error") && !audioProfile.endsWith("required-error"))QVERIFY(activeFailure);else if(!audioProfile.endsWith("required-error"))QVERIFY(subtitleRestored);
    qInfo().noquote()<<"Partial audio report:"<<writer.readAllStandardOutput();
  }
  void optionalAudioDoesNotBlockPlayback_data() {
    QTest::addColumn<QString>("audioProfile");
    for(const auto* name:{"late-aac","late-opus","late-vorbis","initial-ready","missing-complete","invalid-complete","writer-failed","never-ready","out-of-order","late-opus-fmp4"})QTest::newRow(name)<<QString(name);
  }
  void optionalAudioDoesNotBlockPlayback() {
    QFETCH(QString,audioProfile);
    Profile profile(temporary.filePath("optional-audio-"+audioProfile));prepare(profile);Window w(profile);w.show();ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    auto* player=w.findChild<Player*>();QVERIFY(player);
    const bool positive=audioProfile=="late-aac" || audioProfile=="late-opus" || audioProfile=="late-vorbis" || audioProfile=="initial-ready" || audioProfile=="out-of-order" || audioProfile=="late-opus-fmp4";
    const QString expected=audioProfile=="late-aac"?"aac":audioProfile=="late-vorbis"?"vorbis":"opus";
    bool primaryDecoded=false,optionalDecoded=false,selected=false,seekDone=false,seekObserved=false,restored=false,changingFrames=false;QByteArray firstFrame;
    QTimer sample;sample.setInterval(100);connect(&sample,&QTimer::timeout,this,[&]{
      if(!w.progressiveSession())return;
      const auto seconds=player->get("time-pos").toDouble();const auto codec=player->get("audio-codec-name").toString();
      if(seconds>=1 && player->get("aid").toInt()==1 && codec=="aac" && player->get("audio-params/samplerate").toInt()==48000)primaryDecoded=true;
      const auto tracks=w.progressivePlaybackStatus()["mediaTracks"].toArray();bool ready=false,optionalSelected=false;
      for(const auto& value:tracks){const auto track=value.toObject();if(track["id"]=="optional-audio" && track["loaded"].toBool()){ready=true;optionalSelected=track["selected"].toBool();}}
      if(positive && ready && seconds>=4.3 && !selected){w.rememberTrack("aid","2");selected=true;}
      if(selected && optionalSelected && codec==expected && player->get("audio-params/samplerate").toInt()==48000)optionalDecoded=true;
      if(optionalDecoded && seconds>=6.3 && !seekDone)seekDone=w.seekTo(2);
      if(seekDone && seconds<3 && !player->get("seeking").toBool())seekObserved=true;
      if(seekObserved && seconds>=4 && optionalSelected && codec==expected)restored=true;
      if(seconds>=4){const auto frame=player->grabFramebuffer();if(!frame.isNull()){const auto digest=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(frame.constBits()),frame.sizeInBytes()),QCryptographicHash::Sha256);if(firstFrame.isEmpty())firstFrame=digest;else changingFrames|=digest!=firstFrame;}}
    });sample.start();
    QProcess writer;writer.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),{QFileInfo(FOXICMP_V2_PLAYBACK_SCRIPT).dir().filePath("progressive_optional_audio_conformance.py"),"--player",playerExecutable(),"--profile",profile.directory,"--fixtures",qEnvironmentVariable("FOXICMP_TEST_MEDIA"),"--work",temporary.filePath("optional-audio-bytes-"+audioProfile),"--media-profile",audioProfile});
    QVERIFY(writer.waitForStarted());const auto guard=qScopeGuard([&]{if(writer.state()!=QProcess::NotRunning){writer.kill();writer.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(writer.state()==QProcess::NotRunning,85000);if(writer.exitCode()!=0)qWarning().noquote()<<writer.readAllStandardError();QVERIFY(writer.exitCode()==0);sample.stop();
    QVERIFY(primaryDecoded);QVERIFY(changingFrames);if(positive){QVERIFY(optionalDecoded);QVERIFY(seekDone);QVERIFY(seekObserved);QVERIFY(restored);}
    auto result=QJsonDocument::fromJson(writer.readAllStandardOutput()).object();QCOMPARE(result["result"].toString(),QString("PASS"));result["primaryAudioDecoded"]=primaryDecoded;result["optionalAudioDecoded"]=optionalDecoded;result["changingDecodedVideo"]=changingFrames;result["selectionRestoredAfterSeek"]=restored;
    qInfo().noquote()<<"Optional audio report:"<<QJsonDocument(result).toJson(QJsonDocument::Compact);
  }
  void optionalSubtitleErrorsKeepPlayback_data() {
    QTest::addColumn<QString>("srtProfile");
    for(const auto* name:{"initial-invalid","late-invalid","cached-truncate","cached-rewrite","missing-complete","writer-failed","cache-missing","required-audio-truncated"})QTest::newRow(name)<<QString(name);
  }
  void optionalSubtitleErrorsKeepPlayback() {
    QFETCH(QString,srtProfile);
    Profile profile(temporary.filePath("srt-isolation-"+srtProfile));prepare(profile);
    Window w(profile);w.show();ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    auto* player=w.findChild<Player*>();QVERIFY(player);
    bool audioDecoded=false,changingVideo=false,textObserved=false,selected=false,seekDone=false,seekObserved=false,restoredText=false;
    QByteArray firstFrame;
    const QString expected=srtProfile=="late-invalid" || srtProfile.startsWith("cached-")?QString::fromUtf8("Ранние"):QString::fromUtf8("Поздние");
    QTimer sample;sample.setInterval(100);connect(&sample,&QTimer::timeout,this,[&]{
      if(!w.progressiveSession())return;
      const auto seconds=player->get("time-pos").toDouble();
      if(seconds>=1 && player->get("audio-codec-name").toString().contains("aac") && player->get("audio-params/samplerate").toInt()==48000)audioDecoded=true;
      if(srtProfile=="required-audio-truncated")return;
      const auto status=w.progressivePlaybackStatus();bool good=false;
      for(const auto& value:status["subtitleTracks"].toArray()) {
        const auto track=value.toObject();if(track["loaded"].toBool() && track["backendError"].toString().isEmpty())good=true;
      }
      if(good && seconds>=4 && !selected){w.rememberTrack("sid","1");selected=true;}
      textObserved |= seconds>=4 && player->get("sub-text").toString().contains(expected);
      if(srtProfile=="cache-missing" && selected && seconds>=6 && !seekDone)seekDone=w.seekTo(2);
      if(seekDone && seconds<3 && !player->get("seeking").toBool())seekObserved=true;
      if(seekObserved && seconds>=4 && player->get("sub-text").toString().contains(expected))restoredText=true;
      if(seconds>=4){const auto frame=player->grabFramebuffer();if(!frame.isNull()){
        const auto digest=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(frame.constBits()),frame.sizeInBytes()),QCryptographicHash::Sha256);
        if(firstFrame.isEmpty())firstFrame=digest;else changingVideo |= digest!=firstFrame;
      }}
    });sample.start();
    QProcess writer;writer.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),{QFileInfo(FOXICMP_V2_PLAYBACK_SCRIPT).dir().filePath("progressive_subtitle_isolation_conformance.py"),"--player",playerExecutable(),"--profile",profile.directory,"--fixtures",qEnvironmentVariable("FOXICMP_TEST_MEDIA"),"--work",temporary.filePath("srt-bytes-"+srtProfile),"--media-profile",srtProfile});
    QVERIFY(writer.waitForStarted());const auto guard=qScopeGuard([&]{if(writer.state()!=QProcess::NotRunning){writer.kill();writer.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(writer.state()==QProcess::NotRunning,85000);if(writer.exitCode()!=0)qWarning().noquote()<<writer.readAllStandardError();QVERIFY(writer.exitCode()==0);sample.stop();
    QVERIFY(audioDecoded);
    if(srtProfile!="required-audio-truncated"){QVERIFY(changingVideo);QVERIFY(textObserved);}
    if(srtProfile=="cache-missing"){QVERIFY(seekDone);QVERIFY(seekObserved);QVERIFY(restoredText);}
    auto result=QJsonDocument::fromJson(writer.readAllStandardOutput()).object();QCOMPARE(result["result"].toString(),QString("PASS"));
    result["decodedAudioBeforeFailure"]=audioDecoded;result["changingDecodedVideo"]=changingVideo;result["validSubtitleTextObserved"]=textObserved;result["backwardSeekAfterBackendFailure"]=seekObserved && restoredText;
    qInfo().noquote()<<"SRT isolation report:"<<QJsonDocument(result).toJson(QJsonDocument::Compact);
  }
  void multiAudioFarSeekWhileIncomplete_data() {
    QTest::addColumn<QString>("seekProfile");
    for(const auto& name:{"mixed","fmp4","webm","mixed-hevc10","mixed-av1-10","mixed-vp8","mixed-known","fmp4-known","webm-known","mixed-hidden-tail"})QTest::newRow(name)<<QString(name);
  }
  void multiAudioFarSeekWhileIncomplete() {
    QFETCH(QString,seekProfile);
    Profile profile(temporary.filePath("far-profile-"+seekProfile));prepare(profile);Window w(profile);w.show();ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    const auto seekCaps=api.request(QJsonObject{{"version",1},{"action","capabilities"}})["qtMultiAudioSeek"].toObject();
    QCOMPARE(seekCaps["mode"].toString(),QString("confirmed-byte-seek"));QVERIFY(seekCaps["confirmedRangesOnly"].toBool());QVERIFY(seekCaps["maxTargetSecondsWhileIncomplete"].toDouble()>=90);
    auto* player=w.findChild<Player*>();QVERIFY(player);
    const QString third=seekProfile.startsWith("fmp4")?"aac":"vorbis",second="opus";
    int stage=0;bool subtitleChosen=false,forward=false,backward=false,pausedSeek=false,resumed=false,completeSeek=false,completeObserved=false,unconfirmedRejected=false,changingFrames=false,audioClockAtTarget=false;
    double pausePosition=0,maxSoftwareAvSync=0;bool seekFromBuffering=false,subtitleTextAtTarget=false,speedPreserved=true;QString observedSubtitleText;int syncSamples=0;QElapsedTimer pauseClock,forwardClock,tailClock,resumeClock; qint64 forwardLatency=-1;QByteArray firstFrame;
    QTimer sample;sample.setInterval(50);connect(&sample,&QTimer::timeout,this,[&]{
      if(!w.progressiveSession())return;
      const auto seconds=player->get("time-pos").toDouble();const auto codec=player->get("audio-codec-name").toString();
      const int aid=player->get("aid").toInt(),sid=player->get("sid").toInt();
      if(stage==7 && seconds>=51 && !player->get("pause").toBool()){
        resumed=true;if(!resumeClock.isValid())resumeClock.start();
        if(resumeClock.elapsed()>=300 && w.seekTo(w.progressiveSession()->status().available-.5))stage=8;
      }
      if(stage==8 && w.progressivePendingSeek<0 && (seconds>70 || w.progressiveSeekTarget>70) && w.progressivePlaybackStatus()["state"]=="buffering"){
        if(!tailClock.isValid())tailClock.start();
        if(tailClock.elapsed()>=800 && w.seekTo(30))stage=9;
      }
      if(stage==9 && seconds>=29.9 && seconds<34 && !player->get("seeking").toBool() && !player->get("pause").toBool()){
        seekFromBuffering=aid==2 && sid==2 && codec==second;stage=10;
      }
      if(stage>=2 && stage<=10 && w.progressivePendingSeek<0 && !player->get("seeking").toBool())speedPreserved &= std::abs(player->get("speed").toDouble()-1.2)<.001;
      if(stage==3 && seconds>=60.4 && seconds<61.5){observedSubtitleText=player->get("sub-text").toString();subtitleTextAtTarget |= observedSubtitleText.contains(QString::fromUtf8("Дальняя перемотка 1"));const auto audio=player->get("audio-pts"),sync=player->get("avsync");if(audio.isValid())audioClockAtTarget|=std::abs(audio.toDouble()-seconds)<1;if(sync.isValid()){++syncSamples;maxSoftwareAvSync=std::max(maxSoftwareAvSync,std::abs(sync.toDouble()));}}
      if(w.progressiveSession()->status().complete){if(!completeSeek)completeSeek=w.seekTo(88);else if(seconds>=87.9 && aid==2 && sid==2 && codec==second)completeObserved=true;return;}
      int audios=0,subs=0;for(const auto& v:player->get("track-list").toList()){const auto type=v.toMap().value("type").toString();audios+=type=="audio";subs+=type=="sub";}
      if(subs==2 && !subtitleChosen){w.rememberTrack("sid","2");subtitleChosen=true;}
      if(stage==0 && audios==3 && subtitleChosen && seconds>=1){w.rememberTrack("aid","3");stage=1;}
      else if(stage==1 && aid==3 && codec==third && sid==2 && seconds>=1.5){w.setSpeed(1.2);forwardClock.start();if(w.seekTo(40) && w.seekTo(60))stage=2;}
      else if(stage==2 && seconds>=59.9 && seconds<63 && aid==3 && sid==2 && codec==third){forward=true;forwardLatency=forwardClock.elapsed();stage=3;}
      if(stage==3 && seconds>=60 && seconds<62){const auto frame=player->grabFramebuffer();if(!frame.isNull()){const auto digest=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(frame.constBits()),frame.sizeInBytes()),QCryptographicHash::Sha256);if(firstFrame.isEmpty())firstFrame=digest;else changingFrames|=digest!=firstFrame;}}
      if(stage==3 && seconds>=61.5){w.rememberTrack("aid","2");stage=4;}
      else if(stage==4 && aid==2 && codec==second){if(w.seekTo(30))stage=5;}
      else if(stage==5 && seconds>=29.9 && seconds<34 && aid==2 && sid==2 && codec==second){backward=true;if(seconds>=31){w.actions["play"]->trigger();if(w.seekTo(50))stage=6;}}
      else if(stage==6 && seconds>=49.9 && seconds<51 && player->get("pause").toBool() && aid==2 && sid==2 && codec==second){
        if(!pauseClock.isValid()){pausePosition=seconds;pauseClock.start();}
        if(pauseClock.elapsed()>=800){pausedSeek=std::abs(seconds-pausePosition)<.08;unconfirmedRejected=!w.seekTo(80);w.actions["play"]->trigger();stage=7;}
      }
    });sample.start();
    QProcess writer;writer.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),{QFileInfo(FOXICMP_V2_PLAYBACK_SCRIPT).dir().filePath("progressive_far_seek_conformance.py"),"--player",playerExecutable(),"--profile",profile.directory,"--fixtures",qEnvironmentVariable("FOXICMP_TEST_MEDIA"),"--work",temporary.filePath("far-bytes-"+seekProfile),"--media-profile",seekProfile});
    QVERIFY(writer.waitForStarted());const auto guard=qScopeGuard([&]{if(writer.state()!=QProcess::NotRunning){writer.kill();writer.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(writer.state()==QProcess::NotRunning,85000);if(writer.exitCode()!=0)qWarning().noquote()<<writer.readAllStandardError();QVERIFY(writer.exitCode()==0);sample.stop();
    QVERIFY(forward);QVERIFY(backward);QVERIFY(pausedSeek);QVERIFY(resumed);QVERIFY(seekFromBuffering);QVERIFY2(subtitleTextAtTarget,qPrintable(observedSubtitleText));QVERIFY(speedPreserved);QVERIFY(unconfirmedRejected);QVERIFY(completeSeek);QVERIFY(completeObserved);QVERIFY(changingFrames);QVERIFY(audioClockAtTarget);QVERIFY(forwardLatency>=0 && forwardLatency<10000);
    auto result=QJsonDocument::fromJson(writer.readAllStandardOutput()).object();QCOMPARE(result["result"].toString(),QString("PASS"));result["audioClockAtTarget"]=audioClockAtTarget;result["softwareAvSyncSamples"]=syncSamples;result["maxSoftwareAvSyncSeconds"]=maxSoftwareAvSync;result["forwardSeekMilliseconds"]=forwardLatency;result["decodedVideoChangesAtTarget"]=changingFrames;result["audioSubtitleSelectionPreserved"]=forward && backward && pausedSeek && completeObserved;result["pausePreserved"]=pausedSeek;result["seekFromBuffering"]=seekFromBuffering;result["subtitleTextAtTarget"]=subtitleTextAtTarget;result["speedPreserved"]=speedPreserved;result["unconfirmedSeekRejected"]=unconfirmedRejected;qInfo().noquote()<<"Far-seek report:"<<QJsonDocument(result).toJson(QJsonDocument::Compact);
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
  void legacyOpeningKeepsUiResponsive_data() {
    QTest::addColumn<QString>("mode");
    for(const auto* mode:{"failure","stop","close","windowclose","replace","destroy","timeout"})QTest::newRow(mode)<<QString(mode);
  }
  void legacyOpeningKeepsUiResponsive() {
    QFETCH(QString,mode);Profile profile(temporary.filePath("legacy-open-"+mode));prepare(profile);
    auto window=std::make_unique<Window>(profile);auto* w=window.get();ProgressiveApi api(w);QVERIFY(api.listen(profile.directory));
    auto entered=std::make_shared<std::promise<void>>();auto ready=entered->get_future();auto release=std::make_shared<std::promise<void>>();auto gate=release->get_future().share();
    auto exited=std::make_shared<std::promise<void>>();auto done=exited->get_future();
    w->legacyOpeningHook=[entered,gate,exited]{entered->set_value();gate.wait();exited->set_value();throw std::runtime_error("opening fixture failure");};
    const auto cleanup=qScopeGuard([release]{try{release->set_value();}catch(const std::future_error&){};});
    std::shared_ptr<ProgressiveSource> replacement;
    if(mode=="failure"){QVERIFY(api.request(message("start"))["ok"].toBool());replacement=w->progressiveSession();QVERIFY(replacement);}
    QSignalSpy completed(w,&Window::progressiveOpeningFinished);QElapsedTimer elapsed;elapsed.start();
    QVERIFY(w->openProgressiveSession("unopened-fixture.json"));QVERIFY(elapsed.elapsed()<200);QVERIFY(w->progressiveOpening());
    QVERIFY(!w->openProgressiveSession("second.json"));
    QTRY_VERIFY_WITH_TIMEOUT(ready.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,3000);
    int ticks=0;QTimer tick;connect(&tick,&QTimer::timeout,&tick,[&]{++ticks;});tick.start(5);QTRY_VERIFY_WITH_TIMEOUT(ticks>=4,1000);
    const auto caps=wire(ProgressiveApi::endpointFor(profile.directory),"{\"version\":1,\"action\":\"capabilities\"}\n");
    QVERIFY(caps["ok"].toBool());QVERIFY(caps["legacyPreparationPending"].toBool());
    QCOMPARE(w->progressiveSession(),replacement);
    if(mode=="stop")w->actions["stop"]->trigger();
    else if(mode=="close")w->closeCurrent();
    else if(mode=="windowclose")w->close();
    else if(mode=="replace"){QVERIFY(api.request(message("start"))["ok"].toBool());replacement=w->progressiveSession();QVERIFY(replacement);}
    else if(mode=="destroy"){elapsed.restart();window.reset();QVERIFY(elapsed.elapsed()<200);}
    else if(mode=="timeout"){QTRY_COMPARE_WITH_TIMEOUT(completed.size(),1,6500);QVERIFY(w->progressiveOpening());QVERIFY(!completed[0][0].toBool());}
    release->set_value();QTRY_VERIFY_WITH_TIMEOUT(done.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,3000);
    if(window){QTRY_VERIFY_WITH_TIMEOUT(!w->progressiveOpening(),3000);QCOMPARE(completed.size(),1);QVERIFY(!completed[0][0].toBool());QCOMPARE(w->progressiveSession(),replacement);}
  }
  void legacyMaintenanceKeepsUiResponsive_data() {
    QTest::addColumn<QString>("mode");
    for(const auto* mode:{"publish","stop","close","truncate","unchanged-truncate"})QTest::newRow(mode)<<QString(mode);
  }
  void legacyMaintenanceKeepsUiResponsive() {
    QFETCH(QString,mode);const auto root=QFileInfo(paths[0]).absolutePath();auto legacy=snapshot;auto tracks=legacy["tracks"].toObject();
    for(const auto& kind:{QString("video"),QString("audio")}){auto track=tracks[kind].toObject();track["path"]=QFileInfo(track["path"].toString()).fileName();tracks[kind]=track;}
    legacy["tracks"]=tracks;const auto manifest=QDir(root).filePath("session.json");
    {QFile file(manifest);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(legacy).toJson());}
    QProcess provider;provider.start(QStringLiteral(FOXICMP_PYTHON_EXECUTABLE),{FOXICMP_LEGACY_FIXTURE,"--manifest",manifest,"--fixtures",qEnvironmentVariable("FOXICMP_TEST_MEDIA")});QVERIFY(provider.waitForStarted());
    const auto cleanupProvider=qScopeGuard([&]{QFile stop(QDir(root).filePath("fixture-stop"));if(stop.open(QIODevice::WriteOnly))stop.close();if(!provider.waitForFinished(4000)){provider.kill();provider.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(root).filePath("fixture-ready.json")),6000);
    Profile profile(QDir(root).filePath("maintenance-profile"));prepare(profile);Window w(profile);w.show();ProgressiveApi api(&w);QVERIFY(api.listen(profile.directory));
    QVERIFY(w.openProgressiveSession(manifest));QTRY_VERIFY_WITH_TIMEOUT(w.progressiveSession(),6000);auto source=w.progressiveSession();const auto receipt=source->releaseReceipt();
    const auto before=source->status();QVERIFY(before.available>4 && before.available<6);
    auto entered=std::make_shared<std::promise<void>>();auto ready=entered->get_future();auto release=std::make_shared<std::promise<void>>();auto gate=release->get_future().share();auto once=std::make_shared<std::atomic<bool>>(false);
    const uint64_t initialRevision=uint64_t(legacy["revision"].toInteger());
    const uint64_t target=initialRevision+(mode=="unchanged-truncate"?0:1);
    std::atomic_store(&source->legacyIoHook,std::make_shared<const std::function<void(uint64_t)>>([=](uint64_t revision){if(revision<target || once->exchange(true))return;entered->set_value();gate.wait();}));
    const auto cleanupGate=qScopeGuard([release]{try{release->set_value();}catch(const std::future_error&){};});
    if(mode!="unchanged-truncate"){QFile control(QDir(root).filePath("fixture-control"));QVERIFY(control.open(QIODevice::WriteOnly));control.write("grow");}
    QTRY_VERIFY_WITH_TIMEOUT(ready.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,6000);
    int ticks=0;QTimer tick;connect(&tick,&QTimer::timeout,&tick,[&]{++ticks;});tick.start(5);QTRY_VERIFY_WITH_TIMEOUT(ticks>=4,1000);
    const auto caps=wire(ProgressiveApi::endpointFor(profile.directory),"{\"version\":1,\"action\":\"capabilities\"}\n");QVERIFY(caps["ok"].toBool());QVERIFY(caps["legacyRefresh"].toObject()["snapshotIndexCommitAtomic"].toBool());
    QCOMPARE(source->status().available,before.available);{std::lock_guard<std::mutex> lock(source->mutex);QCOMPARE(source->snapshot.revision,initialRevision);}
    QElapsedTimer elapsed;elapsed.start();source->notify("paused");QVERIFY(elapsed.elapsed()<200);
    if(mode=="stop" || mode=="close") {
      elapsed.restart();if(mode=="stop")w.actions["stop"]->trigger();else w.closeCurrent();QVERIFY(elapsed.elapsed()<200);QVERIFY(!w.progressiveSession());QVERIFY(!receipt->load());
    }else if(mode.contains("truncate")){QFile file(paths[0]);QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.resize(0));}
    release->set_value();
    if(mode=="publish") {
      QTRY_VERIFY_WITH_TIMEOUT(source->status().available>10,6000);{std::lock_guard<std::mutex> lock(source->mutex);QCOMPARE(source->snapshot.revision,initialRevision+1);}
      auto* player=w.findChild<Player*>();QTRY_VERIFY_WITH_TIMEOUT(player->get("time-pos").toDouble()>.2,10000);
    }else if(mode.contains("truncate"))QTRY_VERIFY_WITH_TIMEOUT(!source->status().error.isEmpty(),6000);
    else QVERIFY(source->status().error.isEmpty());
    w.closeCurrent();source.reset();QTRY_VERIFY_WITH_TIMEOUT(receipt->load(),6000);
    const auto leases=[&]{QFile file(QDir(root).filePath("leases.json"));return file.open(QIODevice::ReadOnly)?QJsonDocument::fromJson(file.readAll()).object():QJsonObject{};};
#ifndef Q_OS_WIN
    if(mode.contains("truncate")) {
      // The retained Unix provider rejects its corrupt manifest/files before
      // processing released. Local descriptors are closed, but its receipt is
      // deliberately conservative until OS process recovery; never fake an ACK.
      QTRY_VERIFY_WITH_TIMEOUT(leases()["leases"].toObject().contains(QString::number(QCoreApplication::applicationPid())),6000);
      qInfo()<<"Corrupt legacy source: local files closed; provider receipt retained for OS recovery";
    }else
#endif
      QTRY_VERIFY_WITH_TIMEOUT(leases()["leases"].isObject() && leases()["leases"].toObject().isEmpty(),6000);
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
    QTRY_VERIFY_WITH_TIMEOUT(w.progressiveSession(),6000);
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
