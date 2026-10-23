#include "player.h"
#include "profile.h"
#include "window.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <time.h>

static qint64 clock100ns() {
  timespec value{};
  clock_gettime(CLOCK_MONOTONIC, &value);
  return qint64(value.tv_sec) * 10000000 + value.tv_nsec / 100;
}
static QByteArray execute(const QString &name, const QStringList &args) {
  QProcess process;
  process.start(name, args);
  if (!process.waitForFinished(15000) || process.exitCode())
    throw std::runtime_error(process.readAllStandardError().constData());
  return process.readAllStandardOutput().trimmed();
}
class AvTest : public QObject {
  Q_OBJECT
private slots:
  void offsetMatrix_data() {
    QTest::addColumn<bool>("fragmented");
    QTest::addColumn<bool>("progressive");
    QTest::addColumn<int>("offset");
    for (bool f : {false, true})
      for (bool p : {false, true})
        for (int o : {0, 70, -70, 240, -240})
          QTest::newRow(qPrintable(QString("%1-%2-%3")
                                       .arg(f ? "fmp4" : "indexed")
                                       .arg(p ? "progressive" : "baseline")
                                       .arg(o)))
              << f << p << o;
  }
  void offsetMatrix() {
    QFETCH(bool, fragmented);
    QFETCH(bool, progressive);
    QFETCH(int, offset);
    const QString media = qEnvironmentVariable("FOXICMP_TEST_AV_MEDIA");
    QVERIFY2(!media.isEmpty(), "Set FOXICMP_TEST_AV_MEDIA to signal fixtures");
    const QString output = qEnvironmentVariable("FOXICMP_TEST_AV_RESULTS");
    QVERIFY2(!output.isEmpty(), "Set FOXICMP_TEST_AV_RESULTS");
    QDir().mkpath(output);
    const QString capture = QDir(output).filePath(
        QString::fromLatin1(QTest::currentDataTag()) + ".csv");
    QFile::remove(capture + ".json");
    QFile::remove(capture + ".audio.ready");
    const QString sink =
        "foxicmp_av_" + QUuid::createUuid().toString(QUuid::Id128);
    const auto module =
        execute("pactl", {"load-module", "module-null-sink",
                          "sink_name=" + sink, "rate=48000", "channels=1"});
    auto unload = qScopeGuard([&] {
      execute("pactl", {"unload-module", QString::fromLatin1(module)});
    });
    QTemporaryDir temporary;
    Profile profile(temporary.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/volume", 100);
    profile.settings.setValue("playback/hardwareDecoding", false);
    profile.settings.setValue("presentation/osd", false);
    Window window(profile);
    window.resize(640, 480);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    QVERIFY(player->set("audio-device", "pulse/" + sink));
    QProcess recorder;
    recorder.start("python3", {QStringLiteral(FOXICMP_AUDIO_SCRIPT),
                               sink + ".monitor", capture + ".audio"});
    QVERIFY(recorder.waitForStarted(5000));
    auto stopCapture = qScopeGuard([&] {
      recorder.terminate();
      if (!recorder.waitForFinished(3000)) {
        recorder.kill();
        recorder.waitForFinished();
      }
    });
    QTRY_VERIFY(QFileInfo::exists(capture + ".audio.ready"));
    QFile levels(capture);
    QVERIFY(levels.open(QIODevice::WriteOnly));
    QTimer sample;
    sample.setTimerType(Qt::PreciseTimer);
    sample.setInterval(10);
    connect(&sample, &QTimer::timeout, this, [&] {
      const auto begin = clock100ns();
      const auto frame = player->grabFramebuffer();
      const auto end = clock100ns();
      const int value =
          frame.isNull()
              ? -1
              : qGray(frame.pixel(frame.width() / 2, frame.height() / 2));
      levels.write(
          QString("v,%1,%2,%3,0\n").arg(begin).arg(end).arg(value).toUtf8());
    });
    sample.start();
    const QString videoName = fragmented ? "video-fragmented.mp4" : "video.mp4";
    const QString audioName =
        QString("audio%1%2.m4a")
            .arg(fragmented ? "-fragmented" : "")
            .arg(offset ? QString("-offset-%1").arg(offset) : "");
    const QStringList paths{QDir(media).filePath(videoName),
                            QDir(media).filePath(audioName)};
    QProcess provider;
    auto stopProvider = qScopeGuard([&] {
      provider.terminate();
      if (provider.state() != QProcess::NotRunning &&
          !provider.waitForFinished(3000)) {
        provider.kill();
        provider.waitForFinished();
      }
    });
    QJsonObject state;
    QJsonArray packets[2];
    QByteArray data[2];
    const auto manifest = temporary.filePath("session.json");
    if (progressive) {
      QJsonObject tracks;
      for (int i = 0; i < 2; ++i) {
        QFile file(paths[i]);
        QVERIFY(file.open(QIODevice::ReadOnly));
        data[i] = file.readAll();
        QFile part(temporary.filePath(i ? "audio.part" : "video.part"));
        QVERIFY(part.open(QIODevice::WriteOnly));
        QCOMPARE(part.write(data[i]), qint64(data[i].size()));
        QFile index(QFileInfo(paths[i]).path() + "/" +
                    QFileInfo(paths[i]).completeBaseName() + ".packets.json");
        QVERIFY(index.open(QIODevice::ReadOnly));
        packets[i] = QJsonDocument::fromJson(index.readAll())
                         .object()["packets"]
                         .toArray();
        tracks[i ? "audio" : "video"] =
            QJsonObject{{"path", i ? "audio.part" : "video.part"},
                        {"container", i ? "m4a" : "mp4"},
                        {"availablePrefixBytes", 0},
                        {"expectedTotalBytes", data[i].size()},
                        {"fragmented", fragmented},
                        {"initializationReady", true},
                        {"state", "downloading"}};
      }
      state = {{"schemaVersion", 1},
               {"sessionId", QUuid::createUuid().toString(QUuid::Id128)},
               {"generation", 1},
               {"revision", 1},
               {"state", "downloading"},
               {"duration100ns", 900000000},
               {"timestampOffset100ns", fragmented ? 10000000 : 0},
               {"tracks", tracks}};
    }
    auto publish = [&](double seconds) {
      auto tracks = state["tracks"].toObject();
      for (int i = 0; i < 2; ++i) {
        qint64 prefix = 0;
        for (const auto &entry : packets[i]) {
          const auto packet = entry.toObject();
          if (packet["dts_time"].toString().toDouble() <
              seconds + (fragmented ? 1 : 0))
            prefix =
                std::max(prefix, packet["pos"].toString().toLongLong() +
                                     packet["size"].toString().toLongLong());
        }
        if (fragmented)
          for (qint64 at = 0; at + 8 <= data[i].size();) {
            const auto size = qFromBigEndian<quint32>(data[i].constData() + at);
            if (size < 8 || size > data[i].size() - at)
              break;
            at += size;
            if (at >= prefix) {
              prefix = at;
              break;
            }
          }
        auto track = tracks[i ? "audio" : "video"].toObject();
        track["availablePrefixBytes"] = prefix;
        tracks[i ? "audio" : "video"] = track;
      }
      state["tracks"] = tracks;
      state["revision"] = state["revision"].toInt() + 1;
      QSaveFile file(manifest);
      if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot publish fixture");
      file.write(QJsonDocument(state).toJson());
      if (!file.commit())
        throw std::runtime_error("Cannot commit fixture");
    };
    QStringList transitions;
    qint64 resumed = 0, sought = 0;
    QSignalSpy loaded(player, &Player::loaded), errors(player, &Player::error);
    if (progressive) {
      publish(5);
      provider.start("python3", {QStringLiteral(FOXICMP_ADAPTER_SCRIPT),
                                 "--provider", manifest, "0", ""});
      QVERIFY(provider.waitForStarted(5000));
      QTRY_VERIFY(QFileInfo::exists(temporary.filePath("provider.json")));
      QVERIFY(window.openProgressiveSession(manifest));
      QTest::qWait(500);
      QCOMPARE(loaded.size(), 0);
      publish(12);
      QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
      QTRY_VERIFY_WITH_TIMEOUT(player->get("time-pos").toDouble() > 8 &&
                                   player->get("pause").toBool(),
                               18000);
      QTest::qWait(1000);
      publish(30);
      QTRY_VERIFY(!player->get("pause").toBool());
      resumed = clock100ns();
      QTest::qWait(6500);
      sought = clock100ns();
      transitions.append(QString::number(sought));
      QVERIFY(window.seekTo(22));
      QTRY_VERIFY(player->get("time-pos").toDouble() >
                  (fragmented ? 22.8 : 21.8));
      QTest::qWait(6500);
      window.closeCurrent();
    } else {
      QVERIFY(
          player->loadSource(paths[0], {{"audio-files-append", paths[1]},
                                        {"rebase-start-time", "no"},
                                        {"start", fragmented ? "1" : "0"}}));
      QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
      QTest::qWait(16000);
      player->command({"stop"});
    }
    sample.stop();
    levels.close();
    QCOMPARE(errors.size(), 0);
    recorder.terminate();
    QVERIFY(recorder.waitForFinished(3000));
    QCOMPARE(recorder.exitCode(), 0);
    auto args = QStringList{QStringLiteral(FOXICMP_AUDIO_SCRIPT), "--analyze",
                            capture, QString::number(offset)};
    args.append(transitions);
    QProcess analysis;
    analysis.start("python3", args);
    QVERIFY(analysis.waitForFinished(15000));
    QVERIFY2(analysis.exitCode() == 0,
             analysis.readAllStandardError().constData());
    const auto result =
        QJsonDocument::fromJson(analysis.readAllStandardOutput()).object();
    QVERIFY(result["max_abs_interval_ms"].isDouble());
    QVERIFY(result["pairs"].toArray().size() >= 5);
    if (progressive) {
      int startPairs = 0, resumePairs = 0, seekPairs = 0;
      for (const auto &pair : result["pairs"].toArray()) {
        const auto time = pair.toObject()["time100ns"].toDouble();
        if (time >= sought)
          ++seekPairs;
        else if (time >= resumed)
          ++resumePairs;
        else
          ++startPairs;
      }
      qInfo() << "Signal pairs by phase:" << startPairs << resumePairs
              << seekPairs;
      QVERIFY(startPairs >= 2 && resumePairs >= 2 && seekPairs >= 2);
    }
    qInfo() << QTest::currentDataTag() << "pairs"
            << result["pairs"].toArray().size() << "maximum interval error ms"
            << result["max_abs_interval_ms"].toDouble();
    QVERIFY(result["max_abs_interval_ms"].toDouble() <= 100);
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  Q_INIT_RESOURCE(resources);
  AvTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "av_test.moc"
