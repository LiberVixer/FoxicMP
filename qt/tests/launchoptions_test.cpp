#include "launchoptions.h"
#include "instancebroker.h"
#include "player.h"
#include "profile.h"
#include "window.h"
#include "webcontrol.h"
#include "playlistpanel.h"
#include <QTcpServer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QtDBus>
#include <QDBusMessage>
#include <sys/stat.h>
#include <QtTest>
#include <QtWidgets>
#include <cmath>

class LaunchOptionsTest : public QObject {
  Q_OBJECT
  QString media, subtitle, audio;
  QByteArray output;
  QByteArray errors;
  QByteArray read(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
  QString executable() { return qEnvironmentVariable("FOXICMP_TEST_EXECUTABLE",
      QDir(QCoreApplication::applicationDirPath()).filePath("foxicmp-qt")); }
  void prepare(Profile &p) {
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("input/mpris", false);
    p.settings.setValue("audio/mute", true);
    p.settings.setValue("ui/autoScale", 100);
    p.settings.setValue("playback/hardwareDecoding", false);
    p.settings.setValue("video/output", qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
  }
  int run(const QStringList &args) {
    QProcess process;
    process.start(executable(), args);
    QElapsedTimer time; time.start();
    while (process.state() != QProcess::NotRunning && time.elapsed() < 8000) {
      QCoreApplication::processEvents(); QTest::qWait(10);
    }
    if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(); return -1; }
    if (process.exitStatus() != QProcess::NormalExit) return -2;
    output = process.readAllStandardOutput();
    errors = process.readAllStandardError();
    return process.exitCode();
  }
private slots:
  void initTestCase() {
    const QDir dir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"));
    media = dir.filePath("chapters.mkv"); subtitle = dir.filePath("subtitle.srt");
    audio = dir.filePath("Русский H264.mp4");
    for (const auto &file : {media, subtitle, audio}) QVERIFY(QFileInfo(file).isFile());
  }
  void parsingAndStrictPayload() {
    QCommandLineParser p; LaunchOptions::addArguments(p);
    QVERIFY(p.parse({"player", "--start", "01:02:03.125", "--pause", "--volume", "0", "--rate", ".125", "--window-size", "800x450", "--window-position=-10,20", "--fullscreen"}));
    QJsonObject o; QString error;
    QVERIFY2(LaunchOptions::parse(p, &o, &error), qPrintable(error));
    QCOMPARE(o.value("start").toDouble(), 3723.125);
    QCOMPARE(o.value("x").toInt(), -10);
    LaunchRequest request; request.files = {media}; request.options = o;
    LaunchRequest decoded;
    QVERIFY(LaunchRequest::decode(request.encode(), &decoded));
    QCOMPARE(decoded.options, o);
    const QList<QJsonObject> bad{
      {{"start", -1}}, {{"start", "1"}}, {{"start", 604801}}, {{"pause", false}},
      {{"volume", 101}}, {{"rate", 0}}, {{"audio-track", 1.2}}, {{"subtitle-track", -1}},
      {{"width", 800}}, {{"width", 1}, {"height", 450}}, {{"x", 2}}, {{"x", 32768}, {"y", 0}},
      {{"mode", "hidden"}}, {{"command", "quit"}}, {{"command", "pause"}, {"rate", 1}},
      {{"subtitle-file", "relative.srt"}}, {{"audio-file", "/not-present-foxicmp.wav"}},
      {{"subtitle-track", 1}, {"subtitle-file", subtitle}}, {{"unknown", true}}, {{"rate", QJsonValue::Null}}, {{"shuffle", 1}}, {{"monitor", ""}}, {{"monitor", "bad\nname"}},
      {{"audio-device", "pulse/foxicmp_dual_private"}}, {{"panscan", "saved:"}}, {{"after", "shell-command"}},
      {{"web-port", 0}}, {{"web-port", 1023}}, {{"web-port", 65536}}, {{"web-port", 1234.5}}, {{"web-port", "13579"}},
      {{"audio-file", QJsonArray{}}}, {{"audio-file", QJsonArray{audio, 1}}},
      {{"subtitle-file", QJsonArray{subtitle, subtitle}}}, {{"audio-delay", .1}},
      {{"audio-file", audio}, {"audio-delay", "1"}}, {{"audio-file", audio}, {"audio-delay", 30.001}},
      {{"audio-file", audio}, {"audio-delay", -30.001}},
      {{"audio-track", 0}, {"audio-file", QJsonArray{audio}}}};
    for (const auto &invalid : bad) {
      request.options = invalid;
      QVERIFY2(!LaunchRequest::decode(request.encode(), &decoded), QJsonDocument(invalid).toJson().constData());
    }
    request.options = {{"pause", true}}; request.enqueue = true;
    QVERIFY(!LaunchRequest::decode(request.encode(), &decoded));
    request.enqueue = false; request.session = QFileInfo(media).absoluteFilePath(); request.files.clear();
    QVERIFY(!LaunchRequest::decode(request.encode(), &decoded));
    request.session.clear(); request.options = {{"command", "pause"}}; request.files = {media};
    QVERIFY(!LaunchRequest::decode(request.encode(), &decoded));
    QVERIFY(!p.parse({"player", "--unknown"}));
  }
  void invalidCliNeverCreatesProfile() {
    QTemporaryDir dir;
    const auto profile = dir.filePath("absent");
    const QList<QStringList> bad{
      {"--start", "nan"}, {"--start", "00:99:00"}, {"--rate", "17"},
      {"--volume", "101"}, {"--audio-track", "1.5"}, {"--subtitle-file", "absent.srt"},
      {"--fullscreen", "--normal"}, {"--window-size", "10x20"},
      {"--window-position", "1"}, {"--rate", "1", "--rate", "2"},
      {"--command", "pause"}, {"--pause", "--new-instance"},
      {"--enqueue", "--pause", media}, {"--command", "stop", media}, {"--shuffle", "--no-shuffle"},
      {"--after", "invalid"}, {"--panscan", "missing"}, {"--monitor", ""}, {"--audio-device", ""},
      {"--web-port", "0"}, {"--web-port", "1023"}, {"--web-port", "65536"}, {"--web-port", "nan"},
      {"--web-port", "13579", "--web-port", "13580"}, {"--reset-library"},
      {"--reset-settings", "--pause"}, {"--reset-settings", media}, {"--reset-settings", "--list-monitors"},
      {"--clipboard", media}, {"--clipboard", "--command", "pause"}};
    for (const auto &args : bad) {
      QCOMPARE(run(QStringList{"--profile", profile} + args), 1);
      QVERIFY(!QFileInfo::exists(profile));
    }
    QTemporaryDir existing;
    const auto ini = existing.filePath("FoxicMPQt.ini");
    QFile file(ini); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("[test]\nvalue=keep\n"); file.close();
    const auto before = QFileInfo(ini).size();
    QCOMPARE(run({"--profile", existing.path(), "--command", "pause"}), 1);
    QCOMPARE(QFileInfo(ini).size(), before);
    QVERIFY(!QFileInfo::exists(InstanceBroker::endpointFor(existing.path())));
  }
  void repeatedExternalFilesParserAndLimits() {
    QCommandLineParser p; LaunchOptions::addArguments(p);
    QVERIFY(p.parse({"foxicmp", "--audio-file", audio, "--audio-delay=-0.25", "--subtitle-file", subtitle,
        "--audio-file", media, "--audio-file", audio}));
    QJsonObject o; QString error;
    QVERIFY2(LaunchOptions::parse(p, &o, &error), qPrintable(error));
    QCOMPARE(LaunchOptions::externalFiles(o, "audio-file"), (QStringList{media, audio}));
    QCOMPARE(LaunchOptions::externalFiles(o, "subtitle-file"), QStringList{subtitle});
    QCOMPARE(o.value("audio-delay").toDouble(), -.25);
    QVERIFY(LaunchOptions::validate({{"audio-file", audio}, {"audio-delay", 30}}));
    QVERIFY(LaunchOptions::validate({{"audio-file", QJsonArray{audio}}, {"audio-delay", -30}}));
    QStringList tooMany{"foxicmp"};
    for (int i = 0; i < 33; ++i) tooMany << "--audio-file" << audio;
    QVERIFY(p.parse(tooMany)); QVERIFY(!LaunchOptions::parse(p, &o, &error));
    QTemporaryDir dir;
    const auto absent = dir.filePath("absent");
    const QList<QStringList> invalid{
      {"--audio-delay", ".1"}, {"--audio-file", audio, "--audio-delay", "nan"},
      {"--audio-file", audio, "--audio-delay", "30.1"},
      {"--audio-file", audio, "--audio-delay=-30.1"},
      {"--audio-file", audio, "--audio-delay", "0", "--audio-delay", "1"},
      {"--audio-file", audio, "--audio-track", "0"},
      {"--subtitle-file", subtitle, "--subtitle-track", "0"},
      {"--audio-file", audio, "--audio-file", "/missing-external-foxicmp"},
      {"--subtitle-file", subtitle, "--subtitle-file", ""},
      tooMany.mid(1)};
    for (const auto &args : invalid) {
      QCOMPARE(run(QStringList{"--profile", absent} + args + QStringList{media}), 1);
      QVERIFY(!QFileInfo::exists(absent));
    }
  }
  void multipleExternalTracksDelayAndReopen() {
    QTemporaryDir dir, external;
    const auto secondAudio = external.filePath("Русский, track '$`.mp4");
    const auto secondSub = external.filePath("Текст, subtitle '$`.srt");
    QVERIFY(QFile::copy(audio, secondAudio)); QVERIFY(QFile::copy(subtitle, secondSub));
    Profile profile(dir.path()); prepare(profile);
    profile.settings.setValue("subtitles/autoLoad", false);
    profile.settings.setValue("playlist/rememberTracks", true);
    Window w(profile); w.show(); auto *player = w.findChild<Player *>();
    // A previous manual delay must return after a temporary launch override.
    QVERIFY(player->set("audio-delay", .125));
    InstanceBroker owner(dir.path()); LaunchRequest initial;
    QCOMPARE(owner.start(initial, false), InstanceBroker::Primary);
    owner.setReceiver([&](const LaunchRequest &r) { w.handleLaunchRequest(r); });
    QSignalSpy loaded(player, &Player::loaded);
    const auto args = QStringList{"--profile", dir.path(), "--reuse-instance", "--pause", "--start", "2",
        "--audio-file", audio, "--audio-file", secondAudio, "--audio-delay=-0.25",
        "--subtitle-file", subtitle, "--subtitle-file", secondSub, media};
    QCOMPARE(run(args), 0); QTRY_COMPARE(loaded.size(), 1);
    const auto paths = [&](const QString &type) {
      QStringList result;
      for (const auto &entry : player->get("track-list").toList()) {
        const auto track = entry.toMap();
        if (track.value("type") == type && track.value("external").toBool())
          result << track.value("external-filename").toString();
      }
      return result;
    };
    const auto selected = [&](const QString &type) {
      const auto id = player->get(type == "audio" ? "aid" : "sid").toInt();
      for (const auto &entry : player->get("track-list").toList()) {
        const auto track = entry.toMap();
        if (track.value("type") == type && track.value("id").toInt() == id)
          return track.value("external-filename").toString();
      }
      return QString();
    };
    QTRY_VERIFY(paths("audio").contains(audio) && paths("audio").contains(secondAudio));
    QTRY_VERIFY(paths("sub").contains(subtitle) && paths("sub").contains(secondSub));
    QTRY_COMPARE(selected("audio"), secondAudio); QTRY_COMPARE(selected("sub"), secondSub);
    QTRY_VERIFY(player->get("pause").toBool());
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 2) < .15);
    QTRY_VERIFY(std::abs(player->get("audio-delay").toDouble() + .25) < .0001);
    QTRY_COMPARE(paths("audio").size(), 2); QTRY_COMPARE(paths("sub").size(), 2);
    // Reattaching already-loaded tracks must select without creating duplicates.
    const int audioCount = paths("audio").size(), subCount = paths("sub").size();
    QCOMPARE(run({"--profile", dir.path(), "--audio-file", secondAudio, "--audio-file", audio,
        "--subtitle-file", secondSub, "--subtitle-file", subtitle, "--audio-delay", ".4"}), 0);
    QTRY_COMPARE(selected("audio"), audio); QTRY_COMPARE(selected("sub"), subtitle);
    QCOMPARE(paths("audio").size(), audioCount); QCOMPARE(paths("sub").size(), subCount);
    QTRY_VERIFY(std::abs(player->get("audio-delay").toDouble() - .4) < .0001);
    QVERIFY(player->get("pause").toBool()); QCOMPARE(loaded.size(), 1);
    // Attachments use existing per-entry persistence; delay is never persisted.
    w.reopenCurrent(); QTRY_COMPARE(loaded.size(), 2);
    QTRY_COMPARE(selected("audio"), audio); QTRY_COMPARE(selected("sub"), subtitle);
    QTRY_COMPARE(paths("audio").size(), audioCount); QTRY_COMPARE(paths("sub").size(), subCount);
    QTRY_VERIFY(std::abs(player->get("audio-delay").toDouble() - .125) < .0001);
    // A new open of the same saved entry must not race its automatic restore
    // against explicit CLI attachment of the same files.
    const auto savedPlaylist = external.filePath("saved.m3u8");
    QVERIFY(w.savePlaylist(savedPlaylist));
    LaunchRequest again; again.activate = false; again.files = {savedPlaylist};
    again.options = {{"audio-file", QJsonArray{audio, secondAudio}},
        {"subtitle-file", QJsonArray{subtitle, secondSub}}, {"audio-delay", .2}, {"pause", true}};
    w.handleLaunchRequest(again); QTRY_COMPARE(loaded.size(), 3);
    QTRY_COMPARE(selected("audio"), secondAudio); QTRY_COMPARE(selected("sub"), secondSub);
    QTRY_COMPARE(paths("audio").size(), audioCount); QTRY_COMPARE(paths("sub").size(), subCount);
    QTRY_VERIFY(std::abs(player->get("audio-delay").toDouble() - .2) < .0001);
    w.openFiles({secondAudio}); QTRY_COMPARE(loaded.size(), 4);
    QTRY_VERIFY(paths("audio").isEmpty()); QTRY_VERIFY(paths("sub").isEmpty());
    QTRY_VERIFY(std::abs(player->get("audio-delay").toDouble() - .125) < .0001);
    for (const auto &key : profile.settings.allKeys()) QVERIFY(!key.contains("audio-delay"));
  }
  void repeatedPendingExternalTracks() {
    QTemporaryDir dir;
    Profile profile(dir.path()); prepare(profile);
    profile.settings.setValue("subtitles/autoLoad", false);
    Window w(profile); w.show(); auto *player = w.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    w.openFiles({media}); QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    // No Qt event-loop turn between requests: async adds can still be pending.
    for (int i = 0; i < 40; ++i) {
      QVERIFY(w.loadExternalAudio(audio));
      QVERIFY(w.loadExternalSubtitles(subtitle));
    }
    const auto count = [&](const QString &type) {
      int result = 0;
      for (const auto &v : player->get("track-list").toList()) {
        const auto t = v.toMap();
        if (t.value("type") == type && t.value("external").toBool()) ++result;
      }
      return result;
    };
    QTRY_COMPARE(count("audio"), 1); QTRY_COMPARE(count("sub"), 1);
    QTest::qWait(500);
    QCOMPARE(count("audio"), 1); QCOMPARE(count("sub"), 1);
    const auto copy = dir.filePath("another audio.mp4");
    QVERIFY(QFile::copy(audio, copy));
    const auto broken = dir.filePath("broken.wav");
    QFile invalid(broken); QVERIFY(invalid.open(QIODevice::WriteOnly));
    invalid.write("not media"); invalid.close();
    // A failed add must advance the queue, preserving the final selection.
    QVERIFY(w.loadExternalAudio(broken));
    QVERIFY(w.loadExternalAudio(copy));
    QVERIFY(w.loadExternalAudio(audio));
    const auto selectedAudio = [&] {
      for (const auto &v : player->get("track-list").toList()) {
        const auto t = v.toMap();
        if (t.value("type") == "audio" && t.value("selected").toBool())
          return t.value("external-filename").toString();
      }
      return QString();
    };
    QTRY_COMPARE(count("audio"), 2); QTRY_COMPARE(selectedAudio(), audio);
    // Cancel both queued and active requests before submitting a new source.
    const auto pendingAudio = dir.filePath("pending audio.mp4");
    const auto pendingSub = dir.filePath("pending subtitle.srt");
    QVERIFY(QFile::copy(audio, pendingAudio)); QVERIFY(QFile::copy(subtitle, pendingSub));
    QVERIFY(w.loadExternalAudio(pendingAudio));
    QVERIFY(w.loadExternalSubtitles(pendingSub));
    w.openFiles({audio}); QTRY_COMPARE(loaded.size(), 2);
    QTRY_COMPARE(count("audio"), 0); QTRY_COMPARE(count("sub"), 0);
    QTest::qWait(500);
    QCOMPARE(count("audio"), 0); QCOMPARE(count("sub"), 0);
  }
  void externalTracksWithoutRememberingAndFailedOpen() {
    QTemporaryDir dir;
    Profile profile(dir.path()); prepare(profile);
    profile.settings.setValue("playlist/rememberTracks", false);
    profile.settings.setValue("subtitles/autoLoad", false);
    Window w(profile); w.show(); auto *player = w.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    LaunchRequest request; request.activate = false; request.files = {media};
    // Legacy singular IPC fields remain supported.
    request.options = {{"audio-file", audio}, {"subtitle-file", subtitle}, {"audio-delay", .3}, {"pause", true}};
    w.handleLaunchRequest(request); QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(std::abs(player->get("audio-delay").toDouble() - .3) < .0001);
    QTRY_VERIFY(player->get("aid").toInt() > 0); QTRY_VERIFY(player->get("sid").toInt() > 0);
    w.reopenCurrent(); QTRY_COMPARE(loaded.size(), 2);
    const auto externalCount = [&] {
      int count = 0;
      for (const auto &v : player->get("track-list").toList()) if (v.toMap().value("external").toBool()) ++count;
      return count;
    };
    QTRY_COMPARE(externalCount(), 0);
    QTRY_VERIFY(std::abs(player->get("audio-delay").toDouble()) < .0001);
    // An empty directory never creates a new generation or consumes options.
    QTemporaryDir empty; request.files = {empty.path()};
    w.handleLaunchRequest(request);
    w.openFiles({media}); QTRY_COMPARE(loaded.size(), 3);
    QTRY_COMPARE(externalCount(), 0);
    QTRY_VERIFY(std::abs(player->get("audio-delay").toDouble()) < .0001);
    player->set("pause", true);
  }
  void clipboardSnapshotAndValidation() {
    QMimeData mime; QStringList files; QString error;
    mime.setUrls({QUrl::fromLocalFile(media), QUrl("https://example.test/фильм.mp4")});
    QVERIFY(LaunchOptions::clipboardFiles(&mime, &files, &error));
    QCOMPARE(files[0], media); QVERIFY(files[1].contains("%"));
    mime.clear(); mime.setText(media + "\r\n" + audio + "\n");
    QVERIFY(LaunchOptions::clipboardFiles(&mime, &files, &error));
    QCOMPARE(files, (QStringList{media, audio}));
    const auto accepted = files;
    for (const auto &bad : {QString(), media + "\n/no-such-foxicmp-clipboard-file", QString("javascript://bad"), QString(256 * 1024 + 1, 'x')}) {
      mime.setText(bad); QVERIFY(!LaunchOptions::clipboardFiles(&mime, &files, &error));
      QCOMPARE(files, accepted);
    }
    QStringList many; for (int i = 0; i < 257; ++i) many << media;
    mime.setText(many.join('\n'));
    QVERIFY(!LaunchOptions::clipboardFiles(&mime, &files, &error));
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    InstanceBroker owner(dir.path()); LaunchRequest initial;
    QCOMPARE(owner.start(initial, false), InstanceBroker::Primary);
    Window w(profile); w.show(); auto *player = w.findChild<Player *>();
    owner.setReceiver([&](const LaunchRequest &r) {
      if (!r.files.isEmpty()) QApplication::clipboard()->setText("/changed-after-snapshot");
      w.handleLaunchRequest(r);
    });
    auto *data = new QMimeData; data->setUrls({QUrl::fromLocalFile(media)});
    QApplication::clipboard()->setMimeData(data);
    QSignalSpy loaded(player, &Player::loaded);
    QVERIFY2(run({"--profile", dir.path(), "--reuse-instance", "--clipboard", "--pause", "--start", "2"}) == 0, errors.constData());
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(player->get("pause").toBool());
    QCOMPARE(player->get("path").toString(), media);
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 2) < .15);
    QTemporaryDir absent;
    QApplication::clipboard()->setText("/nonexistent-foxicmp-source");
    QCOMPARE(run({"--profile", absent.filePath("missing"), "--clipboard"}), 1);
    QVERIFY(!QFileInfo::exists(absent.filePath("missing")));
    QApplication::clipboard()->clear();
  }
  void temporaryWebPortRollbackAndPersistence() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    QTcpServer savedPort, nextPort, occupied;
    QVERIFY(savedPort.listen(QHostAddress::LocalHost));
    QVERIFY(nextPort.listen(QHostAddress::LocalHost));
    QVERIFY(occupied.listen(QHostAddress::LocalHost));
    profile.settings.setValue("web/enabled", true);
    profile.settings.setValue("web/port", savedPort.serverPort());
    const int wanted = nextPort.serverPort(); nextPort.close();
    // The saved port is deliberately busy: startup must bypass it entirely.
    Window w(profile, {}, {}, {}, wanted); w.show();
    QVERIFY(w.webControl->running()); QCOMPARE(w.webControl->port(), wanted);
    QVERIFY(w.diagnosticMessages.isEmpty());
    w.webControlDialog();
    auto *dialog = w.findChild<QDialog *>("webControlDialog"); QVERIFY(dialog);
    QCOMPARE(dialog->findChild<QSpinBox *>("webPort")->value(), wanted);
    QVERIFY(dialog->findChild<QLabel *>("webStatus")->text().contains("командой"));
    InstanceBroker owner(dir.path()); LaunchRequest initial;
    QCOMPARE(owner.start(initial, false), InstanceBroker::Primary);
    owner.setReceiver([&](const LaunchRequest &r) { w.handleLaunchRequest(r); });
    const auto token = w.webControl->token(); auto *original = w.webControl;
    QCOMPARE(run({"--profile", dir.path(), "--reuse-instance", "--web-port", QString::number(wanted)}), 0);
    QCOMPARE(w.webControl, original); QCOMPARE(w.webControl->token(), token);
    LaunchRequest bad; bad.files = {media}; bad.options = {{"web-port", occupied.serverPort()}, {"volume", 11}};
    auto *player = w.findChild<Player *>(); const auto volume = player->get("volume");
    QSignalSpy loaded(player, &Player::loaded); w.handleLaunchRequest(bad);
    QCOMPARE(w.webControl, original); QCOMPARE(w.webControl->token(), token);
    QCOMPARE(player->get("volume"), volume); QCOMPARE(loaded.size(), 0);
    QVERIFY(!w.diagnosticMessages.isEmpty());
    QTcpServer target; QVERIFY(target.listen(QHostAddress::LocalHost));
    const int changed = target.serverPort(); target.close();
    QCOMPARE(run({"--profile", dir.path(), "--reuse-instance", "--web-port", QString::number(changed)}), 0);
    QTRY_COMPARE(w.webControl->port(), changed);
    QVERIFY(w.webControl->token() != token);
    QCOMPARE(dialog->findChild<QSpinBox *>("webPort")->value(), changed);
    QCOMPARE(profile.settings.value("web/port").toInt(), int(savedPort.serverPort()));
    QVERIFY(profile.settings.value("web/enabled").toBool());
    // Neither temporary value replaced the configured port.
    dialog->close();
    QTemporaryDir disabledDir; Profile disabled(disabledDir.path()); prepare(disabled);
    Window disabledWindow(disabled);
    QVERIFY(!disabledWindow.webControl->running());
    LaunchRequest enable; enable.activate = false; enable.options = {{"web-port", wanted}};
    disabledWindow.handleLaunchRequest(enable);
    QVERIFY(disabledWindow.webControl->running());
    QCOMPARE(disabledWindow.webControl->port(), wanted);
    QVERIFY(!disabled.settings.contains("web/port"));
    QVERIFY(!disabled.settings.value("web/enabled").toBool());
  }
  void primaryCliTemporaryWebPortAndRestart() {
    QTemporaryDir dir;
    QTcpServer ports[2];
    for (auto &server : ports) QVERIFY(server.listen(QHostAddress::LocalHost));
    const int saved = ports[0].serverPort(), temporary = ports[1].serverPort();
    ports[1].close();
    {
      Profile p(dir.path()); prepare(p); p.settings.setValue("input/mpris", true);
      p.settings.setValue("web/enabled", true); p.settings.setValue("web/port", saved);
      p.settings.sync();
    }
    QProcess child;
    child.start(executable(), {"--profile", dir.path(), "--web-port", QString::number(temporary), "--pause", media});
    QVERIFY(child.waitForStarted());
    QNetworkAccessManager manager;
    const auto page = [&] {
      auto *reply = manager.get(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/").arg(temporary))));
      QElapsedTimer timer; timer.start();
      while (!reply->isFinished() && timer.elapsed() < 1000) QTest::qWait(10);
      const bool ok = reply->isFinished() && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
      reply->abort(); reply->deleteLater(); return ok;
    };
    QTRY_VERIFY_WITH_TIMEOUT(page(), 8000);
    const auto quit = QDBusMessage::createMethodCall("org.mpris.MediaPlayer2.FoxicMP", "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2", "Quit");
    QDBusConnection::sessionBus().asyncCall(quit);
    QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5000);
    QCOMPARE(child.exitCode(), 0);
    QSettings kept(dir.filePath("FoxicMPQt.ini"), QSettings::IniFormat);
    QCOMPARE(kept.value("web/port").toInt(), saved); QVERIFY(kept.value("web/enabled").toBool());
    ports[0].close();
    Profile restored(dir.path()); Window next(restored);
    QVERIFY(next.webControl->running()); QCOMPARE(next.webControl->port(), saved);
    QVERIFY(!next.property("foxicmpTemporaryWebPort").toBool());
  }
  void resetCliPreservesLibraryAndExactBackup() {
    QTemporaryDir dir;
    {
      Profile p(dir.path()); prepare(p); p.settings.setValue("audio/volume", 17);
      p.settings.setValue("web/enabled", true);
      p.settings.setValue("integration/defaultsBackupV1", QByteArray("undo"));
      p.recordRecent(media); p.addFavorite("Кино", media, 2); p.savePosition(media, 2, 100);
      p.settings.sync();
    }
    const auto ini = dir.filePath("FoxicMPQt.ini");
    const auto before = read(ini);
    QCOMPARE(run({"--profile", dir.path(), "--reset-settings"}), 0);
    const auto result = QJsonDocument::fromJson(output).object();
    QVERIFY(result.value("reset").toBool()); QVERIFY(result.value("keptLibrary").toBool());
    QCOMPARE(read(result.value("backup").toString()), before);
    {
      QSettings settings(ini, QSettings::IniFormat);
      QCOMPARE(settings.value("audio/volume").toInt(), 75);
      QVERIFY(!settings.value("web/enabled").toBool());
      QVERIFY(!settings.value("integration/checkOnStartup").toBool());
      QCOMPARE(settings.value("history/files").toStringList(), QStringList{media});
      QVERIFY(settings.contains("favorites/entries"));
      QCOMPARE(settings.value("integration/defaultsBackupV1").toByteArray(), QByteArray("undo"));
    }
    QCOMPARE(run({"--profile", dir.path(), "--reset-settings", "--reset-library"}), 0);
    QVERIFY(!QJsonDocument::fromJson(output).object().value("keptLibrary").toBool());
    QSettings cleared(ini, QSettings::IniFormat);
    QVERIFY(!cleared.contains("history/files")); QVERIFY(!cleared.contains("favorites/entries"));
    QCOMPARE(QDir(dir.filePath("Backups")).entryList({"before-reset-*.ini"}, QDir::Files).size(), 2);
    QTemporaryDir legacy;
    const QByteArray raw = "; preserve this comment in the backup\n[audio]\nvolume=11\n";
    QFile legacyIni(legacy.filePath("FoxicMPQt.ini"));
    QVERIFY(legacyIni.open(QIODevice::WriteOnly)); legacyIni.write(raw); legacyIni.close();
    QCOMPARE(run({"--profile", legacy.path(), "--reset-settings"}), 0);
    QCOMPARE(read(QJsonDocument::fromJson(output).object().value("backup").toString()), raw);
  }
  void resetCliRefusesRunningAndUnsafeProfiles() {
    QTemporaryDir dir, links;
    {
      Profile p(dir.path()); p.settings.setValue("audio/volume", 19); p.settings.sync();
      const auto before = read(p.settings.fileName());
      QVERIFY(QFile::link(dir.path(), links.filePath("alias")));
      for (const auto &path : {dir.path(), links.filePath("alias")})
        QCOMPARE(run({"--profile", path, "--reset-settings"}), 1);
      QCOMPARE(read(p.settings.fileName()), before);
      QVERIFY(!QFileInfo::exists(dir.filePath("Backups")));
    }
    for (const auto &kind : {"absent", "missing-ini", "ini-link", "fifo", "malformed", "backup-file"}) {
      QTemporaryDir bad, outside;
      const auto profile = bad.filePath("profile"), ini = profile + "/FoxicMPQt.ini";
      if (QString(kind) != "absent") QVERIFY(QDir().mkpath(profile));
      const QByteArray original = "[profile]\nversion=1\n[audio]\nvolume=19\n";
      if (QString(kind) == "ini-link") {
        QFile f(outside.filePath("target")); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(original); f.close();
        QVERIFY(QFile::link(f.fileName(), ini));
      } else if (QString(kind) == "fifo") {
        QCOMPARE(::mkfifo(QFile::encodeName(ini).constData(), 0600), 0);
      } else if (QString(kind) == "malformed" || QString(kind) == "backup-file") {
        QFile f(ini); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QString(kind) == "malformed" ? QByteArray("[broken\nvalue=keep\n") : original); f.close();
        if (QString(kind) == "backup-file") { QFile backup(profile + "/Backups"); QVERIFY(backup.open(QIODevice::WriteOnly)); backup.write("keep"); }
      }
      const auto before = QString(kind) == "fifo" ? QByteArray() : read(ini);
      QCOMPARE(run({"--profile", profile, "--reset-settings"}), 1);
      if (QString(kind) != "fifo") QCOMPARE(read(ini), before);
      if (QString(kind) == "absent") QVERIFY(!QFileInfo::exists(profile));
    }
  }
  void portableResetBypassesBrokenStartupLanguage() {
    QTemporaryDir program;
    const auto copy = program.filePath("foxicmp-qt");
    QVERIFY(QFile::copy(executable(), copy));
    QVERIFY(QFile::setPermissions(copy, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    const auto ini = program.filePath("FoxicMPQt.ini");
    QCOMPARE(::mkfifo(QFile::encodeName(ini).constData(), 0600), 0);
    QProcess reset; reset.start(copy, {"--portable", "--reset-settings"});
    QVERIFY(reset.waitForFinished(4000)); QCOMPARE(reset.exitCode(), 1);
    QVERIFY(!QFileInfo::exists(program.filePath("Backups")));
    QVERIFY(QFile::remove(ini));
    const auto language = program.filePath("language.json");
    QCOMPARE(::mkfifo(QFile::encodeName(language).constData(), 0600), 0);
    const auto raw = QByteArray("[appearance]\nlanguage=custom\nlanguageFile=") + language.toUtf8() + "\n";
    QFile file(ini); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(raw); file.close();
    reset.start(copy, {"--portable", "--reset-settings"});
    QVERIFY(reset.waitForFinished(4000)); QCOMPARE(reset.exitCode(), 0);
    const auto result = QJsonDocument::fromJson(reset.readAllStandardOutput()).object();
    QVERIFY(result.value("reset").toBool());
    QCOMPARE(read(result.value("backup").toString()), raw);
  }
  void loadedOptionsAndNoLeakToNextFile() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    Window w(profile); w.show(); auto *player = w.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    LaunchRequest request;
    request.files = {media}; request.activate = false;
    request.options = {{"start", 2.25}, {"pause", true}, {"volume", 23}, {"rate", 1.5},
        {"audio-track", 0}, {"subtitle-file", subtitle}, {"width", 810}, {"height", 510}, {"x", 30}, {"y", 40}};
    w.handleLaunchRequest(request);
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(player->get("pause").toBool());
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 2.25) < .15);
    QTRY_COMPARE(player->get("volume").toInt(), 23);
    QTRY_COMPARE(player->get("speed").toDouble(), 1.5);
    QTRY_VERIFY(!player->get("aid").toBool());
    QTRY_VERIFY(player->get("sid").toInt() > 0);
    QTRY_COMPARE(w.size(), QSize(810,510));
    QTRY_COMPARE(w.QWidget::pos(), QPoint(30,40));
    // Explicit zero overrides a remembered position, without a sticky CLI flag.
    request.options = {{"pause", true}, {"start", 0}, {"audio-file", audio}, {"subtitle-track", 0}};
    w.handleLaunchRequest(request);
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(player->get("pause").toBool());
    QTRY_VERIFY(player->get("time-pos").toDouble() < .15);
    QTRY_VERIFY(!player->get("sid").toBool());
    QTRY_VERIFY(player->get("aid").toInt() > 0);
    w.openFiles({media});
    QTRY_COMPARE(loaded.size(), 3);
    QTRY_VERIFY(!player->get("pause").toBool());
    QTRY_COMPARE(player->get("speed").toDouble(), 1.);
    player->set("pause", true);
  }
  void realCliForwardsAndControlsCurrentWindow() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    InstanceBroker owner(dir.path()); LaunchRequest initial;
    QCOMPARE(owner.start(initial, false), InstanceBroker::Primary);
    Window w(profile); w.show(); auto *player = w.findChild<Player *>();
    owner.setReceiver([&](const LaunchRequest &r) { w.handleLaunchRequest(r); });
    QSignalSpy loaded(player, &Player::loaded);
    QCOMPARE(run({"--profile", dir.path(), "--reuse-instance", "--pause", "--start", "00:02.5", "--volume", "31", "--rate", "1.5", "--fullscreen", media}), 0);
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(w.isFullScreen());
    QTRY_VERIFY(player->get("pause").toBool());
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 2.5) < .15);
    QTRY_COMPARE(player->get("volume").toInt(), 31);
    QCOMPARE(run({"--profile", dir.path(), "--command", "play"}), 0);
    QTRY_VERIFY(!player->get("pause").toBool());
    QCOMPARE(run({"--profile", dir.path(), "--command", "pause"}), 0);
    QTRY_VERIFY(player->get("pause").toBool());
    const bool muted = player->get("mute").toBool();
    QCOMPARE(run({"--profile", dir.path(), "--command", "mute"}), 0);
    QTRY_COMPARE(player->get("mute").toBool(), !muted);
    player->set("mute", true);
    QCOMPARE(run({"--profile", dir.path(), "--start", "1", "--rate", "2", "--volume", "0", "--normal", "--window-size", "800x500"}), 0);
    QTRY_VERIFY(!w.isFullScreen());
    QTRY_COMPARE(w.size(), QSize(800,500));
    QTRY_COMPARE(player->get("volume").toInt(), 0);
    QTRY_COMPARE(player->get("speed").toDouble(), 2.);
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 1) < .15);
    QCOMPARE(run({"--profile", dir.path(), "--enqueue", audio}), 0);
    QCOMPARE(run({"--profile", dir.path(), "--command", "next"}), 0);
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_COMPARE(player->get("speed").toDouble(), 1.);
    QCOMPARE(run({"--profile", dir.path(), "--command", "previous"}), 0);
    QTRY_COMPARE(loaded.size(), 3);
    // ACK means delivered; unavailable commands still use the player's guards.
    QCOMPARE(run({"--profile", dir.path(), "--command", "stop"}), 0);
    QTRY_VERIFY(player->get("idle-active").toBool());
    QCOMPARE(loaded.size(), 3);
  }
  void discoveryQueriesNeverCreateProfile() {
    QTemporaryDir dir;
    const auto absent = dir.filePath("missing");
    QProcess process;
    process.start(executable(), {"--profile", absent, "--list-monitors", "--list-audio-devices"});
    QVERIFY(process.waitForFinished(8000));
    QCOMPARE(process.exitCode(), 0);
    const auto result = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    QVERIFY(!result.value("monitors").toArray().isEmpty());
    QCOMPARE(result.value("monitors").toArray()[0].toObject().value("index").toInt(), 1);
    const auto devices = result.value("audioDevices").toArray();
    bool automatic = false;
    for (const auto &v : devices) if (v.toObject().value("name") == "auto") automatic = true;
    QVERIFY(automatic);
    QVERIFY(!QFileInfo::exists(absent));
    QCOMPARE(run({"--profile", absent, "--list-monitors", "--volume", "50"}), 1);
    QCOMPARE(run({"--profile", absent, "--list-audio-devices", media}), 1);
    QVERIFY(!QFileInfo::exists(absent));
  }
  void namedResourcesAndForwardedSettings() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    profile.settings.setValue("transform/remember", true);
    profile.settings.setValue("transform/presetsV1", QJsonDocument(QJsonArray{
      QJsonObject{{"name", "Кино"}, {"state", QJsonObject{{"width", 1.3}}}}}).toJson(QJsonDocument::Compact));
    InstanceBroker owner(dir.path()); LaunchRequest initial;
    QCOMPARE(owner.start(initial, false), InstanceBroker::Primary);
    QStringList systemCalls;
    Window w(profile, [&](const QString &action) { systemCalls << action; });
    w.show(); auto *player = w.findChild<Player *>();
    owner.setReceiver([&](const LaunchRequest &r) { w.handleLaunchRequest(r); });
    QSignalSpy loaded(player, &Player::loaded);
    QCOMPARE(run({"--profile", dir.path(), "--reuse-instance", "--panscan", "saved:Кино", "--shuffle", "--audio-device", "auto", "--after", "shutdown", "--monitor", "1", "--fullscreen", "--pause", media}), 0);
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(std::abs(player->get("video-scale-x").toDouble() - 1.3) < .0001);
    QTRY_VERIFY(w.isFullScreen());
    QCOMPARE(w.screen(), QGuiApplication::screens()[0]);
    QCOMPARE(w.property("foxicmpLaunchScreen").toString(), w.screen()->name());
    QVERIFY(profile.settings.value("playlist/shuffle").toBool());
    QCOMPARE(profile.settings.value("audio/device").toString(), QString("auto"));
    QVERIFY(!profile.settings.contains("playback/after"));
    QVERIFY(w.findChild<QAction *>("afterOnce_shutdown")->isChecked());
    const auto saved = profile.settings.value("transform/stateV1").toByteArray();
    QVERIFY(!saved.isEmpty());
    const auto sink = qEnvironmentVariable("PULSE_SINK");
    if (!sink.isEmpty()) {
      const auto device = "pulse/" + sink;
      QCOMPARE(run({"--profile", dir.path(), "--audio-device", device}), 0);
      QTRY_COMPARE(profile.settings.value("audio/device").toString(), device);
      QTRY_COMPARE(player->get("audio-device").toString(), device);
    }
    const double beforeVolume = player->get("volume").toDouble();
    for (const auto &bad : QList<QJsonObject>{{{"monitor", "not-a-real-monitor-foxicmp"}}, {{"audio-device", "pulse/not-a-real-device-foxicmp"}}, {{"panscan", "saved:Не существует"}}}) {
      LaunchRequest r; r.files = {audio}; r.options = bad; r.options["volume"] = 12;
      const auto errors = w.diagnosticMessages.size(); w.handleLaunchRequest(r);
      QCOMPARE(w.diagnosticMessages.size(), errors + 1);
      QCOMPARE(loaded.size(), 1); QCOMPARE(player->get("volume").toDouble(), beforeVolume);
      QCOMPARE(profile.settings.value("transform/stateV1").toByteArray(), saved);
    }
    QCOMPARE(run({"--profile", dir.path(), "--no-shuffle", "--panscan", "fit", "--monitor", "primary", "--normal"}), 0);
    QTRY_COMPARE(player->get("video-scale-x").toDouble(), 1.);
    QVERIFY(!profile.settings.value("playlist/shuffle").toBool());
    QCOMPARE(run({"--profile", dir.path(), "--after", "none"}), 0);
    QVERIFY(w.findChild<QAction *>("afterOnce_none")->isChecked());
    QVERIFY(systemCalls.isEmpty());
  }
  void afterNoneAndSystemActionsAreOneShot() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    profile.settings.setValue("playback/after", "close");
    QStringList systemCalls;
    Window w(profile, [&](const QString &action) { systemCalls << action; });
    w.show(); auto *player = w.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded), ended(player, &Player::ended);
    LaunchRequest r; r.files = {media}; r.options = {{"pause", true}, {"after", "none"}};
    w.handleLaunchRequest(r); QTRY_COMPARE(loaded.size(), 1);
    player->command({"seek", "7.7", "absolute+exact"}); player->set("pause", false);
    QTRY_COMPARE(ended.size(), 1);
    QCOMPARE(profile.settings.value("files/currentIndex").toInt(), 0);
    QCOMPARE(profile.settings.value("playback/after").toString(), QString("close"));
    w.openFiles({media}); QTRY_COMPARE(loaded.size(), 2);
    player->command({"seek", "7.7", "absolute+exact"}); player->set("pause", false);
    QTRY_COMPARE(profile.settings.value("files/currentIndex").toInt(), -1);
    r.options = {{"pause", true}, {"after", "shutdown"}};
    w.handleLaunchRequest(r); QTRY_COMPARE(loaded.size(), 3);
    w.findChild<QAction *>("action_stop")->trigger();
    QVERIFY(systemCalls.isEmpty());
    QVERIFY(w.findChild<QAction *>("afterOnce_shutdown")->isChecked());
    w.openFiles({media}); QTRY_COMPARE(loaded.size(), 4);
    player->command({"seek", "7.7", "absolute+exact"}); player->set("pause", false);
    QTRY_COMPARE(systemCalls, QStringList{"shutdown"});
    QVERIFY(w.findChild<QAction *>("afterOnce_none")->isChecked());
    w.openFiles({media}); QTRY_COMPARE(loaded.size(), 5);
    player->command({"seek", "7.7", "absolute+exact"}); player->set("pause", false);
    QTRY_COMPARE(profile.settings.value("files/currentIndex").toInt(), -1);
    QCOMPARE(systemCalls.size(), 1);
  }
  void windowModesAndFailedOpenDoNotLeak() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    Window w(profile); w.show(); auto *player = w.findChild<Player *>();
    LaunchRequest r; r.options = {{"mode", "minimized"}}; r.activate = false;
    w.handleLaunchRequest(r); QTRY_VERIFY(w.isMinimized());
    r.options = {{"mode", "normal"}};
    w.handleLaunchRequest(r); QTRY_VERIFY(!w.isMinimized());
    r.files = {dir.path()}; r.options = {{"pause", true}, {"rate", 3}};
    w.handleLaunchRequest(r); // Empty directory doesn't open a generation.
    QSignalSpy loaded(player, &Player::loaded);
    w.openFiles({media}); QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(!player->get("pause").toBool());
    QTRY_COMPARE(player->get("speed").toDouble(), 1.);
    player->set("pause", true);
    r.files = {media}; r.options = {{"pause", true}, {"mode", "minimized"}};
    w.handleLaunchRequest(r); QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(w.isMinimized());
    QTRY_VERIFY(player->get("pause").toBool());
  }
};
int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv); Q_INIT_RESOURCE(resources);
  LaunchOptionsTest test; return QTest::qExec(&test, argc, argv);
}
#include "launchoptions_test.moc"
