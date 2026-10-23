#include "player.h"
#include "profile.h"
#include "videoshaders.h"
#include "window.h"
#include "preferencesdriver.h"
#include <QtTest>
#include <QtWidgets>

class VideoShadersTest : public QObject {
  Q_OBJECT
  QTemporaryDir fixtures;
  QString media;
  QJsonObject measurements;
  void dialog(Window &w, const std::function<void(QDialog *)> &fn,
              const QString &action = "videoOutputSettings") {
    const auto objectName = action == "videoOutputSettings"
                                ? QStringLiteral("videoOutputDialog")
                                : action == "preferences"
                                      ? QStringLiteral("preferencesDialog")
                                      : QString();
    afterDialogReady(&w, objectName, [fn](QDialog *d) { fn(d); });
    auto *a = w.findChild<QAction *>("action_" + action);
    QVERIFY(a);
    a->trigger();
  }
  QJsonObject probe(const QString &source) {
    QProcess p;
    p.start(QDir(QCoreApplication::applicationDirPath())
                .filePath("foxicmp-shader-check"),
            {"--probe"});
    if (!p.waitForStarted(1000))
      return {};
    p.write(source.toUtf8());
    p.closeWriteChannel();
    if (!p.waitForFinished(4000)) {
      p.kill();
      p.waitForFinished();
      return {};
    }
    return QJsonDocument::fromJson(p.readAllStandardOutput()).object();
  }
private slots:
  void initTestCase() {
    QVERIFY(fixtures.isValid());
    media = fixtures.filePath("video.mkv");
    QProcess p;
    p.start("ffmpeg", {"-y", "-nostdin", "-v", "error", "-f", "lavfi", "-i",
                       "testsrc2=s=320x180:r=25:d=10", "-c:v", "ffv1", media});
    QVERIFY(p.waitForFinished(15000));
    QVERIFY2(p.exitCode() == 0, p.readAllStandardError().constData());
  }
  void compileAndOffscreenPixels() {
    for (const auto &id : {"identity", "gray", "invert", "sharpen"}) {
      const auto source = VideoShaders::preset(id);
      QVERIFY2(VideoShaders::compile(source).isEmpty(), id);
      const auto result = probe(source);
      QVERIFY(result.value("ok").toBool());
      const int red = result.value("red").toInt(),
                green = result.value("green").toInt(),
                blue = result.value("blue").toInt();
      if (QString(id) == "identity" || QString(id) == "sharpen") {
        QVERIFY(std::abs(red - 64) <= 1);
        QVERIFY(std::abs(green - 128) <= 1);
        QVERIFY(std::abs(blue - 192) <= 1);
      }
      if (QString(id) == "invert") {
        QVERIFY(std::abs(red - 191) <= 1);
        QVERIFY(std::abs(green - 127) <= 1);
        QVERIFY(std::abs(blue - 63) <= 1);
      }
      if (QString(id) == "gray") {
        QCOMPARE(red, green);
        QCOMPARE(green, blue);
        QVERIFY(std::abs(red - 119) <= 2);
      }
      measurements[QString("probe-") + id] = result;
    }
    QVERIFY(
        !VideoShaders::compile("vec4 hook(){ this is invalid; }").isEmpty());
  }
  void profileLimitsOrderPortableFilesAndFallback() {
    QTemporaryDir dir, moved;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    QString error;
    QVERIFY(VideoShaders::read(s, &error).isEmpty());
    QVERIFY(error.isEmpty());
    QList<VideoShaders::Entry> entries{
        {"Invert", "MAIN", VideoShaders::preset("invert"), true},
        {"Grey", "SCALED", VideoShaders::preset("gray"), true},
        {"Draft", "MAIN", "not compiled while disabled", false}};
    s.setValue("video/shadersV1", VideoShaders::encode(entries));
    s.setValue("video/shadersEnabled", true);
    QCOMPARE(VideoShaders::read(s, &error).size(), 3);
    QVERIFY(error.isEmpty());
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(480, 270);
    p.show();
    p.set("mute", true);
    p.set("hwdec", "no");
    QSignalSpy loaded(&p, &Player::loaded);
    QVERIFY(p.loadSource(media, {{"pause", "yes"}}));
    QTRY_COMPARE(loaded.size(), 1);
    QTest::qWait(150);
    const auto before =
        p.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
    QVERIFY(VideoShaders::apply(&p, s));
    auto paths = p.get("glsl-shaders").toStringList();
    QCOMPARE(paths.size(), 2);
    for (int i = 0; i < 2; ++i) {
      QVERIFY(paths[i].startsWith(dir.filePath("Shaders/")));
      QFile file(paths[i]);
      QVERIFY(file.open(QIODevice::ReadOnly));
      QCOMPARE(file.readAll(), VideoShaders::nativeSource(entries[i]).toUtf8());
    }
    QTest::qWait(120);
    QCOMPARE(p.grabFramebuffer().convertToFormat(QImage::Format_RGB32), before);
    QVERIFY(p.get("pause").toBool());
    entries.swapItemsAt(0, 1);
    s.setValue("video/shadersV1", VideoShaders::encode(entries));
    QVERIFY(VideoShaders::apply(&p, s));
    const auto reversed = p.get("glsl-shaders").toStringList();
    QCOMPARE(reversed, QStringList({paths[1], paths[0]}));
    QSettings movedSettings(moved.filePath("profile.ini"),
                            QSettings::IniFormat);
    movedSettings.setValue("video/shadersV1", VideoShaders::encode(entries));
    movedSettings.setValue("video/shadersEnabled", true);
    QVERIFY(VideoShaders::apply(&p, movedSettings));
    for (const auto &file : p.get("glsl-shaders").toStringList())
      QVERIFY(file.startsWith(moved.filePath("Shaders/")));
    s.setValue("video/shadersEnabled", false);
    QVERIFY(VideoShaders::apply(&p, s));
    QVERIFY(p.get("glsl-shaders").toStringList().isEmpty());
    QVERIFY(QFile::exists(paths[0]));
    s.setValue("video/shadersV1", "not json");
    QVERIFY(!VideoShaders::apply(&p, s));
    QVERIFY(!p.QObject::property("foxicmpShaderStatus").toString().isEmpty());
    auto invalid = entries;
    invalid[0].name = "bad\n//!HOOK OUTPUT";
    QVERIFY(!VideoShaders::validateStructure(invalid).isEmpty());
    invalid = entries;
    invalid[0].source = "//!HOOK OUTPUT\nvec4 hook(){return vec4(1);}";
    QVERIFY(!VideoShaders::validateStructure(invalid).isEmpty());
    invalid = entries;
    invalid[0].source = QString(65537, 'x');
    QVERIFY(!VideoShaders::validateStructure(invalid).isEmpty());
    while (invalid.size() < 17)
      invalid.append(entries.first());
    QVERIFY(!VideoShaders::validateStructure(invalid).isEmpty());
    p.set("pause", false);
    QTRY_VERIFY(p.get("time-pos").toDouble() > .2);
    measurements["softwareFallbackPixelsUnchanged"] = true;
    measurements["portableShaderFilesVerified"] = true;
  }
  void importMpvMultiPassAndRejectUnsupportedDirectives() {
    QTemporaryDir dir;
    const auto path = dir.filePath("film-shader.glsl");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray source =
        "// a harmless comment\n"
        "//!HOOK MAIN\n"
        "//!BIND HOOKED\n"
        "//!DESC First pass\n"
        "vec4 hook() { return HOOKED_tex(HOOKED_pos); }\n"
        "//!HOOK SCALED\n"
        "//!DESC Second pass\n"
        "//!BIND HOOKED\n"
        "vec4 hook() { vec4 c = HOOKED_tex(HOOKED_pos); return vec4(vec3(1.0) - c.rgb, c.a); }\n";
    QCOMPARE(file.write(source), source.size());
    file.close();
    QList<VideoShaders::Entry> entries;
    QString error;
    QVERIFY2(VideoShaders::importMpvFile(path, &entries, &error),
             qPrintable(error));
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].name, QString("First pass"));
    QCOMPARE(entries[0].stage, QString("MAIN"));
    QCOMPARE(entries[1].name, QString("Second pass"));
    QCOMPARE(entries[1].stage, QString("SCALED"));
    for (const auto &entry : entries) {
      QVERIFY(!entry.source.contains("//!"));
      QVERIFY(!entry.source.contains("#version"));
      QVERIFY(VideoShaders::compile(entry.source).isEmpty());
    }
    const auto rejected = [&](const QByteArray &text) {
      QFile f(dir.filePath("reject.glsl"));
      QVERIFY(f.open(QIODevice::WriteOnly));
      QCOMPARE(f.write(text), text.size());
      f.close();
      QList<VideoShaders::Entry> out;
      QString failure;
      QVERIFY(!VideoShaders::importMpvFile(f.fileName(), &out, &failure));
      QVERIFY(!failure.isEmpty());
      QVERIFY(out.isEmpty());
    };
    rejected("//!HOOK MAIN\n//!SAVE A\nvec4 hook(){return HOOKED_tex(HOOKED_pos);}\n");
    rejected("//!HOOK MAIN\n//!WHEN 1.0\nvec4 hook(){return HOOKED_tex(HOOKED_pos);}\n");
    rejected("//!COMPUTE 8 8 1\nvoid main(){}\n");
    rejected("//!HOOK MAIN\n#version 330\nvec4 hook(){return HOOKED_tex(HOOKED_pos);}\n");
    rejected("//!HOOK MAIN\n//!DESC no body\n");
    measurements["mpvMultiPassImported"] = entries.size();
    measurements["mpvUnsupportedDirectivesRejected"] = true;
  }
  void dialogCancelApplyEditOrderResetAndValidation() {
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
    const auto fill = [](QDialog *d) {
      d->findChild<QCheckBox *>("video/shadersEnabled")->setChecked(true);
      d->findChild<QPushButton *>("videoShaderAdd")->click();
      d->findChild<QLineEdit *>("videoShaderName")->setText("First");
      auto *selector = d->findChild<QComboBox *>("videoShaderPreset");
      selector->setCurrentIndex(selector->findData("invert"));
      d->findChild<QPushButton *>("videoShaderAdd")->click();
      d->findChild<QLineEdit *>("videoShaderName")->setText("Second");
      d->findChild<QPushButton *>("videoShaderUp")->click();
    };
    dialog(w, [&](QDialog *d) {
      QVERIFY(d->findChild<QPushButton *>("videoShaderImport"));
      fill(d);
      d->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Cancel)
          ->click();
    });
    QVERIFY(!profile.settings.contains("video/shadersV1"));
    QVERIFY(!QDir(dir.filePath("Shaders")).exists());
    dialog(w, [&](QDialog *d) {
      fill(d);
      auto *stage = d->findChild<QComboBox *>("videoShaderStage");
      stage->setCurrentIndex(stage->findData("SCALED"));
      d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    auto entries = VideoShaders::read(profile.settings);
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].name, QString("Second"));
    QCOMPARE(entries[0].stage, QString("SCALED"));
    QCOMPARE(entries[1].name, QString("First"));
    QVERIFY(p->get("pause").toBool());
    QCOMPARE(p->get("glsl-shaders").toStringList().size(), 2);
    dialog(w, [&](QDialog *d) {
      auto *tabs = d->findChild<QTabWidget *>();
      tabs->setCurrentWidget(d->findChild<QWidget *>("videoShadersPage"));
      const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      if (!out.isEmpty()) {
        QTest::qWait(80);
        QVERIFY(d->grab().save(out + "/video-shaders.png"));
      }
      d->findChild<QPushButton *>("videoShaderReset")->click();
      d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    QVERIFY(VideoShaders::read(profile.settings).isEmpty());
    QVERIFY(!profile.settings.value("video/shadersEnabled").toBool());
    QVERIFY(p->get("glsl-shaders").toStringList().isEmpty());
    for (const auto &action : {"videoOutputSettings", "preferences", "image"})
      dialog(
          w,
          [&](QDialog *d) {
            d->findChild<QCheckBox *>("video/shadersEnabled")->setChecked(true);
            d->findChild<QPushButton *>("videoShaderAdd")->click();
            d->findChild<QPlainTextEdit *>("videoShaderSource")
                ->setPlainText("vec4 hook(){ invalid; }");
            d->findChild<QDialogButtonBox *>()
                ->button(QDialogButtonBox::Ok)
                ->click();
            QVERIFY(d->isVisible());
            QVERIFY(
                !d->findChild<QLabel *>("videoShaderStatus")->text().isEmpty());
            d->findChild<QDialogButtonBox *>()
                ->button(QDialogButtonBox::Cancel)
                ->click();
          },
          action);
    QVERIFY(VideoShaders::read(profile.settings).isEmpty());
    QVERIFY(p->get("pause").toBool());
  }
  void statisticsResetCopyPatternAndPlayback() {
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
    QTRY_VERIFY(p->get("time-pos").toDouble() > .2);
    QTRY_VERIFY(p->renderStatistics().value("renderCalls").toLongLong() > 2);
    const auto action = w.findChild<QAction *>("action_renderStatistics");
    QVERIFY(action);
    QCOMPARE(action->shortcut(), QKeySequence("Ctrl+J"));
    for (auto *other : w.findChildren<QAction *>())
      if (other != action && other->objectName().startsWith("action_"))
        QVERIFY(other->shortcut() != action->shortcut());
    action->trigger();
    auto *d = w.findChild<QDialog *>("renderStatisticsDialog");
    QVERIFY(d);
    QVERIFY(!d->isModal());
    d->findChild<QPushButton *>("renderStatisticsCopy")->click();
    auto json =
        QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8())
            .object();
    QCOMPARE(json.value("renderer").toString(), QString("Software"));
    QVERIFY(json.value("renderCalls").toDouble() > 0);
    QVERIFY(json.value("peakMs").toDouble() >=
            json.value("averageMs").toDouble());
    measurements["renderStatsBeforeReset"] = json;
    p->set("pause", true);
    d->findChild<QPushButton *>("renderStatisticsReset")->click();
    QCOMPARE(p->renderStatistics().value("renderCalls").toLongLong(), 0);
    QVERIFY(p->get("pause").toBool());
    QVERIFY(p->renderStatistics().value("vo-passes").toMap().isEmpty());
    auto *pattern = d->findChild<QCheckBox *>("renderTearingToggle");
    pattern->setChecked(true);
    QTRY_VERIFY(p->findChild<QWidget *>("videoTearingPattern"));
    p->set("pause", false);
    const auto before = p->get("time-pos").toDouble();
    QTRY_VERIFY(p->get("time-pos").toDouble() > before + .2);
    pattern->setChecked(false);
    QVERIFY(!p->findChild<QWidget *>("videoTearingPattern"));
    const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!out.isEmpty()) {
      QTest::qWait(550);
      QVERIFY(d->grab().save(out + "/video-render-statistics.png"));
      pattern->setChecked(true);
      QTest::qWait(40);
      QVERIFY(p->grab().save(out + "/video-tearing-pattern.png"));
    }
    d->close();
    QTest::qWait(30);
    QVERIFY(!p->findChild<QWidget *>("videoTearingPattern"));
    QVERIFY(!p->get("pause").toBool());
  }
  void cleanupTestCase() {
    const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!out.isEmpty()) {
      QSaveFile f(out + "/video-shaders-measurements.json");
      QVERIFY(f.open(QIODevice::WriteOnly));
      const auto data = QJsonDocument(measurements).toJson();
      QCOMPARE(f.write(data), data.size());
      QVERIFY(f.commit());
    }
  }
};
QTEST_MAIN(VideoShadersTest)
#include "videoshaders_test.moc"
