#include "preferencesdriver.h"
#include "audiooutput.h"
#include "mediaoptions.h"
#include "player.h"
#include "profile.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>
#include <cstring>
class AudioRoutingTest : public QObject {
  Q_OBJECT
  QTemporaryDir fixtures;
  QString video, external, primary, secondary;
  QString primaryModule, secondaryModule;
  QJsonObject measurements;
  QByteArray run(const QStringList &args) {
    QProcess p;
    p.start("pactl", args);
    if (!p.waitForFinished(3000) || p.exitCode() != 0)
      return {};
    return p.readAllStandardOutput().trimmed();
  }
  void ffmpeg(const QStringList &args) {
    QProcess p;
    p.start("ffmpeg", QStringList{"-y", "-nostdin", "-v", "error"} + args);
    QVERIFY(p.waitForFinished(12000));
    QVERIFY2(p.exitCode() == 0, p.readAllStandardError().constData());
  }
  void setup(Profile &p) {
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("input/mpris", false);
    p.settings.setValue("audio/mute", true);
    p.settings.setValue("audio/device", "pulse/" + primary);
    p.settings.setValue("playlist/rememberTracks", false);
    p.settings.setValue("video/output", qEnvironmentVariable(
                                            "FOXICMP_TEST_OUTPUT", "software"));
  }
  QList<QVariantMap> audio(Player *p) {
    QList<QVariantMap> result;
    for (const auto &entry : p->get("track-list").toList())
      if (entry.toMap().value("type") == "audio")
        result << entry.toMap();
    return result;
  }
  QVariantMap selected(Player *p) {
    for (const auto &t : audio(p))
      if (t.value("id").toString() == p->get("aid").toString())
        return t;
    return {};
  }
  void acceptPreferences(Window &w) {
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog");
      QVERIFY(d);
      d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    w.findChild<QAction *>("action_preferences")->trigger();
  }
  void choose(Window &w, const QString &id) {
    auto *menu = w.findChild<QMenu *>("audioTracksMenu");
    QVERIFY(menu);
    for (auto *a : menu->actions())
      if (a->data().isValid() && a->data().toString() == id) {
        a->trigger();
        return;
      }
    QFAIL("Audio track action not found");
  }
  QStringList privateRoutes() {
    QStringList result;
    const auto modules =
        QJsonDocument::fromJson(run({"--format=json", "list", "modules"}))
            .array();
    for (const auto &entry : modules) {
      const auto m = entry.toObject();
      if (m.value("name") == "module-combine-sink" &&
          m.value("argument").toString().contains("sink_name=foxicmp_dual_") &&
          m.value("argument").toString().contains("slaves=" + primary + ","))
        result << m.value("argument").toString();
    }
    return result;
  }
  double rms(const QByteArray &bytes) {
    double sum = 0;
    const auto count = bytes.size() / 4;
    for (qsizetype i = 0; i < count; ++i) {
      float value;
      std::memcpy(&value, bytes.constData() + i * 4, 4);
      sum += double(value) * value;
    }
    return count ? std::sqrt(sum / count) : 0;
  }
  QByteArray capture(const QString &sink, Player *p) {
    QProcess monitor;
    monitor.start("parec",
                  {"--raw", "--format=float32le", "--rate=48000",
                   "--channels=2", "--latency-msec=20",
                   "--process-time-msec=10", "--device=" + sink + ".monitor"});
    if (!monitor.waitForStarted(2000))
      return {};
    p->set("pause", false);
    QTest::qWait(1800);
    monitor.terminate();
    monitor.waitForFinished(1000);
    return monitor.readAllStandardOutput();
  }
private slots:
  void initTestCase() {
    QVERIFY(fixtures.isValid());
    QVERIFY(!QStandardPaths::findExecutable("parec").isEmpty());
    const auto suffix = QString::number(QCoreApplication::applicationPid());
    primary = "foxicmp_route_primary_" + suffix;
    secondary = "foxicmp_route_secondary_" + suffix;
    primaryModule = QString::fromUtf8(
        run({"load-module", "module-null-sink", "sink_name=" + primary}));
    secondaryModule = QString::fromUtf8(
        run({"load-module", "module-null-sink", "sink_name=" + secondary}));
    QVERIFY(!primaryModule.isEmpty() && !secondaryModule.isEmpty());
    video = fixtures.filePath("movie.mkv");
    external = fixtures.filePath("movie.wav");
    QVERIFY(QDir(fixtures.path()).mkdir("audio: русский"));
    ffmpeg({"-f", "lavfi", "-i", "color=black:s=320x180:r=25", "-f", "lavfi",
            "-i", "sine=frequency=440:sample_rate=48000", "-t", "30", "-c:v",
            "ffv1", "-c:a", "pcm_s16le", video});
    ffmpeg({"-f", "lavfi", "-i",
            "sine=frequency=880:sample_rate=48000:duration=8", "-c:a",
            "pcm_s16le", external});
    ffmpeg({"-f", "lavfi", "-i",
            "sine=frequency=660:sample_rate=48000:duration=8", "-c:a", "flac",
            fixtures.filePath("audio: русский/movie.rus.flac")});
  }
  void defaultSearchDisabledAndPreferencesCancelApply() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    setup(profile);
    Window w(profile);
    w.show();
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded);
    w.openFiles({video});
    QTRY_COMPARE(loaded.size(), 1);
    p->set("pause", true);
    QCOMPARE(audio(p).size(), 1);
    const auto edit = [&](bool save) {
      QTimer::singleShot(70, &w, [&] {
        auto *d = w.findChild<QDialog *>("audioProcessingDialog");
        QVERIFY(d);
        auto *mode = d->findChild<QComboBox *>("audio/autoLoad");
        mode->setCurrentIndex(mode->findData("fuzzy"));
        d->findChild<QPlainTextEdit *>("audio/searchPaths")
            ->setPlainText("audio: русский");
        d->findChild<QLineEdit *>("audio/extensions")->setText("wav, flac");
        auto *policy = d->findChild<QComboBox *>("audio/selectionPolicy");
        policy->setCurrentIndex(policy->findData("external"));
        auto *second = d->findChild<QComboBox *>("audio/secondDevice");
        QVERIFY(second->findData("pulse/" + secondary) >= 0);
        second->setCurrentIndex(second->findData("pulse/" + secondary));
        auto *tabs = d->findChild<QTabWidget *>();
        tabs->setCurrentIndex(3);
        const auto screenshots =
            qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        if (save && !screenshots.isEmpty()) {
          QTest::qWait(40);
          d->grab().save(screenshots + "/audio-routing.png");
        }
        d->findChild<QDialogButtonBox *>()
            ->button(save ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
      });
      w.findChild<QAction *>("action_audioProcessingSettings")->trigger();
    };
    edit(false);
    QVERIFY(!profile.settings.contains("audio/autoLoad"));
    QCOMPARE(audio(p).size(), 1);
    edit(true);
    QTRY_COMPARE(audio(p).size(), 3);
    QTRY_VERIFY(selected(p).value("external").toBool());
    QCOMPARE(p->get("audio-file-paths").toStringList(),
             QStringList{"audio: русский"});
    QVERIFY(p->get("pause").toBool());
    QCOMPARE(profile.settings.value("audio/secondDevice").toString(),
             "pulse/" + secondary);
    QVERIFY(!p->QObject::property("foxicmpDualActive").toBool());
  }
  void exactAllAndDisabledSearchWithEmbeddedPriority() {
    const auto unrelated = fixtures.filePath("unrelated.wav");
    QVERIFY(QFile::copy(external, unrelated));
    QTemporaryDir dir;
    Profile profile(dir.path());
    setup(profile);
    profile.settings.setValue("audio/autoLoad", "exact");
    profile.settings.setValue("audio/searchPaths", "audio: русский");
    profile.settings.setValue("audio/extensions", "wav, flac");
    profile.settings.setValue("audio/selectionPolicy", "internal");
    Window w(profile);
    w.show();
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded);
    w.openFiles({video});
    QTRY_COMPARE(loaded.size(), 1);
    p->set("pause", true);
    // mpv exact matching recognizes a language suffix such as .rus.flac.
    QCOMPARE(audio(p).size(), 3);
    for (const auto &track : audio(p))
      QVERIFY(track.value("external-filename").toString() != unrelated);
    QVERIFY(!selected(p).value("external").toBool());
    profile.settings.setValue("audio/autoLoad", "all");
    acceptPreferences(w);
    w.reopenCurrent();
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_COMPARE(audio(p).size(), 4);
    QVERIFY(!selected(p).value("external").toBool());
    profile.settings.setValue("audio/autoLoad", "no");
    acceptPreferences(w);
    w.reopenCurrent();
    QTRY_COMPARE(loaded.size(), 3);
    QCOMPARE(audio(p).size(), 1);
    QVERIFY(QFile::remove(unrelated));
  }
  void manualAndRememberedSelectionWinAndNoDuplicateRestore() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    setup(profile);
    profile.settings.setValue("audio/autoLoad", "fuzzy");
    profile.settings.setValue("audio/selectionPolicy", "external");
    Window w(profile);
    w.show();
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded);
    w.openFiles({video});
    QTRY_COMPARE(loaded.size(), 1);
    p->set("pause", true);
    QTRY_VERIFY(selected(p).value("external").toBool());
    QString internal;
    for (const auto &track : audio(p))
      if (!track.value("external").toBool())
        internal = track.value("id").toString();
    QVERIFY(!internal.isEmpty());
    choose(w, internal);
    acceptPreferences(w);
    QTRY_VERIFY(!selected(p).value("external").toBool());
    w.reopenCurrent();
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(selected(p).value("external").toBool());
    profile.settings.setValue("playlist/rememberTracks", true);
    for (const auto &track : audio(p))
      if (!track.value("external").toBool())
        internal = track.value("id").toString();
    choose(w, internal);
    w.reopenCurrent();
    QTRY_COMPARE(loaded.size(), 3);
    QTRY_VERIFY(!selected(p).value("external").toBool());
    QVERIFY(w.loadExternalAudio(external));
    QTRY_VERIFY(selected(p).value("external").toBool());
    w.reopenCurrent();
    QTRY_COMPARE(loaded.size(), 4);
    QTRY_COMPARE(selected(p).value("external-filename").toString(), external);
    QCOMPARE(audio(p).size(), 2);
  }
  void twoOutputsCarryAudioFallbackRecoveryAndDisable() {
    QTemporaryDir dir;
    QSettings settings(dir.filePath("profile.ini"), QSettings::IniFormat);
    settings.setValue("audio/device", "pulse/" + primary);
    settings.setValue("audio/secondDevice", "pulse/" + secondary);
    settings.setValue("audio/dualOutput", true);
    const auto before = privateRoutes();
    const auto defaultSink = run({"get-default-sink"});
    {
      Player p(nullptr,
               qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
      p.resize(320, 180);
      p.show();
      p.set("volume", 60);
      AudioOutput::apply(&p, settings);
      QTRY_VERIFY_WITH_TIMEOUT(
          p.QObject::property("foxicmpDualActive").toBool(), 8000);
      QVERIFY(
          p.get("audio-device").toString().startsWith("pulse/foxicmp_dual_"));
      QSignalSpy loaded(&p, &Player::loaded);
      QVERIFY(p.loadSource(video, {{"pause", "yes"}, {"loop-file", "inf"}}));
      QTRY_COMPARE(loaded.size(), 1);
      p.set("mute", false);
      const auto first = capture(primary, &p);
      const auto second = capture(secondary, &p);
      QVERIFY2(
          rms(first) > .01 && rms(second) > .01,
          qPrintable(QString("RMS %1 / %2").arg(rms(first)).arg(rms(second))));
      measurements["primaryRms"] = rms(first);
      measurements["secondaryRms"] = rms(second);
      QVERIFY(std::abs(rms(first) / rms(second) - 1) < .25);
      run({"unload-module", secondaryModule});
      secondaryModule.clear();
      QTRY_VERIFY_WITH_TIMEOUT(
          !p.QObject::property("foxicmpDualActive").toBool(), 7000);
      QCOMPARE(p.get("audio-device").toString(), "pulse/" + primary);
      QVERIFY(rms(capture(primary, &p)) > .01);
      QVERIFY(p.get("duration").toDouble() - p.get("time-pos").toDouble() > 10);
      secondaryModule = QString::fromUtf8(
          run({"load-module", "module-null-sink", "sink_name=" + secondary}));
      QVERIFY(!secondaryModule.isEmpty());
      QTRY_VERIFY_WITH_TIMEOUT(
          p.QObject::property("foxicmpDualActive").toBool(), 8000);
      double recovered = 0;
      QElapsedTimer recovery;
      recovery.start();
      do {
        recovered = rms(capture(secondary, &p));
      } while (recovered <= .01 && recovery.elapsed() < 8000);
      QVERIFY2(
          recovered > .01,
          qPrintable(
              QString(
                  "Recovery RMS=%1 time=%2 idle=%3 eof=%4 loop=%5 device=%6")
                  .arg(recovered)
                  .arg(p.get("time-pos").toString(),
                       p.get("idle-active").toString(),
                       p.get("eof-reached").toString(),
                       p.get("loop-file").toString(),
                       p.get("audio-device").toString())));
      measurements["recoveredSecondaryRms"] = recovered;
      settings.setValue("audio/dualOutput", false);
      AudioOutput::apply(&p, settings);
      QCOMPARE(p.get("audio-device").toString(), "pulse/" + primary);
      QTRY_COMPARE_WITH_TIMEOUT(privateRoutes(), before, 7000);
      QCOMPARE(run({"get-default-sink"}), defaultSink);
    }
    QCOMPARE(privateRoutes(), before);
  }
  void rejectedSameDeviceAndDestructorCleanup() {
    QTemporaryDir dir;
    QSettings settings(dir.filePath("profile.ini"), QSettings::IniFormat);
    settings.setValue("audio/device", "pulse/" + primary);
    settings.setValue("audio/secondDevice", "pulse/" + primary);
    settings.setValue("audio/dualOutput", true);
    const auto before = privateRoutes();
    {
      Player p(nullptr,
               qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
      p.show();
      AudioOutput::apply(&p, settings);
      QTRY_VERIFY_WITH_TIMEOUT(p.QObject::property("foxicmpAudioOutputStatus")
                                   .toString()
                                   .contains("основной"),
                               7000);
      QVERIFY(!p.QObject::property("foxicmpDualActive").toBool());
      QCOMPARE(privateRoutes(), before);
      settings.setValue("audio/secondDevice", "pulse/" + secondary);
      AudioOutput::apply(&p, settings);
      QTRY_VERIFY_WITH_TIMEOUT(
          p.QObject::property("foxicmpDualActive").toBool(), 8000);
    }
    QTRY_COMPARE_WITH_TIMEOUT(privateRoutes(), before, 7000);
  }
  void parentTerminationClosesLeaseAndRapidReconfiguration() {
    const auto before = privateRoutes();
    const auto helper = QDir(QCoreApplication::applicationDirPath())
                            .filePath("helpers/audio_output.py");
    QProcess launcher;
    launcher.start("python3",
                   {"-c",
                    "import subprocess,sys,time; "
                    "child=subprocess.Popen(sys.argv[1:],stdin=subprocess.PIPE)"
                    "; time.sleep(30)",
                    "python3", helper, "--primary", "pulse/" + primary,
                    "--secondary", "pulse/" + secondary});
    QVERIFY(launcher.waitForStarted(2000));
    QTRY_VERIFY_WITH_TIMEOUT(launcher.canReadLine(), 7000);
    const auto reply = QJsonDocument::fromJson(launcher.readLine()).object();
    QCOMPARE(reply.value("state").toString(), QString("ready"));
    QVERIFY(privateRoutes().size() > before.size());
    launcher.kill();
    launcher.waitForFinished(2000);
    QTRY_COMPARE_WITH_TIMEOUT(privateRoutes(), before, 7000);
    QTemporaryDir dir;
    QSettings settings(dir.filePath("profile.ini"), QSettings::IniFormat);
    settings.setValue("audio/device", "pulse/" + primary);
    settings.setValue("audio/secondDevice", "pulse/" + secondary);
    settings.setValue("audio/dualOutput", true);
    {
      Player p(nullptr,
               qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
      p.show();
      AudioOutput::apply(&p, settings);
      settings.setValue("audio/dualOutput", false);
      AudioOutput::apply(&p, settings);
      settings.setValue("audio/dualOutput", true);
      AudioOutput::apply(&p, settings);
      QTRY_VERIFY_WITH_TIMEOUT(
          p.QObject::property("foxicmpDualActive").toBool(), 9000);
    }
    QTRY_COMPARE_WITH_TIMEOUT(privateRoutes(), before, 7000);
  }
  void cleanupTestCase() {
    const auto output = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!output.isEmpty()) {
      QSaveFile file(output + "/audio-routing-measurements.json");
      QVERIFY(file.open(QIODevice::WriteOnly));
      const auto bytes = QJsonDocument(measurements).toJson();
      QCOMPARE(file.write(bytes), bytes.size());
      QVERIFY(file.commit());
    }
    if (!secondaryModule.isEmpty())
      run({"unload-module", secondaryModule});
    if (!primaryModule.isEmpty())
      run({"unload-module", primaryModule});
  }
};
QTEST_MAIN(AudioRoutingTest)
#include "audiorouting_test.moc"
