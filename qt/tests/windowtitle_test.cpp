#include "preferencesdriver.h"
#include "windowtitle.h"
#include "window.h"
#include "classicpreferences.h"
#include "player.h"
#include "timeline.h"
#include "classicstatus.h"
#include "classicvolume.h"
#include "theme.h"
#include "i18n.h"
#include <QtWidgets>
#include <QtTest>
#include <QScopeGuard>

class WindowTitleTest : public QObject {
  Q_OBJECT
  void prepare(Profile &p) {
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("input/mpris", false);
    p.settings.setValue("audio/mute", true);
    p.settings.setValue("playback/hardwareDecoding", false);
    p.settings.setValue("video/output", qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
  }
  QAction *action(Window &w, const QString &id) {
    return w.findChild<QAction *>("action_" + id);
  }
  WindowTitle::Media sample() {
    return {"/tmp/Видео/Тест.mkv", "Фильм {state} %1", "Пауза", "01:23", "45:00", true};
  }
private slots:
  void modesFallbackEscapesAndBounds() {
    const auto m = sample();
    QCOMPARE(WindowTitle::format(m, "title", {}, false, false), QString("Фильм {state} %1 - ") + WindowTitle::idle());
    QCOMPARE(WindowTitle::format(m, "name", {}, true, true), QString("Тест.mkv [Пауза] [01:23 / 45:00] - ") + WindowTitle::idle());
    QCOMPARE(WindowTitle::format(m, "app", {}, false, false), WindowTitle::idle());
    QCOMPARE(WindowTitle::format(m, "app", {}, true, false), WindowTitle::idle() + " [Пауза]");
    QCOMPARE(WindowTitle::format(m, "path", {}, false, false), QString("/tmp/Видео/Тест.mkv - ") + WindowTitle::idle());
    QCOMPARE(WindowTitle::format(m, "custom", "{{{title}}} | {unknown} | {name} | {path} | {state} | {position}/{duration} | {app}", true, true),
             QString("{Фильм {state} %1} | {unknown} | Тест.mkv | /tmp/Видео/Тест.mkv | Пауза | 01:23/45:00 | ") + WindowTitle::idle());
    auto missing = m; missing.title = " \n\t";
    QCOMPARE(WindowTitle::format(missing, "unknown", {}, false, false), QString("Тест.mkv - ") + WindowTitle::idle());
    QCOMPARE(WindowTitle::format(m, "custom", " \n", false, false), QString("Фильм {state} %1 - ") + WindowTitle::idle());
    QCOMPARE(WindowTitle::format(m, "custom", "{name", false, false), QString("{name"));
    auto idle = m; idle.active = false;
    QCOMPARE(WindowTitle::format(idle, "custom", "Wrong", true, true), WindowTitle::idle());
    idle.active = true; idle.source.clear();
    QCOMPARE(WindowTitle::format(idle, "name", {}, true, true), WindowTitle::idle());
    auto huge = m; huge.title = QString(10000, 'x');
    QVERIFY(WindowTitle::format(huge, "custom", "{title}{title}", false, false).size() <= 2048);
    huge.title = "abc\nxyz\tfoo\x1b";
    QCOMPARE(WindowTitle::format(huge, "title", {}, false, false), QString("abc xyz foo - ") + WindowTitle::idle());
  }
  void localUrlsAndNetworkCredentials() {
    QCOMPARE(WindowTitle::name("file:///tmp/Video%20name.mkv"), QString("Video name.mkv"));
    QCOMPARE(WindowTitle::path("file:///tmp/Video%20name.mkv"), QString("/tmp/Video name.mkv"));
    const QString url = "https://user:secret@example.org/Видео%20тест.mkv?token=private#fragment";
    QCOMPARE(WindowTitle::name(url), QString("Видео тест.mkv"));
    QCOMPARE(WindowTitle::path(url), QString("https://example.org/Видео тест.mkv"));
    QCOMPARE(WindowTitle::name("https://example.org/?token=private"), QString("https://example.org/"));
    QVERIFY(!WindowTitle::path(url).contains("secret"));
    QVERIFY(!WindowTitle::path(url).contains("private"));
  }
  void portableSettingsPersistWithoutDefaultWrites() {
    QTemporaryDir dir;
    const auto ini = dir.filePath("FoxicMPQt.ini");
    {
      QSettings settings(ini, QSettings::IniFormat);
      QCOMPARE(WindowTitle::render(settings, sample()), QString("Фильм {state} %1 - ") + WindowTitle::idle());
      QVERIFY(settings.allKeys().isEmpty());
      settings.setValue("ui/titleMode", "custom");
      settings.setValue("ui/titleTemplate", "{name} | {state}");
    }
    QSettings reopened(ini, QSettings::IniFormat);
    QCOMPARE(WindowTitle::render(reopened, sample()), QString("Тест.mkv | Пауза"));
  }
  void realPlaybackPreferencesAndReopen() {
    QTemporaryDir dir;
    const QString input = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv");
    QVERIFY(QFileInfo::exists(input));
    const auto source = dir.filePath("Проверка заголовка.mkv");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-v", "error", "-y", "-i", input, "-c", "copy", "-metadata", "title=Фильм {state} %1", source});
    QVERIFY(ffmpeg.waitForFinished(15000));
    QCOMPARE(ffmpeg.exitCode(), 0);
    {
      Profile p(dir.path()); prepare(p);
      Window w(p); w.show();
      QCOMPARE(w.windowTitle(), WindowTitle::idle());
      auto *player = w.findChild<Player *>(); QVERIFY(player);
      QSignalSpy loaded(player, &Player::loaded);
      w.openFiles({source});
      QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
      QTRY_VERIFY(!player->get("pause").toBool());
      QSignalSpy properties(player, &Player::property);
      QVERIFY(player->set("pause", true));
      const auto pauseObserved = [&] {
        for (const auto &event : properties)
          if (event[0].toString() == "pause" && event[1].toBool())
            return true;
        return false;
      };
      QTRY_VERIFY(pauseObserved());
      QTRY_COMPARE(w.windowTitle(), QString("Фильм {state} %1 - ") + WindowTitle::idle());
      for (bool accept : {false, true}) {
        afterPreferencesReady(&w, [&, accept] {
          auto *dialog = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(dialog);
          QTimer::singleShot(3000, dialog, &QDialog::reject);
          auto *tabs = dialog->findChild<QTabWidget *>();
          auto *page = dialog->findChild<QWidget *>("titlePreferences"); QVERIFY(page);
          tabs->setCurrentWidget(page);
          auto *mode = page->findChild<QComboBox *>("ui/titleMode");
          auto *pattern = page->findChild<QLineEdit *>("ui/titleTemplate");
          auto *showState = page->findChild<QCheckBox *>("ui/titleState");
          auto *showPosition = page->findChild<QCheckBox *>("ui/titlePosition");
          auto *preview = page->findChild<QLabel *>("titlePreview");
          QVERIFY(mode && pattern && showState && showPosition && preview);
          mode->setCurrentIndex(mode->findData("name"));
          showState->setChecked(true); showPosition->setChecked(true);
          QVERIFY2(preview->text().startsWith("Проверка заголовка.mkv [Пауза]"), qPrintable(preview->text()));
          mode->setCurrentIndex(mode->findData("custom"));
          QVERIFY(pattern->isEnabled()); QVERIFY(!showState->isEnabled());
          pattern->setText("{title} | {name} | {state} | {position}/{duration} | {app}");
          QVERIFY(preview->text().contains("Фильм {state} %1"));
          if (accept) {
            const auto shots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
            if (!shots.isEmpty()) {
              QDir().mkpath(shots);
              QVERIFY(dialog->grab().save(shots + "/window-title.png"));
            }
          }
          dialog->findChild<QDialogButtonBox *>()->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
        });
        action(w, "preferences")->trigger();
        if (!accept) {
          QVERIFY(!p.settings.contains("ui/titleMode"));
          QCOMPARE(w.windowTitle(), QString("Фильм {state} %1 - ") + WindowTitle::idle());
        }
      }
      QTRY_VERIFY(w.windowTitle().contains(" | Пауза | "));
      QVERIFY(w.windowTitle().startsWith("Фильм {state} %1 | Проверка заголовка.mkv"));
      QVERIFY(player->set("pause", false));
      QTRY_VERIFY(w.windowTitle().contains(" | Воспроизведение | "));
      const auto before = w.windowTitle();
      QVERIFY(player->command({"seek", "3", "absolute+exact"}));
      QTRY_VERIFY(w.windowTitle() != before);
      QVERIFY(player->set("pause", true));
      QTRY_VERIFY(w.windowTitle().contains(" | Пауза | "));
      // Changing metadata after load must update the title without recursion.
      player->property("media-title", "Другой {name}");
      QTRY_VERIFY(w.windowTitle().startsWith("Другой {name} | "));
      action(w, "stop")->trigger();
      QTRY_COMPARE(w.windowTitle(), WindowTitle::idle());
      w.openFiles({input});
      QTRY_VERIFY_WITH_TIMEOUT(loaded.size() >= 2, 10000);
      QVERIFY(player->set("pause", true));
      QTRY_VERIFY(w.windowTitle().startsWith("chapters.mkv | chapters.mkv"));
      action(w, "closeFile")->trigger();
      QCOMPARE(w.windowTitle(), WindowTitle::idle());
      w.close();
    }
    {
      Profile p(dir.path()); prepare(p);
      QCOMPARE(p.settings.value("ui/titleMode").toString(), QString("custom"));
      Window w(p); w.show();
      auto *player = w.findChild<Player *>();
      QSignalSpy loaded(player, &Player::loaded);
      w.openFiles({source});
      QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
      QTRY_VERIFY(w.windowTitle().startsWith("Фильм {state} %1 | Проверка заголовка.mkv"));
      w.close();
    }
  }
  void classicStatusAndClockFollowPlayback() {
    QTemporaryDir dir; Profile p(dir.path()); prepare(p);
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *status = w.findChild<QLabel *>("playbackStatus");
    auto *clock = w.findChild<QLabel *>("playbackClock");
    auto *gpu = w.findChild<QLabel *>("hardwareDecoderIndicator");
    auto *area = w.findChild<QWidget *>("classicStatusArea");
    auto *player = w.findChild<Player *>();
    auto *timeline = w.findChild<Timeline *>();
    QVERIFY(status && clock && gpu && area && player && timeline);
    QVERIFY(clock->text().isEmpty()); QVERIFY(gpu->isHidden());
    QVERIFY(!QIcon(":/classic-gpu.svg").pixmap(20, 20).isNull());
    QSignalSpy loaded(player, &Player::loaded);
    const auto input = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv");
    w.openFiles({input});
    QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
    QVERIFY(player->set("pause", true));
    QTRY_VERIFY(status->text().startsWith("⏸  chapters (1. Начало)"));
    QVERIFY(!clock->text().isEmpty());
    QCOMPARE(status->textFormat(), Qt::PlainText);
    QCOMPARE(status->font().pixelSize(), 13);
    QVERIFY(player->command({"seek", "5", "absolute+exact"}));
    QTRY_VERIFY(status->text().endsWith("(2. Вторая глава)"));
    // Windows removes only a matching leading chapter number.
    timeline->chapters[1] = QVariantMap{{"time", 4}, {"title", "02. Финал"}};
    player->property("time-pos", 5.);
    QCOMPARE(status->text(), QString("⏸  chapters (2. Финал)"));
    timeline->chapters[1] = QVariantMap{{"time", 4}, {"title", "Chapter 02"}};
    player->property("time-pos", 5.);
    QCOMPARE(status->text(), QString("⏸  chapters (2)"));
    timeline->chapters[1] = QVariantMap{{"time", 4}, {"title", "1984"}};
    player->property("time-pos", 5.);
    QVERIFY(status->text().endsWith("(2. 1984)"));
    const QString longTitle = QString(180, QChar(u'Ж')) + " <b>Фильм</b>";
    player->property("media-title", longTitle);
    QVERIFY(status->text().contains(longTitle));
    w.resize(560, 320); QCoreApplication::processEvents();
    QVERIFY(status->fontMetrics().horizontalAdvance(status->text()) > status->width());
    QVERIFY(clock->isVisible());
    QVERIFY(clock->geometry().right() <= area->width());
    QVERIFY(status->geometry().right() < clock->geometry().left());
    QVERIFY(clock->mapTo(&w, clock->rect().topRight()).x() < w.findChild<QSlider *>("volumeSlider")->mapTo(&w, QPoint()).x());
    QTest::mouseClick(clock, Qt::LeftButton);
    QVERIFY(clock->text().startsWith("- "));
    QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, clock->rect().center(), clock->mapToGlobal(clock->rect().center()));
    QApplication::sendEvent(clock, &menuEvent);
    auto *milliseconds = w.findChild<QAction *>("classicClockMilliseconds"); QVERIFY(milliseconds);
    milliseconds->trigger();
    QVERIFY(QRegularExpression("\\.\\d{3}(?: / |$)").match(clock->text()).hasMatch());
    w.findChild<QMenu *>("classicClockMenu")->close();
    QVERIFY(player->set("speed", 1.5));
    QVERIFY(player->set("pause", false));
    QTRY_VERIFY(status->text().startsWith("▶ (1.5x)  "));
    QVERIFY(player->set("pause", true));
    QTRY_VERIFY(status->text().startsWith("⏸  "));
    QVERIFY(!status->text().contains("(1.5x)"));
    timeline->chapterHovered("2. Под курсором");
    QCOMPARE(status->text(), QString("2. Под курсором"));
    timeline->hoverEnded();
    QVERIFY(status->text().startsWith("⏸  "));
    // A timer wider than the entire flexible slot is suppressed, never clipped.
    clock->setText(QString(500, '9'));
    static_cast<ClassicStatusArea *>(area)->arrange();
    QVERIFY(clock->isHidden());
    player->property("time-pos", 5.);
    QVERIFY(clock->isVisible());
    const auto shots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!shots.isEmpty()) {
      QDir().mkpath(shots);
      QVERIFY(w.grab().save(shots + "/classic-status-narrow.png"));
      w.resize(1060, 680); QCoreApplication::processEvents();
      player->property("media-title", "Проверка Windows Classic");
      QVERIFY(w.grab().save(shots + "/classic-status.png"));
    }
    action(w, "closeFile")->trigger();
    QCOMPARE(clock->text(), QString());
    QCOMPARE(w.windowTitle(), WindowTitle::idle());
    QVERIFY(gpu->isHidden());
    w.close();
  }
  void classicVolumeTrackGeometryAndInput_data() {
    QTest::addColumn<QString>("theme");
    QTest::newRow("dark") << QString("dark");
    QTest::newRow("light") << QString("light");
  }
  void classicVolumeTrackGeometryAndInput() {
    QFETCH(QString, theme);
    QTemporaryDir dir; Profile p(dir.path()); prepare(p);
    p.settings.setValue("appearance/theme", theme);
    p.settings.setValue("appearance/themeBrightness", 9);
    p.settings.setValue("appearance/volumeFill", "#ffffff");
    p.settings.setValue("appearance/volumeBackground", "#808080");
    Window w(p); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *volume = dynamic_cast<ClassicVolumeSlider *>(w.findChild<QSlider *>("volumeSlider"));
    QVERIFY(volume);
    QCOMPARE(volume->size(), QSize(66, 22));
    QCOMPARE(volume->property("volumeFill").toString(), QString("#ffffff"));
    QCOMPARE(volume->property("volumeBackground").toString(), QString("#808080"));
    auto *player = w.findChild<Player *>();
    QVERIFY(player->set("mute", false));
    QTRY_VERIFY(!player->get("mute").toBool());
    QTRY_VERIFY(!volume->property("muted").toBool());
    volume->setValue(40);
    volume->setMuted(false);
    auto *panel = w.findChild<QWidget *>("controls");
    // Render the parent too: grabbing a transparent child alone omits the
    // stylesheet gradient behind it and substitutes a flat palette background.
    const auto capture = [&] { return panel->grab(volume->geometry()); };
    const auto low = capture();
    QVERIFY(!low.isNull());
    auto image = low.toImage();
    const bool dark = theme == "dark";
    if (dark) {
      const QImage reference(FOXICMP_VOLUME_REFERENCE);
      QVERIFY(!reference.isNull());
      QCOMPARE(image.copy(4, 3, 55, 16).convertToFormat(QImage::Format_RGB32),
               reference.convertToFormat(QImage::Format_RGB32));
    }
    QCOMPARE(image.pixelColor(10, 4), dark ? QColor(24, 29, 34) : QColor(126, 133, 139));
    QCOMPARE(image.pixelColor(10, 17), dark ? QColor(112, 117, 122) : QColor(198, 205, 211));
    QCOMPARE(image.pixelColor(23, 4), dark ? QColor(34, 39, 44) : QColor(166, 173, 179));
    QCOMPARE(image.pixelColor(23, 17), dark ? QColor(215, 220, 225) : QColor(245, 248, 250));
    QCOMPARE(image.pixelColor(10, 3), dark ? QColor(94, 99, 104) : QColor(157, 165, 172));
    const QPoint right(volume->width() - 7, 11);
    QTest::mousePress(volume, Qt::LeftButton, Qt::NoModifier, right);
    QTest::mouseMove(volume, right);
    QTest::mouseRelease(volume, Qt::LeftButton, Qt::NoModifier, right);
    QVERIFY(volume->value() >= 80);
    volume->setMuted(false);
    const auto high = capture();
    QVERIFY(!high.isNull());
    QVERIFY(low.toImage() != high.toImage());
    QVERIFY(player->set("mute", true));
    QTRY_VERIFY(volume->property("muted").toBool());
    const auto muted = capture();
    QVERIFY(!muted.isNull());
    QVERIFY(muted.toImage() != high.toImage());
    const auto shots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!shots.isEmpty()) {
      QDir().mkpath(shots);
      const auto prefix = shots + "/volume-" + theme;
      QVERIFY(low.save(prefix + "-low.png"));
      QVERIFY(high.save(prefix + "-high.png"));
      QVERIFY(muted.save(prefix + "-muted.png"));
      QVERIFY(player->set("mute", false));
      QTRY_VERIFY(!volume->property("muted").toBool());
      volume->setValue(40);
      volume->setMuted(false);
      QVERIFY(w.findChild<QWidget *>("controls")->grab().save(prefix + "-toolbar.png"));
      const int left = std::max(0, volume->x() - 23);
      QVERIFY(panel->grab(QRect(left, 0, panel->width() - left, panel->height()))
                  .save(prefix + "-corner.png"));
      QVERIFY(w.grab().save(prefix + "-window.png"));
    }
    w.close();
  }
  void englishPreviewAndPlaybackState() {
    QTemporaryDir dir; Profile p(dir.path()); prepare(p);
    p.settings.setValue("appearance/language", "en");
    p.settings.setValue("ui/titleMode", "custom");
    p.settings.setValue("ui/titleTemplate", "{state} | {name}");
    Window w(p); w.show();
    auto *player = w.findChild<Player *>(); QSignalSpy loaded(player, &Player::loaded);
    w.openFiles({QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv")});
    QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
    QVERIFY(player->set("pause", true));
    QTRY_VERIFY(w.windowTitle().startsWith("Paused | "));
    afterPreferencesReady(&w, [&] {
      auto *dialog = w.findChild<QDialog *>("preferencesDialog"); QVERIFY(dialog);
      const auto closeOnExit = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
      auto *tabs = dialog->findChild<QTabWidget *>();
      auto *page = dialog->findChild<QWidget *>("titlePreferences"); QVERIFY(page);
      ClassicPreferences::selectPage(tabs, page);
      QCOMPARE(tabs->tabText(tabs->currentIndex()), QString("Window"));
      QVERIFY(page->isVisible());
      QVERIFY(page->findChild<QLabel *>("titlePreview")->text().startsWith("Paused | "));
      dialog->reject();
    });
    action(w, "preferences")->trigger();
    QVERIFY(player->set("pause", false));
    QVERIFY(player->command({"seek", "7.8", "absolute+exact"}));
    QTRY_COMPARE_WITH_TIMEOUT(w.windowTitle(), WindowTitle::idle(), 5000);
    w.close();
    p.settings.setValue("appearance/language", "ru");
    QVERIFY(I18n::load(p.settings, dir.path()));
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv); applyTheme(app); Q_INIT_RESOURCE(resources);
  WindowTitleTest test; return QTest::qExec(&test, argc, argv);
}
#include "windowtitle_test.moc"
