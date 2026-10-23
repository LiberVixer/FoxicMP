#include "player.h"
#include "profile.h"
#include "progressivesource.h"
#include "window.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QProcess>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <cstring>
#include <future>

class ProgressiveTest : public QObject {
  Q_OBJECT
  QTemporaryDir temporary;
  QString root, manifest;
  QByteArray video, audio;
  QJsonObject document;
  QProcess provider;
  bool indexedMode = false;
  void formats(bool derivedDuration = false) {
    QTest::addColumn<bool>("indexed");
    QTest::newRow("fmp4") << false;
    QTest::newRow("indexed") << true;
    if (derivedDuration)
      QTest::newRow("indexed-derived-duration") << true;
  }
  IndexedMp4 indexFor(const QByteArray &data, int kind) {
    IndexedMp4 index;
    index.open(
        [&](uint64_t offset, void *buffer, size_t count) {
          if (!harvester::RangeReady(offset, count, data.size()))
            return false;
          std::memcpy(buffer, data.constData() + offset, count);
          return true;
        },
        data.size(), data.size(), kind, 0);
    return index;
  }
  uint64_t prefix(const QByteArray &data, int fragments) {
    if (indexedMode) {
      const auto index = indexFor(data, &data == &audio ? 1 : 0);
      uint64_t end = 0;
      for (const auto &sample : index.samples())
        if (sample.decode < int64_t(fragments) * 10000000)
          end = sample.offset + sample.size;
      return end;
    }
    uint64_t position = 0;
    int seen = 0;
    while (position + 8 <= uint64_t(data.size())) {
      uint64_t length = qFromBigEndian<quint32>(data.constData() + position);
      const auto kind = data.mid(qsizetype(position + 4), 4);
      if (length == 1)
        length = qFromBigEndian<quint64>(data.constData() + position + 8);
      if (length < 8 || length > uint64_t(data.size()) - position)
        return 0;
      position += length;
      if (kind == "mdat" && ++seen == fragments)
        return position;
    }
    return data.size();
  }
  QByteArray load(const QString &name) {
    QFile file(QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(name));
    if (!file.open(QIODevice::ReadOnly))
      return {};
    return file.readAll();
  }
  void publish(uint64_t videoPrefix, uint64_t audioPrefix,
               bool complete = false) {
    auto tracks = document["tracks"].toObject();
    for (auto kind : {QString("video"), QString("audio")}) {
      auto track = tracks[kind].toObject();
      const uint64_t length = kind == "video" ? videoPrefix : audioPrefix;
      track["availablePrefixBytes"] = qint64(length);
      track["expectedTotalBytes"] =
          complete      ? QJsonValue(qint64(length))
          : indexedMode ? QJsonValue(qint64(kind == "video" ? video.size()
                                                            : audio.size()))
                        : QJsonValue(QJsonValue::Null);
      track["state"] = complete ? "complete" : "downloading";
      tracks[kind] = track;
    }
    document["tracks"] = tracks;
    document["state"] = complete ? "complete" : "downloading";
    document["revision"] = document["revision"].toInt() + 1;
    QSaveFile file(manifest);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(document).toJson(QJsonDocument::Compact));
    QVERIFY(file.commit());
  }
  void startProvider() {
    provider.start("python3", {QStringLiteral(FOXICMP_ADAPTER_SCRIPT),
                               "--provider", manifest, "0", ""});
    QVERIFY(provider.waitForStarted(5000));
    QTRY_VERIFY(QFileInfo::exists(QDir(root).filePath("provider.json")));
  }
private slots:
  void indexedSamplesMatchFfprobe_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("indexed-video") << 0;
    QTest::newRow("indexed-audio") << 1;
  }
  void indexedSamplesMatchFfprobe() {
    QFETCH(int, kind);
    const auto &data = kind ? audio : video;
    const auto index = indexFor(data, kind);
    const QString file = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                             .filePath(kind ? "progressive-indexed/audio.m4a"
                                            : "progressive-indexed/video.mp4");
    QProcess probe;
    probe.start("ffprobe",
                {"-v", "error", "-show_packets", "-show_entries",
                 "packet=pos,size,pts_time,dts_time,duration_time,flags", "-of",
                 "json", file});
    QVERIFY(probe.waitForFinished(5000));
    QCOMPARE(probe.exitCode(), 0);
    const auto packets = QJsonDocument::fromJson(probe.readAllStandardOutput())
                             .object()["packets"]
                             .toArray();
    QCOMPARE(index.samples().size(), size_t(packets.size()));
    for (qsizetype i = 0; i < packets.size(); ++i) {
      const auto packet = packets[i].toObject();
      const auto &sample = index.samples()[size_t(i)];
      QCOMPARE(sample.offset, packet["pos"].toString().toULongLong());
      QCOMPARE(sample.size, packet["size"].toString().toULongLong());
      QVERIFY(std::abs(double(sample.start) / 1e7 -
                       packet["pts_time"].toString().toDouble()) < .000002);
      QVERIFY(std::abs(double(sample.decode) / 1e7 -
                       packet["dts_time"].toString().toDouble()) < .000002);
      QVERIFY(std::abs(double(sample.end - sample.start) / 1e7 -
                       packet["duration_time"].toString().toDouble()) <
              .000002);
      QCOMPARE(sample.sync, packet["flags"].toString().contains('K'));
    }
    QCOMPARE(index.duration(), int64_t(200000000));
  }
  void indexedMalformedMetadataAndPartialFirstPacket() {
    const auto original = load("progressive-indexed/video.mp4");
    QVERIFY(!original.isEmpty());
    for (const auto &change : QList<QPair<QByteArray, int>>{{"stts", 12},
                                                            {"stco", 12},
                                                            {"stsz", 12},
                                                            {"stss", 12},
                                                            {"elst", 20}}) {
      QByteArray malformed = original;
      const int location = malformed.indexOf(change.first);
      QVERIFY(location > 0);
      qToBigEndian<quint32>(change.first == "stss" ? 2 : 0,
                            malformed.data() + location + change.second);
      QVERIFY_EXCEPTION_THROWN(indexFor(malformed, 0), std::runtime_error);
    }
    const auto full = indexFor(original, 0);
    const uint64_t boundary =
        full.samples().front().offset + full.samples().front().size - 1;
    IndexedMp4 partial;
    bool crossedBoundary = false;
    QVERIFY_EXCEPTION_THROWN(
        partial.open(
            [&](uint64_t at, void *buffer, size_t count) {
              if (!harvester::RangeReady(at, count, boundary)) {
                crossedBoundary = true;
                return false;
              }
              std::memcpy(buffer, original.constData() + at, count);
              return true;
            },
            boundary, original.size(), 0, 0),
        std::runtime_error);
    QVERIFY(!crossedBoundary);
    QByteArray wrongCodec = original;
    wrongCodec.replace(wrongCodec.indexOf("avc1", wrongCodec.indexOf("moov")),
                       4, "hvc1");
    QVERIFY_EXCEPTION_THROWN(indexFor(wrongCodec, 0), std::runtime_error);
  }
  void init() {
    indexedMode =
        QString::fromLatin1(QTest::currentDataTag()).contains("indexed");
    const QString folder =
        indexedMode ? "progressive-indexed/" : "progressive/";
    video = load(folder + "video.mp4");
    audio = load(folder + "audio.m4a");
    if (video.isEmpty() || audio.isEmpty())
      QSKIP("Generate progressive fixtures first");
    root =
        temporary.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QVERIFY(QDir().mkpath(root));
    QVERIFY(QFile::setPermissions(root, QFileDevice::ReadOwner |
                                            QFileDevice::WriteOwner |
                                            QFileDevice::ExeOwner));
    manifest = QDir(root).filePath("session.json");
    QJsonObject tracks;
    for (auto kind : {QString("video"), QString("audio")}) {
      const QString name = kind + ".part";
      QFile file(QDir(root).filePath(name));
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write(kind == "video" ? video : audio);
      tracks[kind] =
          QJsonObject{{"path", name},
                      {"container", kind == "video" ? "mp4" : "m4a"},
                      {"availablePrefixBytes", 0},
                      {"expectedTotalBytes", QJsonValue(QJsonValue::Null)},
                      {"fragmented", !indexedMode},
                      {"initializationReady", true},
                      {"state", "downloading"}};
    }
    document = {{"schemaVersion", 1},
                {"sessionId", QUuid::createUuid().toString(QUuid::Id128)},
                {"generation", 1},
                {"revision", 1},
                {"state", "downloading"},
                {"duration100ns", 200000000},
                {"tracks", tracks}};
    if (QString::fromLatin1(QTest::currentDataTag())
            .contains("derived-duration"))
      document.remove("duration100ns");
    publish(prefix(video, 12), prefix(audio, 5));
    startProvider();
  }
  void cleanup() {
    provider.terminate();
    if (!provider.waitForFinished(3000)) {
      provider.kill();
      provider.waitForFinished(3000);
    }
  }
  void confirmedPrefixWaitCancelAndRelease_data() { formats(); }
  void confirmedPrefixWaitCancelAndRelease() {
    auto source = ProgressiveSource::open(manifest);
    QTRY_VERIFY(source->status().connected);
    QVERIFY(source->status().available > 4 && source->status().available < 6);
    QCOMPARE(source->size(0), indexedMode ? int64_t(video.size()) : int64_t(0));
    char bytes[32];
    QCOMPARE(source->read(0, 0, bytes, sizeof(bytes)), int64_t(sizeof(bytes)));
    QCOMPARE(QByteArray(bytes, sizeof(bytes)), video.left(sizeof(bytes)));
    const uint64_t boundary = prefix(audio, 5);
    QVERIFY(!source->seekable(1, boundary + 1));
    auto pending = std::async(std::launch::async, [source, boundary] {
      char byte;
      return source->read(1, boundary, &byte, 1);
    });
    auto cancelOnExit = qScopeGuard([&] { source->cancel(); });
    QVERIFY(pending.wait_for(std::chrono::milliseconds(200)) ==
            std::future_status::timeout);
    source->cancel();
    QVERIFY(pending.wait_for(std::chrono::seconds(2)) ==
            std::future_status::ready);
    QCOMPARE(pending.get(), int64_t(-1));
    cancelOnExit.dismiss();
    source.reset();
    const QString leases = QDir(root).filePath("leases.json");
    QTRY_VERIFY([&] {
      QFile file(leases);
      if (!file.open(QIODevice::ReadOnly))
        return false;
      return QJsonDocument::fromJson(file.readAll())
          .object()["leases"]
          .toObject()
          .isEmpty();
    }());
  }
  void waitsForCompletionInsteadOfByteEnd_data() { formats(); }
  void waitsForCompletionInsteadOfByteEnd() {
    publish(video.size(), audio.size());
    auto source = ProgressiveSource::open(manifest);
    QTRY_VERIFY(source->status().connected);
    auto pending =
        std::async(std::launch::async, [source, length = audio.size()] {
          char byte;
          return source->read(1, uint64_t(length), &byte, 1);
        });
    auto cancelOnExit = qScopeGuard([&] { source->cancel(); });
    QVERIFY(pending.wait_for(std::chrono::milliseconds(200)) ==
            std::future_status::timeout);
    publish(video.size(), audio.size(), true);
    QTRY_VERIFY(source->status().complete);
    QVERIFY(pending.wait_for(std::chrono::seconds(2)) ==
            std::future_status::ready);
    QCOMPARE(pending.get(), int64_t(0));
  }
  void generationChangeCancelsRead() {
    auto source = ProgressiveSource::open(manifest);
    QTRY_VERIFY(source->status().connected);
    document["generation"] = 2;
    publish(prefix(video, 12), prefix(audio, 5));
    QTRY_VERIFY(!source->status().error.isEmpty());
    QVERIFY(source->status().error.contains("generation"));
    char byte;
    QCOMPARE(source->read(0, 0, &byte, 1), int64_t(-1));
  }
  void sharedMpvEngineDecodesBothTracks_data() { formats(true); }
  void sharedMpvEngineDecodesBothTracks() {
    publish(video.size(), audio.size(), true);
    auto source = ProgressiveSource::open(manifest);
    QTRY_VERIFY(source->status().connected);
    Player player(nullptr,
                  qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    player.resize(640, 360);
    player.show();
    QVERIFY(QTest::qWaitForWindowExposed(&player));
    player.set("mute", true);
    player.progressiveSource(source);
    QSignalSpy loaded(&player, &Player::loaded);
    QSignalSpy errors(&player, &Player::error);
    const QString url = "foxicmp-session://" + source->id();
    QVERIFY(player.loadSource(
        url + "/video",
        {{"audio-files-append", url + "/audio"}, {"rebase-start-time", "no"}}));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(player.get("time-pos").toDouble() > .3, 5000);
    QTRY_VERIFY(player.get("audio-codec-name")
                    .toString()
                    .contains("aac", Qt::CaseInsensitive));
    QVERIFY(player.get("video-codec").toString().contains("264"));
    QCOMPARE(errors.size(), 0);
    player.progressiveSource({});
    player.command({"stop"});
    QTRY_VERIFY(player.get("idle-active").toBool());
  }
  void windowBufferingSeekManualPauseAndClose_data() { formats(true); }
  void windowBufferingSeekManualPauseAndClose() {
    const QString profileRoot = temporary.filePath("profile");
    QVERIFY(QDir().mkpath(profileRoot));
    Profile profile(profileRoot);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("playback/pauseMinimized", true);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    connect(player, &Player::error, this, [](const QString &error) {
      qWarning() << "Player error:" << error;
    });
    connect(player, &Player::commandFinished, this, [](quint64 id, int error) {
      if (error < 0)
        qWarning() << "Command error:" << id << error;
    });
    QSignalSpy loaded(player, &Player::loaded);
    QVERIFY(window.openProgressiveSession(manifest));
    QTest::qWait(600);
    QCOMPARE(loaded.size(), 0);
    publish(prefix(video, 12), prefix(audio, 12));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(player->get("time-pos").toDouble() > .3, 5000);
    QTRY_VERIFY(player->get("audio-codec-name")
                    .toString()
                    .contains("aac", Qt::CaseInsensitive));
    QVERIFY(!window.seekTo(18));
    QVERIFY(!window.setAbLoop(1, 18, 2));
    int wraps = 0;
    double previous = 0;
    connect(player, &Player::property, &window,
            [&](const QString &name, const QVariant &value) {
              if (name == "time-pos" && value.isValid()) {
                const double now = value.toDouble();
                if (previous > 1.3 && now < .7)
                  ++wraps;
                previous = now;
              }
            });
    QVERIFY(window.setAbLoop(.5, 1.5, 2));
    QVERIFY(player->get("ab-loop-count").isValid());
    QTRY_VERIFY_WITH_TIMEOUT(player->get("time-pos").toDouble() > 2, 6000);
    QCOMPARE(wraps, 1);
    window.clearAbLoop();
    QVERIFY(!window.findChild<QAction *>("action_keyframeNext")->isEnabled());
    window.showMinimized();
    QTRY_VERIFY(player->get("pause").toBool());
    QTest::qWait(200);
    QVERIFY(player->get("pause").toBool());
    window.showNormal();
    QTRY_VERIFY(!player->get("pause").toBool());
    QTest::qWait(500);
    QVERIFY(window.seekTo(11));
    QTRY_VERIFY_WITH_TIMEOUT(player->get("pause").toBool(), 10000);
    const double before = player->get("time-pos").toDouble();
    int ticks = 0;
    QTimer responsiveness;
    connect(&responsiveness, &QTimer::timeout, this, [&] { ++ticks; });
    responsiveness.start(50);
    const int seconds =
        qEnvironmentVariableIntValue("FOXICMP_PROGRESSIVE_STALL_SECONDS");
    QTest::qWait(seconds > 0 ? seconds * 1000 : 600);
    QVERIFY(std::abs(player->get("time-pos").toDouble() - before) < .2);
    QCOMPARE(loaded.size(), 1);
    QVERIFY(ticks >= 5);
    window.findChild<QAction *>("action_play")->trigger();
    publish(video.size(), audio.size(), true);
    QTest::qWait(600);
    QVERIFY(player->get("pause").toBool());
    window.findChild<QAction *>("action_play")->trigger();
    QTRY_VERIFY(!player->get("pause").toBool());
    QTRY_VERIFY(player->get("time-pos").toDouble() > before + .2);
    QVERIFY(window.seekTo(2));
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 2) < .5);
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
  }
  void closeWhileInitialBufferingAndUnsupportedSourcePreservesQueue() {
    const QString profileRoot = temporary.filePath("buffer-profile");
    QVERIFY(QDir().mkpath(profileRoot));
    Profile profile(profileRoot);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    profile.settings.setValue("integration/prompted", true);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.addFiles({QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                         .filePath("Русский H264.mp4")});
    auto *queue = window.findChild<QListWidget *>("playlist");
    auto *player = window.findChild<Player *>();
    QCOMPARE(queue->count(), 1);
    auto malformed = document;
    auto tracks = malformed["tracks"].toObject();
    auto videoTrack = tracks["video"].toObject();
    videoTrack["fragmented"] = false;
    videoTrack["expectedTotalBytes"] = qint64(video.size());
    tracks["video"] = videoTrack;
    malformed["tracks"] = tracks;
    const QString unsupported = QDir(root).filePath("unsupported.json");
    QFile file(unsupported);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(malformed).toJson());
    file.close();
    QVERIFY(!window.openProgressiveSession(unsupported));
    QCOMPARE(queue->count(), 1);
    QVERIFY(window.openProgressiveSession(manifest));
    QTest::qWait(400);
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
    QCOMPARE(queue->count(), 1);
    const QString leases = QDir(root).filePath("leases.json");
    QTRY_VERIFY([&] {
      QFile file(leases);
      if (!file.open(QIODevice::ReadOnly))
        return false;
      return QJsonDocument::fromJson(file.readAll())
          .object()["leases"]
          .toObject()
          .isEmpty();
    }());
  }
  void realHttpDownloadProviderPlayerAndMerge_data() { formats(); }
  void realHttpDownloadProviderPlayerAndMerge() {
    const QString downloadRoot = temporary.filePath(
        "download-" + QUuid::createUuid().toString(QUuid::Id128));
    QVERIFY(QDir().mkpath(downloadRoot));
    QProcess server;
    const QString script =
        "import http.server,pathlib,sys,time,threading\n"
        "root=pathlib.Path(sys.argv[1]); log=pathlib.Path(sys.argv[2])\n"
        "barrier=threading.Barrier(2); lock=threading.Lock()\n"
        "class Handler(http.server.BaseHTTPRequestHandler):\n"
        " def do_GET(self):\n"
        "  with lock:\n"
        "   with log.open('a') as f: f.write(self.path+'\\n')\n"
        "  data=(root/self.path.lstrip('/')).read_bytes()\n"
        "  self.send_response(200); "
        "self.send_header('Content-Length',str(len(data))); "
        "self.end_headers()\n"
        "  barrier.wait(timeout=5)\n"
        "  for p in range(0,len(data),2560):\n"
        "   self.wfile.write(data[p:p+2560]); self.wfile.flush(); "
        "time.sleep(.1)\n"
        " def log_message(self,*args): pass\n"
        "server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)\n"
        "print(server.server_port,flush=True); server.serve_forever()\n";
    const QString requestsPath = QDir(downloadRoot).filePath("requests.txt");
    server.start("python3", {"-u", "-c", script,
                             QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                                 .filePath(indexedMode ? "progressive-indexed"
                                                       : "progressive"),
                             requestsPath});
    QVERIFY(server.waitForStarted(5000));
    QVERIFY(server.waitForReadyRead(5000));
    const int port = server.readLine().trimmed().toInt();
    QVERIFY(port > 0);
    auto stopServer = qScopeGuard([&] {
      server.kill();
      server.waitForFinished(3000);
    });
    const QString base = QString("http://127.0.0.1:%1").arg(port);
    QJsonArray formats{QJsonObject{{"format_id", "137"},
                                   {"url", base + "/video.mp4"},
                                   {"protocol", "http"},
                                   {"ext", "mp4"},
                                   {"vcodec", "avc1.64001e"},
                                   {"acodec", "none"},
                                   {"height", 180}},
                       QJsonObject{{"format_id", "140"},
                                   {"url", base + "/audio.m4a"},
                                   {"protocol", "http"},
                                   {"ext", "m4a"},
                                   {"vcodec", "none"},
                                   {"acodec", "mp4a.40.2"}}};
    QJsonObject info{{"id", "qt-local-pair"}, {"title", "Qt pair"},
                     {"extractor", "test"},   {"extractor_key", "Test"},
                     {"webpage_url", base},   {"duration", 20},
                     {"formats", formats}};
    const QString infoPath = QDir(downloadRoot).filePath("input.json");
    QFile input(infoPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(QJsonDocument(info).toJson());
    input.close();
    QProcess downloader;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("YTD_TEMP_DIR", downloadRoot);
    downloader.setProcessEnvironment(environment);
    const QString finished = QDir(downloadRoot).filePath("finished.mp4");
    downloader.start("python3",
                     {QStringLiteral(FOXICMP_ADAPTER_SCRIPT), "--ignore-config",
                      "--load-info-json", infoPath, "-f", "137+140", "--quiet",
                      "--no-progress", "-o", finished});
    QVERIFY(downloader.waitForStarted(5000));
    auto stopDownloader = qScopeGuard([&] {
      if (downloader.state() != QProcess::NotRunning) {
        downloader.kill();
        downloader.waitForFinished(3000);
      }
    });
    const QString profileRoot = QDir(downloadRoot).filePath("player");
    QVERIFY(QDir().mkpath(profileRoot));
    Profile profile(profileRoot);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QByteArray output;
    QString session;
    QString openError;
    bool opened = false, playedDuringDownload = false;
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 45000 &&
           downloader.state() != QProcess::NotRunning) {
      QTest::qWait(50);
      output += downloader.readAllStandardOutput();
      for (int newline; (newline = output.indexOf('\n')) >= 0;) {
        const auto line = output.left(newline);
        output.remove(0, newline + 1);
        if (!line.startsWith("[FoxicMP] "))
          continue;
        const auto event = QJsonDocument::fromJson(line.mid(10)).object();
        if (event["ready"].toBool() && !opened) {
          session = event["manifest"].toString();
          opened = window.openProgressiveSession(session);
          if (!opened)
            for (auto *label : window.findChildren<QLabel *>())
              if (label->text().startsWith("Прогрессивный источник:"))
                openError = label->text();
        }
      }
      if (opened && player->get("time-pos").toDouble() > .2) {
        QFile file(session);
        if (file.open(QIODevice::ReadOnly) &&
            QJsonDocument::fromJson(file.readAll()).object()["state"] !=
                "complete")
          playedDuringDownload = true;
      }
    }
    QVERIFY2(downloader.state() == QProcess::NotRunning,
             "Downloader did not finish");
    QCOMPARE(downloader.exitCode(), 0);
    QVERIFY2(opened,
             (openError.toUtf8() + "\n" + downloader.readAllStandardError())
                 .constData());
    QVERIFY(playedDuringDownload);
    QVERIFY(QFileInfo(finished).size() > 10000);
    QFile requests(requestsPath);
    QVERIFY(requests.open(QIODevice::ReadOnly));
    auto paths = requests.readAll().trimmed().split('\n');
    std::sort(paths.begin(), paths.end());
    QCOMPARE(paths, QList<QByteArray>({"/audio.m4a", "/video.mp4"}));
    QFile manifestFile(session);
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    const auto finalState =
        QJsonDocument::fromJson(manifestFile.readAll()).object();
    QCOMPARE(finalState["state"].toString(), QString("complete"));
    for (const auto &track : finalState["tracks"].toObject())
      QVERIFY(QFileInfo::exists(
          QDir(QFileInfo(session).absolutePath())
              .filePath(track.toObject()["path"].toString())));
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  Q_INIT_RESOURCE(resources);
  ProgressiveTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "progressive_test.moc"
