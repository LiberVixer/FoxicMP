#include "devicesource.h"
#include "dvbscanner.h"
#include "mediasource.h"
#include "player.h"
#include "playlistfile.h"
#include "profile.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>

class DevicesTest : public QObject {
  Q_OBJECT
private slots:
  void sourceIdentityAndMissingBackend() {
    QTemporaryDir temporary;
    DeviceSource::Request request;
    request.kind = "dvd";
    request.path = temporary.filePath("Русский фильм #1.iso");
    request.title = 2;
    const QString source = DeviceSource::encode(request);
    DeviceSource::Request restored;
    QString error;
    QVERIFY(DeviceSource::decode(source, &restored, &error));
    QCOMPARE(restored.path, request.path);
    QCOMPARE(restored.title, 2);
    QCOMPARE(MediaSource::normalize(source), source);
    QVERIFY(MediaSource::isUrl(source));
    QVERIFY(!MediaSource::isNetwork(source));
    QVERIFY(!DeviceSource::decode(source + "&title=1", &restored, &error));
    QVERIFY(!DeviceSource::decode(source + "&bad=1", &restored, &error));
    QVERIFY(!DeviceSource::decode(
        "foxicmp-device://dvd?path=/tmp/a.iso&title=-1", &restored, &error));
    QFile image(request.path);
    QVERIFY(image.open(QIODevice::WriteOnly));
    image.write("fixture");
    image.close();
    QVERIFY(
        DeviceSource::resolve(source, {}, {}, temporary.path()).url.isEmpty());
    const auto plan =
        DeviceSource::resolve(source, {"dvd"}, {}, temporary.path());
    QCOMPARE(plan.url, QString("dvd://1")); // GUI numbering starts at one.
    QCOMPARE(plan.options["dvd-device"], request.path);
    QCOMPARE(plan.options["dvd-angle"], QString("1"));
    QCOMPARE(plan.options["ytdl"], QString("no"));
    const QString playlist = temporary.filePath("discs.m3u8");
    QVERIFY(PlaylistFile::write(playlist, {source}, &error));
    QStringList entries;
    QVERIFY(PlaylistFile::read(playlist, &entries, &error));
    QCOMPARE(entries, QStringList{source});
    Profile profile(temporary.path());
    profile.savePosition(source, 3, 12);
    QCOMPARE(profile.position(source), 3.);
    QVERIFY(!profile.addFavorite({}, source, 3).isEmpty());
    QCOMPARE(profile.favorites().first().file, source);
    QCOMPARE(profile.favorites().first().title, DeviceSource::label(source));
    request.kind = "test-video";
    request.path.clear();
    const auto live = DeviceSource::encode(request);
    profile.savePosition(live, 10, 0);
    QCOMPARE(profile.position(live), 0.);
    QVERIFY(DeviceSource::isLive(live));
    QVERIFY(DeviceSource::resolve(live, {"av"}, {}, temporary.path())
                .url.isEmpty());
    QVERIFY(!DeviceSource::resolve(live, {"av"}, {"lavfi"}, temporary.path())
                 .url.isEmpty());
    request.kind = "dvd";
    request.path = "/dev/sr0";
    profile.savePosition(DeviceSource::encode(request), 10, 100);
    QCOMPARE(profile.position(DeviceSource::encode(request)), 0.);
  }
  void liveCaptureAndDialogs() {
    QTemporaryDir temporary;
    Profile profile(temporary.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("playback/hardwareDecoding", false);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    profile.settings.setValue("playback/after", "exit");
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    auto *queue = window.findChild<QListWidget *>("playlist");
    QSignalSpy loaded(player, &Player::loaded), errors(player, &Player::error);
    bool accepted = false;
    const auto pulseSource = qEnvironmentVariable("FOXICMP_TEST_PULSE_SOURCE");
    QTimer::singleShot(100, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("openCaptureDialog");
      if (!dialog)
        return;
      auto *type = dialog->findChild<QComboBox *>("captureType");
      type->setCurrentIndex(type->findData("test-video"));
      if (!pulseSource.isEmpty()) {
        auto *audio = dialog->findChild<QComboBox *>("captureAudio");
        const int row = audio->findData("pulse:" + pulseSource);
        QVERIFY(row >= 0);
        audio->setCurrentIndex(row);
      }
      dialog->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Open)
          ->click();
      accepted = true;
    });
    window.findChild<QAction *>("action_openCapture")->trigger();
    QVERIFY(accepted);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    QTRY_VERIFY(player->get("time-pos").toDouble() > .2);
    if (!pulseSource.isEmpty()) {
      QTRY_VERIFY(
          player->get("audio-params").toMap().value("samplerate").toInt() > 0);
      QCOMPARE(profile.settings.value("devices/captureAudio").toString(),
               "pulse:" + pulseSource);
    }
    const auto frame = player->grabFramebuffer();
    QVERIFY(!frame.isNull());
    QVERIFY(frame.pixelColor(frame.width() / 2, frame.height() / 2) !=
            QColor(Qt::black));
    QVERIFY(!window.seekTo(3));
    QVERIFY(!window.findChild<QAction *>("action_frameBack")->isEnabled());
    QVERIFY(!window.findChild<QAction *>("action_frameNext")->isEnabled());
    QVERIFY(
        DeviceSource::isLive(queue->item(0)->data(Qt::UserRole).toString()));
    QVERIFY(QMetaObject::invokeMethod(player, "ended", Qt::DirectConnection,
                                      Q_ARG(bool, true)));
    QVERIFY(window.isVisible());
    QCOMPARE(profile.settings.value("devices/captureKind").toString(),
             QString("test-video"));
    const auto existing = queue->item(0)->data(Qt::UserRole).toString();
    DeviceSource::Request invalid;
    invalid.kind = "dvd";
    invalid.path = temporary.filePath("missing.iso");
    QVERIFY(!window.openDeviceSource(DeviceSource::encode(invalid)));
    QCOMPARE(queue->item(0)->data(Qt::UserRole).toString(), existing);
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
    QCOMPARE(errors.size(), 0);
    QCOMPARE(profile.position(existing), 0.);
    QTRY_VERIFY(window.findChild<QAction *>("action_frameNext")->isEnabled());
    QVERIFY(window.isVisible()); // No after-playback action for a live input.
    QTimer::singleShot(100, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("openTelevisionDialog");
      if (dialog)
        dialog->reject();
    });
    window.findChild<QAction *>("action_openTelevision")->trigger();
  }
  void audioCapture_data() {
    QTest::addColumn<QString>("kind");
    QTest::addColumn<QString>("path");
    QTest::newRow("generated-audio") << QString("test-audio") << QString();
    const auto source = qEnvironmentVariable("FOXICMP_TEST_PULSE_SOURCE");
    if (!source.isEmpty())
      QTest::newRow("private-pulse-monitor") << QString("pulse") << source;
  }
  void audioCapture() {
    QFETCH(QString, kind);
    QFETCH(QString, path);
    QTemporaryDir temporary;
    Profile profile(temporary.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded), errors(player, &Player::error);
    DeviceSource::Request request;
    request.kind = kind;
    request.path = path;
    const auto source = DeviceSource::encode(request);
    QVERIFY(window.openDeviceSource(source));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    QTRY_VERIFY(player->get("time-pos").toDouble() > .3);
    QVERIFY(player->get("audio-params").toMap().value("samplerate").toInt() >
            0);
    QVERIFY(!window.seekTo(1));
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
    QCOMPARE(profile.position(source), 0.);
    QCOMPARE(errors.size(), 0);
  }
  void dvdPlayback_data() {
    QTest::addColumn<QString>("relative");
    QTest::newRow("dvd-folder") << QString("devices/DVD");
    QTest::newRow("dvd-iso") << QString("devices/test-dvd.iso");
  }
  void dvdPlayback() {
    QFETCH(QString, relative);
    const QString path =
        QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(relative);
    QVERIFY2(QFileInfo::exists(path), "Generate DVD fixtures first");
    QTemporaryDir temporary;
    Profile profile(temporary.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("playback/hardwareDecoding", false);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded), errors(player, &Player::error);
    DeviceSource::Request request;
    request.kind = "dvd";
    request.path = path;
    request.title = 2;
    const auto source = DeviceSource::encode(request);
    if (relative.endsWith("DVD")) {
      bool accepted = false;
      QTimer::singleShot(100, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("openDiscDialog");
        if (!dialog)
          return;
        dialog->findChild<QComboBox *>("discPath")->setEditText(path);
        dialog->findChild<QSpinBox *>("discTitle")->setValue(2);
        dialog->findChild<QDialogButtonBox *>()
            ->button(QDialogButtonBox::Open)
            ->click();
        accepted = true;
      });
      window.findChild<QAction *>("action_openDisc")->trigger();
      QVERIFY(accepted);
      QCOMPARE(profile.settings.value("devices/discPath").toString(), path);
    } else
      QVERIFY(window.openDeviceSource(source));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    QTRY_VERIFY(player->get("time-pos").toDouble() > .2);
    QTRY_VERIFY(std::abs(player->get("duration").toDouble() - 12) < 1);
    QVERIFY(std::abs(player->get("options/end").toDouble() - 12) < 1);
    qInfo() << "DVD video:" << player->get("video-codec")
            << "format:" << player->get("video-format")
            << "audio:" << player->get("audio-codec-name");
    QVERIFY(player->get("video-format").toString().contains("mpeg2"));
    QVERIFY(player->get("audio-codec-name").toString().contains("ac3"));
    const auto frame = player->grabFramebuffer();
    QVERIFY(!frame.isNull());
    QVERIFY(frame.pixelColor(frame.width() / 2, frame.height() / 2).blue() >
            100);
    QTRY_VERIFY(player->get("chapter-list").toList().size() == 2);
    player->set("pause", true);
    window.findChild<QAction *>("action_chapterNext")->trigger();
    QTRY_VERIFY(player->get("chapter").toInt() == 1);
    window.findChild<QAction *>("action_chapterPrevious")->trigger();
    QTRY_VERIFY(player->get("chapter").toInt() == 0);
    QVERIFY(window.seekTo(2));
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 2) < .3);
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
    QVERIFY(std::abs(profile.position(source) - 2) < .3);
    QVERIFY(window.openDeviceSource(source));
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(player->get("time-pos").toDouble() > 1.8);
    QVERIFY(window.findChild<QAction *>("action_discTitle")->isEnabled());
    bool titleAccepted = false;
    QTimer::singleShot(100, &window, [&] {
      if (auto *dialog = window.findChild<QInputDialog *>()) {
        dialog->setIntValue(1);
        dialog->accept();
        titleAccepted = true;
      }
    });
    window.findChild<QAction *>("action_discTitle")->trigger();
    QVERIFY(titleAccepted);
    QTRY_COMPARE(loaded.size(), 3);
    QTRY_VERIFY(std::abs(player->get("duration").toDouble() - 6) < 1);
    request.title = 0;
    QVERIFY(window.openDeviceSource(DeviceSource::encode(request)));
    QTRY_COMPARE(loaded.size(), 4);
    QTRY_VERIFY(std::abs(player->get("duration").toDouble() - 12) < 1);
    QSignalSpy ended(player, &Player::ended);
    QVERIFY(window.seekTo(10.5));
    QTRY_VERIFY_WITH_TIMEOUT(!ended.isEmpty() && ended.last()[0].toBool(),
                             10000);
    window.closeCurrent();
    QTRY_VERIFY(player->get("idle-active").toBool());
    QTRY_COMPARE(player->get("options/end").toString(), QString("none"));
    QCOMPARE(errors.size(), 0);
    QVERIFY(profile.recentFiles().contains(source));
  }
  void scannerSavesChannelsAndCancellationPreservesThem() {
    const QByteArray channels(
        "News;Provider:650000000:INVERSION_AUTO:6900:0:101:102:0:0:1:0:0:"
        "0\nMusic:660000000:INVERSION_AUTO:6900:0:201:202:0:0:2:0:0:0\n");
    QCOMPARE(DeviceSource::channels(channels), QStringList({"News", "Music"}));
    QString error;
    QVERIFY(DeviceSource::channels(QByteArray("bad\xff"), &error).isEmpty());
    QTemporaryDir temporary;
    const auto initial = temporary.filePath("initial.conf");
    QFile tuning(initial);
    QVERIFY(tuning.open(QIODevice::WriteOnly));
    tuning.write("[CHANNEL]\nDELIVERY_SYSTEM = DVBT\nFREQUENCY = 650000000\n");
    tuning.close();
    const auto executable = temporary.filePath("scanner");
    QFile fake(executable);
    QVERIFY(fake.open(QIODevice::WriteOnly));
    fake.write("#!/usr/bin/env python3\nimport pathlib,sys\na=sys.argv\nassert "
               "a[1:9]==['-a','2','-f','0','-I','DVBV5','-O','VDR']\npathlib."
               "Path(a[a.index('-o')+1]).write_text('News:1:2:3:4:5:6:7:8:9:10:"
               "11:12\\n')\nprint('scan complete')\n");
    fake.close();
    QVERIFY(fake.setPermissions(QFileDevice::ReadOwner |
                                QFileDevice::WriteOwner |
                                QFileDevice::ExeOwner));
    DvbScanner scanner(temporary.path());
    QSignalSpy finished(&scanner, &DvbScanner::finished);
    QVERIFY(scanner.start(executable, initial, 2, &error));
    QTRY_COMPARE(finished.size(), 1);
    QVERIFY(finished[0][0].toBool());
    QFile saved(temporary.filePath("FoxicMPQt.channels.conf"));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto before = saved.readAll();
    saved.close();
    QVERIFY(QFileInfo::exists(temporary.filePath("FoxicMPQt.dvb-tuning.conf")));
    QVERIFY(fake.open(QIODevice::WriteOnly | QIODevice::Truncate));
    fake.write("#!/usr/bin/env python3\nimport time\ntime.sleep(30)\n");
    fake.close();
    QVERIFY(scanner.start(executable, initial, 2, &error));
    QTRY_VERIFY(scanner.running());
    scanner.cancel();
    QTRY_COMPARE(finished.size(), 2);
    QVERIFY(!finished[1][0].toBool());
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), before);
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  Q_INIT_RESOURCE(resources);
  DevicesTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "devices_test.moc"
