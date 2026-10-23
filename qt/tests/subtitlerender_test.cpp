#include "mediaoptions.h"
#include "player.h"
#include "profile.h"
#include "subtitlerender.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <limits>
class SubtitleRenderTest : public QObject {
  Q_OBJECT
  QTemporaryDir fixtures;
  QString video, embedded, subtitles;
  void write(const QString &path, const QByteArray &data) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(data), data.size());
  }
  QRect bounds(const QImage &image) {
    QRect result;
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x) {
        const auto c = image.pixelColor(x, y);
        if (std::max({c.red(), c.green(), c.blue()}) > 40)
          result = result.united(QRect(x, y, 1, 1));
      }
    return result;
  }
  void load(Player &p) {
    QSignalSpy loaded(&p, &Player::loaded);
    p.resize(640, 360);
    p.show();
    p.set("hwdec", "no");
    QVERIFY(p.loadSource(video, {{"pause", "yes"}}));
    QTRY_COMPARE(loaded.size(), 1);
    QVERIFY(p.command({"sub-add", subtitles, "select"}));
    QVERIFY(p.command({"seek", "1.4", "absolute+exact"}));
    QTRY_VERIFY(!bounds(p.grabFramebuffer()).isNull());
  }
  void settings(QSettings &s) {
    s.setValue("subtitles/autoLoad", false);
    s.setValue("subtitles/overrideAss", true);
    s.setValue("playback/hardwareDecoding", false);
    s.setValue("audio/mute", true);
  }
private slots:
  void initTestCase() {
    QVERIFY(fixtures.isValid());
    video = fixtures.filePath("black.mkv");
    embedded = fixtures.filePath("embedded.mkv");
    subtitles = fixtures.filePath("text.srt");
    write(subtitles, "1\n00:00:01,000 --> 00:00:02,000\nFirst MMMM\n\n"
                     "2\n00:00:02,120 --> 00:00:04,000\nSecond MMMM\n\n"
                     "3\n00:00:06,000 --> 00:00:10,000\nLast MMMM\n");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-y", "-nostdin", "-v", "error", "-f", "lavfi",
                            "-i", "color=black:s=640x360:r=25", "-t", "12",
                            "-c:v", "ffv1", video});
    QVERIFY(ffmpeg.waitForFinished(10000));
    QCOMPARE(ffmpeg.exitCode(), 0);
    ffmpeg.start("ffmpeg",
                 {"-y", "-nostdin", "-v", "error", "-i", video, "-i", subtitles,
                  "-map", "0:v", "-map", "1:0", "-c", "copy", embedded});
    QVERIFY(ffmpeg.waitForFinished(10000));
    QCOMPARE(ffmpeg.exitCode(), 0);
  }
  void sanitizedOptionsAndNativeDefaults() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("test.ini"), QSettings::IniFormat);
    QCOMPARE(SubtitleRender::values(s), SubtitleRender::defaults());
    s.setValue("subtitles/render/hinting", "invalid");
    s.setValue("subtitles/render/shaper", "injected");
    s.setValue("subtitles/render/colorCompat", "invalid");
    s.setValue("subtitles/render/bitmapBlur",
               std::numeric_limits<double>::quiet_NaN());
    QCOMPARE(SubtitleRender::values(s), SubtitleRender::defaults());
    s.setValue("subtitles/render/bitmapBlur", 100);
    QCOMPARE(SubtitleRender::values(s).value("bitmapBlur").toDouble(), 3.);
    s.setValue("subtitles/render/bitmapBlur", -1);
    QCOMPARE(SubtitleRender::values(s).value("bitmapBlur").toDouble(), 0.);
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    QSignalSpy errors(&p, &Player::error);
    QVERIFY(SubtitleRender::apply(&p, s));
    QVERIFY(errors.isEmpty());
    QCOMPARE(p.get("sub-ass-hinting").toString(), QString("none"));
    QCOMPARE(p.get("sub-ass-shaper").toString(), QString("complex"));
    QVERIFY(p.get("embeddedfonts").toBool());
    QVERIFY(!p.get("sub-clear-on-seek").toBool());
    s.setValue("subtitles/render/hinting", "native");
    s.setValue("subtitles/render/shaper", "simple");
    s.setValue("subtitles/render/colorCompat", "full");
    s.setValue("subtitles/render/bitmapBlur", 1.25);
    const auto defaults = SubtitleRender::defaults();
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
      if (it.value().typeId() == QMetaType::Bool)
        s.setValue("subtitles/render/" + it.key(), !it.value().toBool());
    QVERIFY(SubtitleRender::apply(&p, s));
    QCOMPARE(p.get("sub-ass-hinting").toString(), QString("native"));
    QCOMPARE(p.get("sub-ass-shaper").toString(), QString("simple"));
    QCOMPARE(p.get("sub-ass-vsfilter-color-compat").toString(),
             QString("full"));
    QCOMPARE(p.get("sub-gauss").toDouble(), 1.25);
    QVERIFY(!p.get("embeddedfonts").toBool());
    QVERIFY(p.get("sub-clear-on-seek").toBool());
    QVERIFY(p.get("sub-gray").toBool());
    QVERIFY(p.get("stretch-image-subs-to-screen").toBool());
    QVERIFY(errors.isEmpty());
  }
  void windowScalingAndHintingPixels() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("test.ini"), QSettings::IniFormat);
    settings(s);
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    MediaOptions::apply(&p, s);
    load(p);
    const auto large = bounds(p.grabFramebuffer());
    p.resize(320, 180);
    QTRY_VERIFY(bounds(p.grabFramebuffer()).width() < large.width() * .7);
    const auto small = bounds(p.grabFramebuffer());
    s.setValue("subtitles/render/scaleByWindow", false);
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(bounds(p.grabFramebuffer()).width() > small.width() + 15);
    s.setValue("subtitles/render/scaleByWindow", true);
    MediaOptions::apply(&p, s);
    p.resize(640, 360);
    QTRY_VERIFY(std::abs(bounds(p.grabFramebuffer()).width() - large.width()) <
                5);
    const auto before = p.grabFramebuffer();
    s.setValue("subtitles/render/hinting", "normal");
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(p.grabFramebuffer() != before);
    QVERIFY(p.get("pause").toBool());
    QVERIFY(std::abs(p.get("time-pos").toDouble() - 1.4) < .05);
  }
  void timingCorrectionAndCacheAcrossSeek() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("test.ini"), QSettings::IniFormat);
    settings(s);
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    MediaOptions::apply(&p, s);
    load(p);
    QVERIFY(p.command({"seek", "2.04", "absolute+exact"}));
    QTRY_VERIFY(std::abs(p.get("time-pos").toDouble() - 2.04) < .02);
    QTRY_VERIFY(bounds(p.grabFramebuffer()).isNull());
    s.setValue("subtitles/render/fixTiming", true);
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(!bounds(p.grabFramebuffer()).isNull());
    s.setValue("subtitles/render/fixTiming", false);
    s.setValue("subtitles/render/stretchDurations", true);
    MediaOptions::apply(&p, s);
    QVERIFY(p.command({"seek", "5", "absolute+exact"}));
    QTRY_VERIFY(std::abs(p.get("time-pos").toDouble() - 5) < .02);
    QTest::qWait(200);
    QTRY_COMPARE(p.get("sub-text").toString(), QString("Second MMMM"));
    QTRY_VERIFY(!bounds(p.grabFramebuffer()).isNull());
    for (bool clear : {false, true}) {
      s.setValue("subtitles/render/clearOnSeek", clear);
      s.setValue("subtitles/render/embeddedFonts", clear);
      MediaOptions::apply(&p, s);
      for (const auto &time : {"6.4", "1.4", "6.4"}) {
        QVERIFY(p.command({"seek", time, "absolute+exact"}));
        const QString expected =
            QString(time) == "1.4" ? "First MMMM" : "Last MMMM";
        QTRY_COMPARE(p.get("sub-text").toString(), expected);
        QTRY_VERIFY(!bounds(p.grabFramebuffer()).isNull());
      }
    }
    QVERIFY(p.get("pause").toBool());
    QVERIFY(p.set("pause", false));
    const auto start = p.get("time-pos").toDouble();
    QTRY_VERIFY(p.get("time-pos").toDouble() > start + .15);
    QVERIFY(!bounds(p.grabFramebuffer()).isNull());
  }
  void bothExternalTracksSurviveFontChangeOnPause() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("test.ini"), QSettings::IniFormat);
    settings(s);
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    MediaOptions::apply(&p, s);
    load(p);
    const auto second = fixtures.filePath("secondary.srt");
    write(second, "1\n00:00:01,000 --> 00:00:10,000\nSecondary MMMM\n");
    QVERIFY(p.command({"sub-add", second, "auto"}));
    QString secondary;
    QTRY_VERIFY(p.get("track-list").toList().size() >= 3);
    for (const auto &entry : p.get("track-list").toList()) {
      const auto track = entry.toMap();
      if (track.value("external-filename").toString() == second)
        secondary = track.value("id").toString();
    }
    QVERIFY(!secondary.isEmpty());
    QVERIFY(p.set("secondary-sid", secondary));
    const auto present = [&] {
      const auto frame = p.grabFramebuffer();
      const auto top = frame.copy(0, 0, frame.width(), frame.height() / 3);
      const auto bottom = frame.copy(0, frame.height() * 2 / 3, frame.width(),
                                     frame.height() / 3);
      return !bounds(top).isNull() && !bounds(bottom).isNull();
    };
    QTRY_VERIFY(present());
    s.setValue("subtitles/render/embeddedFonts", false);
    MediaOptions::apply(&p, s);
    QTest::qWait(300);
    QTRY_VERIFY(present());
    QTRY_COMPARE(p.get("sub-text").toString(), QString("First MMMM"));
    QTRY_COMPARE(p.get("secondary-sub-text").toString(),
                 QString("Secondary MMMM"));
    QVERIFY(p.get("pause").toBool());
    QVERIFY(std::abs(p.get("time-pos").toDouble() - 1.4) < .05);
  }
  void bitmapGrayscalePixelsAndTextUnaffected() {
    // A generated PGS display set: one solid yellow 64x16 object on a
    // 640x360 canvas. Tests exercise the decoder and rendered pixels.
    const auto segment = [](quint32 pts, quint8 type, const QByteArray &data) {
      QByteArray out;
      QDataStream stream(&out, QIODevice::WriteOnly);
      stream << quint8('P') << quint8('G') << pts << quint32(0) << type
             << quint16(data.size());
      return out + data;
    };
    QByteArray sup;
    const quint32 pts = 90000;
    sup +=
        segment(pts, 0x16,
                QByteArray::fromHex("02800168100000800000010000000000c800fa"));
    sup += segment(pts, 0x17, QByteArray::fromHex("010000c800fa00400010"));
    sup += segment(pts, 0x14, QByteArray::fromHex("000001d29210ff"));
    QByteArray rle;
    for (int i = 0; i < 16; ++i)
      rle += QByteArray::fromHex("00c040010000");
    QByteArray object;
    QDataStream os(&object, QIODevice::WriteOnly);
    const auto length = rle.size() + 4;
    os << quint16(0) << quint8(0) << quint8(0xc0) << quint8(length >> 16)
       << quint16(length & 0xffff) << quint16(64) << quint16(16);
    sup += segment(pts, 0x15, object + rle);
    sup += segment(pts, 0x80, {});
    sup += segment(900000, 0x16, QByteArray::fromHex("0280016810000100000000"));
    sup += segment(900000, 0x80, {});
    const auto path = fixtures.filePath("bitmap.sup");
    write(path, sup);
    QTemporaryDir dir;
    QSettings s(dir.filePath("test.ini"), QSettings::IniFormat);
    settings(s);
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    MediaOptions::apply(&p, s);
    load(p);
    QVERIFY(p.command({"sub-add", path, "select"}));
    const auto yellow = [](const QImage &frame) {
      int count = 0;
      for (int y = 0; y < frame.height(); ++y)
        for (int x = 0; x < frame.width(); ++x) {
          const auto c = frame.pixelColor(x, y);
          count += c.red() > 120 && c.green() > 120 && c.blue() < 80;
        }
      return count;
    };
    QTRY_VERIFY(yellow(p.grabFramebuffer()) > 500);
    s.setValue("subtitles/render/bitmapGray", true);
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(yellow(p.grabFramebuffer()) == 0 &&
                !bounds(p.grabFramebuffer()).isNull());
    s.setValue("subtitles/render/bitmapGray", false);
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(yellow(p.grabFramebuffer()) > 500);
    const auto crispImage = p.grabFramebuffer();
    const auto crisp = bounds(crispImage);
    s.setValue("subtitles/render/bitmapBlur", 1.5);
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(bounds(p.grabFramebuffer()).width() > crisp.width());
    s.setValue("subtitles/render/bitmapBlur", 0);
    MediaOptions::apply(&p, s);
    QTRY_COMPARE(p.grabFramebuffer(), crispImage);
    QVERIFY(p.command({"sub-add", subtitles, "select"}));
    QTRY_COMPARE(p.get("sub-text").toString(), QString("First MMMM"));
    QTest::qWait(150);
    const auto text = p.grabFramebuffer();
    s.setValue("subtitles/render/bitmapGray", true);
    MediaOptions::apply(&p, s);
    QTest::qWait(150);
    QCOMPARE(p.grabFramebuffer(), text);
  }
  void dialogCancelApplyResetAndEmbeddedPausedRefresh() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    settings(profile.settings);
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({embedded});
    QTRY_COMPARE(loaded.size(), 1);
    QVERIFY(player->set("pause", true));
    QString sid;
    for (const auto &entry : player->get("track-list").toList())
      if (entry.toMap().value("type") == "sub")
        sid = entry.toMap().value("id").toString();
    QVERIFY(!sid.isEmpty());
    QVERIFY(player->set("sid", sid));
    QVERIFY(window.seekTo(1.4));
    QTRY_COMPARE(player->get("sub-text").toString(), QString("First MMMM"));
    const auto edit = [&](bool save, bool reset) {
      QTimer::singleShot(100, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("subtitleStyleDialog");
        QVERIFY(dialog);
        dialog->findChild<QCheckBox *>("subtitles/render/embeddedFonts")
            ->setChecked(false);
        dialog->findChild<QCheckBox *>("subtitles/render/clearOnSeek")
            ->setChecked(true);
        dialog->findChild<QComboBox *>("subtitles/render/hinting")
            ->setCurrentIndex(1);
        dialog->findChild<QDoubleSpinBox *>("subtitles/render/bitmapBlur")
            ->setValue(1.2);
        if (reset)
          dialog->findChild<QPushButton *>("subtitleRenderReset")->click();
        auto *tabs = dialog->findChild<QTabWidget *>();
        QCOMPARE(tabs->count(), 5);
        const auto screenshots =
            qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        if (save && !reset && !screenshots.isEmpty())
          for (int i : {3, 4}) {
            tabs->setCurrentIndex(i);
            QTest::qWait(40);
            dialog->grab().save(screenshots +
                                (i == 3 ? "/subtitle-render.png"
                                        : "/subtitle-compatibility.png"));
          }
        dialog->findChild<QDialogButtonBox *>()
            ->button(save ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
      });
      window.findChild<QAction *>("action_subtitleStyleSettings")->trigger();
    };
    edit(false, false);
    QVERIFY(!profile.settings.contains("subtitles/render/embeddedFonts"));
    QVERIFY(player->get("embeddedfonts").toBool());
    edit(true, false);
    QTest::qWait(250);
    QVERIFY(!profile.settings.value("subtitles/render/embeddedFonts").toBool());
    QTRY_VERIFY(!bounds(player->grabFramebuffer()).isNull());
    QVERIFY(player->get("pause").toBool());
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 1.4) < .05);
    QVERIFY(window.seekTo(6.4));
    QTRY_COMPARE(player->get("sub-text").toString(), QString("Last MMMM"));
    QVERIFY(window.seekTo(1.4));
    QTRY_COMPARE(player->get("sub-text").toString(), QString("First MMMM"));
    edit(true, true);
    QTest::qWait(250);
    QVERIFY(profile.settings.value("subtitles/render/embeddedFonts").toBool());
    QVERIFY(!profile.settings.value("subtitles/render/clearOnSeek").toBool());
    QCOMPARE(player->get("sub-ass-hinting").toString(), QString("none"));
    QTRY_VERIFY(!bounds(player->grabFramebuffer()).isNull());
    QVERIFY(player->get("pause").toBool());
  }
};
QTEST_MAIN(SubtitleRenderTest)
#include "subtitlerender_test.moc"
