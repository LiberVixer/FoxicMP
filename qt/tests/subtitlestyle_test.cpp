#include "mediaoptions.h"
#include "player.h"
#include "profile.h"
#include "subtitledocument.h"
#include "subtitleeditor.h"
#include "subtitlestyle.h"
#include "theme.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <limits>
class SubtitleStyleTest : public QObject {
  Q_OBJECT
  QTemporaryDir fixtures;
  QString video, srt;
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
  int maximum(const QImage &image) {
    int value = 0;
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x) {
        const auto c = image.pixelColor(x, y);
        value = std::max({value, c.red(), c.green(), c.blue()});
      }
    return value;
  }
  int colored(const QImage &image, int channel) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x) {
        const auto c = image.pixelColor(x, y);
        if (channel == 0   ? c.red() > 100 && c.green() < 70 && c.blue() < 70
            : channel == 1 ? c.green() > 100 && c.red() < 70 && c.blue() < 70
                           : c.blue() > 100 && c.red() < 70 && c.green() < 70)
          ++count;
      }
    return count;
  }
  void write(const QString &path, const QString &text) {
    QSaveFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    auto b = text.toUtf8();
    QCOMPARE(f.write(b), b.size());
    QVERIFY(f.commit());
  }
  void settings(QSettings &s) {
    s.setValue("subtitles/styleEnabled", true);
    s.setValue("subtitles/autoLoad", false);
    s.setValue("subtitles/alignX", "left");
    s.setValue("subtitles/alignY", "top");
    s.setValue("subtitles/marginX", 50);
    s.setValue("subtitles/marginY", 40);
    s.setValue("playback/hardwareDecoding", false);
    s.setValue("audio/mute", true);
  }
  void load(Player &p, const QString &sub) {
    connect(&p, &Player::error, this,
            [](const QString &error) { qWarning() << error; });
    QSignalSpy loaded(&p, &Player::loaded);
    p.resize(640, 360);
    p.show();
    p.set("hwdec", "no");
    QVERIFY(p.loadSource(video, {{"pause", "yes"}}));
    QTRY_COMPARE(loaded.size(), 1);
    QVERIFY(p.command({"sub-add", sub, "select"}));
    QVERIFY(p.command({"seek", "1.2", "absolute+exact"}));
    QTRY_VERIFY(!p.get("sub-text").toString().isEmpty());
    QTRY_VERIFY(!bounds(p.grabFramebuffer()).isNull());
  }
private slots:
  void initTestCase() {
    QVERIFY(fixtures.isValid());
    srt = fixtures.filePath("text.srt");
    video = fixtures.filePath("black.mkv");
    write(srt, "1\n00:00:01,000 --> 00:00:10,000\nMMMMMM\nMMMMMM\n");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-y", "-nostdin", "-v", "error", "-f", "lavfi",
                            "-i", "color=black:s=640x360:r=25", "-t", "12",
                            "-c:v", "ffv1", video});
    QVERIFY(ffmpeg.waitForFinished(10000));
    QCOMPARE(ffmpeg.exitCode(), 0);
  }
  void sanitizeAndGeneratedAss() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    settings(s);
    s.setValue("subtitles/scaleX", 9000);
    s.setValue("subtitles/angle", std::numeric_limits<double>::quiet_NaN());
    s.setValue("subtitles/alignY", "bad");
    const auto v = SubtitleStyle::values(s);
    QCOMPARE(v["scaleX"].toInt(), 300);
    QCOMPARE(v["angle"].toInt(), 0);
    QCOMPARE(v["alignY"].toString(), QString("bottom"));
    s.setValue("subtitles/scaleX", 150);
    s.setValue("subtitles/scaleY", 70);
    s.setValue("subtitles/angle", 25);
    s.setValue("subtitles/horizontalPosition", 70);
    s.setValue("subtitles/font", "Test,\nInjected");
    s.setValue("subtitles/color", "#80ff0000");
    s.setValue("subtitles/textOpacity", 50);
    SubtitleDocument doc;
    QString error;
    QVERIFY(doc.parse(
        "1\n00:00:01,000 --> 00:00:02,000\n<i>Русский</i> {\\pos(0,0)}\\evil\n",
        "srt", &error));
    const auto text = SubtitleStyle::assCopy(doc, s, &error);
    QVERIFY(text.contains("PlayResX: 1280"));
    QVERIFY(text.contains("TestInjected"));
    QVERIFY(!text.contains("<i>"));
    QVERIFY(!text.contains("{\\pos(0,0)}"));
    QVERIFY(text.contains("150.000,70.000"));
    QVERIFY(text.contains("&HBF0000FF"));
    SubtitleDocument parsed;
    QVERIFY2(parsed.parse(text, "ass", &error), qPrintable(error));
    QCOMPARE(parsed.cues.size(), 1);
    QVERIFY(SubtitleStyle::isCopy(
        dir.path() + "/Subtitles/Edits/foxicmp-styled-test.ass", dir.path()));
    QVERIFY(!SubtitleStyle::isCopy(dir.path() + "/else/foxicmp-styled-test.ass",
                                   dir.path()));
    s.setValue("subtitles/styleEnabled", false);
    const auto neutral = SubtitleStyle::assCopy(doc, s, &error);
    QVERIFY(neutral.contains("100.000,100.000"));
  }
  void srtLayoutSpacingOpacityShadowPixels() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    settings(s);
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    MediaOptions::apply(&p, s);
    load(p, srt);
    const auto initial = bounds(p.grabFramebuffer());
    QVERIFY(initial.left() < 100);
    QVERIFY(initial.top() < 100);
    s.setValue("subtitles/marginX", 170);
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(bounds(p.grabFramebuffer()).left() > initial.left() + 40);
    const auto moved = bounds(p.grabFramebuffer());
    s.setValue("subtitles/marginY", 120);
    MediaOptions::apply(&p, s);
    QTest::qWait(200);
    QTRY_VERIFY(bounds(p.grabFramebuffer()).top() > moved.top() + 25);
    s.setValue("subtitles/spacing", 8);
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(bounds(p.grabFramebuffer()).width() > initial.width() + 12);
    const auto letters = bounds(p.grabFramebuffer());
    s.setValue("subtitles/lineSpacing", 25);
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(bounds(p.grabFramebuffer()).height() > letters.height() + 8);
    s.setValue("subtitles/textOpacity", 25);
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(maximum(p.grabFramebuffer()) < 100);
    s.setValue("subtitles/textOpacity", 100);
    s.setValue("subtitles/shadowColor", "#ff00ff00");
    s.setValue("subtitles/shadowOffset", 8);
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(colored(p.grabFramebuffer(), 1) > 30);
    s.setValue("subtitles/backColor", "#ff0000ff");
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(colored(p.grabFramebuffer(), 2) > 100);
    s.setValue("subtitles/styleEnabled", false);
    MediaOptions::apply(&p, s);
    QTRY_VERIFY(bounds(p.grabFramebuffer()).bottom() > 300);
    QTRY_VERIFY(colored(p.grabFramebuffer(), 2) == 0);
  }
  void assGeometryAuthorStyleAndSecondaryPixels() {
    QTemporaryDir dir;
    QSettings s(dir.filePath("profile.ini"), QSettings::IniFormat);
    settings(s);
    s.setValue("subtitles/alignX", "center");
    s.setValue("subtitles/alignY", "center");
    s.setValue("subtitles/color", "#ffff0000");
    s.setValue("subtitles/secondaryColor", "#ff0000ff");
    SubtitleDocument doc;
    QString error;
    QVERIFY(doc.load(srt, "UTF-8", &error));
    const auto path = fixtures.filePath("geometry.ass");
    write(path, SubtitleStyle::assCopy(doc, s, &error));
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    SubtitleStyle::apply(&p, s, true);
    load(p, path);
    const auto initial = bounds(p.grabFramebuffer());
    QTRY_VERIFY(colored(p.grabFramebuffer(), 0) > 100);
    s.setValue("subtitles/overrideAss", false);
    s.setValue("subtitles/color", "#ff00ff00");
    s.setValue("subtitles/scaleX", 180);
    SubtitleStyle::apply(&p, s, false);
    QTest::qWait(100);
    QTRY_VERIFY(colored(p.grabFramebuffer(), 0) > 100);
    QCOMPARE(bounds(p.grabFramebuffer()), initial);
    s.setValue("subtitles/overrideAss", true);
    SubtitleStyle::apply(&p, s, false);
    QTRY_VERIFY(colored(p.grabFramebuffer(), 1) > 100);
    QTRY_VERIFY(bounds(p.grabFramebuffer()).width() > initial.width() * 1.5);
    s.setValue("subtitles/scaleY", 50);
    SubtitleStyle::apply(&p, s, false);
    QTRY_VERIFY(bounds(p.grabFramebuffer()).height() < initial.height() * .8);
    s.setValue("subtitles/angle", 45);
    SubtitleStyle::apply(&p, s, false);
    QTRY_VERIFY(bounds(p.grabFramebuffer()).height() > initial.height() * 1.5);
    s.setValue("subtitles/scaleX", 100);
    s.setValue("subtitles/scaleY", 100);
    s.setValue("subtitles/angle", 0);
    auto karaoke = SubtitleStyle::assCopy(doc, s, &error);
    karaoke.replace("MMMMMM\\NMMMMMM", "{\\k100} {\\k100}MMMMMM");
    write(path, karaoke);
    p.command({"sub-reload"});
    QTRY_VERIFY(colored(p.grabFramebuffer(), 2) > 30);
  }
  void styleDialogCancelApplyResetAndScreenshots() {
    QTemporaryDir dir;
    Profile p(dir.path());
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("input/mpris", false);
    p.settings.setValue("video/output", qEnvironmentVariable(
                                            "FOXICMP_TEST_OUTPUT", "software"));
    Window window(p);
    window.show();
    auto *action = window.findChild<QAction *>("action_subtitleStyleSettings");
    QVERIFY(action);
    const auto edit = [&](bool save, bool reset) {
      QTimer::singleShot(100, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("subtitleStyleDialog");
        QVERIFY(dialog);
        dialog->findChild<QCheckBox *>("subtitles/styleEnabled")
            ->setChecked(true);
        dialog->findChild<QDoubleSpinBox *>("subtitles/scaleX")->setValue(170);
        dialog->findChild<QDoubleSpinBox *>("subtitles/textOpacity")
            ->setValue(60);
        dialog->findChild<QComboBox *>("subtitles/alignX")->setCurrentIndex(0);
        if (reset)
          dialog->findChild<QPushButton *>("subtitleStyleReset")->click();
        const auto screenshots =
            qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        auto *tabs = dialog->findChild<QTabWidget *>();
        if (save && !reset && !screenshots.isEmpty())
          for (int i = 0; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            QTest::qWait(30);
            dialog->grab().save(screenshots +
                                QString("/subtitle-style-%1.png").arg(i));
          }
        dialog->findChild<QDialogButtonBox *>()
            ->button(save ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
      });
      action->trigger();
    };
    edit(false, false);
    QVERIFY(!p.settings.contains("subtitles/styleEnabled"));
    edit(true, false);
    QCOMPARE(p.settings.value("subtitles/scaleX").toInt(), 170);
    auto *player = window.findChild<Player *>();
    QCOMPARE(player->get("sub-align-x").toString(), QString("left"));
    edit(true, true);
    QVERIFY(!p.settings.value("subtitles/styleEnabled").toBool());
    QCOMPARE(p.settings.value("subtitles/scaleX").toInt(), 100);
    QCOMPARE(player->get("sub-align-x").toString(), QString("center"));
  }
  void assCopyApplicationAndLiveStyleUpdate() {
    QTemporaryDir dir;
    Profile p(dir.path());
    settings(p.settings);
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("input/mpris", false);
    p.settings.setValue("video/output", qEnvironmentVariable(
                                            "FOXICMP_TEST_OUTPUT", "software"));
    Window window(p);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({video});
    QTRY_COMPARE(loaded.size(), 1);
    QVERIFY(window.loadExternalSubtitles(srt));
    QTRY_VERIFY(player->get("track-list").toList().size() > 1);
    player->set("pause", true);
    window.seekTo(1.2);
    QTRY_VERIFY(!player->get("sub-text").toString().isEmpty());
    QTimer::singleShot(150, &window, [&] {
      auto *editor = window.findChild<SubtitleEditor *>("subtitleEditor");
      QVERIFY(editor);
      editor->findChild<QCheckBox *>("subtitleAssCopy")->setChecked(true);
      const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      if (!screenshots.isEmpty())
        editor->grab().save(screenshots + "/subtitle-ass-copy.png");
      editor->findChild<QPushButton *>("subtitleApplyCopy")->click();
      editor->close();
    });
    window.findChild<QAction *>("action_subtitleEditor")->trigger();
    QString path;
    QTRY_VERIFY(([&] {
      for (const auto &entry : player->get("track-list").toList()) {
        const auto track = entry.toMap();
        if (track.value("id").toString() == player->get("sid").toString())
          path = track.value("external-filename").toString();
      }
      return SubtitleStyle::isCopy(path, p.directory);
    })());
    QTRY_VERIFY(!bounds(player->grabFramebuffer()).isNull());
    const auto initial = bounds(player->grabFramebuffer());
    QTimer::singleShot(100, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("subtitleStyleDialog");
      QVERIFY(dialog);
      dialog->findChild<QDoubleSpinBox *>("subtitles/scaleX")->setValue(180);
      dialog->findChild<QDoubleSpinBox *>("subtitles/horizontalPosition")
          ->setValue(65);
      dialog->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Ok)
          ->click();
    });
    window.findChild<QAction *>("action_subtitleStyleSettings")->trigger();
    QTRY_VERIFY(bounds(player->grabFramebuffer()).width() >
                initial.width() * 1.5);
    QTRY_VERIFY(bounds(player->grabFramebuffer()).left() > initial.left() + 30);
    const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty())
      window.grab().save(screenshots + "/subtitle-style-video.png");
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.readAll().contains("180.000,100.000"));
    QFile source(srt);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QVERIFY(source.readAll().contains("MMMMMM"));
  }
};
int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  Q_INIT_RESOURCE(resources);
  applyTheme(app);
  SubtitleStyleTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "subtitlestyle_test.moc"
