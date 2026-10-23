#include "preferencesdriver.h"
#include "audioprocessing.h"
#include "mediaoptions.h"
#include "player.h"
#include "profile.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>
#include <cstring>
#include <limits>
class AudioProcessingTest : public QObject {
  Q_OBJECT
  QTemporaryDir fixtures;
  QString stereo, surround, tagged, dc;
  QJsonObject measurements;
  struct Signal {
    int channels = 0, rate = 0;
    QVector<float> samples;
    double rms(int channel, int skipFrames = 0) const {
      double sum = 0;
      int n = 0;
      for (qsizetype i = channel + skipFrames * channels; i < samples.size();
           i += channels) {
        sum += double(samples[i]) * samples[i];
        ++n;
      }
      return n ? std::sqrt(sum / n) : 0;
    }
    double tailRms(int channel) const {
      return rms(channel,
                 std::max(0, int(samples.size() / channels) - rate / 2));
    }
    double peak() const {
      double result = 0;
      for (auto value : samples)
        result = std::max(result, std::abs(double(value)));
      return result;
    }
    double tone(int channel, int frequency) const {
      double x = 0, y = 0;
      const auto n = samples.size() / channels;
      for (qsizetype i = 0; i < n; ++i) {
        const auto phase = 2 * 3.141592653589793 * frequency * i / rate;
        x += samples[i * channels + channel] * std::cos(phase);
        y += samples[i * channels + channel] * std::sin(phase);
      }
      return n ? 2 * std::hypot(x, y) / n : 0;
    }
  };
  void ffmpeg(const QStringList &arguments) {
    QProcess process;
    process.start("ffmpeg",
                  QStringList{"-y", "-nostdin", "-v", "error"} + arguments);
    QVERIFY(process.waitForFinished(10000));
    QVERIFY2(process.exitCode() == 0,
             process.readAllStandardError().constData());
  }
  Signal read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
      return {};
    const auto bytes = file.readAll();
    if (bytes.size() < 44 || bytes.left(4) != "RIFF")
      return {};
    const auto u16 = [&](qsizetype i) {
      return quint16(quint8(bytes[i]) | (quint16(quint8(bytes[i + 1])) << 8));
    };
    const auto u32 = [&](qsizetype i) {
      return quint32(u16(i)) | (quint32(u16(i + 2)) << 16);
    };
    Signal signal;
    quint16 format = 0, bits = 0;
    for (qsizetype i = 12; i + 8 <= bytes.size();) {
      const auto size = u32(i + 4);
      const auto body = i + 8;
      if (bytes.mid(i, 4) == "fmt " && size >= 16) {
        format = u16(body);
        signal.channels = u16(body + 2);
        signal.rate = u32(body + 4);
        bits = u16(body + 14);
        if (format == 0xfffe && size >= 40)
          format = u16(body + 24);
      }
      if (bytes.mid(i, 4) == "data") {
        if (format != 3 || bits != 32 || signal.channels <= 0)
          return {};
        const auto length = std::min<qsizetype>(size, bytes.size() - body);
        signal.samples.resize(length / 4);
        std::memcpy(signal.samples.data(), bytes.constData() + body,
                    length / 4 * 4);
        return signal;
      }
      if (size > quint32(bytes.size() - body))
        return {};
      i = body + size + (size & 1);
    }
    return {};
  }
  Signal render(QSettings &s, const QString &source,
                std::function<void(Player &)> beforeStart = {}) {
    static int serial = 0;
    const auto destination =
        fixtures.filePath(QString("render-%1.wav").arg(++serial));
    {
      Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"),
               {{"ao", "pcm"},
                {"ao-pcm-file", destination},
                {"audio-format", "float"},
                {"audio-samplerate", "48000"},
                {"audio-display", "no"},
                {"gapless-audio", "no"}});
      p.resize(320, 180);
      p.show();
      p.set("volume", 100);
      p.set("mute", false);
      if (!AudioProcessing::apply(&p, s))
        return {};
      QSignalSpy ended(&p, &Player::ended);
      if (!p.loadSource(source, {{"pause", "yes"}}))
        return {};
      QElapsedTimer timeout;
      timeout.start();
      while (!p.get("audio-out-params").isValid() && timeout.elapsed() < 5000)
        QTest::qWait(10);
      if (!p.get("audio-out-params").isValid())
        return {};
      if (beforeStart)
        beforeStart(p);
      p.set("pause", false);
      while (ended.isEmpty() && timeout.elapsed() < 12000)
        QTest::qWait(10);
      if (ended.isEmpty() || !ended.first().first().toBool())
        return {};
    }
    return read(destination);
  }
private slots:
  void initTestCase() {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QVERIFY(fixtures.isValid());
    stereo = fixtures.filePath("stereo.wav");
    surround = fixtures.filePath("surround.wav");
    tagged = fixtures.filePath("tagged.flac");
    dc = fixtures.filePath("dc.wav");
    ffmpeg({"-f", "lavfi", "-i",
            "aevalsrc=0.03+0.02*sin(2*PI*440*t)|0.03+0.02*sin(2*PI*880*t):s="
            "48000:d=3:c=stereo",
            "-c:a", "pcm_f32le", dc});
    ffmpeg({"-f", "lavfi", "-i",
            "aevalsrc=0.05*sin(2*PI*440*t)|0.1*sin(2*PI*880*t):s=48000:d=3:c="
            "stereo",
            "-c:a", "pcm_f32le", stereo});
    ffmpeg({"-f", "lavfi", "-i",
            "aevalsrc=0.05*sin(2*PI*200*t)|0.05*sin(2*PI*300*t)|0.05*sin(2*PI*"
            "400*t)|0.05*sin(2*PI*80*t)|0.05*sin(2*PI*600*t)|0.05*sin(2*PI*700*"
            "t):s=48000:d=1:c=5.1",
            "-c:a", "pcm_f32le", surround});
    ffmpeg({"-i", stereo, "-c:a", "flac", "-metadata",
            "REPLAYGAIN_TRACK_GAIN=-6.0206 dB", "-metadata",
            "REPLAYGAIN_TRACK_PEAK=0.1", "-metadata",
            "REPLAYGAIN_ALBUM_GAIN=-12.0412 dB", "-metadata",
            "REPLAYGAIN_ALBUM_PEAK=0.1", tagged});
  }
  void sanitizedDefaultsAndLegacyNormalization() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("test.ini"), QSettings::IniFormat);
    QCOMPARE(AudioProcessing::values(s), AudioProcessing::defaults());
    QVERIFY(AudioProcessing::graph(AudioProcessing::values(s)).isEmpty());
    s.setValue("audio/balance", 900);
    s.setValue("audio/gainDb", std::numeric_limits<double>::quiet_NaN());
    s.setValue("audio/mixLayout", "invalid");
    s.setValue("audio/replaygain", "invalid");
    s.setValue("audio/normalizeWindow", 4);
    const auto v = AudioProcessing::values(s);
    QCOMPARE(v.value("balance").toInt(), 100);
    QCOMPARE(v.value("gainDb").toInt(), 0);
    QCOMPARE(v.value("normalizeWindow").toInt(), 5);
    QCOMPARE(v.value("mixLayout").toString(), QString("stereo"));
    QCOMPARE(v.value("replaygain").toString(), QString("no"));
    s.clear();
    s.setValue("audio/normalize", true);
    const auto legacy = AudioProcessing::graph(AudioProcessing::values(s));
    QVERIFY(legacy.contains("dynaudnorm=f=250:g=31:p=0.900000:m=10.000000"));
  }
  void gainBalanceChannelRoutingAndNeutralResetSamples() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("test.ini"), QSettings::IniFormat);
    s.setValue("audio/channels", "stereo");
    const auto baseline = render(s, stereo);
    QCOMPARE(baseline.channels, 2);
    QVERIFY(std::abs(baseline.rms(0) - .05 / std::sqrt(2.)) < .0001);
    QVERIFY(std::abs(baseline.rms(1) - .1 / std::sqrt(2.)) < .0001);
    measurements["baselineRmsLeft"] = baseline.rms(0);
    measurements["baselineRmsRight"] = baseline.rms(1);
    s.setValue("audio/gainDb", 6.0206);
    auto signal = render(s, stereo);
    QCOMPARE(signal.channels, 2);
    measurements["gain6dBRatio"] = signal.rms(0) / baseline.rms(0);
    QVERIFY(std::abs(signal.rms(0) / baseline.rms(0) - 2) < .01);
    s.setValue("audio/gainDb", 0);
    s.setValue("audio/balance", 100);
    signal = render(s, stereo);
    QCOMPARE(signal.channels, 2);
    measurements["balanceFullRightLeftRms"] = signal.rms(0);
    QVERIFY(signal.rms(0) < .000001);
    QVERIFY(std::abs(signal.rms(1) / baseline.rms(1) - 1) < .001);
    s.setValue("audio/balance", -50);
    signal = render(s, stereo);
    QVERIFY(std::abs(signal.rms(1) / baseline.rms(1) - .5) < .001);
    s.setValue("audio/balance", 0);
    for (const auto &mode : {"swap", "left", "right", "mono"}) {
      s.setValue("audio/channelMode", mode);
      signal = render(s, stereo);
      QCOMPARE(signal.channels, 2);
      if (QString(mode) == "swap") {
        QVERIFY(signal.tone(0, 880) > .099);
        QVERIFY(signal.tone(1, 440) > .049);
      } else if (QString(mode) == "left") {
        QVERIFY(signal.tone(1, 440) > .049);
        QVERIFY(signal.tone(1, 880) < .0001);
      } else if (QString(mode) == "right") {
        QVERIFY(signal.tone(0, 880) > .099);
        QVERIFY(signal.tone(0, 440) < .0001);
      } else {
        QVERIFY(std::abs(signal.tone(0, 440) - .025) < .0001);
        QVERIFY(std::abs(signal.tone(1, 880) - .05) < .0001);
      }
    }
    s.setValue("audio/channelMode", "normal");
    signal = render(s, stereo);
    QCOMPARE(signal.samples, baseline.samples);
  }
  void runtimeGraphChangesOnPauseAndResetSamples() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("test.ini"), QSettings::IniFormat);
    s.setValue("audio/channels", "stereo");
    const auto baseline = render(s, stereo);
    const auto raised = render(s, stereo, [&](Player &p) {
      QVERIFY(p.get("pause").toBool());
      s.setValue("audio/gainDb", 6.0206);
      QVERIFY(AudioProcessing::apply(&p, s));
      QVERIFY(AudioProcessing::apply(&p, s));
      QVERIFY(p.get("pause").toBool());
    });
    QCOMPARE(raised.channels, 2);
    // PCM file output is untimed and can prepare a large prefix before
    // filters change. Measure the settled final half-second, not that prefix.
    measurements["runtimeGain6dBRatio"] =
        raised.tailRms(0) / baseline.tailRms(0);
    QVERIFY(std::abs(raised.tailRms(0) / baseline.tailRms(0) - 2.) < .01);
    const auto reset = render(s, stereo, [&](Player &p) {
      s.setValue("audio/gainDb", 0);
      QVERIFY(AudioProcessing::apply(&p, s));
    });
    QCOMPARE(reset.channels, 2);
    QVERIFY(std::abs(reset.tailRms(0) / baseline.tailRms(0) - 1.) < .001);
  }
  void surroundCenterLfeLevelsAndLayoutsSamples() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("test.ini"), QSettings::IniFormat);
    s.setValue("audio/mixer", true);
    s.setValue("audio/mixLayout", "stereo");
    s.setValue("audio/mixNormalize", false);
    s.setValue("audio/centerLevel", 0);
    s.setValue("audio/surroundLevel", 0);
    s.setValue("audio/lfeLevel", 0);
    auto signal = render(s, surround);
    QCOMPARE(signal.channels, 2);
    QVERIFY(signal.tone(0, 200) > .049);
    QVERIFY(signal.tone(0, 400) < .0001);
    QVERIFY(signal.tone(0, 80) < .0001);
    s.setValue("audio/centerLevel", 1);
    s.setValue("audio/lfeLevel", 1);
    s.setValue("audio/surroundLevel", 1);
    signal = render(s, surround);
    QCOMPARE(signal.channels, 2);
    QVERIFY(signal.tone(0, 400) > .04);
    QVERIFY(signal.tone(0, 80) > .02);
    QVERIFY(signal.tone(0, 600) > .04);
    QVERIFY(signal.tone(1, 700) > .04);
    measurements["center400HzInLeft"] = signal.tone(0, 400);
    measurements["lfe80HzInLeft"] = signal.tone(0, 80);
    measurements["surround600HzInLeft"] = signal.tone(0, 600);
    const auto mixed = signal.rms(0);
    s.setValue("audio/mixNormalize", true);
    signal = render(s, surround);
    measurements["matrixNormalizationRmsRatio"] = signal.rms(0) / mixed;
    QVERIFY(signal.rms(0) < mixed * .7);
    for (const auto &layout : {"quad", "4.0", "5.0", "5.1", "7.1"}) {
      s.setValue("audio/mixLayout", layout);
      signal = render(s, surround);
      const int count = QString(layout) == "7.1"   ? 8
                        : QString(layout) == "5.1" ? 6
                        : QString(layout) == "5.0" ? 5
                                                   : 4;
      QCOMPARE(signal.channels, count);
      QVERIFY(!signal.samples.isEmpty());
    }
  }
  void normalizerLimiterAndReplaygainSamples() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("test.ini"), QSettings::IniFormat);
    s.setValue("audio/channels", "stereo");
    const auto baseline = render(s, stereo);
    s.setValue("audio/normalize", true);
    s.setValue("audio/normalizeFrameMs", 50);
    s.setValue("audio/normalizeWindow", 5);
    s.setValue("audio/normalizePeak", .5);
    s.setValue("audio/normalizeMaxGain", 4);
    auto signal = render(s, stereo);
    QCOMPARE(signal.channels, 2);
    measurements["normalizerRmsRatio"] = signal.rms(0) / baseline.rms(0);
    measurements["normalizerPeak"] = signal.peak();
    QVERIFY(signal.rms(0) > baseline.rms(0) * 1.4);
    QVERIFY(signal.peak() <= .501);
    s.setValue("audio/normalizeMaxGain", 100);
    const auto coupled = render(s, stereo);
    s.setValue("audio/normalizeCouple", false);
    const auto separate = render(s, stereo);
    QVERIFY(separate.rms(0) > coupled.rms(0) * 1.7);
    measurements["uncoupledVsCoupledLeftRmsRatio"] =
        separate.rms(0) / coupled.rms(0);
    s.setValue("audio/normalizeCouple", true);
    s.setValue("audio/normalizeRms", .1);
    const auto rmsLimited = render(s, stereo);
    QCOMPARE(rmsLimited.channels, 2);
    QVERIFY(rmsLimited.rms(0) < coupled.rms(0) * .7);
    measurements["targetRmsVsPeakOnlyLeftRatio"] =
        rmsLimited.rms(0) / coupled.rms(0);
    s.setValue("audio/normalizeRms", 0);
    s.setValue("audio/normalizeMaxGain", 1);
    s.setValue("audio/normalizeDc", true);
    const auto corrected = render(s, dc);
    QCOMPARE(corrected.channels, 2);
    double mean = 0;
    for (qsizetype i = 0; i < corrected.samples.size(); i += 2)
      mean += corrected.samples[i];
    mean /= corrected.samples.size() / 2;
    measurements["dcCorrectedMeanLeft"] = mean;
    QVERIFY(std::abs(mean) < .003);
    s.setValue("audio/normalize", false);
    s.setValue("audio/gainDb", 20);
    s.setValue("audio/limiter", true);
    s.setValue("audio/limiterPeak", .4);
    signal = render(s, stereo);
    measurements["limiterPeak"] = signal.peak();
    QVERIFY(signal.peak() <= .401 && signal.peak() > .38);
    s.setValue("audio/gainDb", 0);
    s.setValue("audio/limiter", false);
    const auto taggedBase = render(s, tagged);
    for (const auto &mode : {"track", "album"}) {
      s.setValue("audio/replaygain", mode);
      signal = render(s, tagged);
      measurements[QString("replaygain_") + mode] =
          signal.rms(0) / taggedBase.rms(0);
      const double gain = QString(mode) == "track" ? .5 : .25;
      QVERIFY2(std::abs(signal.rms(0) / taggedBase.rms(0) - gain) < .01,
               qPrintable(QString("ReplayGain %1 ratio %2")
                              .arg(mode)
                              .arg(signal.rms(0) / taggedBase.rms(0))));
    }
    s.setValue("audio/replaygain", "track");
    s.setValue("audio/replaygainPreamp", 6.0206);
    signal = render(s, tagged);
    QVERIFY(std::abs(signal.rms(0) / taggedBase.rms(0) - 1.) < .01);
    measurements["replaygainPreampCancellationRatio"] =
        signal.rms(0) / taggedBase.rms(0);
    s.setValue("audio/replaygainPreamp", 0);
    s.setValue("audio/replaygainFallback", -6.0206);
    signal = render(s, stereo);
    QVERIFY(std::abs(signal.rms(0) / baseline.rms(0) - .5) < .01);
  }
  void preferencesCancelApplyResetAndRuntimePreservesPause() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({stereo});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    auto *action =
        window.findChild<QAction *>("action_audioProcessingSettings");
    QVERIFY(action);
    QCOMPARE(action->shortcut(), QKeySequence("Ctrl+Alt+A"));
    const auto edit = [&](bool save, bool reset) {
      QTimer::singleShot(80, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("audioProcessingDialog");
        QVERIFY(dialog);
        dialog->findChild<QDoubleSpinBox *>("audio/gainDb")->setValue(3);
        dialog->findChild<QDoubleSpinBox *>("audio/balance")->setValue(-40);
        dialog->findChild<QCheckBox *>("audio/normalize")->setChecked(true);
        dialog->findChild<QDoubleSpinBox *>("audio/normalizeWindow")
            ->setValue(4);
        dialog->findChild<QCheckBox *>("audio/mixer")->setChecked(true);
        if (reset)
          dialog->findChild<QPushButton *>("audioProcessingReset")->click();
        auto *tabs = dialog->findChild<QTabWidget *>();
        QCOMPARE(tabs->count(), 6);
        const auto screenshots =
            qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        if (save && !reset && !screenshots.isEmpty())
          for (int i = 0; i < 3; ++i) {
            tabs->setCurrentIndex(i);
            QTest::qWait(30);
            dialog->grab().save(screenshots +
                                QString("/audio-processing-%1.png").arg(i));
          }
        dialog->findChild<QDialogButtonBox *>()
            ->button(save ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
      });
      action->trigger();
    };
    edit(false, false);
    QVERIFY(!profile.settings.contains("audio/gainDb"));
    edit(true, false);
    QCOMPARE(profile.settings.value("audio/gainDb").toDouble(), 3.);
    QCOMPARE(profile.settings.value("audio/normalizeWindow").toInt(), 5);
    QVERIFY(player->get("pause").toBool());
    QVERIFY(!player->get("af").toList().isEmpty());
    edit(true, true);
    QCOMPARE(profile.settings.value("audio/gainDb").toDouble(), 0.);
    QVERIFY(!profile.settings.value("audio/normalize").toBool());
    QVERIFY(!profile.settings.value("audio/mixer").toBool());
    QVERIFY(player->get("af").toList().isEmpty());
    QVERIFY(player->get("pause").toBool());
    afterPreferencesReady(&window, [&] {
      auto *dialog = window.findChild<QDialog *>("preferencesDialog");
      QVERIFY(dialog);
      const auto copies = dialog->findChildren<QCheckBox *>("audio/normalize");
      QCOMPARE(copies.size(), 2);
      copies.first()->setChecked(true);
      QVERIFY(copies.last()->isChecked());
      copies.last()->setChecked(false);
      QVERIFY(!copies.first()->isChecked());
      copies.first()->setChecked(true);
      dialog->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Ok)
          ->click();
    });
    window.findChild<QAction *>("action_preferences")->trigger();
    QVERIFY(profile.settings.value("audio/normalize").toBool());
    QVERIFY(!player->get("af").toList().isEmpty());
  }
  void customChainMeasuredAndProfileValidation() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("test.ini"), QSettings::IniFormat);
    s.setValue("audio/channels", "stereo");
    const auto baseline = render(s, stereo);
    s.setValue("audio/customGraph", "volume=0.5");
    auto signal = render(s, stereo);
    QCOMPARE(signal.samples, baseline.samples);
    s.setValue("audio/customEnabled", true);
    signal = render(s, stereo);
    QCOMPARE(signal.channels, 2);
    measurements["customVolumeHalfRatio"] = signal.rms(0) / baseline.rms(0);
    QVERIFY(std::abs(signal.rms(0) / baseline.rms(0) - .5) < .001);
    s.setValue("audio/gainDb", 6.0206);
    signal = render(s, stereo);
    QVERIFY(std::abs(signal.rms(0) / baseline.rms(0) - 1) < .001);
    s.setValue("audio/gainDb", 0);
    s.setValue("audio/customGraph", "lowpass=f=500");
    signal = render(s, stereo);
    measurements["customLowpass880Ratio"] =
        signal.tone(1, 880) / baseline.tone(1, 880);
    QVERIFY(signal.tone(1, 880) < baseline.tone(1, 880) * .4);
    s.setValue("audio/customGraph", "volume=20");
    s.setValue("audio/limiter", true);
    s.setValue("audio/limiterPeak", .4);
    signal = render(s, stereo);
    measurements["customBeforeLimiterPeak"] = signal.peak();
    QVERIFY(signal.peak() <= .401 && signal.peak() > .38);
    s.setValue("audio/limiter", false);
    s.setValue("audio/customGraph", "volume=0.5");
    signal = render(s, stereo, [&](Player &p) {
      s.setValue("audio/customGraph", "no_such_foxicmp_filter=1");
      QVERIFY(!AudioProcessing::apply(&p, s));
      QVERIFY(p.get("pause").toBool());
      QVERIFY(
          !p.QObject::property("foxicmpCustomGraphError").toString().isEmpty());
      QVERIFY(p.get("af").toList().isEmpty());
      s.setValue("audio/customGraph", "volume=0.25");
      QVERIFY(AudioProcessing::apply(&p, s));
      QVERIFY(
          p.QObject::property("foxicmpCustomGraphError").toString().isEmpty());
    });
    QCOMPARE(signal.channels, 2);
    QVERIFY(std::abs(signal.tailRms(0) / baseline.tailRms(0) - .25) < .001);
    QVERIFY(
        !AudioProcessing::validateGraph("no_such_foxicmp_filter").isEmpty());
    QVERIFY(!AudioProcessing::validateGraph(QString(8193, 'a')).isEmpty());
    QVERIFY(
        !AudioProcessing::validateGraph(QString("anull") + QChar(0)).isEmpty());
    QVERIFY(!AudioProcessing::validateGraph("asplit=2").isEmpty());
    QVERIFY(AudioProcessing::validateGraph("highpass=f=100,lowpass=f=8000")
                .isEmpty());
    QVERIFY(AudioProcessing::validateGraph("bass=g=3:f=100:w=0.5").isEmpty());
    QVERIFY(
        AudioProcessing::validateGraph("treble=g=3:f=3000:w=0.5").isEmpty());
  }
  void customFilterDialogTransactionsAndPresets() {
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
    w.openFiles({stereo});
    QTRY_COMPARE(loaded.size(), 1);
    p->set("pause", true);
    auto *action = w.findChild<QAction *>("action_audioProcessingSettings");
    const auto edit = [&](int step) {
      QTimer::singleShot(50, &w, [&] {
        auto *d = w.findChild<QDialog *>("audioProcessingDialog");
        QVERIFY(d);
        auto *tabs = d->findChild<QTabWidget *>();
        tabs->setCurrentWidget(d->findChild<QWidget *>("audioFiltersPage"));
        auto *enabled = d->findChild<QCheckBox *>("audio/customEnabled");
        auto *editor = d->findChild<QPlainTextEdit *>("audio/customGraph");
        auto *selector = d->findChild<QComboBox *>("audioFilterPresets");
        auto *buttons = d->findChild<QDialogButtonBox *>();
        if (step == 0 || step == 1) {
          selector->setCurrentIndex(4);
          d->findChild<QPushButton *>("audioFilterLoad")->click();
          QCOMPARE(editor->toPlainText(), QString("volume=-6dB"));
          enabled->setChecked(true);
          QTimer::singleShot(40, d, [&] {
            auto *input = d->findChild<QInputDialog *>();
            QVERIFY(input);
            input->setTextValue("Мой тихий звук");
            input->accept();
          });
          d->findChild<QPushButton *>("audioFilterSave")->click();
          QCOMPARE(selector->count(), 6);
          buttons
              ->button(step == 0 ? QDialogButtonBox::Cancel
                                 : QDialogButtonBox::Ok)
              ->click();
        } else if (step == 2) {
          editor->setPlainText("no_such_foxicmp_filter");
          buttons->button(QDialogButtonBox::Ok)->click();
          QVERIFY(d->isVisible());
          QVERIFY(
              !d->findChild<QLabel *>("audioFilterStatus")->text().isEmpty());
          QCOMPARE(profile.settings.value("audio/customGraph").toString(),
                   QString("volume=-6dB"));
          editor->setPlainText("highpass=f=100,lowpass=f=8000");
          d->findChild<QPushButton *>("audioFilterCheck")->click();
          QVERIFY(d->findChild<QLabel *>("audioFilterStatus")
                      ->text()
                      .contains("FFmpeg"));
          const auto output = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
          if (!output.isEmpty())
            QVERIFY(d->grab().save(output + "/audio-filters.png"));
          buttons->button(QDialogButtonBox::Ok)->click();
        } else {
          QCOMPARE(selector->count(), 6);
          selector->setCurrentIndex(5);
          d->findChild<QPushButton *>("audioFilterLoad")->click();
          QCOMPARE(editor->toPlainText(), QString("volume=-6dB"));
          d->findChild<QPushButton *>("audioFilterDelete")->click();
          QCOMPARE(selector->count(), 5);
          d->findChild<QPushButton *>("audioFilterReset")->click();
          QVERIFY(!enabled->isChecked());
          QVERIFY(editor->toPlainText().isEmpty());
          buttons->button(QDialogButtonBox::Ok)->click();
        }
      });
      action->trigger();
    };
    edit(0);
    QVERIFY(!profile.settings.contains("audio/customEnabled"));
    QVERIFY(!profile.settings.contains("audio/filterPresetsV1"));
    edit(1);
    QVERIFY(profile.settings.value("audio/customEnabled").toBool());
    QCOMPARE(QJsonDocument::fromJson(
                 profile.settings.value("audio/filterPresetsV1").toByteArray())
                 .array()
                 .size(),
             1);
    QVERIFY(p->get("pause").toBool());
    QVERIFY(!p->get("af").toList().isEmpty());
    edit(2);
    QVERIFY(profile.settings.value("audio/customGraph")
                .toString()
                .contains("highpass"));
    edit(3);
    QVERIFY(!profile.settings.value("audio/customEnabled").toBool());
    QVERIFY(p->get("af").toList().isEmpty());
    QVERIFY(p->get("pause").toBool());
    afterPreferencesReady(&w, [&] {
      auto *d = w.findChild<QDialog *>("preferencesDialog");
      QVERIFY(d);
      d->findChild<QCheckBox *>("audio/customEnabled")->setChecked(true);
      d->findChild<QPlainTextEdit *>("audio/customGraph")
          ->setPlainText("invalid_foxicmp_filter");
      auto *buttons = d->findChild<QDialogButtonBox *>();
      buttons->button(QDialogButtonBox::Ok)->click();
      QVERIFY(d->isVisible());
      QVERIFY(!profile.settings.value("audio/customEnabled").toBool());
      d->findChild<QCheckBox *>("audio/customEnabled")->setChecked(false);
      buttons->button(QDialogButtonBox::Ok)->click();
    });
    w.findChild<QAction *>("action_preferences")->trigger();
    QCOMPARE(profile.settings.value("audio/customGraph").toString(),
             QString("invalid_foxicmp_filter"));
    QVERIFY(p->get("af").toList().isEmpty());
  }
  void customPresetImportExportAndCancel() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("audio/mute", true);
    const QJsonArray initial{
        QJsonObject{{"name", "Мой пресет"}, {"graph", "volume=0.5"}}};
    profile.settings.setValue(
        "audio/filterPresetsV1",
        QJsonDocument(initial).toJson(QJsonDocument::Compact));
    Window w(profile);
    w.show();
    const auto path = dir.filePath("пресеты.json");
    const auto clickFile = [&](QDialog *d, const QString &button) {
      QTimer::singleShot(150, d, [&] {
        QFileDialog *file = nullptr;
        for (auto *candidate : d->findChildren<QFileDialog *>())
          if (candidate->isVisible())
            file = candidate;
        QVERIFY(file);
        auto *name = file->findChild<QLineEdit *>("fileNameEdit");
        QVERIFY(name);
        name->setText(path);
        QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
      });
      d->findChild<QPushButton *>(button)->click();
    };
    for (int step = 0; step < 3; ++step) {
      QTimer::singleShot(50, &w, [&] {
        auto *d = w.findChild<QDialog *>("audioProcessingDialog");
        QVERIFY(d);
        auto *selector = d->findChild<QComboBox *>("audioFilterPresets");
        QCOMPARE(selector->count(), 6);
        if (step == 0) {
          clickFile(d, "audioFilterExport");
          QFile f(path);
          QVERIFY(f.open(QIODevice::ReadOnly));
          auto object = QJsonDocument::fromJson(f.readAll()).object();
          QCOMPARE(object.value("format").toString(),
                   QString("FoxicMP-audio-filters-v1"));
          QCOMPARE(object.value("presets").toArray(), initial);
          f.close();
        }
        const auto write = [&](const QJsonObject &object) {
          QFile f(path);
          QVERIFY(f.open(QIODevice::WriteOnly));
          QVERIFY(f.write(QJsonDocument(object).toJson()) > 0);
        };
        write(QJsonObject{{"format", "unknown"}, {"presets", QJsonArray{}}});
        clickFile(d, "audioFilterImport");
        QCOMPARE(selector->count(), 6);
        QVERIFY(!d->findChild<QLabel *>("audioFilterStatus")->text().isEmpty());
        const QJsonArray imported{
            QJsonObject{{"name", "Новый"}, {"graph", "lowpass=f=500"}},
            QJsonObject{{"name", "Ещё один"}, {"graph", "anull"}}};
        write(QJsonObject{{"format", "FoxicMP-audio-filters-v1"},
                          {"presets", imported}});
        clickFile(d, "audioFilterImport");
        QCOMPARE(selector->count(), 7);
        QVERIFY(!profile.settings.value("audio/customEnabled", false).toBool());
        d->findChild<QDialogButtonBox *>()
            ->button(step == 2 ? QDialogButtonBox::Ok
                               : QDialogButtonBox::Cancel)
            ->click();
      });
      w.findChild<QAction *>("action_audioProcessingSettings")->trigger();
      const auto saved =
          QJsonDocument::fromJson(
              profile.settings.value("audio/filterPresetsV1").toByteArray())
              .array();
      QCOMPARE(saved.size(), step == 2 ? 2 : 1);
    }
    profile.settings.sync();
    Profile reopened(dir.path());
    QCOMPARE(QJsonDocument::fromJson(
                 reopened.settings.value("audio/filterPresetsV1").toByteArray())
                 .array()
                 .size(),
             2);
    QVERIFY(!reopened.settings.value("audio/customEnabled").toBool());
  }
  void cleanupTestCase() {
    const auto output = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!output.isEmpty()) {
      QSaveFile file(output + "/audio-processing-measurements.json");
      QVERIFY(file.open(QIODevice::WriteOnly));
      const auto bytes = QJsonDocument(measurements).toJson();
      QCOMPARE(file.write(bytes), bytes.size());
      QVERIFY(file.commit());
    }
  }
};
QTEST_MAIN(AudioProcessingTest)
#include "audioprocessing_test.moc"
