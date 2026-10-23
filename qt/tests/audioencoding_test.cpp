#include "audioencoding.h"
#include "audioprocessing.h"
#include "player.h"
#include "profile.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>
#include <cstring>

class AudioEncodingTest : public QObject {
  Q_OBJECT
  QTemporaryDir fixtures;
  QString source, ac3, mp3, videoAc3;
  QJsonObject measurements;
  struct Output {
    QByteArray bytes;
    QVariantMap params;
    int format = 0, bits = 0, channels = 0, rate = 0;
    QByteArray data;
  };
  void ffmpeg(const QStringList &args) {
    QProcess p;
    p.start("ffmpeg", QStringList{"-y", "-nostdin", "-v", "error"} + args);
    QVERIFY(p.waitForFinished(12000));
    QVERIFY2(p.exitCode() == 0, p.readAllStandardError().constData());
  }
  Output read(const QString &path, const QVariantMap &params) {
    QFile f(path);
    Output o;
    o.params = params;
    if (!f.open(QIODevice::ReadOnly))
      return o;
    o.bytes = f.readAll();
    const auto &b = o.bytes;
    const auto u16 = [&](qsizetype i) {
      return quint16(quint8(b[i]) | quint16(quint8(b[i + 1])) << 8);
    };
    const auto u32 = [&](qsizetype i) {
      return quint32(u16(i)) | quint32(u16(i + 2)) << 16;
    };
    if (b.left(4) != "RIFF")
      return o;
    for (qsizetype i = 12; i + 8 <= b.size();) {
      auto n = u32(i + 4);
      auto body = i + 8;
      if (b.mid(i, 4) == "fmt " && n >= 16) {
        o.format = u16(body);
        o.channels = u16(body + 2);
        o.rate = u32(body + 4);
        o.bits = u16(body + 14);
        if (o.format == 0xfffe && n >= 40)
          o.format = u16(body + 24);
      }
      if (b.mid(i, 4) == "data") {
        o.data = b.mid(body, std::min<qsizetype>(n, b.size() - body));
        break;
      }
      if (n > quint32(b.size() - body))
        break;
      i = body + n + (n & 1);
    }
    return o;
  }
  Output render(QSettings &s, const QString &media,
                std::function<void(Player &)> runtime = {}) {
    static int n = 0;
    const auto path = fixtures.filePath(QString("output-%1.wav").arg(++n));
    QVariantMap params;
    const auto diagnosticDir =
        qEnvironmentVariable("FOXICMP_AUDIO_DIAGNOSTIC_DIR");
    QMap<QString, QString> logOptions;
    if (!diagnosticDir.isEmpty()) {
      QDir().mkpath(diagnosticDir);
      logOptions["log-file"] =
          QDir(diagnosticDir)
              .filePath(QString("mpv-%1-%2.log")
                            .arg(QCoreApplication::applicationPid())
                            .arg(n));
    }
    logOptions.insert("ao", "pcm");
    logOptions.insert("ao-pcm-file", path);
    logOptions.insert("audio-display", "no");
    logOptions.insert("gapless-audio", "no");
    {
      Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"),
               logOptions);
      p.resize(320, 180);
      p.show();
      p.set("volume", 100);
      p.set("mute", false);
      if (!AudioProcessing::apply(&p, s))
        return {};
      QSignalSpy loaded(&p, &Player::loaded), ended(&p, &Player::ended);
      if (!p.loadSource(media, {{"pause", "yes"}}))
        return {};
      QElapsedTimer deadline;
      deadline.start();
      while ((loaded.isEmpty() || !p.get("audio-out-params").isValid()) &&
             deadline.elapsed() < 5000)
        QTest::qWait(10);
      if (loaded.isEmpty())
        return {};
      if (runtime) {
        const auto diagnostics = [&p](const QString &stage) {
          QJsonObject snapshot{{"stage", stage}};
          for (const auto *name :
               {"audio-params", "audio-out-params", "aid", "audio-spdif", "af",
                "time-pos", "pause", "seeking", "eof-reached", "idle-active",
                "track-list"})
            snapshot[name] = QJsonValue::fromVariant(p.get(name));
          qInfo().noquote()
              << QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
        };
        if (qEnvironmentVariableIsSet("FOXICMP_TRANSITION_DIAGNOSTICS"))
          diagnostics("before runtime");
        runtime(p);
        if (qEnvironmentVariableIsSet("FOXICMP_TRANSITION_DIAGNOSTICS"))
          diagnostics("after runtime");
      }
      params = p.get("audio-out-params").toMap();
      p.set("pause", false);
      while (ended.isEmpty() && deadline.elapsed() < 12000)
        QTest::qWait(10);
      if (ended.isEmpty() || !ended.first().first().toBool()) {
        QJsonObject snapshot{{"diagnosticRender", n}};
        for (const auto *name :
             {"audio-params", "audio-out-params", "aid", "audio-spdif", "af",
              "time-pos", "pause", "seeking", "eof-reached", "idle-active",
              "track-list"})
          snapshot[name] = QJsonValue::fromVariant(p.get(name));
        qWarning().noquote()
            << QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
        return {};
      }
    }
    return read(path, params);
  }
private slots:
  void initTestCase() {
    QVERIFY(fixtures.isValid());
    source = fixtures.filePath("signal.wav");
    ac3 = fixtures.filePath("signal.ac3");
    mp3 = fixtures.filePath("signal.mp3");
    videoAc3 = fixtures.filePath("video-ac3.mkv");
    ffmpeg({"-f", "lavfi", "-i",
            "aevalsrc=0.123456789*sin(2*PI*441*t)|0.0987654321*sin(2*PI*883*t):"
            "s=48000:d=2:c=stereo",
            "-c:a", "pcm_f64le", source});
    ffmpeg({"-i", source, "-c:a", "ac3", "-b:a", "192k", ac3});
    ffmpeg({"-i", source, "-c:a", "libmp3lame", "-b:a", "128k", mp3});
    ffmpeg({"-f", "lavfi", "-i", "testsrc2=s=96x54:r=10:d=2", "-i", ac3, "-c:v",
            "mpeg4", "-c:a", "copy", "-shortest", videoAc3});
  }
  void defaultsSanitizedAndFormatsMeasured() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    QCOMPARE(AudioEncoding::values(s), AudioEncoding::defaults());
    for (const auto &fmt : QStringList{"s16", "s24", "s32", "float"}) {
      s.setValue("audio/pcmFormat", fmt);
      auto o = render(s, source);
      QVERIFY2(!o.data.isEmpty(), qPrintable(fmt));
      QCOMPARE(o.channels, 2);
      QCOMPARE(o.rate, 48000);
      QCOMPARE(o.format, fmt == "float" ? 3 : 1);
      QCOMPARE(o.bits, fmt == "s16" ? 16 : 32);
      measurements[fmt] =
          QJsonObject{{"bits", o.bits},
                      {"wavFormat", o.format},
                      {"bytes", o.data.size()},
                      {"nativeFormat", o.params.value("format").toString()}};
      if (fmt == "s24") {
        int nonzero = 0;
        for (qsizetype i = 0; i + 4 <= o.data.size(); i += 4)
          nonzero += quint8(o.data[i]) != 0;
        measurements["int24NonzeroLowBytes"] = nonzero;
        QCOMPARE(nonzero, 0);
      }
    }
    s.setValue("audio/pcmFormat", "invalid");
    s.setValue("audio/sampleRate", 999);
    s.setValue("audio/decoderThreads", 99);
    s.setValue("audio/ac3Drc", 9);
    s.setValue("audio/preferredDecoder", "+ac3,-");
    s.setValue("audio/spdifCodecs", QStringList{"ac3", "unknown", "ac3"});
    const auto v = AudioEncoding::values(s);
    QCOMPARE(v.value("pcmFormat").toString(), QString("auto"));
    QCOMPARE(v.value("sampleRate").toInt(), 0);
    QCOMPARE(v.value("decoderThreads").toInt(), 16);
    QCOMPARE(v.value("ac3Drc").toDouble(), 1.);
    QVERIFY(v.value("preferredDecoder").toString().isEmpty());
    QCOMPARE(v.value("spdifCodecs").toStringList(), QStringList{"ac3"});
  }
  void resamplingAutoResetAndPausedRuntime() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    for (int rate : {44100, 96000}) {
      s.setValue("audio/sampleRate", rate);
      s.setValue("audio/pcmFormat", "s24");
      const auto o = render(s, source);
      QVERIFY(!o.data.isEmpty());
      QCOMPARE(o.rate, rate);
      QCOMPARE(o.bits, 32);
      for (qsizetype i = 0; i + 4 <= o.data.size(); i += 4)
        QCOMPARE(quint8(o.data[i]), quint8(0));
      measurements[QString("rate%1Frames").arg(rate)] = o.data.size() / 8;
    }
    s.setValue("audio/pcmFormat", "s16");
    s.setValue("audio/sampleRate", 44100);
    const auto sink = qEnvironmentVariable("PULSE_SINK");
    QVERIFY2(sink.startsWith("foxicmp_"), "Use an isolated FoxicMP test sink");
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.show();
    p.set("mute", true);
    p.set("audio-device", "pulse/" + sink);
    QVERIFY(AudioProcessing::apply(&p, s));
    QSignalSpy loaded(&p, &Player::loaded);
    QVERIFY(p.loadSource(source, {{"pause", "yes"}}));
    QTRY_COMPARE(loaded.size(), 1);
    QVERIFY(p.get("pause").toBool());
    s.setValue("audio/pcmFormat", "auto");
    s.setValue("audio/sampleRate", 0);
    QVERIFY(AudioProcessing::apply(&p, s));
    QVERIFY(p.get("pause").toBool());
    QVERIFY(p.get("af").toList().isEmpty());
    p.set("pause", false);
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > .1, 3000);
    measurements["autoResetPlaybackAdvances"] = true;
    const auto automatic = render(s, source);
    QVERIFY(!automatic.data.isEmpty());
    measurements["autoResetNativeFormat"] =
        automatic.params.value("format").toString();
  }
  void ac3PassthroughPayloadAndConflicts() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    s.setValue("audio/passthrough", true);
    s.setValue("audio/spdifCodecs", QStringList{"ac3"});
    auto o = render(s, ac3);
    QVERIFY(!o.data.isEmpty());
    QVERIFY2(o.params.value("format").toString().contains("ac3"),
             qPrintable(o.params.value("format").toString()));
    QVERIFY(o.data.contains(QByteArray::fromHex("72f81f4e")));
    const auto burst = o.data.indexOf(QByteArray::fromHex("72f81f4e"));
    const int payloadBytes =
        (quint8(o.data[burst + 6]) | int(quint8(o.data[burst + 7])) << 8) / 8;
    auto payload = o.data.mid(burst + 8, payloadBytes);
    for (int i = 0; i + 1 < payload.size(); i += 2)
      std::swap(payload.data()[i], payload.data()[i + 1]);
    QFile encoded(ac3);
    QVERIFY(encoded.open(QIODevice::ReadOnly));
    QVERIFY(payloadBytes > 100);
    QCOMPARE(payload, encoded.read(payloadBytes));
    measurements["ac3BitstreamFormat"] = o.params.value("format").toString();
    measurements["iec61937Ac3PreambleFound"] = true;
    measurements["ac3PayloadMatchesSource"] = true;
    o = render(s, ac3, [&](Player &p) {
      QVERIFY(p.get("pause").toBool());
      s.setValue("audio/gainDb", -6);
      QVERIFY(AudioProcessing::apply(&p, s));
      // During decoder reinitialization the output format can temporarily be
      // absent. Require initialized PCM before testing the next transition;
      // an absent format is not evidence of a successful decoder restart.
      QTRY_VERIFY_WITH_TIMEOUT(
          [&] {
            const auto format =
                p.get("audio-out-params").toMap().value("format").toString();
            return !format.isEmpty() && !format.contains("ac3") &&
                   !p.get("audio-params")
                        .toMap()
                        .value("format")
                        .toString()
                        .isEmpty() &&
                   p.get("aid").toInt() > 0;
          }(),
          5000);
      QVERIFY(p.get("pause").toBool());
      s.setValue("audio/gainDb", 0);
      QVERIFY(AudioProcessing::apply(&p, s));
      QTRY_VERIFY_WITH_TIMEOUT(p.get("audio-out-params")
                                   .toMap()
                                   .value("format")
                                   .toString()
                                   .contains("ac3"),
                               5000);
      QVERIFY(p.get("pause").toBool());
    });
    QVERIFY(!o.data.isEmpty());
    for (const auto &key :
         QStringList{"normalize", "dualOutput", "decoderDownmix"}) {
      s.setValue("audio/" + key, true);
      o = render(s, ac3);
      QVERIFY(!o.data.isEmpty());
      QVERIFY(!o.params.value("format").toString().contains("ac3"));
      s.setValue("audio/" + key, false);
    }
    s.setValue("audio/ac3Drc", .5);
    o = render(s, ac3);
    QVERIFY(!o.data.isEmpty());
    QVERIFY(!o.params.value("format").toString().contains("ac3"));
    s.setValue("audio/ac3Drc", 0);
    o = render(s, ac3);
    QVERIFY(o.params.value("format").toString().contains("ac3"));
    s.setValue("audio/spdifCodecs", QStringList{});
    o = render(s, ac3);
    QVERIFY(!o.data.isEmpty());
    QVERIFY(!o.params.value("format").toString().contains("ac3"));
  }
  void pausedTransitionsPreserveSourceAndPosition_data() {
    QTest::addColumn<bool>("video");
    QTest::newRow("audio-only") << false;
    QTest::newRow("audio-video") << true;
  }
  void pausedTransitionsPreserveSourceAndPosition() {
    QFETCH(bool, video);
    QTemporaryDir dir;
    QSettings settings(dir.filePath("profile.ini"), QSettings::IniFormat);
    settings.setValue("audio/passthrough", true);
    settings.setValue("audio/spdifCodecs", QStringList{"ac3"});
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"),
             {{"ao", "pcm"},
              {"ao-pcm-file", dir.filePath("transitions.wav")},
              {"audio-display", "no"},
              {"gapless-audio", "no"}});
    p.resize(320, 180);
    p.show();
    QVERIFY(AudioProcessing::apply(&p, settings));
    QSignalSpy loaded(&p, &Player::loaded), ended(&p, &Player::ended),
        errors(&p, &Player::error);
    QVERIFY(p.loadSource(video ? videoAc3 : ac3, {{"pause", "yes"}}));
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(p.get("audio-out-params")
                    .toMap()
                    .value("format")
                    .toString()
                    .contains("ac3"));
    QVERIFY(p.command({"seek", "0.7", "absolute+exact"}));
    QTRY_VERIFY(!p.get("seeking").toBool() &&
                std::abs(p.get("time-pos").toDouble() - .7) < .04);
    const auto position = p.get("time-pos").toDouble();
    const auto currentSource = p.get("path").toString();
    const auto track = p.get("aid").toInt();
    const int cycles =
        qEnvironmentVariableIntValue("FOXICMP_AUDIO_STRESS_CYCLES") > 0
            ? qEnvironmentVariableIntValue("FOXICMP_AUDIO_STRESS_CYCLES")
            : 12;
    for (int cycle = 0; cycle < cycles; ++cycle) {
      for (const bool pcm : {true, false}) {
        settings.setValue("audio/gainDb", pcm ? -6 : 0);
        QVERIFY(AudioProcessing::apply(&p, settings));
        QElapsedTimer deadline;
        deadline.start();
        bool ready = false;
        do {
          const auto format =
              p.get("audio-out-params").toMap().value("format").toString();
          ready = !format.isEmpty() && (format.contains("ac3") != pcm) &&
                  !p.get("seeking").toBool();
          if (ready || !ended.isEmpty())
            break;
          QTest::qWait(5);
        } while (deadline.elapsed() < 1500);
        QVERIFY2(
            ready,
            qPrintable(
                QString("cycle=%1 pcm=%2 aid=%3 pos=%4 audio=%5 out=%6 "
                        "ended=%7 errors=%8")
                    .arg(cycle)
                    .arg(pcm)
                    .arg(p.get("aid").toString())
                    .arg(p.get("time-pos").toDouble())
                    .arg(QString::fromUtf8(
                        QJsonDocument::fromVariant(p.get("audio-params"))
                            .toJson(QJsonDocument::Compact)))
                    .arg(QString::fromUtf8(
                        QJsonDocument::fromVariant(p.get("audio-out-params"))
                            .toJson(QJsonDocument::Compact)))
                    .arg(ended.size())
                    .arg(errors.size())));
        QVERIFY(p.get("pause").toBool());
        QCOMPARE(p.get("path").toString(), currentSource);
        QCOMPARE(p.get("aid").toInt(), track);
        QVERIFY2(std::abs(p.get("time-pos").toDouble() - position) < .04,
                 qPrintable(QString("Paused position changed from %1 to %2")
                                .arg(position)
                                .arg(p.get("time-pos").toDouble())));
        QCOMPARE(ended.size(), 0);
        QCOMPARE(loaded.size(), 1);
      }
    }
    for (int step = 0; step < 12; ++step) {
      settings.setValue("audio/gainDb", step % 2 ? -6 : 0);
      QVERIFY(AudioProcessing::apply(&p, settings));
    }
    QTRY_VERIFY(!p.get("seeking").toBool() && !p.get("audio-out-params")
                                                   .toMap()
                                                   .value("format")
                                                   .toString()
                                                   .isEmpty());
    QVERIFY(p.get("pause").toBool());
    QVERIFY(std::abs(p.get("time-pos").toDouble() - position) < .04);
    QCOMPARE(ended.size(), 0);
    QCOMPARE(loaded.size(), 1);
    settings.setValue("audio/gainDb", 0);
    QVERIFY(AudioProcessing::apply(&p, settings));
    QVERIFY(p.loadSource(source, {{"pause", "yes"}}));
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(!p.get("audio-out-params")
                     .toMap()
                     .value("format")
                     .toString()
                     .isEmpty());
    QCOMPARE(p.get("path").toString(), source);
    QVERIFY(p.get("pause").toBool());
    QVERIFY(p.get("time-pos").toDouble() < .1);
    QCOMPARE(ended.size(), 1);
    // Disabled audio must stay disabled when changing decoder options.
    QVERIFY(p.set("aid", "no"));
    settings.setValue("audio/decoderThreads", 2);
    QVERIFY(AudioProcessing::apply(&p, settings));
    QVERIFY(p.get("aid").toInt() <= 0);
    measurements[video ? "pausedTransitionCyclesVideo"
                       : "pausedTransitionCyclesAudio"] = cycles;
    measurements["pausedTransitionSourceAndPositionPreserved"] = true;
  }
  void decoderPreferencesFallbackAndSpeedPolicy() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    s.setValue("audio/preferredDecoder", "mp3");
    s.setValue("audio/decoderThreads", 2);
    auto o = render(s, mp3);
    QVERIFY(!o.data.isEmpty());
    o = render(s, ac3);
    QVERIFY(!o.data.isEmpty());
    s.setValue("audio/preferredDecoder", "missing_decoder_foxicmp");
    o = render(s, source, [&](Player &p) {
      QVERIFY(p.get("ad").toString().isEmpty());
      QVERIFY(p.QObject::property("foxicmpAudioEncodingStatus")
                  .toString()
                  .contains("недоступен"));
    });
    QVERIFY(!o.data.isEmpty());
    s.setValue("audio/preferredDecoder", "");
    s.setValue("audio/passthrough", true);
    o = render(s, source, [&](Player &p) {
      p.set("speed", 1.5);
      QVERIFY(AudioProcessing::apply(&p, s));
      QVERIFY(p.get("audio-spdif").toString().isEmpty());
      p.set("speed", 1);
      QVERIFY(AudioProcessing::apply(&p, s));
      QVERIFY(p.get("audio-spdif").toString().contains("ac3"));
      QVERIFY(p.get("pause").toBool());
    });
    QVERIFY(!o.data.isEmpty());
    s.setValue("audio/decoderDownmix", true);
    s.setValue("audio/mixer", true);
    Player p;
    QVERIFY(AudioProcessing::apply(&p, s));
    QVERIFY(!p.get("ad-lavc-downmix").toBool());
  }
  void preferencesCancelApplyResetAndScreenshot() {
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
    for (int step = 0; step < 3; ++step) {
      QTimer::singleShot(60, &w, [&] {
        auto *d = w.findChild<QDialog *>("audioProcessingDialog");
        QVERIFY(d);
        auto *tabs = d->findChild<QTabWidget *>();
        tabs->setCurrentWidget(
            d->findChild<QScrollArea *>("audioEncodingPage"));
        auto *format = d->findChild<QComboBox *>("audio/pcmFormat");
        format->setCurrentIndex(format->findData("s24"));
        auto *rate = d->findChild<QComboBox *>("audio/sampleRate");
        rate->setCurrentIndex(rate->findData(96000));
        d->findChild<QSpinBox *>("audio/decoderThreads")->setValue(2);
        d->findChild<QDoubleSpinBox *>("audio/ac3Drc")->setValue(.5);
        d->findChild<QCheckBox *>("audio/passthrough")->setChecked(true);
        if (step == 2)
          d->findChild<QPushButton *>("audioEncodingReset")->click();
        const auto path = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        if (step == 1 && !path.isEmpty())
          QVERIFY(d->grab().save(path + "/audio-encoding.png"));
        d->findChild<QDialogButtonBox *>()
            ->button(step == 0 ? QDialogButtonBox::Cancel
                               : QDialogButtonBox::Ok)
            ->click();
      });
      w.findChild<QAction *>("action_audioProcessingSettings")->trigger();
      if (step == 0)
        QVERIFY(!profile.settings.contains("audio/pcmFormat"));
      if (step == 1) {
        QCOMPARE(profile.settings.value("audio/pcmFormat").toString(),
                 QString("s24"));
        QCOMPARE(profile.settings.value("audio/sampleRate").toInt(), 96000);
        QVERIFY(p->get("pause").toBool());
        QVERIFY(p->get("audio-spdif").toString().isEmpty());
      }
      if (step == 2) {
        QCOMPARE(profile.settings.value("audio/pcmFormat").toString(),
                 QString("auto"));
        QVERIFY(!profile.settings.value("audio/passthrough").toBool());
        QVERIFY(p->get("af").toList().isEmpty());
        QVERIFY(p->get("pause").toBool());
      }
    }
  }
  void cleanupTestCase() {
    const auto out = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!out.isEmpty()) {
      QSaveFile f(out + "/audio-encoding-measurements.json");
      QVERIFY(f.open(QIODevice::WriteOnly));
      auto bytes = QJsonDocument(measurements).toJson();
      QCOMPARE(f.write(bytes), bytes.size());
      QVERIFY(f.commit());
    }
  }
};
QTEST_MAIN(AudioEncodingTest)
#include "audioencoding_test.moc"
