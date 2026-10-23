#include "mediasource.h"
#include "onlineoptions.h"
#include "player.h"
#include "playlistfile.h"
#include "profile.h"
#include "theme.h"
#include "window.h"
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
#include <QtWidgets>

class SourcesTest : public QObject {
  Q_OBJECT
private slots:
  void normalizationAndPlaylist() {
    QString error;
    QCOMPARE(
        MediaSource::normalize("https://example.org/a%20b.mp4?q=1", &error),
        QString("https://example.org/a%20b.mp4?q=1"));
    QVERIFY(MediaSource::isNetwork("srt://127.0.0.1:9000?mode=caller"));
    QVERIFY(MediaSource::isNetwork(
        "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"));
    QVERIFY(MediaSource::isNetwork(
        "acestream://0123456789abcdef0123456789abcdef01234567"));
    QVERIFY(MediaSource::normalize("gopher://example.org/a", &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QCOMPARE(MediaSource::displayName("https://example.org/a%20b.mp4?q=1"),
             QString("a b.mp4"));

    QTemporaryDir dir;
    const QString list = dir.filePath("online.m3u8");
    const QStringList sources{"https://example.org/video.mp4?q=1",
                              dir.filePath("local movie.mkv")};
    QVERIFY2(PlaylistFile::write(list, sources, &error), qPrintable(error));
    QStringList restored;
    QVERIFY2(PlaylistFile::read(list, &restored, &error), qPrintable(error));
    QCOMPARE(restored, sources);
  }

  void foldersClipboardExternalReopenAndClose() {
    const QDir media(qEnvironmentVariable("FOXICMP_TEST_MEDIA"));
    if (!media.exists())
      QSKIP("Set FOXICMP_TEST_MEDIA");
    QTemporaryDir dir;
    const QString folder = dir.filePath("folder");
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(QFile::copy(media.filePath("Русский H264.mp4"),
                        QDir(folder).filePath("movie10.mp4")));
    QVERIFY(QFile::copy(media.filePath("Русский H264.mp4"),
                        QDir(folder).filePath("movie2.mp4")));
    QFile text(QDir(folder).filePath("notes.txt"));
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("not media");
    text.close();

    Profile profile(dir.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    auto *queue = window.findChild<QListWidget *>("playlist");
    QSignalSpy loaded(player, &Player::loaded);
    QVERIFY(window.openDirectory(folder));
    QTRY_COMPARE(loaded.size(), 1);
    QCOMPARE(queue->count(), 2);
    QCOMPARE(queue->item(0)->text(), QString("movie2.mp4"));
    QCOMPARE(queue->item(1)->text(), QString("movie10.mp4"));
    player->set("pause", true);
    player->command({"seek", "1", "absolute+exact"});
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 1) < .15);

    QVERIFY(window.loadExternalAudio(media.filePath("Русский H264.mp4")));
    QTRY_VERIFY([&] {
      for (const QVariant &track : player->get("track-list").toList()) {
        const QVariantMap item = track.toMap();
        if (item.value("type") == "audio" && item.value("external").toBool())
          return true;
      }
      return false;
    }());

    window.reopenCurrent();
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 1) < .2);

    QApplication::clipboard()->setText("gopher://example.org/video.mp4");
    window.openClipboard();
    QCOMPARE(queue->count(), 2);
    QCOMPARE(loaded.size(), 2);
    QApplication::clipboard()->setText(media.filePath("VP9.webm"));
    window.openClipboard();
    QTRY_COMPARE(loaded.size(), 3);
    QCOMPARE(queue->count(), 1);

    window.addFiles({media.filePath("AV1.mkv")});
    QCOMPARE(queue->count(), 2);
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
    QCOMPARE(queue->count(), 2);
    QVERIFY(queue->currentRow() >= 0);
    window.findChild<QAction *>("action_play")->trigger();
    QTRY_COMPARE(loaded.size(), 4);
  }

  void networkUrlHistoryFavoriteAndDialog() {
    const QString media = qEnvironmentVariable("FOXICMP_TEST_MEDIA");
    if (media.isEmpty())
      QSKIP("Set FOXICMP_TEST_MEDIA");
    QProcess server;
    server.setProgram("python3");
    server.setArguments(
        {"-u", "-c",
         "import os,sys; from http.server import "
         "ThreadingHTTPServer,SimpleHTTPRequestHandler; "
         "os.chdir(sys.argv[1]); "
         "s=ThreadingHTTPServer(('127.0.0.1',0),SimpleHTTPRequestHandler); "
         "print(s.server_port,flush=True); s.serve_forever()",
         media});
    server.start();
    QVERIFY(server.waitForStarted(5000));
    QVERIFY(server.waitForReadyRead(5000));
    bool ok = false;
    const int port = QString::fromUtf8(server.readLine()).trimmed().toInt(&ok);
    QVERIFY(ok && port > 0);
    const QString url =
        QString("http://127.0.0.1:%1/chapters.mkv?source=test").arg(port);

    QTemporaryDir dir;
    Profile profile(dir.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    auto *queue = window.findChild<QListWidget *>("playlist");
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({url});
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    QCOMPARE(queue->item(0)->data(Qt::UserRole).toString(), url);
    QCOMPARE(profile.recentFiles(), QStringList{url});
    const QString id = profile.addFavorite("Online", url, 1.0);
    QVERIFY(!id.isEmpty());
    QCOMPARE(profile.favorites().first().file, url);
    QString error;
    const QString list = dir.filePath("network.m3u8");
    QVERIFY(PlaylistFile::write(list, {url}, &error));
    QStringList restored;
    QVERIFY(PlaylistFile::read(list, &restored, &error));
    QCOMPARE(restored, QStringList{url});

    QApplication::clipboard()->setText(url);
    QTimer::singleShot(100, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("urlDialog");
      QVERIFY(dialog);
      dialog->findChild<QPushButton *>("pasteSourceAddress")->click();
      QCOMPARE(dialog->findChild<QLineEdit *>("sourceAddress")->text(), url);
      auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
      QVERIFY(ok); ok->click();
    });
    window.findChild<QAction *>("action_openUrl")->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 10000);

    server.terminate();
    if (!server.waitForFinished(3000)) {
      server.kill();
      server.waitForFinished(3000);
    }
  }

  void onlineOptionsAndPlaylistImporter() {
    QTemporaryDir dir;
    const QString fake = dir.filePath("fake-yt-dlp");
    auto writeFake = [&](const QByteArray &output) {
      QFile script(fake);
      QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
      script.write("#!/bin/sh\nprintf '%s' '");
      script.write(output);
      script.write("'\n");
      script.close();
      QVERIFY(QFile::setPermissions(
          fake, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                    QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                    QFileDevice::ExeGroup));
    };
    writeFake("{\"entries\":[{\"id\":\"alpha\",\"url\":\"abc123\","
              "\"ie_key\":\"Youtube\",\"title\":\"Первая\"},"
              "{\"webpage_url\":\"https://example.org/video.mp4?q=1\","
              "\"title\":\"Вторая\"}]}");
    Profile profile(dir.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("online/ytdlpPath", fake);
    profile.settings.setValue("online/maxHeight", 1080);
    profile.settings.setValue("online/maxFps", 60);
    profile.settings.setValue("online/videoCodec", "h264");
    profile.settings.setValue("online/hdr", "avoid");
    profile.settings.setValue("online/audioCodec", "aac");
    profile.settings.setValue("online/audioLanguage", "ru");
    profile.settings.setValue("online/maxVideoBitrate", 5000);
    profile.settings.setValue("online/networkTimeout", 17);
    const QString format = OnlineOptions::formatSelector(profile.settings);
    QVERIFY(format.contains("height<=1080"));
    QVERIFY(format.contains("fps<=60"));
    QVERIFY(format.contains("vbr<=5000"));
    QVERIFY(format.contains("vcodec^=avc"));
    QVERIFY(format.contains("dynamic_range=SDR"));
    QVERIFY(format.contains("acodec^=mp4a"));
    QVERIFY(format.contains("language^=ru"));
    const auto options = OnlineOptions::mpvOptions(profile.settings);
    QCOMPARE(options.value("ytdl"), QString("yes"));
    QCOMPARE(options.value("network-timeout"), QString("17"));
    QVERIFY(options.value("script-opts").contains(fake));
    QString specialError;
    const QString ace = OnlineOptions::playbackUrl(
        profile.settings,
        "acestream://0123456789abcdef0123456789abcdef01234567", &specialError);
    const QUrl aceUrl(ace);
    QCOMPARE(aceUrl.path(), QString("/ace/getstream"));
    QCOMPARE(QUrlQuery(aceUrl).queryItemValue("id"),
             QString("0123456789abcdef0123456789abcdef01234567"));
    const QString magnet =
        "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567";
    const QUrl torrUrl(
        OnlineOptions::playbackUrl(profile.settings, magnet, &specialError));
    QCOMPARE(torrUrl.path(), QString("/stream"));
    QCOMPARE(QUrlQuery(torrUrl).queryItemValue("link"), magnet);
    QCOMPARE(QUrlQuery(torrUrl).queryItemValue("play"), QString("1"));
    QVERIFY(OnlineOptions::playbackUrl(profile.settings, "acestream://bad",
                                       &specialError)
                .isEmpty());
    QVERIFY(!specialError.isEmpty());

    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QSignalSpy imported(&window, &Window::onlinePlaylistFinished);
    QVERIFY(window.loadOnlinePlaylist(
        "https://www.youtube.com/playlist?list=local-test"));
    QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 5000);
    QVERIFY(imported.first().first().toBool());
    auto *queue = window.findChild<QListWidget *>("playlist");
    QCOMPARE(queue->count(), 2);
    QCOMPARE(queue->item(0)->text(), QString("Первая"));
    QCOMPARE(queue->item(0)->data(Qt::UserRole).toString(),
             QString("https://www.youtube.com/watch?v=abc123"));
    QCOMPARE(queue->item(1)->text(), QString("Вторая"));

    writeFake("not-json");
    QVERIFY(window.loadOnlinePlaylist(
        "https://www.youtube.com/playlist?list=broken"));
    QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 2, 5000);
    QVERIFY(!imported.last().first().toBool());
    QCOMPARE(queue->count(), 2);

    QFile invalidExecutable(fake);
    QVERIFY(invalidExecutable.open(QIODevice::WriteOnly | QIODevice::Truncate));
    invalidExecutable.write("#!/foxicmp-test/missing-interpreter\n");
    invalidExecutable.close();
    QVERIFY(window.loadOnlinePlaylist("https://example.org/failed-start"));
    QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 3, 5000);
    QVERIFY(!imported.last().first().toBool());
    QCOMPARE(queue->count(), 2);

    QVERIFY(invalidExecutable.open(QIODevice::WriteOnly | QIODevice::Truncate));
    invalidExecutable.write("#!/bin/sh\nexec sleep 30\n");
    invalidExecutable.close();
    QVERIFY(window.loadOnlinePlaylist("https://example.org/cancel"));
    auto *progress =
        window.findChild<QProgressDialog *>("onlinePlaylistProgress");
    QVERIFY(progress);
    progress->findChild<QPushButton *>()->click();
    QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 4, 5000);
    QVERIFY(!imported.last().first().toBool());
    QCOMPARE(queue->count(), 2);
  }
};

int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  Q_INIT_RESOURCE(resources);
  applyTheme(app);
  SourcesTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "sources_test.moc"
