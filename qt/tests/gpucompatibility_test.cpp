#include "player.h"
#include "videoshaders.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>

class GpuCompatibilityTest : public QObject {
  Q_OBJECT
  QString corpus;
  QJsonArray results;
  QJsonObject active;
  static int colored(const QImage &im) {
    int count = 0;
    for (int y = 0; y < im.height(); y += 8)
      for (int x = 0; x < im.width(); x += 8) {
        const auto c = im.pixelColor(x, y);
        if (std::max({c.red(), c.green(), c.blue()}) -
            std::min({c.red(), c.green(), c.blue()}) > 12) ++count;
      }
    return count;
  }
  static int different(const QImage &a, const QImage &b) {
    if (a.isNull() || a.size() != b.size()) return -1;
    int count = 0;
    for (int y = 0; y < a.height(); y += 2)
      for (int x = 0; x < a.width(); x += 2) {
        const auto l = a.pixelColor(x, y), r = b.pixelColor(x, y);
        if (std::max({std::abs(l.red() - r.red()), std::abs(l.green() - r.green()),
                      std::abs(l.blue() - r.blue())}) > 4) ++count;
      }
    return count;
  }
  void recordDriver(Player &p) {
    active["renderer"] = p.outputName();
    active["driver"] = p.QObject::property("foxicmpGlDriver").toString();
    active["swapIntervalReported"] = QJsonValue::fromVariant(p.QObject::property("foxicmpSwapIntervalReported"));
    active["stats"] = QJsonObject::fromVariantMap(p.renderStatistics());
  }
private slots:
  void initTestCase() {
    QVERIFY2(QGuiApplication::platformName() == "xcb", "This trial is X11 only; Wayland is excluded");
    corpus = qEnvironmentVariable("FOXICMP_MATRIX_DIR");
    QVERIFY(QFile::exists(corpus + "/Русский H264.mp4"));
  }
  void decoderBackends_data() {
    QTest::addColumn<QString>("backend");
    for (const auto *name : {"no", "auto-copy", "vaapi-copy", "nvdec-copy", "vdpau-copy"})
      QTest::newRow(name) << QString(name);
  }
  void decoderBackends() {
    QFETCH(QString, backend);
    active = {{"case", "decoderBackends"}, {"requestedBackend", backend}};
    QStringList warnings;
    Player p(nullptr, "opengl");
    connect(&p, &Player::diagnosticMessage, this, [&](const QString &s) {
      if (warnings.size() < 40) warnings << s.left(1000);
    });
    p.resize(640, 360);
    p.show();
    QTRY_VERIFY_WITH_TIMEOUT(p.ready(), 10000);
    if (qEnvironmentVariableIsSet("FOXICMP_GPU_REQUIRE_GL"))
      QCOMPARE(p.outputName(), QString("OpenGL"));
    QVERIFY(p.set("mute", true));
    QVERIFY(p.set("hwdec-codecs", "all"));
    QVERIFY(p.set("hwdec", backend));
    QSignalSpy loaded(&p, &Player::loaded), failed(&p, &Player::fileFailed);
    QJsonArray samples;
    for (const auto *name : {"Русский H264.mp4", "hevc-main10.mkv", "vp9.webm", "av1.mkv"}) {
      loaded.clear();
      QVERIFY(p.loadSource(corpus + "/" + QString::fromUtf8(name), {{"pause", "no"}}));
      QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
      QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > .2, 10000);
      QTRY_VERIFY_WITH_TIMEOUT(colored(p.grabFramebuffer()) > 50, 10000);
      const auto actual = p.get("hwdec-current").toString();
      samples.append(QJsonObject{{"file", QString::fromUtf8(name)}, {"hwdecCurrent", actual},
                                  {"softwareDecode", actual.isEmpty() || actual == "no"},
                                  {"codec", p.get("video-codec").toString()}});
      QVERIFY(p.set("pause", true));
      QVERIFY(p.command({"seek", "1", "absolute+exact"}));
      QTRY_VERIFY_WITH_TIMEOUT(!p.get("seeking").toBool() &&
                              std::abs(p.get("time-pos").toDouble() - 1) < .15, 10000);
      QVERIFY(p.get("pause").toBool());
      QCOMPARE(failed.size(), 0);
    }
    recordDriver(p);
    active["samples"] = samples;
    active["diagnostics"] = QJsonArray::fromStringList(warnings);
  }
  void shadersScalingAndWindowTransitions() {
    active = {{"case", "shadersScalingAndWindowTransitions"}};
    Player p(nullptr, "opengl");
    p.resize(720, 405);
    p.show();
    QTRY_VERIFY_WITH_TIMEOUT(p.ready(), 10000);
    QCOMPARE(p.outputName(), QString("OpenGL"));
    QVERIFY(p.set("hwdec", "no"));
    QVERIFY(p.set("mute", true));
    QSignalSpy loaded(&p, &Player::loaded);
    QVERIFY(p.loadSource(corpus + "/Русский H264.mp4", {{"pause", "yes"}, {"start", "1"}}));
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(colored(p.grabFramebuffer()) > 50, 10000);
    QTemporaryDir profile;
    QSettings s(profile.filePath("test.ini"), QSettings::IniFormat);
    const auto original = p.grabFramebuffer();
    s.setValue("video/shadersEnabled", true);
    s.setValue("video/shadersV1", VideoShaders::encode({{"Gray", "MAIN", VideoShaders::preset("gray"), true}}));
    QVERIFY(VideoShaders::apply(&p, s));
    QImage gray;
    QTRY_VERIFY_WITH_TIMEOUT(!(gray = p.grabFramebuffer()).isNull() && colored(gray) < 10, 10000);
    QVERIFY(different(original, gray) > 500);
    active["grayscaleChangedPixels"] = different(original, gray);
    // Exercise the imported chain on the actual native GL renderer. Two
    // inversions must restore the original frame within a four-level tolerance.
    QFile shader(profile.filePath("double-invert.glsl"));
    QVERIFY(shader.open(QIODevice::WriteOnly));
    const auto body = VideoShaders::preset("invert");
    shader.write(("//!HOOK MAIN\n//!BIND HOOKED\n//!DESC First\n" + body +
                  "//!HOOK MAIN\n//!BIND HOOKED\n//!DESC Second\n" + body).toUtf8());
    shader.close();
    QList<VideoShaders::Entry> imported;
    QString error;
    QVERIFY2(VideoShaders::importMpvFile(shader.fileName(), &imported, &error), qPrintable(error));
    QCOMPARE(imported.size(), 2);
    s.setValue("video/shadersV1", VideoShaders::encode(imported));
    QVERIFY(VideoShaders::apply(&p, s));
    QImage restored;
    QTRY_VERIFY_WITH_TIMEOUT(!(restored = p.grabFramebuffer()).isNull() &&
                              different(original, restored) >= 0 && different(original, restored) < 30, 10000);
    active["doubleInvertDifferentPixels"] = different(original, restored);
    active["passes"] = QJsonValue::fromVariant(p.get("vo-passes"));
    s.setValue("video/shadersEnabled", false);
    QVERIFY(VideoShaders::apply(&p, s));
    QVERIFY(p.set("scale", "nearest"));
    QTest::qWait(100);
    const auto nearest = p.grabFramebuffer();
    QVERIFY(p.set("scale", "bilinear"));
    QImage linear;
    QTRY_VERIFY_WITH_TIMEOUT(!(linear = p.grabFramebuffer()).isNull() && different(nearest, linear) > 500, 10000);
    active["nearestBilinearDifferentPixels"] = different(nearest, linear);
    QVERIFY(p.get("pause").toBool());
    p.resize(800, 480);
    QTRY_VERIFY_WITH_TIMEOUT(colored(p.grabFramebuffer()) > 50, 10000);
    QVERIFY(p.command({"seek", "0", "absolute+exact"}));
    QVERIFY(p.set("pause", false));
    p.showMinimized();
    QTest::qWait(350);
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > .2, 10000);
    p.showNormal();
    QTRY_VERIFY_WITH_TIMEOUT(colored(p.grabFramebuffer()) > 50, 10000);
    recordDriver(p);
    active["resizeMinimizeRestore"] = true;
  }
  void cleanup() {
    if (active.isEmpty()) return;
    active["passed"] = !QTest::currentTestFailed();
    results.append(active);
    active = {};
  }
  void cleanupTestCase() {
    QSaveFile out(qEnvironmentVariable("FOXICMP_GPU_REPORT"));
    QVERIFY(out.open(QIODevice::WriteOnly));
    QJsonObject report{{"schema", 1}, {"platform", QGuiApplication::platformName()},
                       {"results", results}};
    const auto bytes = QJsonDocument(report).toJson();
    QCOMPARE(out.write(bytes), bytes.size());
    QVERIFY(out.commit());
  }
};
QTEST_MAIN(GpuCompatibilityTest)
#include "gpucompatibility_test.moc"
