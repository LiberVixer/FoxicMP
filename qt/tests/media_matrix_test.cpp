#include "player.h"
#include <QtTest>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

class MediaMatrixTest : public QObject {
  Q_OBJECT
  QString directory;
  QJsonObject manifest, current;
  QJsonArray results;
  static bool colored(const QImage &image) {
    if (image.isNull())
      return false;
    int count = 0;
    for (int y = 0; y < image.height(); y += 12)
      for (int x = 0; x < image.width(); x += 12) {
        const auto c = image.pixelColor(x, y);
        if (std::max({c.red(), c.green(), c.blue()}) -
                std::min({c.red(), c.green(), c.blue()}) > 35)
          ++count;
      }
    return count > 20;
  }
private slots:
  void initTestCase() {
    directory = qEnvironmentVariable("FOXICMP_MATRIX_DIR");
    QVERIFY2(!directory.isEmpty(), "Set FOXICMP_MATRIX_DIR to the generated corpus");
    QFile file(directory + "/manifest.json");
    QVERIFY(file.open(QIODevice::ReadOnly));
    manifest = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(manifest.value("schema").toInt(), 1);
    QVERIFY(!manifest.value("cases").toArray().isEmpty());
  }
  void compatibility_data() {
    QTest::addColumn<QJsonObject>("fixture");
    for (const auto &value : manifest.value("cases").toArray()) {
      auto object = value.toObject();
      QTest::newRow(qPrintable(object.value("file").toString())) << object;
    }
  }
  void compatibility() {
    QFETCH(QJsonObject, fixture);
    current = {{"file", fixture.value("file")}, {"expected", fixture.value("expected")}};
    const auto path = directory + "/" + fixture.value("file").toString();
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()),
             fixture.value("sha256").toString());
    QStringList diagnostics;
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    connect(&p, &Player::diagnosticMessage, this, [&](const QString &text) { diagnostics << text.left(512); });
    p.resize(480, 270);
    p.show();
    QTRY_VERIFY_WITH_TIMEOUT(p.ready(), 10000);
    current["renderer"] = p.outputName();
    QVERIFY(p.set("hwdec", "no"));
    QVERIFY(p.set("mute", true));
    QSignalSpy loaded(&p, &Player::loaded), failed(&p, &Player::fileFailed), ended(&p, &Player::ended);
    QVERIFY(p.loadSource(path, {{"pause", "yes"}}));
    if (fixture.value("expected") == "reject") {
      QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 10000);
      QCOMPARE(loaded.size(), 0);
      QVERIFY(!ended.isEmpty());
      QVERIFY(!ended.last().first().toBool());
      current["rejected"] = true;
      // Reuse the same libmpv instance after a failed demux/open.
      QVERIFY(p.loadSource(directory + "/Русский H264.mp4", {{"pause", "no"}}));
      QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
      current["fileFormat"] = p.get("file-format").toString();
      current["seekPreroll"] = p.get("file-local-options/hr-seek-demuxer-offset").toDouble();
      QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > .2, 10000);
      QTRY_VERIFY_WITH_TIMEOUT(colored(p.grabFramebuffer()), 10000);
      QCOMPARE(failed.size(), 1);
      current["recovered"] = true;
    } else {
      QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
      current["fileFormat"] = p.get("file-format").toString();
      current["seekPreroll"] = p.get("file-local-options/hr-seek-demuxer-offset").toDouble();
      const auto tracks = p.get("track-list").toList();
      QList<int> audio, subtitles;
      QMap<int, QString> audioCodecs;
      int videos = 0;
      for (const auto &value : tracks) {
        const auto track = value.toMap();
        if (track.value("type") == "video") ++videos;
        if (track.value("type") == "audio") {
          audio << track.value("id").toInt();
          audioCodecs.insert(track.value("id").toInt(), track.value("codec").toString());
        }
        if (track.value("type") == "sub") subtitles << track.value("id").toInt();
      }
      QCOMPARE(videos, fixture.value("video").toInt());
      QCOMPARE(audio.size(), fixture.value("audio").toInt());
      QCOMPARE(subtitles.size(), fixture.value("subtitles").toInt());
      if (fixture.contains("chapters"))
        QCOMPARE(p.get("chapter-list").toList().size(), fixture.value("chapters").toInt());
      QVERIFY(p.set("pause", false));
      QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > .2, 10000);
      if (videos) {
        QTRY_VERIFY_WITH_TIMEOUT(colored(p.grabFramebuffer()), 10000);
        QVERIFY(!p.get("video-codec").toString().isEmpty());
      }
      if (!audio.isEmpty()) {
        QTRY_VERIFY_WITH_TIMEOUT(!p.get("audio-params").toMap().isEmpty(), 10000);
        QVERIFY(p.get("audio-params").toMap().value("samplerate").toInt() > 0);
        QVERIFY(!p.get("audio-codec-name").toString().isEmpty());
      }
      current["videoCodec"] = p.get("video-codec").toString();
      current["audioCodec"] = p.get("audio-codec-name").toString();
      current["audioParams"] = QJsonObject::fromVariantMap(p.get("audio-params").toMap());
      current["tracks"] = QJsonArray::fromVariantList(tracks);
      QVERIFY(p.set("pause", true));
      QTRY_VERIFY(p.get("pause").toBool());
      QTest::qWait(100);
      for (int id : audio) {
        QVERIFY(p.set("aid", id));
        QTRY_COMPARE(p.get("aid").toInt(), id);
        QTRY_VERIFY(!p.get("audio-params").toMap().isEmpty());
        QTRY_COMPARE(p.get("audio-codec-name").toString(), audioCodecs.value(id));
      }
      for (int id : subtitles) {
        QVERIFY(p.set("sid", id));
        QTRY_COMPARE(p.get("sid").toInt(), id);
      }
      const double duration = p.get("duration").toDouble();
      QVERIFY2(duration > 2.5 && duration < 4.5, qPrintable(QString::number(duration)));
      QVERIFY(p.command({"seek", "1.25", "absolute+exact"}));
      QElapsedTimer seekTimer;
      seekTimer.start();
      while (seekTimer.elapsed() < 5000 &&
             (p.get("seeking").toBool() ||
              std::abs(p.get("time-pos").toDouble() - 1.25) >= .15))
        QTest::qWait(25);
      current["seekActual"] = p.get("time-pos").toDouble();
      current["seekPending"] = p.get("seeking").toBool();
      current["seekable"] = p.get("seekable").toBool();
      current["duration"] = duration;
      current["diagnostics"] = QJsonArray::fromStringList(diagnostics);
      QVERIFY2(!p.get("seeking").toBool() &&
                   std::abs(p.get("time-pos").toDouble() - 1.25) < .15,
               qPrintable(QString::fromUtf8(QJsonDocument(current).toJson(QJsonDocument::Compact))));
      const double pausedAt = p.get("time-pos").toDouble();
      QTest::qWait(120);
      QVERIFY(std::abs(p.get("time-pos").toDouble() - pausedAt) < .05);
      current["pauseStable"] = true;
      QVERIFY(p.set("speed", 4.));
      QVERIFY(p.set("pause", false));
      QTRY_VERIFY_WITH_TIMEOUT(!ended.isEmpty(), 10000);
      QVERIFY(ended.last().first().toBool());
      QCOMPARE(failed.size(), 0);
      current["naturalEnd"] = true;
    }
    current["diagnostics"] = QJsonArray::fromStringList(diagnostics);
  }
  void mpegPrerollResetsForNextSource() {
    Player p;
    p.resize(480, 270);
    p.show();
    QTRY_VERIFY(p.ready());
    p.set("hwdec", "no");
    p.set("mute", true);
    QSignalSpy loaded(&p, &Player::loaded);
    for (const auto &name : {"h264-ac3.ts", "Русский H264.mp4", "mpeg2.mpg", "multi-track.mkv"}) {
      loaded.clear();
      QVERIFY(p.loadSource(directory + "/" + QString::fromUtf8(name), {{"pause", "yes"}}));
      QTRY_COMPARE(loaded.size(), 1);
      const auto format = p.get("file-format").toString();
      QCOMPARE(p.get("file-local-options/hr-seek-demuxer-offset").toDouble(),
               format == "mpeg" || format == "mpegts" ? 1.0 : 0.0);
    }
  }
  void realSource() {
    const auto path = qEnvironmentVariable("FOXICMP_REAL_MEDIA");
    if (path.isEmpty()) QSKIP("Optional user-selected local source");
    const QFileInfo source(path);
    QVERIFY(source.isFile());
    const auto originalSize = source.size();
    const auto originalModified = source.lastModified();
    current = {{"file", source.fileName()}, {"bytes", originalSize},
               {"expected", "real-source"}};
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(960, 540);
    p.show();
    QTRY_VERIFY_WITH_TIMEOUT(p.ready(), 10000);
    QVERIFY(p.set("hwdec", "no"));
    QVERIFY(p.set("mute", true));
    QSignalSpy loaded(&p, &Player::loaded), errors(&p, &Player::error);
    QVERIFY(p.loadSource(path, {{"pause", "no"}, {"start", "30"}}));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > 30.2, 15000);
    const double duration = p.get("duration").toDouble();
    QVERIFY(duration > 120);
    current["duration"] = duration;
    current["renderer"] = p.outputName();
    current["videoCodec"] = p.get("video-codec").toString();
    current["tracks"] = QJsonArray::fromVariantList(p.get("track-list").toList());
    // Real files can include an attached cover; the default must remain the
    // moving video track rather than the first image-looking stream.
    bool movingVideo = false;
    QList<int> audio;
    for (const auto &value : p.get("track-list").toList()) {
      const auto track = value.toMap();
      if (track.value("type") == "video" && track.value("selected").toBool())
        movingVideo = !track.value("albumart").toBool() && !track.value("image").toBool();
      if (track.value("type") == "audio") audio << track.value("id").toInt();
    }
    QVERIFY(movingVideo);
    QVERIFY(!audio.isEmpty());
    QTRY_VERIFY(!p.get("audio-params").toMap().isEmpty());
    current["audioParams"] = QJsonObject::fromVariantMap(p.get("audio-params").toMap());
    current["audioCodec"] = p.get("audio-codec-name").toString();
    QVERIFY(p.set("pause", true));
    QJsonArray seeks;
    QSet<QByteArray> frames;
    for (double target : {60., 600., duration / 2, duration - 15}) {
      QVERIFY(p.command({"seek", QString::number(target, 'f', 3), "absolute+exact"}));
      QTRY_VERIFY_WITH_TIMEOUT(!p.get("seeking").toBool() &&
                              std::abs(p.get("time-pos").toDouble() - target) < .15, 15000);
      QImage image;
      QTRY_VERIFY_WITH_TIMEOUT(!(image = p.grabFramebuffer()).isNull(), 10000);
      const auto frameHash = QCryptographicHash::hash(
          QByteArray::fromRawData(reinterpret_cast<const char *>(image.constBits()),
                                  image.sizeInBytes()), QCryptographicHash::Sha256);
      frames.insert(frameHash);
      seeks.append(QJsonObject{{"requested", target}, {"actual", p.get("time-pos").toDouble()},
                                {"frameSha256", QString::fromLatin1(frameHash.toHex())}});
    }
    QVERIFY(frames.size() >= 3);
    current["seeks"] = seeks;
    QVERIFY(p.set("aid", "no"));
    QTRY_VERIFY(!p.get("aid").toBool());
    for (int id : audio) {
      QVERIFY(p.set("aid", id));
      QTRY_COMPARE(p.get("aid").toInt(), id);
      QTRY_VERIFY(!p.get("audio-params").toMap().isEmpty());
    }
    const double beforeResume = p.get("time-pos").toDouble();
    QVERIFY(p.set("pause", false));
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > beforeResume + .4, 10000);
    QVERIFY(errors.isEmpty());
    QCOMPARE(QFileInfo(path).size(), originalSize);
    QCOMPARE(QFileInfo(path).lastModified(), originalModified);
    current["sourceUnchanged"] = true;
    current["audioDisableRestore"] = true;
  }
  void cleanup() {
    if (current.isEmpty()) return;
    current["passed"] = !QTest::currentTestFailed();
    results.append(current);
    current = {};
  }
  void cleanupTestCase() {
    const auto output = qEnvironmentVariable("FOXICMP_MATRIX_REPORT");
    QVERIFY2(!output.isEmpty(), "Set FOXICMP_MATRIX_REPORT");
    QSaveFile file(output);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QJsonObject report{{"schema", 1}, {"platform", QGuiApplication::platformName()},
                       {"requestedOutput", qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software")},
                       {"corpus", manifest}, {"results", results}};
    const auto bytes = QJsonDocument(report).toJson();
    QCOMPARE(file.write(bytes), bytes.size());
    QVERIFY(file.commit());
  }
};
QTEST_MAIN(MediaMatrixTest)
#include "media_matrix_test.moc"
