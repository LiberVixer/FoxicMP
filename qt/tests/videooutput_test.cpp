#include "preferencesdriver.h"
#include "mediaoptions.h"
#include "player.h"
#include "profile.h"
#include "videooutput.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>

class VideoOutputTest : public QObject {
  Q_OBJECT
  QTemporaryDir fixtures;
  QString source, interlaced, gradient, colors;
  QJsonObject measurements;
  void ffmpeg(const QStringList &args) {
    QProcess p;
    p.start("ffmpeg", QStringList{"-y", "-nostdin", "-v", "error"} + args);
    QVERIFY(p.waitForFinished(15000));
    QVERIFY2(p.exitCode() == 0, p.readAllStandardError().constData());
  }
  QImage frame(Player &p) {
    QTest::qWait(120);
    return p.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
  }
  int different(const QImage &a, const QImage &b) {
    if (a.size() != b.size())
      return -1;
    int n = 0;
    for (int y = 0; y < a.height(); ++y)
      for (int x = 0; x < a.width(); ++x)
        n += a.pixel(x, y) != b.pixel(x, y);
    return n;
  }
  QString option(Player &p, const char *name) {
    const auto value = p.get(name);
    if (value.typeId() == QMetaType::Bool)
      return value.toBool() ? "yes" : "no";
    return value.typeId() == QMetaType::QVariantList ||
                   value.typeId() == QMetaType::QStringList
               ? value.toStringList().join(',')
               : value.toString();
  }
  int midtones(const QImage &a) {
    int n = 0;
    for (int y = 20; y < a.height() - 20; ++y)
      for (int x = 20; x < a.width() - 20; ++x) {
        const int c = qRed(a.pixel(x, y));
        n += c > 40 && c < 215;
      }
    return n;
  }
private slots:
  void initTestCase() {
    QVERIFY(fixtures.isValid());
    QImage checker(64, 48, QImage::Format_RGB32);
    for (int y = 0; y < 48; ++y)
      for (int x = 0; x < 64; ++x)
        checker.setPixelColor(x, y,
                              (x / 3 + y / 3) % 2 ? Qt::white : Qt::black);
    const auto png = fixtures.filePath("checker.png");
    QVERIFY(checker.save(png));
    source = fixtures.filePath("checker.mkv");
    interlaced = fixtures.filePath("interlaced.mkv");
    ffmpeg({"-loop", "1", "-i", png, "-t", "8", "-r", "25", "-c:v", "ffv1",
            "-pix_fmt", "yuv444p", source});
    ffmpeg({"-f", "lavfi", "-i", "testsrc2=s=320x240:r=50:d=8", "-vf",
            "tinterlace=interleave_top", "-c:v", "ffv1", "-flags",
            "+ildct+ilme", interlaced});
    gradient = fixtures.filePath("gradient.mkv");
    ffmpeg({"-f", "lavfi", "-i",
            "nullsrc=s=1024x64:r=25:d=8,format=yuv444p10le,geq=lum='64+876*X/"
            "(W-1)':cb=512:cr=512",
            "-c:v", "ffv1", "-color_range", "tv", gradient});
    QImage patches(320, 240, QImage::Format_RGB32);
    for (int y = 0; y < patches.height(); ++y)
      for (int x = 0; x < patches.width(); ++x)
        patches.setPixelColor(x, y,
                              x < 100   ? QColor(210, 40, 40)
                              : x < 220 ? QColor(40, 60, 210)
                                        : QColor(70, 70, 70));
    const auto pngColors = fixtures.filePath("colors.png");
    QVERIFY(patches.save(pngColors));
    colors = fixtures.filePath("colors.mkv");
    ffmpeg({"-loop", "1", "-i", pngColors, "-t", "8", "-r", "25", "-c:v",
            "ffv1", "-pix_fmt", "yuv444p", colors});
  }
  void highPrecisionSoftwareFrame() {
    QTemporaryDir dir;
    QSettings settings(dir.filePath("profile.ini"), QSettings::IniFormat);
    settings.setValue("playback/hardwareDecoding", false);
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(1024, 64);
    p.show();
    QVERIFY(VideoOutput::apply(&p, settings));
    QSignalSpy loaded(&p, &Player::loaded);
    QVERIFY(p.loadSource(gradient, {{"pause", "yes"}}));
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!frame(p).isNull(), 5000);
    const auto eight =
        p.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
    QCOMPARE(eight.depth(), 32);
    QSet<int> levels8, levels16;
    for (int x = 0; x < eight.width(); ++x)
      levels8.insert(qRed(eight.pixel(x, eight.height() / 2)));
    settings.setValue("video/softwarePrecision", "16");
    QVERIFY(VideoOutput::apply(&p, settings));
    const auto sixteen = p.grabFramebuffer();
    QCOMPARE(sixteen.format(), QImage::Format_RGBX64);
    const auto *line = reinterpret_cast<const QRgba64 *>(
        sixteen.constScanLine(sixteen.height() / 2));
    for (int x = 0; x < sixteen.width(); ++x)
      levels16.insert(line[x].red());
    QVERIFY2(levels16.size() > 700,
             qPrintable(QString::number(levels16.size())));
    QVERIFY(levels16.size() > levels8.size() * 2);
    QVERIFY(p.get("pause").toBool());
    measurements["gradient8BitLevels"] = levels8.size();
    measurements["gradient16BitLevels"] = levels16.size();
    // Native subtitle blending must work with the new 64-bit destination too.
    QFile subtitle(dir.filePath("precision.srt"));
    QVERIFY(subtitle.open(QIODevice::WriteOnly));
    subtitle.write(
        "1\n00:00:00,000 --> 00:00:08,000\nHIGH PRECISION SUBTITLE\n");
    subtitle.close();
    QVERIFY(p.set("sub-font-size", 100));
    QVERIFY(p.set("sub-color", "#ffff00"));
    QVERIFY(p.command({"sub-add", subtitle.fileName(), "select"}));
    QImage subtitled;
    const auto plain = sixteen.convertToFormat(QImage::Format_RGB32);
    QTRY_VERIFY_WITH_TIMEOUT((subtitled = frame(p)).size() == plain.size() &&
                                 different(subtitled, plain) > 20,
                             5000);
    measurements["highPrecisionSubtitleChangedPixels"] =
        different(subtitled, plain);
    QVERIFY(p.get("pause").toBool());
    QVERIFY(p.set("sid", "no"));
    settings.setValue("video/softwarePrecision", "8");
    QVERIFY(VideoOutput::apply(&p, settings));
    QTRY_COMPARE_WITH_TIMEOUT(different(frame(p), eight), 0, 5000);
    p.set("pause", false);
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > .1, 3000);
  }
  void hueGammaPixelsPreviewAndReset() {
    QTemporaryDir dir;
    QSettings settings(dir.filePath("profile.ini"), QSettings::IniFormat);
    settings.setValue("playback/hardwareDecoding", false);
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(320, 240);
    p.show();
    QVERIFY(MediaOptions::colorFilter(settings).isEmpty());
    MediaOptions::apply(&p, settings);
    QSignalSpy loaded(&p, &Player::loaded);
    QVERIFY(p.loadSource(colors, {{"pause", "yes"}}));
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!frame(p).isNull(), 5000);
    const auto before = frame(p);
    settings.setValue("video/hue", 90);
    settings.setValue("video/gamma", 100);
    MediaOptions::apply(&p, settings);
    p.command({"seek", "0", "absolute+exact"});
    QImage after;
    QTRY_VERIFY_WITH_TIMEOUT((after = frame(p)).size() == before.size() &&
                                 different(before, after) > 10000,
                             5000);
    QVERIFY(p.get("pause").toBool());
    // A neutral grey patch brightens with gamma 2, and the red patch changes
    // hue.
    QVERIFY(qRed(after.pixel(260, 120)) > qRed(before.pixel(260, 120)) + 30);
    QVERIFY(after.pixelColor(40, 120).green() >
            before.pixelColor(40, 120).green() + 30);
    const auto preview = dir.filePath("preview.png");
    ffmpeg({"-i", colors, "-vf", MediaOptions::previewFilters(settings),
            "-frames:v", "1", preview});
    const QImage reference(preview);
    QVERIFY(!reference.isNull());
    int maxError = 0;
    for (int x : {40, 160, 260})
      for (auto channel : {0, 1, 2}) {
        const auto a = after.pixelColor(x, 120),
                   b = reference.pixelColor(x, 120);
        const int av[] = {a.red(), a.green(), a.blue()},
                  bv[] = {b.red(), b.green(), b.blue()};
        maxError = std::max(maxError, std::abs(av[channel] - bv[channel]));
      }
    QVERIFY2(maxError <= 4, qPrintable(QString::number(maxError)));
    measurements["hueGammaChangedPixels"] = different(before, after);
    measurements["previewColorMaxChannelError"] = maxError;
    settings.remove("video/hue");
    settings.remove("video/gamma");
    MediaOptions::apply(&p, settings);
    p.command({"seek", "0", "absolute+exact"});
    QTRY_COMPARE_WITH_TIMEOUT(different(frame(p), before), 0, 5000);
    QVERIFY(MediaOptions::colorFilter(settings).isEmpty());
  }
  void nativeDefaultsSanitizationAndReset() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    QCOMPARE(VideoOutput::values(s), VideoOutput::defaults());
    Player p;
    const auto sw = p.get("sws-scaler"), zimg = p.get("sws-allow-zimg"),
               scale = p.get("scale"), down = p.get("dscale"),
               chroma = p.get("cscale"), codecs = p.get("hwdec-codecs");
    QVERIFY(VideoOutput::apply(&p, s));
    QCOMPARE(p.get("sws-scaler"), sw);
    QCOMPARE(p.get("scale"), scale);
    QCOMPARE(p.get("hwdec-codecs"), codecs);
    const QList<QPair<QString, QString>> colorOptions{
        {"outputLevels", "video-output-levels"},
        {"fboFormat", "fbo-format"},
        {"ditherDepth", "dither-depth"},
        {"targetPrim", "target-prim"},
        {"targetTrc", "target-trc"},
        {"toneMapping", "tone-mapping"}};
    const QList<QStringList> choices{
        {"full", "limited"},
        {"rgba8", "rgb10_a2", "rgba16", "rgba16f", "rgba32f"},
        {"no", "8", "10", "12", "16"},
        {"bt.709", "bt.2020", "dci-p3", "adobe"},
        {"srgb", "bt.1886", "gamma2.2", "gamma2.4"},
        {"clip", "mobius", "reinhard", "hable", "bt.2390"}};
    for (int i = 0; i < colorOptions.size(); ++i) {
      const auto entry = colorOptions[i];
      const auto original = p.get(entry.second.toUtf8());
      for (const auto &value : choices[i]) {
        s.setValue("video/" + entry.first, value);
        QVERIFY2(VideoOutput::apply(&p, s),
                 qPrintable(entry.first + ": " + value));
        const auto actual = option(p, entry.second.toUtf8());
        QCOMPARE(actual, value);
      }
      s.remove("video/" + entry.first);
      QVERIFY(VideoOutput::apply(&p, s));
      QCOMPARE(p.get(entry.second.toUtf8()), original);
    }
    s.setValue("video/softwareScaler", "point");
    s.setValue("video/upscaler", "lanczos");
    s.setValue("video/downscaler", "bilinear");
    s.setValue("video/chromaScaler", "spline36");
    QVERIFY(VideoOutput::apply(&p, s));
    QCOMPARE(p.get("sws-scaler").toString(), QString("point"));
    QVERIFY(!p.get("sws-allow-zimg").toBool());
    s.clear();
    QVERIFY(VideoOutput::apply(&p, s));
    QCOMPARE(p.get("sws-scaler"), sw);
    QCOMPARE(p.get("sws-allow-zimg"), zimg);
    QCOMPARE(p.get("scale"), scale);
    QCOMPARE(p.get("dscale"), down);
    QCOMPARE(p.get("cscale"), chroma);
    s.setValue("video/softwarePrecision", "10");
    s.setValue("video/targetTrc", "pq");
    s.setValue("video/softwareScaler", "invalid");
    s.setValue("video/upscaler", "evil");
    s.setValue("video/decoderThreads", 99);
    s.setValue("video/hardwareFallbackFrames", 0);
    s.setValue("video/hardwareCodecs", QStringList{"hevc", "evil", "hevc"});
    auto v = VideoOutput::values(s);
    QCOMPARE(v.value("softwarePrecision").toString(), QString("8"));
    QCOMPARE(v.value("targetTrc").toString(), QString("default"));
    QCOMPARE(v.value("softwareScaler").toString(), QString("default"));
    QCOMPARE(v.value("upscaler").toString(), QString("default"));
    QCOMPARE(v.value("decoderThreads").toInt(), 16);
    QCOMPARE(v.value("hardwareFallbackFrames").toInt(), 1);
    QCOMPARE(v.value("hardwareCodecs").toStringList(), QStringList{"hevc"});
  }
  void softwareScalerChangesPixelsAndKeepsPause() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    s.setValue("playback/hardwareDecoding", false);
    s.setValue("video/softwareScaler", "point");
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(301, 227);
    p.show();
    p.set("mute", true);
    QVERIFY(VideoOutput::apply(&p, s));
    QSignalSpy loaded(&p, &Player::loaded);
    QVERIFY(p.loadSource(source, {{"pause", "yes"}}));
    QTRY_COMPARE(loaded.size(), 1);
    QImage nearest;
    QTRY_VERIFY_WITH_TIMEOUT(!(nearest = frame(p)).isNull(), 5000);
    s.setValue("video/softwareScaler", "bilinear");
    QVERIFY(VideoOutput::apply(&p, s));
    QImage linear;
    QTRY_VERIFY_WITH_TIMEOUT((linear = frame(p)).size() == nearest.size() &&
                                 different(nearest, linear) > 1000,
                             5000);
    QVERIFY(p.get("pause").toBool());
    measurements["nearestBilinearDifferentPixels"] = different(nearest, linear);
    measurements["nearestMidtones"] = midtones(nearest);
    measurements["bilinearMidtones"] = midtones(linear);
    QVERIFY(midtones(linear) > midtones(nearest) + 1000);
    s.setValue("video/softwareScaler", "point");
    QVERIFY(VideoOutput::apply(&p, s));
    QTRY_COMPARE_WITH_TIMEOUT(different(frame(p), nearest), 0, 5000);
    const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!out.isEmpty()) {
      QVERIFY(nearest.save(out + "/video-scale-nearest.png"));
      QVERIFY(linear.save(out + "/video-scale-bilinear.png"));
    }
    p.set("pause", false);
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > .1, 3000);
  }
  void decoderPolicyAndDeinterlacePlayback() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    s.setValue("video/hardwareCodecsDefault", false);
    s.setValue("video/hardwareCodecs", QStringList{});
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(320, 240);
    p.show();
    p.set("mute", true);
    QVERIFY(VideoOutput::apply(&p, s));
    QCOMPARE(option(p, "hwdec"), QString("no"));
    s.setValue("video/hardwareCodecs", QStringList{"h264", "hevc"});
    s.setValue("video/hardwareBackend", "vaapi-copy");
    s.setValue("video/decoderThreads", 2);
    QVERIFY(VideoOutput::apply(&p, s));
    QCOMPARE(option(p, "hwdec"), QString("vaapi-copy"));
    QCOMPARE(p.get("hwdec-codecs").toString(), QString("h264,hevc"));
    QCOMPARE(p.get("vd-lavc-threads").toInt(), 2);
    QSignalSpy loaded(&p, &Player::loaded);
    QVERIFY(p.loadSource(interlaced, {{"pause", "yes"}}));
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!frame(p).isNull(), 5000);
    QVERIFY(p.get("hwdec-current").toString() == "no" ||
            p.get("hwdec-current").toString().isEmpty());
    p.command({"seek", "1", "absolute+exact"});
    QTest::qWait(100);
    const auto before = frame(p);
    s.setValue("video/deinterlace", true);
    QVERIFY(VideoOutput::apply(&p, s));
    QVERIFY(p.get("deinterlace").toBool());
    p.command({"seek", "1", "absolute+exact"});
    QImage after;
    QTRY_VERIFY_WITH_TIMEOUT((after = frame(p)).size() == before.size() &&
                                 different(before, after) > 100,
                             5000);
    QVERIFY(p.get("pause").toBool());
    measurements["deinterlaceChangedPixels"] = different(before, after);
    measurements["deinterlaceEstimatedFps"] =
        p.get("estimated-vf-fps").toDouble();
    s.setValue("video/deinterlace", false);
    s.setValue("playback/hardwareDecoding", false);
    QVERIFY(VideoOutput::apply(&p, s));
    QVERIFY(!p.get("deinterlace").toBool());
    QCOMPARE(option(p, "hwdec"), QString("no"));
    p.set("pause", false);
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > 1.1, 3000);
  }
  void preferencesCancelApplyResetAndMasterSync() {
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
    w.openFiles({source});
    QTRY_COMPARE(loaded.size(), 1);
    p->set("pause", true);
    auto *action = w.findChild<QAction *>("action_videoOutputSettings");
    QVERIFY(action);
    QCOMPARE(action->shortcut(), QKeySequence("Ctrl+Alt+V"));
    for (int step = 0; step < 3; ++step) {
      QTimer::singleShot(70, &w, [&] {
        auto *d = w.findChild<QDialog *>("videoOutputDialog");
        QVERIFY(d);
        auto *tabs = d->findChild<QTabWidget *>();
        QCOMPARE(tabs->count(), 6);
        auto *precision = d->findChild<QComboBox *>("video/softwarePrecision");
        precision->setCurrentIndex(precision->findData("16"));
        auto *range = d->findChild<QComboBox *>("video/outputLevels");
        range->setCurrentIndex(range->findData("limited"));
        if (step == 2) {
          d->findChild<QPushButton *>("videoColorReset")->click();
          QCOMPARE(precision->currentData().toString(), QString("8"));
          QCOMPARE(range->currentData().toString(), QString("default"));
          QCOMPARE(d->findChild<QComboBox *>("video/softwareScaler")
                       ->currentData()
                       .toString(),
                   QString("lanczos"));
          QVERIFY(d->findChild<QCheckBox *>("video/deinterlace")->isChecked());
        }
        if (step == 1) {
          d->findChild<QPushButton *>("videoOutputReset")->click();
          QCOMPARE(precision->currentData().toString(), QString("16"));
          QCOMPARE(range->currentData().toString(), QString("limited"));
        }
        auto *sw = d->findChild<QComboBox *>("video/softwareScaler");
        sw->setCurrentIndex(sw->findData("lanczos"));
        d->findChild<QCheckBox *>("video/deinterlace")->setChecked(true);
        d->findChild<QCheckBox *>("playback/hardwareDecoding")
            ->setChecked(false);
        d->findChild<QSpinBox *>("video/decoderThreads")->setValue(2);
        if (step == 2)
          d->findChild<QPushButton *>("videoOutputReset")->click();
        const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        if (step == 1 && !out.isEmpty())
          for (int i = 0; i < 3; ++i) {
            tabs->setCurrentIndex(i);
            QTest::qWait(30);
            QVERIFY(
                d->grab().save(out + QString("/video-output-%1.png").arg(i)));
          }
        d->findChild<QDialogButtonBox *>()
            ->button(step == 0 ? QDialogButtonBox::Cancel
                               : QDialogButtonBox::Ok)
            ->click();
      });
      action->trigger();
      if (step == 0) {
        QVERIFY(!profile.settings.contains("video/softwareScaler"));
        QVERIFY(!profile.settings.contains("video/softwarePrecision"));
      }
      if (step == 1) {
        QCOMPARE(p->grabFramebuffer().depth(), 64);
        QCOMPARE(profile.settings.value("video/outputLevels").toString(),
                 QString("limited"));
        QCOMPARE(profile.settings.value("video/softwareScaler").toString(),
                 QString("lanczos"));
        QVERIFY(p->get("pause").toBool());
        QVERIFY(p->get("deinterlace").toBool());
        QCOMPARE(option(*p, "hwdec"), QString("no"));
      }
      if (step == 2) {
        QCOMPARE(p->grabFramebuffer().depth(), 32);
        QCOMPARE(p->get("video-output-levels").toString(), QString("auto"));
        QCOMPARE(profile.settings.value("video/softwareScaler").toString(),
                 QString("default"));
        QVERIFY(!p->get("deinterlace").toBool());
        QVERIFY(p->get("pause").toBool());
      }
    }
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog");
      QVERIFY(d);
      auto boxes = d->findChildren<QCheckBox *>("playback/hardwareDecoding");
      QCOMPARE(boxes.size(), 2);
      boxes.first()->setChecked(false);
      QVERIFY(!boxes.last()->isChecked());
      boxes.last()->setChecked(true);
      QVERIFY(boxes.first()->isChecked());
      d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    w.findChild<QAction *>("action_preferences")->trigger();
    QVERIFY(profile.settings.value("playback/hardwareDecoding").toBool());

    for (int step = 0; step < 3; ++step) {
      afterPreferencesReady(&w, [&] {
        auto *d = w.findChild<QDialog *>("preferencesDialog");
        QVERIFY(d);
        auto *hue = d->findChild<QSpinBox *>("video/hue");
        auto *gamma = d->findChild<QSpinBox *>("video/gamma");
        QVERIFY(hue && gamma);
        hue->setValue(90);
        gamma->setValue(100);
        d->findChild<QSpinBox *>("video/brightness")->setValue(15);
        if (step == 2) {
          auto *resetImage = d->findChild<QPushButton *>("classicColorReset");
          QVERIFY(resetImage);
          resetImage->click();
          for (const auto &key :
               {"hue", "gamma", "brightness", "contrast", "saturation"})
            QCOMPARE(d->findChild<QSpinBox *>(QString("video/") + key)->value(),
                     0);
        }
        d->findChild<QDialogButtonBox *>()
            ->button(step == 0 ? QDialogButtonBox::Cancel
                               : QDialogButtonBox::Ok)
            ->click();
      });
      w.findChild<QAction *>("action_preferences")->trigger();
      if (step == 0) {
        QCOMPARE(profile.settings.value("video/hue").toInt(), 0);
        QCOMPARE(profile.settings.value("video/gamma").toInt(), 0);
      } else {
        QCOMPARE(profile.settings.value("video/hue").toInt(),
                 step == 1 ? 90 : 0);
        QCOMPARE(profile.settings.value("video/gamma").toInt(),
                 step == 1 ? 100 : 0);
      }
      QVERIFY(p->get("pause").toBool());
    }
  }
  void cleanupTestCase() {
    const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!out.isEmpty()) {
      QSaveFile f(out + "/video-output-measurements.json");
      QVERIFY(f.open(QIODevice::WriteOnly));
      auto b = QJsonDocument(measurements).toJson();
      QCOMPARE(f.write(b), b.size());
      QVERIFY(f.commit());
    }
  }
};
QTEST_MAIN(VideoOutputTest)
#include "videooutput_test.moc"
