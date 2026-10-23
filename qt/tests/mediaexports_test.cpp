#include "mediaexports.h"
#include "player.h"
#include "profile.h"
#include "window.h"
#include <QCryptographicHash>
#include <QtTest>
#include <QtWidgets>

class MediaExportsTest : public QObject {
  Q_OBJECT
  QString media;
  QJsonObject results;
  void setup(Profile &profile) {
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
  }
  void load(Window &w) {
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded);
    w.show();
    w.openFiles({media});
    QTRY_COMPARE(loaded.size(), 1);
    QVERIFY(p->set("pause", true));
    QVERIFY(p->command({"seek", "2", "absolute+exact"}));
    QTRY_VERIFY(std::abs(p->get("time-pos").toDouble() - 2) < .1);
    QTRY_VERIFY(!p->get("seeking").toBool());
    QTest::qWait(100);
  }
  void settings(Window &w, const std::function<void(QDialog *)> &edit) {
    QTimer::singleShot(80, &w, [&w, edit] {
      auto *d = w.findChild<QDialog *>("captureSettingsDialog");
      QVERIFY(d);
      edit(d);
    });
    w.findChild<QAction *>("action_captureSettings")->trigger();
  }
  QByteArray bytes(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
      return {};
    return f.readAll();
  }
private slots:
  void initTestCase() {
    media = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                .filePath("chapters.mkv");
    QVERIFY(QFileInfo(media).isFile());
  }
  void nativeFramesClipboardDisplayAndQuickSave() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    setup(profile);
    profile.settings.setValue("ui/screenshotSubtitles", false);
    Window w(profile);
    load(w);
    auto *p = w.findChild<Player *>();
    QVERIFY(
        w.loadExternalSubtitles(QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                                    .filePath("subtitle.srt")));
    QTRY_VERIFY(!p->get("sub-text").toString().isEmpty());
    QSignalSpy saved(&w, &Window::frameSaved),
        captured(&w, &Window::imageCaptured);
    QVERIFY(w.captureImage(dir.filePath("frame.png")));
    QTRY_COMPARE(captured.size(), 1);
    QVERIFY(captured.last()[0].toBool());
    const QImage raw(dir.filePath("frame.png"));
    QCOMPARE(raw.size(), QSize(320, 180));
    profile.settings.setValue("ui/screenshotSubtitles", true);
    QVERIFY(w.captureImage(dir.filePath("subtitles.png")));
    QTRY_COMPARE(captured.size(), 2);
    QVERIFY(captured.last()[0].toBool());
    QVERIFY(QImage(dir.filePath("subtitles.png")) != raw);
    w.findChild<QAction *>("action_snapshotClipboard")->trigger();
    QTRY_COMPARE(captured.size(), 3);
    QCOMPARE(QApplication::clipboard()->image().size(), raw.size());
    auto *overlay = new QLabel("OSD test", p);
    overlay->setStyleSheet("background:#00ff00;color:#000000");
    overlay->setGeometry(10, 10, 120, 40);
    overlay->show();
    overlay->raise();
    QVERIFY(w.captureImage(dir.filePath("display.png"), false, true));
    QTRY_COMPARE(captured.size(), 4);
    const QImage display(dir.filePath("display.png"));
    QCOMPARE(display.size(), p->size() * p->devicePixelRatioF());
    QVERIFY(display.size() != raw.size());
    QVERIFY(display
                .pixelColor(qRound(12 * p->devicePixelRatioF()),
                            qRound(12 * p->devicePixelRatioF()))
                .green() > 200);
    delete overlay;
    w.findChild<QAction *>("action_snapshotQuick")->trigger();
    w.findChild<QAction *>("action_snapshotQuick")->trigger();
    QTRY_COMPARE(captured.size(), 6);
    QCOMPARE(QDir(dir.filePath("Screenshots"))
                 .entryList({"*.png"}, QDir::Files)
                 .size(),
             2);
    QVERIFY(!w.captureImage(media));
    QVERIFY(w.captureImage(dir.filePath("missing/fail.png")));
    QTRY_COMPARE(captured.size(), 7);
    QVERIFY(!captured.last()[0].toBool());
    QVERIFY(p->get("pause").toBool());
    QVERIFY(std::abs(p->get("time-pos").toDouble() - 2) < .1);
    QVERIFY(p->set("pause", false));
    QTRY_VERIFY(p->get("time-pos").toDouble() > 2.2);
    results["nativeFrameSubtitlesClipboardDisplayQuickAndPlayback"] = true;
  }
  void imageFormatsQualityAtomicFailure() {
    QTemporaryDir dir;
    QImage image(320, 180, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x)
        image.setPixelColor(x, y,
                            QColor((x * 37 + y * 11) % 256,
                                   (x * 13 + y * 47) % 256,
                                   (x * 71 + y * 19) % 256));
    QString error;
    for (const auto &format : MediaExports::formats()) {
      const auto path = dir.filePath("frame." + format);
      QVERIFY2(MediaExports::writeImage(image, path, 90, &error),
               qPrintable(error));
      QCOMPARE(QImage(path).size(), image.size());
      if (format == "png" || format == "bmp")
        QCOMPARE(QImage(path).convertToFormat(QImage::Format_RGB32), image);
    }
    QVERIFY(
        MediaExports::writeImage(image, dir.filePath("low.jpg"), 10, &error));
    QVERIFY(
        MediaExports::writeImage(image, dir.filePath("high.jpg"), 95, &error));
    QVERIFY(bytes(dir.filePath("high.jpg")).size() >
            bytes(dir.filePath("low.jpg")).size());
    const auto bad = dir.filePath("keep.xyz");
    QFile file(bad);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("keep");
    file.close();
    QVERIFY(!MediaExports::writeImage(image, bad, 90, &error));
    QCOMPARE(bytes(bad), QByteArray("keep"));
    results["imageFormatsQualityAndAtomicFailure"] = true;
  }
  void propertiesReportAndSettingsDialogs() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    setup(profile);
    Window w(profile);
    load(w);
    auto *p = w.findChild<Player *>();
    const auto props = w.mediaProperties();
    QCOMPARE(props.value("source").toString(), media);
    QVERIFY(props.value("duration").toDouble() > 0);
    QVERIFY(props.value("video-codec-name")
                .toString()
                .contains("h264", Qt::CaseInsensitive));
    QVERIFY(props.value("track-list").toList().size() >= 3);
    QVERIFY(props.value("video-params").toMap().value("w").toInt() == 320);
    auto *action = w.findChild<QAction *>("action_mediaProperties");
    action->trigger();
    auto *d = w.findChild<QDialog *>("mediaPropertiesDialog");
    QVERIFY(d && d->isVisible());
    QCOMPARE(w.findChildren<QDialog *>("mediaPropertiesDialog").size(), 1);
    action->trigger();
    QCOMPARE(w.findChildren<QDialog *>("mediaPropertiesDialog").size(), 1);
    auto *json = d->findChild<QPlainTextEdit *>("mediaPropertiesJson");
    QCOMPARE(QJsonDocument::fromJson(json->toPlainText().toUtf8())
                 .object()
                 .value("source")
                 .toString(),
             media);
    auto *tracks = d->findChild<QTableWidget *>("mediaPropertiesTracks");
    QVERIFY(tracks->rowCount() >= 3);
    const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!out.isEmpty())
      QVERIFY(d->grab().save(out + "/media-properties.png"));
    d->findChild<QPushButton *>("mediaPropertiesProbe")->click();
    auto *report = d->findChild<QPlainTextEdit *>("mediaPropertiesReport");
    QTRY_VERIFY_WITH_TIMEOUT(report->toPlainText().contains("codec_name") ||
                                 report->toPlainText().contains("Format"),
                             12000);
    QVERIFY(p->get("pause").toBool());
    d->close();
    QTest::qWait(20);
    settings(w, [&](QDialog *d) {
      d->findChild<QSpinBox *>("capture/quality")->setValue(17);
      d->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Cancel)
          ->click();
    });
    QVERIFY(!profile.settings.contains("capture/quality"));
    settings(w, [&](QDialog *d) {
      auto *f = d->findChild<QComboBox *>("capture/format");
      f->setCurrentIndex(f->findData("jpg"));
      d->findChild<QSpinBox *>("capture/quality")->setValue(76);
      d->findChild<QSpinBox *>("capture/columns")->setValue(3);
      d->findChild<QSpinBox *>("capture/rows")->setValue(2);
      if (!out.isEmpty())
        QVERIFY(d->grab().save(out + "/capture-settings.png"));
      d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    QCOMPARE(profile.settings.value("capture/quality").toInt(), 76);
    QCOMPARE(profile.settings.value("capture/format").toString(),
             QString("jpg"));
    settings(w, [&](QDialog *d) {
      d->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::RestoreDefaults)
          ->click();
      QCOMPARE(d->findChild<QSpinBox *>("capture/quality")->value(), 90);
      d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    QMap<QString, QString> keys;
    for (auto *a : w.findChildren<QAction *>()) {
      const auto key = a->shortcut().toString();
      if (!a->objectName().startsWith("action_") || key.isEmpty())
        continue;
      QVERIFY2(!keys.contains(key),
               qPrintable(key + " " + a->objectName() + " " + keys.value(key)));
      keys[key] = a->objectName();
    }
    results["propertiesNativeTracksReportSettingsCancelResetShortcuts"] = true;
  }
  void thumbnailSheetAndCancellation() {
    ThumbnailSheet sheet;
    QSignalSpy ready(&sheet, &ThumbnailSheet::ready),
        failed(&sheet, &ThumbnailSheet::failed);
    QVERIFY(!sheet.start(media, 0, 2, 2, 160));
    QVERIFY(!sheet.start(media, 8, 7, 2, 160));
    QVERIFY(sheet.start(media, 8, 2, 2, 160, 0));
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
    QCOMPARE(failed.size(), 0);
    const auto image = qvariant_cast<QImage>(ready.last()[0]);
    QCOMPARE(image.size(), QSize(368, 330));
    QVERIFY(image.pixelColor(30, 90) != QColor(32, 36, 40));
    const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!out.isEmpty())
      QVERIFY(image.save(out + "/thumbnail-sheet.png"));
    QVERIFY(sheet.start(media, 8, 4, 4, 240));
    sheet.cancel();
    QTest::qWait(300);
    QCOMPARE(ready.size(), 1);
    QVERIFY(sheet.start(media, 8, 1, 1, 160, 999));
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 6000);
    results["sheetGridSelectedStreamCancelFailure"] = true;
  }
  void menuSheetAndCopyPreservePlayback() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    setup(profile);
    profile.settings.setValue("capture/columns", 2);
    profile.settings.setValue("capture/rows", 2);
    profile.settings.setValue("capture/thumbWidth", 160);
    Window w(profile);
    load(w);
    auto *p = w.findChild<Player *>();
    const auto choose = [&](const QString &id, const QString &path) {
      QTimer::singleShot(80, &w, [&w, path] {
        auto *dialog = w.findChild<QFileDialog *>();
        QVERIFY(dialog);
        dialog->setDirectory(QFileInfo(path).absolutePath());
        auto *name = dialog->findChild<QLineEdit *>("fileNameEdit");
        QVERIFY(name);
        name->setText(QFileInfo(path).fileName());
        qInfo() << "Export file selection:" << dialog->selectedFiles();
        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
      });
      w.findChild<QAction *>("action_" + id)->trigger();
    };
    choose("thumbnailSheet", dir.filePath("sheet.png"));
    if (auto *job = w.findChild<ThumbnailSheet *>())
      connect(job, &ThumbnailSheet::failed, &w, [](const QString &error) {
        qWarning() << "Sheet failed:" << error;
      });
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("sheet.png")),
                             15000);
    QCOMPARE(QImage(dir.filePath("sheet.png")).size(), QSize(368, 330));
    choose("saveMediaCopy", dir.filePath("copy.mkv"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("copy.mkv")), 6000);
    QCOMPARE(bytes(dir.filePath("copy.mkv")), bytes(media));
    QVERIFY(p->get("pause").toBool());
    QVERIFY(std::abs(p->get("time-pos").toDouble() - 2) < .1);
    QTimer::singleShot(80, &w, [&] {
      auto *dialog = w.findChild<QFileDialog *>();
      QVERIFY(dialog);
      w.openFiles({QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                       .filePath(QString::fromUtf8("Русский H264.mp4"))});
      dialog->setDirectory(dir.path());
      dialog->findChild<QLineEdit *>("fileNameEdit")
          ->setText("original-copy.mkv");
      QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    });
    w.findChild<QAction *>("action_saveMediaCopy")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo::exists(dir.filePath("original-copy.mkv")), 6000);
    QCOMPARE(bytes(dir.filePath("original-copy.mkv")), bytes(media));
    results["sheetAndCopyMenuDialogsAndPause"] = true;
  }
  void copyBytesCancelAndSourceGuard() {
    QTemporaryDir dir;
    const auto source = dir.filePath("large.bin"),
               target = dir.filePath("copy.bin");
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray block(1024 * 1024, 'x');
    for (int i = 0; i < 16; ++i)
      QCOMPARE(file.write(block), block.size());
    file.close();
    MediaCopy copy;
    QSignalSpy finished(&copy, &MediaCopy::finished);
    QVERIFY(!copy.start(source, source));
    QVERIFY(copy.start(source, target));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 6000);
    QVERIFY(finished.last()[0].toBool());
    QCOMPARE(bytes(source), bytes(target));
    QFile old(target);
    QVERIFY(old.open(QIODevice::WriteOnly | QIODevice::Truncate));
    old.write("keep");
    old.close();
    auto connection = connect(&copy, &MediaCopy::progress, &copy,
                              [&](qint64, qint64) { copy.cancel(); });
    QVERIFY(copy.start(source, target));
    QTRY_COMPARE(finished.size(), 2);
    QVERIFY(!finished.last()[0].toBool());
    QCOMPARE(bytes(target), QByteArray("keep"));
    disconnect(connection);
    connection =
        connect(&copy, &MediaCopy::progress, &copy, [&](qint64, qint64) {
          QFile changing(source);
          QVERIFY(changing.open(QIODevice::WriteOnly | QIODevice::Append));
          changing.write("changed");
          disconnect(connection);
        });
    QVERIFY(copy.start(source, target));
    QTRY_COMPARE(finished.size(), 3);
    QVERIFY(!finished.last()[0].toBool());
    QCOMPARE(bytes(target), QByteArray("keep"));
    QVERIFY(!copy.start(source, dir.filePath("missing/copy.bin")));
    results["byteCopyAtomicCancelAndChangedSourceGuard"] = true;
  }
  void cleanupTestCase() {
    const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!out.isEmpty()) {
      QFile f(out + "/media-exports-measurements.json");
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QJsonDocument(results).toJson());
    }
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication app(argc, argv);
  MediaExportsTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "mediaexports_test.moc"
