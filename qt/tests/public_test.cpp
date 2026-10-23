#include "framepreview.h"
#include "player.h"
#include "profile.h"
#include "window.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

class PublicTest : public QObject {
  Q_OBJECT
private slots:
  void onlinePreview() {
    const auto url = qEnvironmentVariable("FOXICMP_PUBLIC_PREVIEW_URL");
    QVERIFY(!url.isEmpty());
    FramePreview preview;
    preview.configure(
        QSize(480, 270), true,
        qEnvironmentVariable("FOXICMP_PUBLIC_EXTRACTOR",
                             QStandardPaths::findExecutable("yt-dlp")),
        {}, 20000);
    QSignalSpy ready(&preview, &FramePreview::ready),
        failed(&preview, &FramePreview::unavailable);
    QElapsedTimer elapsed;
    elapsed.start();
    preview.request(url, 3);
    QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !failed.isEmpty(), 45000);
    QVERIFY2(failed.isEmpty(),
             failed.isEmpty() ? "" : qPrintable(failed.last()[0].toString()));
    const auto image = qvariant_cast<QImage>(ready.last()[0]);
    QVERIFY(!image.isNull());
    QVERIFY(image.width() <= 480 && image.height() <= 270);
    const auto output = qEnvironmentVariable("FOXICMP_PUBLIC_PREVIEW_OUTPUT");
    if (!output.isEmpty())
      QVERIFY(image.save(output));
    qInfo() << "Decoded online preview" << image.size() << "target seconds"
            << ready.last()[1] << "elapsed ms" << elapsed.elapsed();
  }
  void downloadAndPlay() {
    const QString info = qEnvironmentVariable("FOXICMP_PUBLIC_INFO");
    const QString work = qEnvironmentVariable("FOXICMP_PUBLIC_WORK");
    QVERIFY(!info.isEmpty() && !work.isEmpty());
    QVERIFY(QDir().mkpath(work));
    QFile metadata(info);
    QVERIFY(metadata.open(QIODevice::ReadOnly));
    const auto input = QJsonDocument::fromJson(metadata.readAll()).object();
    auto requested = input["requested_formats"].toArray();
    QStringList ids;
    if (requested.isEmpty())
      ids.append(input["format_id"].toString());
    for (const auto &format : requested)
      ids.append(format.toObject()["format_id"].toString());
    QTemporaryDir profileRoot;
    Profile profile(profileRoot.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("playback/hardwareDecoding", false);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    QProcess downloader;
    auto environment = QProcessEnvironment::systemEnvironment();
    // Growing sources require a private local directory. A VirtualBox shared
    // output folder cannot enforce the required Unix owner/mode permissions.
    environment.insert("YTD_TEMP_DIR", profileRoot.path());
    downloader.setProcessEnvironment(environment);
    downloader.start("python3",
                     {QStringLiteral(FOXICMP_ADAPTER_SCRIPT), "--ignore-config",
                      "--load-info-json", info, "-f", ids.join('+'), "--quiet",
                      "--no-progress", "--socket-timeout", "20", "--limit-rate",
                      "400K", "-o", QDir(work).filePath("finished.mp4")});
    QVERIFY(downloader.waitForStarted(5000));
    auto stop = qScopeGuard([&] {
      if (downloader.state() != QProcess::NotRunning) {
        downloader.kill();
        downloader.waitForFinished(3000);
      }
    });
    QByteArray output, diagnostics;
    QString manifest, openError;
    bool opened = false, playedDuringDownload = false, seeked = false;
    int frames = 0, syncSamples = 0;
    double maximumSync = 0, maximumPosition = 0;
    QByteArray previousFrame;
    QElapsedTimer elapsed, stable;
    elapsed.start();
    stable.start();
    while (elapsed.elapsed() < 600000 &&
           downloader.state() != QProcess::NotRunning) {
      QTest::qWait(100);
      output += downloader.readAllStandardOutput();
      diagnostics += downloader.readAllStandardError();
      for (int newline; (newline = output.indexOf('\n')) >= 0;) {
        const auto line = output.left(newline);
        output.remove(0, newline + 1);
        if (!line.startsWith("[FoxicMP] "))
          continue;
        const auto event = QJsonDocument::fromJson(line.mid(10)).object();
        if (event["ready"].toBool() && !opened) {
          manifest = event["manifest"].toString();
          opened = window.openProgressiveSession(manifest);
          stable.restart();
          if (!opened)
            for (auto *label : window.findChildren<QLabel *>())
              if (label->text().startsWith("Прогрессивный источник:"))
                openError = label->text();
        }
      }
      const double position = player->get("time-pos").toDouble();
      maximumPosition = std::max(maximumPosition, position);
      if (!opened || position < 1 || player->get("pause").toBool()) {
        stable.restart();
        continue;
      }
      QFile session(manifest);
      if (session.open(QIODevice::ReadOnly) &&
          QJsonDocument::fromJson(session.readAll()).object()["state"] !=
              "complete")
        playedDuringDownload = true;
      const auto frame = player->grabFramebuffer().scaled(32, 18);
      const QByteArray pixels(reinterpret_cast<const char *>(frame.constBits()),
                              frame.sizeInBytes());
      if (!previousFrame.isEmpty() && pixels != previousFrame)
        ++frames;
      previousFrame = pixels;
      const auto sync = player->get("avsync");
      if (stable.elapsed() > 1000 && sync.isValid() &&
          std::isfinite(sync.toDouble())) {
        ++syncSamples;
        maximumSync = std::max(maximumSync, std::abs(sync.toDouble()));
      }
      if (!seeked && position > 12 && window.seekTo(5)) {
        seeked = true;
        stable.restart();
      }
    }
    diagnostics += downloader.readAllStandardError();
    QFile log(QDir(work).filePath("downloader.log"));
    QVERIFY(log.open(QIODevice::WriteOnly));
    log.write(diagnostics);
    QVERIFY2(downloader.state() == QProcess::NotRunning,
             "Public download timeout");
    QVERIFY2(downloader.exitCode() == 0, diagnostics.constData());
    QVERIFY2(opened, openError.toUtf8().constData());
    QVERIFY(playedDuringDownload);
    QVERIFY(frames > 4);
    QVERIFY(seeked);
    QVERIFY(syncSamples > 20);
    QVERIFY(maximumSync <= .1);
    QVERIFY(player->get("video-codec").toString().contains("264"));
    QVERIFY(player->get("audio-codec-name")
                .toString()
                .contains("aac", Qt::CaseInsensitive));
    QVERIFY(QFileInfo(QDir(work).filePath("finished.mp4")).size() > 10000);
    QFile session(manifest);
    QVERIFY(session.open(QIODevice::ReadOnly));
    const auto finalState = QJsonDocument::fromJson(session.readAll()).object();
    QCOMPARE(finalState["state"].toString(), QString("complete"));
    for (const auto &track : finalState["tracks"].toObject())
      QVERIFY(QFileInfo::exists(
          QDir(QFileInfo(manifest).absolutePath())
              .filePath(track.toObject()["path"].toString())));
    QJsonObject result{
        {"url", input["webpage_url"]},
        {"formats", ids.join('+')},
        {"duration", input["duration"]},
        {"playedDuringDownload", playedDuringDownload},
        {"changedFrames", frames},
        {"cacheSeekPassed", seeked},
        {"avsyncSamples", syncSamples},
        {"maxInternalAvsyncSeconds", maximumSync},
        {"maxPositionSeconds", maximumPosition},
        {"elapsedSeconds", elapsed.elapsed() / 1000.},
        {"session", manifest},
        {"timestampOffset100ns", finalState["timestampOffset100ns"]},
        {"passed", true}};
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
    const auto providerFile =
        QDir(QFileInfo(manifest).absolutePath()).filePath("provider.json");
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
          QFile provider(providerFile);
          if (!provider.open(QIODevice::ReadOnly))
            return false;
          const auto status =
              QJsonDocument::fromJson(provider.readAll()).object();
          return status.contains("ready") && !status["ready"].toBool();
        }(),
        45000);
    for (const auto &track : finalState["tracks"].toObject())
      QVERIFY(!QFileInfo::exists(
          QDir(QFileInfo(manifest).absolutePath())
              .filePath(track.toObject()["path"].toString())));
    result["providerCleanupPassed"] = true;
    QFile report(QDir(work).filePath("result.json"));
    QVERIFY(report.open(QIODevice::WriteOnly));
    report.write(QJsonDocument(result).toJson());
    qInfo().noquote() << QJsonDocument(result).toJson(QJsonDocument::Compact);
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  Q_INIT_RESOURCE(resources);
  PublicTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "public_test.moc"
