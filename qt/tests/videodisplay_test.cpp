#include "player.h"
#include "profile.h"
#include "videodisplay.h"
#include "window.h"
#include <QColorSpace>
#include <QOpenGLWidget>
#include <QtTest>
#include <QtWidgets>
#include <cmath>
#include <limits>

class VideoDisplayTest : public QObject {
  Q_OBJECT
  QTemporaryDir fixtures;
  QString icc, invalid, media;
  QByteArray bytes;
  QJsonObject measurements;
  void write(const QString &file, const QByteArray &data) {
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(data), data.size());
  }
  void editDialog(Window &w, const std::function<void(QDialog *)> &edit,
                  const QString &action = "videoOutputSettings") {
    QTimer::singleShot(80, &w, [&w, edit] {
      for (auto *d : w.findChildren<QDialog *>())
        if (d->isVisible() && d->isModal()) {
          edit(d);
          return;
        }
      QFAIL("No active preferences dialog");
    });
    auto *a = w.findChild<QAction *>("action_" + action);
    QVERIFY(a);
    a->trigger();
  }
private slots:
  void initTestCase() {
    QVERIFY(fixtures.isValid());
    bytes = QColorSpace(QColorSpace::SRgb).iccProfile();
    QVERIFY(bytes.size() > 132);
    icc = fixtures.filePath("display.icc");
    invalid = fixtures.filePath("invalid.icc");
    write(icc, bytes);
    write(invalid, "invalid ICC");
    media = fixtures.filePath("av.mkv");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg",
                 {"-y", "-nostdin", "-v", "error", "-f", "lavfi", "-i",
                  "testsrc2=s=320x180:r=25:d=10", "-f", "lavfi", "-i",
                  "sine=frequency=997:sample_rate=48000:duration=10", "-c:v",
                  "ffv1", "-c:a", "pcm_s16le", media});
    QVERIFY(ffmpeg.waitForFinished(15000));
    QVERIFY2(ffmpeg.exitCode() == 0, ffmpeg.readAllStandardError().constData());
  }
  void defaultsSanitizationAndSafetyPolicy() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    QCOMPARE(VideoDisplay::values(s), VideoDisplay::defaults());
    s.setValue("video/syncMode", "display-desync");
    s.setValue("video/vsync", "evil");
    s.setValue("video/temporalScaler", "evil");
    s.setValue("video/displayFps", std::numeric_limits<double>::quiet_NaN());
    s.setValue("video/maxAudioChange", 99);
    s.setValue("video/maxVideoChange", -10);
    s.setValue("video/iccIntent", 99);
    auto v = VideoDisplay::values(s);
    QCOMPARE(v.value("syncMode").toString(), QString("audio"));
    QCOMPARE(v.value("vsync").toString(), QString("on"));
    QCOMPARE(v.value("temporalScaler").toString(), QString("default"));
    QCOMPARE(v.value("displayFps").toDouble(), 0.);
    QCOMPARE(v.value("maxAudioChange").toDouble(), 1.);
    QCOMPARE(v.value("maxVideoChange").toDouble(), 0.);
    QCOMPARE(v.value("iccIntent").toInt(), 3);
    for (const auto &mode :
         {"display-resample", "display-tempo", "display-vdrop"}) {
      v["syncMode"] = mode;
      QCOMPARE(VideoDisplay::effectiveSync(v, true, 1, false, 59.94, true),
               QString(mode));
      QCOMPARE(VideoDisplay::effectiveSync(v, false, 1, false, 59.94, true),
               QString("audio"));
      QCOMPARE(VideoDisplay::effectiveSync(v, true, 0, false, 59.94, true),
               QString("audio"));
      QCOMPARE(VideoDisplay::effectiveSync(v, true, 1, true, 59.94, true),
               QString("audio"));
      QCOMPARE(VideoDisplay::effectiveSync(v, true, 1, false, 0, true),
               QString("audio"));
      QCOMPARE(VideoDisplay::effectiveSync(v, true, 1, false, 59.94, false),
               QString("audio"));
    }
  }
  void iccImportValidationDedupAndPortableMove() {
    QVERIFY(VideoDisplay::validateIcc(bytes).isEmpty());
    QVERIFY(!VideoDisplay::validateIcc(bytes.left(128)).isEmpty());
    auto forged = bytes;
    forged.replace(12, 4, "prtr");
    QVERIFY(!VideoDisplay::validateIcc(forged).isEmpty());
    QVERIFY(!VideoDisplay::validateIcc(QByteArray(16 * 1024 * 1024 + 1, 'x'))
                 .isEmpty());
    QTemporaryDir dir, moved;
    QString error;
    const auto saved = VideoDisplay::importIcc(icc, dir.path(), &error);
    QVERIFY2(!saved.isEmpty(), qPrintable(error));
    QVERIFY(saved.startsWith("ColorProfiles/") && !QDir::isAbsolutePath(saved));
    QCOMPARE(VideoDisplay::readIcc(VideoDisplay::resolveIcc(saved, dir.path()),
                                   &error),
             bytes);
    QCOMPARE(VideoDisplay::importIcc(icc, dir.path(), &error), saved);
    QCOMPARE(QDir(dir.filePath("ColorProfiles")).entryList(QDir::Files).size(),
             1);
    QVERIFY(QDir(moved.path()).mkpath("ColorProfiles"));
    QVERIFY(QFile::copy(dir.filePath(saved), moved.filePath(saved)));
    QCOMPARE(VideoDisplay::readIcc(
                 VideoDisplay::resolveIcc(saved, moved.path()), &error),
             bytes);
    QVERIFY(VideoDisplay::importIcc(invalid, dir.path(), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QCOMPARE(VideoDisplay::readIcc(dir.filePath(saved), &error), bytes);
    const auto blockedDirectory = fixtures.filePath("not-a-directory");
    write(blockedDirectory, "file");
    QVERIFY(VideoDisplay::importIcc(icc, blockedDirectory, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    measurements["iccBytes"] = bytes.size();
    measurements["portableIccMoveVerified"] = true;
  }
  void liveFallbackPauseSeekAndAvClock() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    QString error;
    const auto saved = VideoDisplay::importIcc(icc, dir.path(), &error);
    s.setValue("video/iccProfile", saved);
    s.setValue("video/iccEnabled", true);
    s.setValue("video/syncMode", "display-resample");
    s.setValue("video/interpolation", true);
    s.setValue("video/displayFps", 72.);
    s.setValue("video/vsync", "off");
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"),
             {}, 0);
    QCOMPARE(p.requestedSwapInterval(), 0);
    QCOMPARE(p.get("opengl-swapinterval").toInt(), 0);
    if (auto *surface = p.findChild<QOpenGLWidget *>())
      QCOMPARE(surface->format().swapInterval(), 0);
    p.resize(480, 270);
    p.show();
    p.set("mute", true);
    p.set("hwdec", "no");
    QVERIFY(VideoDisplay::apply(&p, s));
    QCOMPARE(p.get("icc-profile").toString(), dir.filePath(saved));
    QVERIFY(!p.get("icc-cache").toBool());
    QVERIFY(!p.get("icc-profile-auto").toBool());
    for (int intent = 0; intent <= 3; ++intent) {
      s.setValue("video/iccIntent", intent);
      QVERIFY(VideoDisplay::apply(&p, s));
      QCOMPARE(p.get("icc-intent").toInt(), intent);
    }
    // Property acceptance is separate from the policy and physical VBlank
    // testing.
    for (const auto &mode :
         {"display-resample", "display-tempo", "display-vdrop"}) {
      QVERIFY(p.set("video-sync", mode));
      QCOMPARE(p.get("video-sync").toString(), QString(mode));
      QVERIFY(VideoDisplay::apply(&p, s));
      QCOMPARE(p.get("video-sync").toString(), QString("audio"));
    }
    s.setValue("video/maxVideoChange", 2.5);
    s.setValue("video/maxAudioChange", .25);
    QVERIFY(VideoDisplay::apply(&p, s));
    QCOMPARE(p.get("video-sync-max-video-change").toDouble(), 2.5);
    QCOMPARE(p.get("video-sync-max-audio-change").toDouble(), .25);
    const auto originalScaler = p.get("tscale");
    for (const auto &kernel : {"oversample", "linear", "mitchell"}) {
      s.setValue("video/temporalScaler", kernel);
      QVERIFY(VideoDisplay::apply(&p, s));
      QCOMPARE(p.get("tscale").toString(), QString(kernel));
    }
    s.remove("video/temporalScaler");
    QVERIFY(VideoDisplay::apply(&p, s));
    QCOMPARE(p.get("tscale"), originalScaler);
    QSignalSpy loaded(&p, &Player::loaded);
    QVERIFY(p.loadSource(media, {{"pause", "yes"}}));
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(!p.grabFramebuffer().isNull());
    QTRY_COMPARE(p.get("video-sync").toString(), QString("audio"));
    QVERIFY(!p.get("interpolation").toBool());
    QCOMPARE(p.get("display-fps-override").toDouble(), 0.);
    QCOMPARE(p.QObject::property("foxicmpRenderInterval").toInt(), 16);
    QVERIFY(p.get("pause").toBool());
    p.set("pause", false);
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > .3, 4000);
    double maxAv = 0;
    int samples = 0;
    for (int i = 0; i < 20; ++i) {
      QTest::qWait(75);
      const auto av = p.get("avsync");
      if (av.isValid()) {
        ++samples;
        maxAv = std::max(maxAv, std::abs(av.toDouble()));
      }
    }
    QVERIFY(samples >= 5);
    QVERIFY2(maxAv < .15, qPrintable(QString::number(maxAv)));
    measurements["softwareFallbackAvSamples"] = samples;
    measurements["softwareFallbackMaxNativeAvSeconds"] = maxAv;
    p.set("pause", true);
    const auto pos = p.get("time-pos").toDouble();
    s.setValue("video/syncMode", "display-tempo");
    QVERIFY(VideoDisplay::apply(&p, s));
    QTest::qWait(180);
    QVERIFY(std::abs(p.get("time-pos").toDouble() - pos) < .1);
    QVERIFY(p.command({"seek", "4", "absolute+exact"}));
    QTRY_VERIFY(p.get("time-pos").toDouble() > 3.9 &&
                p.get("time-pos").toDouble() < 4.2);
    s.setValue("video/iccProfile", invalid);
    QVERIFY(!VideoDisplay::apply(&p, s));
    QCOMPARE(p.get("icc-profile").toString(), QString());
    QVERIFY(!p.QObject::property("foxicmpIccStatus").toString().isEmpty());
    s.clear();
    QVERIFY(VideoDisplay::apply(&p, s));
    QCOMPARE(p.get("video-sync").toString(), QString("audio"));
    QVERIFY(p.get("pause").toBool());
    p.set("pause", false);
    QTRY_VERIFY(p.get("time-pos").toDouble() > 4.3);
  }
  void dialogCancelApplyIndependentResetsAndScreenshots() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    Window w(profile);
    w.show();
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded);
    w.openFiles({media});
    QTRY_COMPARE(loaded.size(), 1);
    p->set("pause", true);
    const auto fill = [&](QDialog *d) {
      d->findChild<QCheckBox *>("video/iccEnabled")->setChecked(true);
      d->findChild<QLineEdit *>("video/iccProfile")->setText(icc);
      auto *mode = d->findChild<QComboBox *>("video/syncMode");
      mode->setCurrentIndex(mode->findData("display-resample"));
      auto *vsync = d->findChild<QComboBox *>("video/vsync");
      vsync->setCurrentIndex(vsync->findData("off"));
      d->findChild<QDoubleSpinBox *>("video/displayFps")->setValue(72.);
      d->findChild<QCheckBox *>("video/interpolation")->setChecked(true);
    };
    editDialog(w, [&](QDialog *d) {
      fill(d);
      d->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Cancel)
          ->click();
    });
    QVERIFY(!profile.settings.contains("video/iccEnabled"));
    QVERIFY(!QDir(dir.filePath("ColorProfiles")).exists());
    editDialog(w, [&](QDialog *d) {
      fill(d);
      d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    const auto saved = profile.settings.value("video/iccProfile").toString();
    QVERIFY(saved.startsWith("ColorProfiles/"));
    QVERIFY(QFile::exists(dir.filePath(saved)));
    QCOMPARE(p->get("icc-profile").toString(), dir.filePath(saved));
    QCOMPARE(profile.settings.value("video/syncMode").toString(),
             QString("display-resample"));
    QCOMPARE(p->get("video-sync").toString(), QString("audio"));
    QCOMPARE(p->requestedSwapInterval(),
             1); // Saved off takes effect only after restart.
    QVERIFY(p->get("pause").toBool());
    editDialog(w, [&](QDialog *d) {
      const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      if (!out.isEmpty()) {
        auto *tabs = d->findChild<QTabWidget *>();
        for (const auto &entry : QList<QPair<QString, QString>>{
                 {"videoIccPage", "video-display-icc.png"},
                 {"videoSyncPage", "video-display-sync.png"}}) {
          tabs->setCurrentWidget(d->findChild<QWidget *>(entry.first));
          QTest::qWait(40);
          QVERIFY(d->grab().save(out + "/" + entry.second));
        }
      }
      d->findChild<QPushButton *>("videoIccReset")->click();
      QCOMPARE(
          d->findChild<QComboBox *>("video/syncMode")->currentData().toString(),
          QString("display-resample"));
      d->findChild<QPushButton *>("videoSyncReset")->click();
      QVERIFY(!d->findChild<QCheckBox *>("video/iccEnabled")->isChecked());
      d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    QCOMPARE(VideoDisplay::values(profile.settings), VideoDisplay::defaults());
    QVERIFY(QFile::exists(
        dir.filePath(saved))); // Reset never deletes imported profiles.
    QVERIFY(p->get("pause").toBool());
  }
  void validationInEveryPreferencesDialog() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    Window w(profile);
    w.show();
    for (const auto &action : {"videoOutputSettings", "preferences", "image"}) {
      editDialog(
          w,
          [&](QDialog *d) {
            d->findChild<QCheckBox *>("video/iccEnabled")->setChecked(true);
            d->findChild<QLineEdit *>("video/iccProfile")->setText(invalid);
            d->findChild<QDialogButtonBox *>()
                ->button(QDialogButtonBox::Ok)
                ->click();
            QVERIFY(d->isVisible());
            QVERIFY(!d->findChild<QLabel *>("videoIccError")->text().isEmpty());
            d->findChild<QDialogButtonBox *>()
                ->button(QDialogButtonBox::Cancel)
                ->click();
          },
          action);
      QVERIFY(!profile.settings.contains("video/iccEnabled"));
    }
    QVERIFY(!QDir(dir.filePath("ColorProfiles")).exists());
  }
  void cleanupTestCase() {
    const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!out.isEmpty()) {
      QSaveFile f(out + "/video-display-measurements.json");
      QVERIFY(f.open(QIODevice::WriteOnly));
      const auto data = QJsonDocument(measurements).toJson();
      QCOMPARE(f.write(data), data.size());
      QVERIFY(f.commit());
    }
  }
};
QTEST_MAIN(VideoDisplayTest)
#include "videodisplay_test.moc"
