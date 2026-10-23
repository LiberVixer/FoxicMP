#include "preferencesdriver.h"
#include "framepreview.h"
#include "mediaoptions.h"
#include "player.h"
#include "profile.h"
#include "theme.h"
#include "videotransform.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>
#include <limits>
class TransformTest : public QObject {
  Q_OBJECT
  QTemporaryDir fixtures;
  QString video, stereoVideo;
  void prepare(Profile &profile) {
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("playback/hardwareDecoding", false);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
  }
  QAction *action(Window &window, const QString &id) {
    return window.findChild<QAction *>("action_" + id);
  }
  QImage filtered(const QString &filters) {
    QProcess ffmpeg;
    QStringList args{"-hide_banner", "-loglevel", "error",     "-threads", "1",
                     "-i",           video,       "-frames:v", "1"};
    if (!filters.isEmpty())
      args << "-vf" << filters;
    args << "-c:v" << "png" << "-threads" << "1" << "-f" << "image2pipe"
         << "pipe:1";
    ffmpeg.start("ffmpeg", args);
    if (!ffmpeg.waitForFinished(6000) || ffmpeg.exitCode() != 0) {
      qWarning() << ffmpeg.readAllStandardError();
      return {};
    }
    return QImage::fromData(ffmpeg.readAllStandardOutput());
  }
  bool red(const QColor &c) {
    return c.red() > 180 && c.green() < 80 && c.blue() < 80;
  }
  bool blue(const QColor &c) {
    return c.blue() > 180 && c.red() < 80 && c.green() < 80;
  }
  bool green(const QColor &c) {
    return c.green() > 150 && c.red() < 80 && c.blue() < 80;
  }
  bool yellow(const QColor &c) {
    return c.red() > 180 && c.green() > 180 && c.blue() < 80;
  }
  QImage screenshot(Window &window, const QString &name) {
    const auto path = fixtures.filePath(name + ".png");
    QSignalSpy saved(&window, &Window::frameSaved);
    if (!window.saveFrame(path, false))
      return {};
    QElapsedTimer timer;
    timer.start();
    while (saved.isEmpty() && timer.elapsed() < 5000)
      QTest::qWait(20);
    return QImage(path);
  }
  void edit(Window &window, const std::function<void(QDialog *)> &fn,
            bool accept = true) {
    QTimer::singleShot(100, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("transformDialog");
      QVERIFY(dialog);
      fn(dialog);
      dialog->findChild<QDialogButtonBox *>()
          ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
          ->click();
    });
    action(window, "transformDialog")->trigger();
  }
private slots:
  void initTestCase() {
    QVERIFY(fixtures.isValid());
    QImage image(320, 240, QImage::Format_RGB32);
    QPainter painter(&image);
    painter.fillRect(0, 0, 160, 120, Qt::red);
    painter.fillRect(160, 0, 160, 120, Qt::blue);
    painter.fillRect(0, 120, 160, 120, Qt::green);
    painter.fillRect(160, 120, 160, 120, Qt::yellow);
    painter.end();
    const auto png = fixtures.filePath("quadrants.png");
    QVERIFY(image.save(png));
    video = fixtures.filePath("quadrants.mkv");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-y", "-hide_banner", "-loglevel", "error", "-loop",
                            "1", "-i", png, "-t", "12", "-r", "10", "-c:v",
                            "ffv1", "-pix_fmt", "yuv444p", video});
    QVERIFY(ffmpeg.waitForFinished(10000));
    QCOMPARE(ffmpeg.exitCode(), 0);
    stereoVideo = fixtures.filePath("stereo.mkv");
    ffmpeg.start("ffmpeg", {"-y", "-hide_banner", "-loglevel", "error", "-i",
                            video, "-c", "copy", "-metadata:s:v:0",
                            "stereo_mode=left_right", stereoVideo});
    QVERIFY(ffmpeg.waitForFinished(6000));
    QCOMPARE(ffmpeg.exitCode(), 0);
  }
  void sanitizePersistenceAndFilterPixels() {
    auto state = VideoTransform::fromMap(
        {{"width", 100000},
         {"height", -2},
         {"x", std::numeric_limits<double>::quiet_NaN()},
         {"left", 100},
         {"right", 100},
         {"rotation", 33},
         {"stereoInput", "sbsl,evil"},
         {"cropAspect", "1;evil"}});
    QCOMPARE(state.width, 5.);
    QCOMPARE(state.height, .2);
    QCOMPARE(state.x, 0.);
    QCOMPARE(state.left, 45.);
    QCOMPARE(state.right, 45.);
    QCOMPARE(state.rotation, 0);
    QCOMPARE(state.stereoInput, QString("auto"));
    QCOMPARE(state.cropAspect, QString("no"));
    QTemporaryDir dir;
    QSettings settings(dir.filePath("profile.ini"), QSettings::IniFormat);
    state.width = 1.5;
    state.save(settings);
    QVERIFY(!settings.contains("transform/stateV1"));
    settings.setValue("transform/remember", true);
    state.save(settings);
    QCOMPARE(VideoTransform::load(settings).width, 1.5);
    settings.setValue("transform/remember", false);
    QCOMPARE(VideoTransform::load(settings).width, 1.);
    settings.setValue("video/rotation", 90);
    QCOMPARE(VideoTransform::load(settings).rotation, 90);
    state = {};
    state.mirror = true;
    auto image = filtered(state.sourceFilters());
    QCOMPARE(image.size(), QSize(320, 240));
    QVERIFY(blue(image.pixelColor(40, 40)));
    QVERIFY(red(image.pixelColor(280, 40)));
    state = {};
    state.flip = true;
    image = filtered(state.sourceFilters());
    QVERIFY(green(image.pixelColor(40, 40)));
    state = {};
    state.rotation = 90;
    image = filtered(state.sourceFilters());
    QCOMPARE(image.size(), QSize(240, 320));
    QVERIFY(green(image.pixelColor(40, 40)));
    QVERIFY(red(image.pixelColor(200, 40)));
    state = {};
    state.left = 25;
    state.right = 25;
    state.top = 25;
    state.bottom = 25;
    image = filtered(state.sourceFilters());
    QCOMPARE(image.size(), QSize(160, 120));
    QVERIFY(red(image.pixelColor(20, 20)));
    QVERIFY(yellow(image.pixelColor(140, 100)));
    state = {};
    state.cropAspect = "16:9";
    image = filtered(state.sourceFilters());
    QCOMPARE(image.size(), QSize(320, 180));
    state.cropAspect = "2.35:1";
    image = filtered(state.sourceFilters());
    QCOMPARE(image.size(), QSize(320, 136));
  }
  void stereoFormatsAndSwapPixels() {
    VideoTransform state;
    state.stereoOutput = "ml";
    QVERIFY(state.sourceFilters().isEmpty());
    state.stereoInput = "sbsl";
    auto image = filtered(state.sourceFilters());
    QCOMPARE(image.size(), QSize(160, 240));
    QVERIFY(red(image.pixelColor(40, 40)));
    QVERIFY(green(image.pixelColor(40, 200)));
    state.swapEyes = true;
    image = filtered(state.sourceFilters());
    QVERIFY(blue(image.pixelColor(40, 40)));
    QVERIFY(yellow(image.pixelColor(40, 200)));
    state.swapEyes = false;
    state.stereoInput = "auto";
    image = filtered(state.sourceFilters("sbsl"));
    QVERIFY(red(image.pixelColor(40, 40)));
    image = filtered(state.sourceFilters("sbs2l"));
    QCOMPARE(image.size(), QSize(160, 240));
    QVERIFY(red(image.pixelColor(140, 40)));
    QVERIFY(green(image.pixelColor(140, 200)));
    state.stereoInput = "abl";
    image = filtered(state.sourceFilters());
    QCOMPARE(image.size(), QSize(320, 120));
    QVERIFY(red(image.pixelColor(40, 40)));
    QVERIFY(blue(image.pixelColor(280, 40)));
    state.stereoInput = "sbsl";
    state.stereoOutput = "irl";
    image = filtered(state.sourceFilters());
    QCOMPARE(image.size(), QSize(160, 480));
    QVERIFY(red(image.pixelColor(40, 20)));
    QVERIFY(blue(image.pixelColor(40, 21)));
    state.stereoOutput = "irl2";
    image = filtered(state.sourceFilters());
    QCOMPARE(image.size(), QSize(320, 960));
    QVERIFY(red(image.pixelColor(40, 20)));
    QVERIFY(blue(image.pixelColor(40, 21)));
    state.stereoOutput = "ab2l";
    image = filtered(state.sourceFilters());
    QCOMPARE(image.size(), QSize(160, 480));
    state.stereoOutput = "arcc";
    image = filtered(state.sourceFilters());
    QVERIFY(!image.isNull());
    state.stereoOutput = "pass";
    state.swapEyes = true;
    image = filtered(state.sourceFilters());
    QCOMPARE(image.size(), QSize(320, 240));
    QVERIFY(blue(image.pixelColor(40, 40)));
    QVERIFY(red(image.pixelColor(280, 40)));
    // All exposed output formats decode on the installed FFmpeg build.
    for (const auto &out : {"ml", "mr", "irl", "irl2", "abl", "ab2l", "sbsl",
                            "sbs2l", "arcd", "arcc", "icl", "chl"}) {
      state.stereoOutput = out;
      QVERIFY2(!filtered(state.sourceFilters()).isNull(), out);
    }
    for (const auto &in : {"sbsl", "sbsr", "sbs2l", "sbs2r", "abl", "abr",
                           "ab2l", "ab2r", "irl", "irr", "icl", "icr"}) {
      state.stereoInput = in;
      state.stereoOutput = "ml";
      QVERIFY2(!filtered(state.sourceFilters()).isNull(), in);
    }
  }
  void dialogCancelApplyPresetAndRestart() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    prepare(profile);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    connect(player, &Player::error, this, [](const QString &error) {
      qWarning() << "Player error:" << error;
    });
    window.openFiles({video});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    for (bool accept : {false, true}) {
      edit(
          window,
          [&](QDialog *dialog) {
            dialog->findChild<QDoubleSpinBox *>("transform/width")
                ->setValue(140);
            dialog->findChild<QDoubleSpinBox *>("transform/height")
                ->setValue(80);
            dialog->findChild<QDoubleSpinBox *>("transform/x")->setValue(10);
            dialog->findChild<QDoubleSpinBox *>("transform/left")->setValue(10);
            dialog->findChild<QCheckBox *>("transform/remember")
                ->setChecked(true);
            dialog->findChild<QCheckBox *>("transform/mirror")
                ->setChecked(true);
            auto *rotation = dialog->findChild<QComboBox *>("video/rotation");
            rotation->setCurrentIndex(rotation->findData(90));
            dialog->findChild<QLineEdit *>("transformPresetName")
                ->setText("Мой кадр");
            dialog->findChild<QPushButton *>("transformPresetAdd")->click();
            QCOMPARE(dialog->findChild<QListWidget *>("transformPresetList")
                         ->count(),
                     1);
            const auto screenshots =
                qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
            if (accept && !screenshots.isEmpty()) {
              auto *tabs = dialog->findChild<QTabWidget *>();
              for (int i = 0; i < tabs->count(); ++i) {
                tabs->setCurrentIndex(i);
                dialog->grab().save(screenshots +
                                    QString("/transform-%1.png").arg(i));
              }
            }
          },
          accept);
      QVERIFY(std::abs(player->get("video-scale-x").toDouble() -
                       (accept ? 1.4 : 1.)) < .00001);
      QCOMPARE(profile.settings.value("transform/remember", false).toBool(),
               accept);
    }
    QVERIFY(std::abs(player->get("video-scale-y").toDouble() - (.8)) < .00001);
    QVERIFY(std::abs(player->get("video-pan-x").toDouble() - (.1)) < .00001);
    QVERIFY(action(window, "mirrorHorizontal")->isChecked());
    action(window, "panCenter")->trigger();
    QVERIFY(std::abs(player->get("video-pan-x").toDouble() - (0.)) < .00001);
    QVERIFY(std::abs(player->get("video-scale-x").toDouble() - (1.4)) < .00001);
    action(window, "widthUp")->trigger();
    QVERIFY(player->get("video-scale-x").toDouble() > 1.4);
    action(window, "panReset")->trigger();
    QVERIFY(std::abs(player->get("video-scale-x").toDouble() - (1.)) < .00001);
    edit(window, [&](QDialog *dialog) {
      auto *list = dialog->findChild<QListWidget *>("transformPresetList");
      QCOMPARE(list->count(), 1);
      list->setCurrentRow(0);
      dialog->findChild<QPushButton *>("transformPresetLoad")->click();
      QCOMPARE(dialog->findChild<QDoubleSpinBox *>("transform/width")->value(),
               140.);
      list->item(0)->setText("Переименован");
    });
    QVERIFY(std::abs(player->get("video-scale-x").toDouble() - (1.4)) < .00001);
    window.close();
    profile.settings.sync();
    Profile restored(dir.path());
    Window next(restored);
    next.show();
    QVERIFY(
        std::abs(next.findChild<Player *>()->get("video-scale-x").toDouble() -
                 1.4) < .00001);
    edit(next, [&](QDialog *dialog) {
      auto *list = dialog->findChild<QListWidget *>("transformPresetList");
      QCOMPARE(list->item(0)->text(), QString("Переименован"));
      list->setCurrentRow(0);
      dialog->findChild<QPushButton *>("transformPresetRemove")->click();
      dialog->findChild<QCheckBox *>("transform/remember")->setChecked(false);
      dialog->findChild<QPushButton *>("transformResetButton")->click();
    });
    next.close();
    Profile finalProfile(dir.path());
    QVERIFY(!finalProfile.settings.contains("transform/stateV1"));
    Window fresh(finalProfile);
    QCOMPARE(fresh.findChild<Player *>()->get("video-scale-x").toDouble(), 1.);
  }
  void metadataStereoAndRuntimeGeometryPixels() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    prepare(profile);
    profile.settings.setValue("ui/showControls", false);
    profile.settings.setValue("ui/showTimeline", false);
    Window window(profile);
    window.resize(640, 480);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    connect(player, &Player::error, this, [](const QString &error) {
      qWarning() << "Player error:" << error;
    });
    window.openFiles({video});
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(player->get("time-pos").toDouble() > .1);
    player->set("pause", true);
    QTRY_VERIFY(red(player->grabFramebuffer().pixelColor(80, 80)));
    edit(window, [&](QDialog *dialog) {
      dialog->findChild<QDoubleSpinBox *>("transform/width")->setValue(50);
      dialog->findChild<QDoubleSpinBox *>("transform/x")->setValue(25);
    });
    QTRY_VERIFY(player->grabFramebuffer().pixelColor(80, 80).lightness() < 20);
    QTRY_VERIFY(red(player->grabFramebuffer().pixelColor(300, 80)));
    const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty())
      window.grab().save(screenshots + "/transform-video.png");
    edit(window, [&](QDialog *dialog) {
      dialog->findChild<QPushButton *>("transformResetButton")->click();
      auto *output = dialog->findChild<QComboBox *>("transform/stereoOutput");
      output->setCurrentIndex(output->findData("ml"));
    });
    // An untagged ordinary video is left intact in Auto mode.
    auto image = screenshot(window, "auto-untagged");
    QCOMPARE(image.size(), QSize(320, 240));
    window.openFiles({stereoVideo});
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(!player->get("video-params")
                     .toMap()
                     .value("stereo-in")
                     .toString()
                     .isEmpty());
    // Filter reconfiguration and the first converted frame are asynchronous.
    // Wait for its pixels while decoding can advance, then verify the paused
    // frame too; a fixed sleep can freeze a queued frame from before conversion.
    // A mono eye can retain its encoded half-width raster. The PNG exporter
    // may instead normalize pixel aspect; inspect positions inside that image
    // and verify its dimensions against the actual output parameters below.
    QTRY_VERIFY_WITH_TIMEOUT(
        !(image = screenshot(window, "auto-tagged")).isNull() &&
          image.height() > 40 && red(image.pixelColor(image.width() * 7 / 8, 40)),
        3000);
    player->set("pause", true);
    image = screenshot(window, "auto-tagged-paused");
    const auto output = player->get("video-out-params").toMap();
    const QSize raster(output.value("w").toInt(), output.value("h").toInt());
    const QSize display(output.value("dw").toInt(), output.value("dh").toInt());
    QVERIFY(image.size() == raster || image.size() == display);
    QCOMPARE(image.height(), 240);
    QVERIFY(red(image.pixelColor(image.width() * 7 / 8, 40)));
    QVERIFY(red(image.pixelColor(image.width() / 8, 40)));
    QVERIFY(green(image.pixelColor(image.width() * 7 / 8, 200)));
    window.openFiles({video});
    QTRY_COMPARE(loaded.size(), 3);
    QTRY_VERIFY_WITH_TIMEOUT(
        blue((image = screenshot(window, "auto-restored-2d")).pixelColor(280, 40)),
        3000);
    player->set("pause", true);
    image = screenshot(window, "auto-restored-2d-paused");
    QCOMPARE(image.size(), QSize(320, 240));
    QVERIFY(blue(image.pixelColor(280, 40)));
  }
  void playerPixelsPreviewAndGeometry() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    prepare(profile);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    connect(player, &Player::error, this, [](const QString &error) {
      qWarning() << "Player error:" << error;
    });
    window.openFiles({video});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    QVERIFY(window.seekTo(1));
    QTRY_VERIFY(player->get("time-pos").toDouble() > .9);
    action(window, "mirrorHorizontal")->trigger();
    QImage image;
    QTRY_VERIFY(!(image = screenshot(window, "mirror")).isNull());
    QVERIFY(blue(image.pixelColor(40, 40)));
    action(window, "transformReset")->trigger();
    edit(window, [&](QDialog *dialog) {
      auto *input = dialog->findChild<QComboBox *>("transform/stereoInput");
      input->setCurrentIndex(input->findData("sbsl"));
      auto *output = dialog->findChild<QComboBox *>("transform/stereoOutput");
      output->setCurrentIndex(output->findData("ml"));
      dialog->findChild<QDoubleSpinBox *>("transform/top")->setValue(10);
    });
    QTest::qWait(100);
    image = screenshot(window, "left-eye");
    QCOMPARE(image.size(), QSize(160, 216));
    QVERIFY(red(image.pixelColor(40, 30)));
    auto *preview = window.findChild<FramePreview *>();
    QSignalSpy ready(preview, &FramePreview::ready);
    preview->request(video, 1);
    QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 6000);
    auto previewImage = ready.last()[0].value<QImage>();
    QVERIFY(red(previewImage.pixelColor(previewImage.width() / 4,
                                        previewImage.height() / 5)));
    action(window, "stereoSwap")->trigger();
    QTest::qWait(100);
    image = screenshot(window, "right-eye");
    QVERIFY(blue(image.pixelColor(40, 30)));
    // Re-applying audio/appearance options must preserve the source filters.
    edit(window, [&](QDialog *dialog) {
      dialog->findChild<QDoubleSpinBox *>("transform/width")->setValue(150);
      dialog->findChild<QDoubleSpinBox *>("transform/x")->setValue(10);
    });
    QVERIFY(std::abs(player->get("video-scale-x").toDouble() - (1.5)) < .00001);
    QVERIFY(std::abs(player->get("video-pan-x").toDouble() - (.1)) < .00001);
    afterPreferencesReady(&window, [&] {
      auto *dialog = window.findChild<QDialog *>("preferencesDialog");
      QVERIFY(dialog);
      dialog->findChild<QSpinBox *>("subtitles/size")->setValue(44);
      dialog->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Ok)
          ->click();
    });
    action(window, "preferences")->trigger();
    QVERIFY(std::abs(player->get("video-scale-x").toDouble() - (1.5)) < .00001);
    QVERIFY(std::abs(player->get("video-pan-x").toDouble() - (.1)) < .00001);
    image = screenshot(window, "after-preferences");
    QVERIFY(blue(image.pixelColor(40, 30)));
    action(window, "fullscreen")->trigger();
    QVERIFY(window.isFullScreen());
    action(window, "escape")->trigger();
    QVERIFY(std::abs(player->get("video-scale-x").toDouble() - (1.5)) < .00001);
    window.openFiles({video});
    QTRY_COMPARE(loaded.size(), 2);
    QVERIFY(std::abs(player->get("video-scale-x").toDouble() - (1.5)) < .00001);
    action(window, "transformReset")->trigger();
    QVERIFY(std::abs(player->get("video-scale-x").toDouble() - (1.)) < .00001);
    QTest::qWait(100);
    QTRY_COMPARE_WITH_TIMEOUT((image = screenshot(window, "reset")).size(),
                              QSize(320, 240), 3000);
    QVERIFY(red(image.pixelColor(40, 40)));
  }
};
int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  Q_INIT_RESOURCE(resources);
  applyTheme(app);
  TransformTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "transform_test.moc"
