#include "preferencesdriver.h"
#include "controls.h"
#include "framepreview.h"
#include "integration.h"
#include "mediaoptions.h"
#include "player.h"
#include "playlistfile.h"
#include "profile.h"
#include "theme.h"
#include "timeline.h"
#include "window.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <QtWidgets>
static bool hasColor(const QImage &image) {
  for (int y = 0; y < image.height(); y += 20)
    for (int x = 0; x < image.width(); x += 20) {
      auto c = image.pixelColor(x, y);
      if (std::max({c.red(), c.green(), c.blue()}) -
              std::min({c.red(), c.green(), c.blue()}) >
          30)
        return true;
    }
  return false;
}
class PlayerTest : public QObject {
  Q_OBJECT
private slots:
  void portableProfile() {
    QTemporaryDir d;
    QVERIFY(d.isValid());
    {
      Profile p(d.path());
      QVERIFY(p.writable());
      QCOMPARE(p.settings.value("audio/volume").toInt(), 75);
      p.settings.setValue("audio/volume", 42);
      p.savePosition("Русский фильм.mkv", 7, 30);
      p.settings.sync();
    }
    Profile p(d.path());
    QCOMPARE(p.settings.value("audio/volume").toInt(), 42);
    QCOMPARE(p.position("Русский фильм.mkv"), 7.0);
    p.savePosition("Русский фильм.mkv", 29, 30);
    QCOMPARE(p.position("Русский фильм.mkv"), 0.0);
  }
  void chapterLines() {
    Timeline t;
    t.resize(301, 16);
    t.duration = 30;
    t.position = 3;
    t.chapters = {QVariantMap{{"time", 15.0}, {"title", "Вторая"}}};
    t.show();
    QTest::qWait(50);
    QImage im = t.grab().toImage();
    for (int y = 0; y < 16; y++)
      QCOMPARE(im.pixelColor(150, y), QColor(94, 99, 104));
    QSignalSpy seek(&t, &Timeline::seek);
    QTest::mouseClick(&t, Qt::LeftButton, Qt::NoModifier, QPoint(200, 8));
    QCOMPARE(seek.size(), 1);
    QCOMPARE(seek.first().first().toDouble(), 20.0);
  }
  void integration() {
    QTemporaryDir d;
    qputenv("XDG_DATA_HOME", d.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", d.path().toUtf8());
    QString err;
    QVERIFY2(Integration::install("/tmp/FoxicMP folder/player", &err),
             qPrintable(err));
    QFile f(d.path() + "/applications/foxicmp-qt.desktop");
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.readAll().contains("FoxicMP folder/player"));
    f.close();
    QVERIFY(Integration::install("/tmp/moved/player", &err));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray text = f.readAll();
    QVERIFY(text.contains("/tmp/moved/player"));
    QVERIFY(!text.contains("FoxicMP folder"));
    f.close();
    QVERIFY(Integration::remove(&err));
    QVERIFY(!f.exists());
  }
  void desktopLaunch() {
    if (QStandardPaths::findExecutable("gio").isEmpty())
      QSKIP("gio not installed");
    QTemporaryDir d;
    QString exe = d.path() + "/Русский $cash%\"`player";
    QFile helper(exe);
    QVERIFY(helper.open(QIODevice::WriteOnly));
    helper.write("#!/usr/bin/python3\nimport "
                 "os,sys,json\nopen(os.environ['FOXICMP_ARG_LOG'],'w').write("
                 "json.dumps(sys.argv[1:]))\n");
    helper.close();
    QVERIFY(helper.setPermissions(QFile::ReadOwner | QFile::WriteOwner |
                                  QFile::ExeOwner));
    QString desktop = d.path() + "/test.desktop",
            record = d.path() + "/args.json";
    QFile entry(desktop);
    QVERIFY(entry.open(QIODevice::WriteOnly));
    entry.write(Integration::desktopText(exe).toUtf8());
    entry.close();
    QString file = d.path() + "/русский ' $cash видео.mp4";
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("FOXICMP_ARG_LOG", record);
    p.setProcessEnvironment(env);
    p.start("desktop-file-validate", {desktop});
    QVERIFY(p.waitForFinished());
    QVERIFY2(p.exitCode() == 0, p.readAllStandardOutput().constData());
    p.start("gio", {"launch", desktop, file});
    QVERIFY(p.waitForFinished());
    QVERIFY2(p.exitCode() == 0, p.readAllStandardOutput().constData());
    QTRY_VERIFY(QFile::exists(record));
    QFile result(record);
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(
        QJsonDocument::fromJson(result.readAll()).array().last().toString(),
        file);
  }
  void interface() {
    QTemporaryDir d;
    Profile profile(d.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    profile.settings.setValue("audio/mute", true);
    Window w(profile);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QTest::qWait(300);
    const QString shots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!shots.isEmpty()) {
      QDir().mkpath(shots);
      w.grab().save(shots + "/idle.png");
    }
    const QString media = qEnvironmentVariable("FOXICMP_TEST_MEDIA");
    if (!media.isEmpty()) {
      auto *p = w.findChild<Player *>();
      QVERIFY(p);
      QSignalSpy loaded(p, &Player::loaded);
      w.openFiles({QDir(media).filePath("chapters.mkv"),
                   QDir(media).filePath("Русский H264.mp4")});
      QTRY_COMPARE(loaded.size(), 1);
      QTRY_VERIFY(p->get("time-pos").toDouble() > 0.3);
      QTRY_VERIFY_WITH_TIMEOUT(hasColor(p->grabFramebuffer()), 10000);
      QCOMPARE(p->get("volume").toDouble(), 75.0);
      QCOMPARE(p->get("mute").toBool(), true);
      w.raise();
      w.activateWindow();
      QVERIFY(QTest::qWaitForWindowActive(&w));
      QTest::keyClick(&w, Qt::Key_F11);
      QTRY_VERIFY(w.isFullScreen());
      QTest::keyClick(&w, Qt::Key_Escape);
      QTRY_VERIFY(!w.isFullScreen());
      QTRY_VERIFY_WITH_TIMEOUT(hasColor(p->grabFramebuffer()), 10000);
      QTRY_VERIFY_WITH_TIMEOUT(hasColor(w.grab().toImage()), 10000);
      if (!shots.isEmpty())
        w.grab().save(shots + "/playback.png");
      QVERIFY(p->command({"seek", "7.5", "absolute+exact"}));
      QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 10000);
      QVERIFY(p->get("path").toString().endsWith("Русский H264.mp4"));
    }
    w.close();
    QCOMPARE(profile.settings.value("files/playlist").toStringList().size(),
             media.isEmpty() ? 0 : 2);
  }
  void playbackTimeInput() {
    double seconds = -1;
    QVERIFY(parsePlaybackTime("01:02:03.125", &seconds));
    QCOMPARE(seconds, 3723.125);
    QVERIFY(parsePlaybackTime("90:05,5", &seconds));
    QCOMPARE(seconds, 5405.5);
    QVERIFY(parsePlaybackTime("3", &seconds));
    QCOMPARE(seconds, 3.0);
    for (auto text :
         {"-1", "1:60", "1:2:60", "1:2:3:4", "abc", "NaN", "1.2345"})
      QVERIFY(!parsePlaybackTime(text, &seconds));
  }
  void inputControls() {
    const QString media = qEnvironmentVariable("FOXICMP_TEST_MEDIA");
    if (media.isEmpty())
      QSKIP("Set FOXICMP_TEST_MEDIA");
    QTemporaryDir d;
    Profile profile(d.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    Window w(profile);
    w.show();
    w.raise();
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&w));
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *p = w.findChild<Player *>();
    auto action = [&](const QString &id) {
      return w.findChild<QAction *>("action_" + id);
    };
    QSignalSpy loaded(p, &Player::loaded);
    w.openFiles({QDir(media).filePath("chapters.mkv")});
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(hasColor(p->grabFramebuffer()), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(p->get("time-pos").toDouble() > 0.2, 10000);
    QVERIFY(!action("play")->icon().isNull());
    QTest::mouseClick(p, Qt::LeftButton);
    QTRY_VERIFY(p->get("pause").toBool());
    const double before = p->get("time-pos").toDouble();
    QTest::keyClick(&w, Qt::Key_Right, Qt::ControlModifier);
    QTRY_VERIFY(p->get("time-pos").toDouble() > before + 0.02);
    const double after = p->get("time-pos").toDouble();
    QTest::keyClick(&w, Qt::Key_Left, Qt::ControlModifier);
    QTRY_VERIFY(p->get("time-pos").toDouble() < after - 0.02);
    QVERIFY(p->get("pause").toBool());
    const QPoint point = p->rect().center();
    QWheelEvent wheel(point, p->mapToGlobal(point), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(p, &wheel);
    QTRY_COMPARE(p->get("volume").toDouble(), 80.0);
    bool invalidRejected = false;
    QTimer::singleShot(100, &w, [&] {
      auto *dialog = w.findChild<QDialog *>("gotoDialog");
      if (!dialog)
        return;
      auto *input = dialog->findChild<QLineEdit *>("gotoTime");
      auto *ok =
          dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
      input->setText("99:00:00");
      ok->click();
      invalidRejected = dialog->isVisible();
      input->setText("00:00:03.250");
      ok->click();
    });
    action("goto")->trigger();
    QVERIFY(invalidRejected);
    QTRY_VERIFY(std::abs(p->get("time-pos").toDouble() - 3.25) < 0.1);
    action("fill")->trigger();
    QCOMPARE(p->get("panscan").toDouble(), 1.0);
    action("zoomIn")->trigger();
    QVERIFY(std::abs(p->get("video-zoom").toDouble() - 0.1) < 0.0001);
    action("fit")->trigger();
    QCOMPARE(p->get("panscan").toDouble(), 0.0);
    QCOMPARE(p->get("video-zoom").toDouble(), 0.0);
    QTest::mouseDClick(p, Qt::LeftButton);
    QTRY_VERIFY(w.isFullScreen());
    // Move the real cursor away from controls: hovering the panel intentionally
    // prevents auto-hide, including when an earlier UI test left it there.
    QTest::mouseMove(p, QPoint(40, 40));
    QTest::qWait(QApplication::doubleClickInterval() + 50);
    QVERIFY(p->get("pause").toBool());
    auto *bar = w.findChild<QWidget *>("controls");
    QTRY_VERIFY_WITH_TIMEOUT(!bar->isVisible(), 5000);
    QTest::mouseMove(p, QPoint(20, 20));
    QTRY_VERIFY(bar->isVisible());
    QTest::keyClick(&w, Qt::Key_Escape);
    QTRY_VERIFY(!w.isFullScreen());
    QVERIFY(bar->isVisible());
    auto *menu = w.findChild<QMenu *>("videoContextMenu");
    QVERIFY(w.findChild<QAction *>("classic_ID_PLAY_GOTO"));
    QVERIFY(w.findChild<QAction *>("classic_ID_VIEW_FULLSCREEN"));
    QContextMenuEvent context(QContextMenuEvent::Mouse, point,
                              p->mapToGlobal(point));
    QApplication::sendEvent(p, &context);
    QTRY_VERIFY(menu->isVisible());
    menu->hide();
    const bool menuWasVisible = w.menuBar()->isVisible();
    action("menu")->trigger();
    QCOMPARE(w.menuBar()->isVisible(), !menuWasVisible);
    action("menu")->trigger();
    QCOMPARE(w.menuBar()->isVisible(), menuWasVisible);
    action("size200")->trigger();
    QVERIFY(w.width() >= w.minimumWidth());
    QVERIFY(!w.isFullScreen());
    bool conflictRejected = false;
    afterPreferencesReady(&w, [&] {
      auto *dialog = w.findChild<QDialog *>("preferencesDialog");
      if (!dialog)
        return;
      auto *edit = dialog->findChild<QKeySequenceEdit *>("key_play");
      auto *ok =
          dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
      edit->setKeySequence(QKeySequence("Ctrl+G"));
      ok->click();
      conflictRejected = dialog->isVisible();
      edit->setKeySequence(QKeySequence("F6"));
      ok->click();
    });
    action("preferences")->trigger();
    QVERIFY(conflictRejected);
    QCOMPARE(profile.settings.value("keys/play").toString(), QString("F6"));
    QVERIFY(!action("play")->icon().isNull());
    w.raise();
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&w));
    QTest::qWait(100);
    QCOMPARE(action("play")->shortcut(), QKeySequence("F6"));
    QSignalSpy activated(action("play"), &QAction::triggered);
    qInfo() << "Before remapped key:" << p->get("pause")
            << p->get("idle-active") << p->get("time-pos");
    QTest::keyClick(&w, Qt::Key_F6);
    QTRY_COMPARE(activated.size(), 1);
    QTRY_VERIFY(!p->get("pause").toBool());
    // The preferences dialog now persists separate window/fullscreen bindings.
    profile.settings.setValue("mouse/window/left", "none");
    profile.settings.setValue("mouse/window/double_left", "none");
    profile.settings.setValue("mouse/window/wheelUp", "none");
    profile.settings.setValue("mouse/clickPause", false);
    profile.settings.setValue("mouse/doubleClickFullscreen", false);
    profile.settings.setValue("mouse/wheelVolume", false);
    QTest::mouseClick(p, Qt::LeftButton);
    QTest::qWait(QApplication::doubleClickInterval() + 50);
    QVERIFY(!p->get("pause").toBool());
    QTest::mouseDClick(p, Qt::LeftButton);
    QVERIFY(!w.isFullScreen());
    QApplication::sendEvent(p, &wheel);
    QCOMPARE(p->get("volume").toDouble(), 80.0);
    w.close();
    Profile restored(d.path());
    Window next(restored);
    QCOMPARE(next.findChild<QAction *>("action_play")->shortcut(),
             QKeySequence("F6"));
  }
  void libraryProfile() {
    QTemporaryDir d;
    QString favoriteId;
    {
      Profile p(d.path());
      for (int i = 0; i < 60; ++i)
        p.recordRecent(d.path() + "/" + QString::number(i) + ".mp4");
      QCOMPARE(p.recentFiles().size(), 50);
      p.recordRecent(d.path() + "/15.mp4");
      QCOMPARE(p.recentFiles().size(), 50);
      QCOMPARE(p.recentFiles().first(), d.path() + "/15.mp4");
      p.settings.setValue("history/enabled", false);
      p.recordRecent(d.path() + "/не записывать.mp4");
      QCOMPARE(p.recentFiles().first(), d.path() + "/15.mp4");
      p.savePosition(d.path() + "/15.mp4", 12, 60);
      favoriteId = p.addFavorite("Русская сцена", d.path() + "/15.mp4", 25.125);
      QVERIFY(!favoriteId.isEmpty());
      p.renameFavorite(favoriteId, "Новое название");
      QVERIFY(p.addFavorite("bad", "a", -1).isEmpty());
      p.settings.sync();
    }
    Profile restored(d.path());
    QCOMPARE(restored.recentFiles().size(), 50);
    QCOMPARE(restored.favorites().size(), 1);
    QCOMPARE(restored.favorites().first().title, QString("Новое название"));
    QCOMPARE(restored.favorites().first().position, 25.125);
    restored.clearHistory();
    QVERIFY(restored.recentFiles().isEmpty());
    QCOMPARE(restored.position(d.path() + "/15.mp4"), 0.0);
    QCOMPARE(restored.favorites().size(), 1);
    QCOMPARE(restored.settings.value("history/enabled").toBool(), false);
    restored.removeFavorite(favoriteId);
    QVERIFY(restored.favorites().isEmpty());
  }
  void playlistFiles() {
    QTemporaryDir d;
    QDir root(d.path());
    root.mkpath("original/media");
    const QString folder = d.path() + "/original";
    QStringList files{folder + "/media/Русский фильм.mp4",
                      folder + "/#chapter.mkv", folder + "/a:b.mp4"};
    QString error;
    QVERIFY2(PlaylistFile::write(folder + "/list.m3u8", files, &error),
             qPrintable(error));
    QStringList restored;
    QVERIFY2(PlaylistFile::read(folder + "/list.m3u8", &restored, &error),
             qPrintable(error));
    QCOMPARE(restored, files);
    QVERIFY(root.rename("original", "moved"));
    QVERIFY(
        PlaylistFile::read(d.path() + "/moved/list.m3u8", &restored, &error));
    QCOMPARE(restored.first(), d.path() + "/moved/media/Русский фильм.mp4");
    const auto savedLocal = restored;
    QFile network(d.path() + "/network.m3u8");
    QVERIFY(network.open(QIODevice::WriteOnly));
    network.write("#EXTM3U\nhttps://example.invalid/video.mp4?q=1\n");
    network.close();
    QVERIFY(PlaylistFile::read(network.fileName(), &restored, &error));
    QCOMPARE(restored, QStringList{"https://example.invalid/video.mp4?q=1"});
    const auto saved = restored;
    QFile bad(d.path() + "/unsupported.m3u8");
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("#EXTM3U\ngopher://example.invalid/video.mp4\n");
    bad.close();
    QVERIFY(!PlaylistFile::read(bad.fileName(), &restored, &error));
    QCOMPARE(restored, saved);
    QVERIFY(!PlaylistFile::write(d.path() + "/moved/list.m3u8", {"bad\nname"},
                                 &error));
    QVERIFY(
        PlaylistFile::read(d.path() + "/moved/list.m3u8", &restored, &error));
    QCOMPARE(restored, savedLocal);
    QFile bom(d.path() + "/bom.m3u8");
    QVERIFY(bom.open(QIODevice::WriteOnly));
    bom.write(QByteArray::fromHex("efbbbf") + "#EXTM3U\r\n" +
              QUrl::fromLocalFile(files.first()).toEncoded() + "\r\n");
    bom.close();
    QVERIFY(PlaylistFile::read(bom.fileName(), &restored, &error));
    QCOMPARE(restored, QStringList{files.first()});
  }
  void queuePlayback() {
    const auto media = qEnvironmentVariable("FOXICMP_TEST_MEDIA");
    if (media.isEmpty())
      QSKIP("Set FOXICMP_TEST_MEDIA");
    QTemporaryDir d;
    Profile profile(d.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    Window w(profile);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *p = w.findChild<Player *>();
    auto *list = w.findChild<QListWidget *>("playlist");
    auto action = [&](const QString &id) {
      return w.findChild<QAction *>("action_" + id);
    };
    QSignalSpy loaded(p, &Player::loaded);
    const QString a = QDir(media).filePath("chapters.mkv"),
                  b = QDir(media).filePath("Русский H264.mp4"),
                  c = QDir(media).filePath("VP9.webm");
    w.openFiles({a});
    QTRY_COMPARE(loaded.size(), 1);
    p->set("pause", true);
    w.addFiles({b, c});
    QCOMPARE(list->count(), 3);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(p->get("path").toString(), a);
    list->clearSelection();
    list->item(2)->setSelected(true);
    action("playlistUp")->trigger();
    QCOMPARE(list->item(1)->data(Qt::UserRole).toString(), c);
    list->clearSelection();
    list->item(0)->setSelected(true);
    action("playlistDown")->trigger();
    QCOMPARE(list->item(1)->data(Qt::UserRole).toString(), a);
    action("next")->trigger();
    QTRY_COMPARE(loaded.size(), 2);
    QCOMPARE(p->get("path").toString(), b);
    p->set("pause", true);
    list->clearSelection();
    list->item(0)->setSelected(true);
    action("playlistRemove")->trigger();
    QCOMPARE(list->count(), 2);
    QCOMPARE(p->get("path").toString(), b);
    action("previous")->trigger();
    QTRY_COMPARE(loaded.size(), 3);
    QCOMPARE(p->get("path").toString(), a);
    p->set("pause", true);
    list->clearSelection();
    list->item(0)->setSelected(true);
    action("playlistRemove")->trigger();
    QTRY_VERIFY(p->get("idle-active").toBool());
    QCOMPARE(list->count(), 1);
    action("play")->trigger();
    QTRY_COMPARE(loaded.size(), 4);
    QCOMPARE(p->get("path").toString(), b);
    p->set("pause", true);
    QVERIFY(w.savePlaylist(d.path() + "/queue.m3u8"));
    QVERIFY(w.loadPlaylist(d.path() + "/queue.m3u8"));
    QTRY_COMPARE(loaded.size(), 5);
    QCOMPARE(list->count(), 1);
    w.openFiles({a, b, c});
    QTRY_COMPARE(loaded.size(), 6);
    p->set("pause", true);
    action("shuffle")->trigger();
    QSet<QString> visited{a};
    QString previous = a, last;
    for (int n = 0; n < 2; ++n) {
      action("next")->trigger();
      QTRY_COMPARE(loaded.size(), 7 + n);
      p->set("pause", true);
      last = p->get("path").toString();
      QVERIFY(!visited.contains(last));
      visited.insert(last);
      if (n == 0)
        previous = last;
    }
    QCOMPARE(visited.size(), 3);
    action("next")->trigger();
    QTest::qWait(100);
    QCOMPARE(loaded.size(), 8);
    action("previous")->trigger();
    QTRY_COMPARE(loaded.size(), 9);
    QCOMPARE(p->get("path").toString(), previous);
    p->set("pause", true);
    action("next")->trigger();
    QTRY_COMPARE(loaded.size(), 10);
    QCOMPARE(p->get("path").toString(), last);
    action("repeatOne")->trigger();
    p->command({"seek", "7.9", "absolute+exact"});
    p->set("pause", false);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 11, 10000);
    QCOMPARE(p->get("path").toString(), last);
    QTRY_VERIFY(p->get("time-pos").toDouble() < 1);
    p->set("pause", true);
    action("shuffle")->trigger();
    action("repeatAll")->trigger();
    w.openFiles({a, b});
    QTRY_COMPARE(loaded.size(), 12);
    action("previous")->trigger();
    QTRY_COMPARE(loaded.size(), 13);
    QCOMPARE(p->get("path").toString(), b);
    p->command({"seek", "7.9", "absolute+exact"});
    p->set("pause", false);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 14, 10000);
    QCOMPARE(p->get("path").toString(), a);
    const QString shots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!shots.isEmpty()) {
      QDir().mkpath(shots);
      w.grab().save(shots + "/playlist.png");
    }
    action("repeatNone")->trigger();
    action("next")->trigger();
    QTRY_COMPARE(loaded.size(), 15);
    p->command({"seek", "7.9", "absolute+exact"});
    p->set("pause", false);
    QTRY_VERIFY_WITH_TIMEOUT(p->get("idle-active").toBool(), 10000);
    QCOMPARE(loaded.size(), 15);
    w.close();
    Profile restored(d.path());
    Window next(restored);
    auto *queue = next.findChild<QListWidget *>("playlist");
    QCOMPARE(queue->count(), 2);
    QCOMPARE(queue->currentRow(), 1);
  }
  void queueKeyboardAndDrop() {
    const auto media = qEnvironmentVariable("FOXICMP_TEST_MEDIA");
    if (media.isEmpty())
      QSKIP("Set FOXICMP_TEST_MEDIA");
    QTemporaryDir d;
    Profile profile(d.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    Window w(profile);
    w.show();
    w.raise();
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&w));
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *p = w.findChild<Player *>();
    auto *list = w.findChild<QListWidget *>("playlist");
    QSignalSpy loaded(p, &Player::loaded);
    const auto a = QDir(media).filePath("chapters.mkv"),
               b = QDir(media).filePath("Русский H264.mp4"),
               c = QDir(media).filePath("VP9.webm");
    w.openFiles({a, b});
    QTRY_COMPARE(loaded.size(), 1);
    p->set("pause", true);
    w.findChild<QAction *>("action_playlist")->trigger();
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Down);
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(p->get("volume").toDouble(), 75.0);
    QCOMPARE(p->get("path").toString(), a);
    QTest::keyClick(list, Qt::Key_Return);
    QTRY_COMPARE(loaded.size(), 2);
    QCOMPARE(p->get("path").toString(), b);
    p->set("pause", true);
    auto *remove = w.findChild<QAction *>("action_playlistRemove");
    remove->setShortcut(QKeySequence(Qt::Key_Delete));
    QTest::keyClick(list, Qt::Key_Delete);
    QTRY_VERIFY(p->get("idle-active").toBool());
    QCOMPARE(list->count(), 1);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(c)});
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton,
                    Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &drop);
    QVERIFY(drop.isAccepted());
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->item(1)->data(Qt::UserRole).toString(), c);
    w.close();
  }
  void bookmarksAndHistory() {
    const auto media = qEnvironmentVariable("FOXICMP_TEST_MEDIA");
    if (media.isEmpty())
      QSKIP("Set FOXICMP_TEST_MEDIA");
    QTemporaryDir d;
    Profile profile(d.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("playback/rememberPosition", false);
    const QString a = QDir(media).filePath("chapters.mkv"),
                  b = QDir(media).filePath("Русский H264.mp4");
    const auto id = profile.addFavorite("Сцена", a, 2.75);
    Window w(profile);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded), errors(p, &Player::error);
    connect(p, &Player::loaded, &w, [p] { p->set("pause", true); });
    w.openFiles({b});
    QTRY_COMPARE(loaded.size(), 1);
    QCOMPARE(profile.recentFiles(), QStringList{b});
    auto *favorites = w.findChild<QMenu *>("favoritesMenu");
    QMetaObject::invokeMethod(favorites, "aboutToShow", Qt::DirectConnection);
    QAction *bookmark = nullptr;
    for (auto *action : favorites->actions())
      if (action->data().toString() == id)
        bookmark = action;
    QVERIFY(bookmark);
    bookmark->trigger();
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(std::abs(p->get("time-pos").toDouble() - 2.75) < 0.1);
    QCOMPARE(w.findChild<QListWidget *>("playlist")->count(), 2);
    QCOMPARE(profile.recentFiles(), (QStringList{a, b}));
    QTimer::singleShot(100, &w, [] {
      if (auto *dialog = QApplication::activeModalWidget()) {
        dialog->findChild<QLineEdit *>("favoriteTitle")
            ->setText("Другая сцена");
        dialog->findChild<QDialogButtonBox *>()
            ->button(QDialogButtonBox::Ok)
            ->click();
      }
    });
    w.findChild<QAction *>("action_favoriteAdd")->trigger();
    QCOMPARE(profile.favorites().size(), 2);
    QCOMPARE(profile.favorites().last().title, QString("Другая сцена"));
    QTimer::singleShot(100, &w, [&] {
      auto *dialog = w.findChild<QDialog *>("favoritesDialog");
      if (!dialog)
        return;
      dialog->findChild<QListWidget *>("favoritesList")->setCurrentRow(0);
      dialog->findChild<QPushButton *>("favoriteOpen")->click();
    });
    w.findChild<QAction *>("action_favoriteManage")->trigger();
    QTRY_COMPARE(loaded.size(), 3);
    QTRY_VERIFY(std::abs(p->get("time-pos").toDouble() - 2.75) < 0.1);
    w.openFiles({a + ".missing"});
    QTRY_VERIFY(errors.size() > 0);
    QCOMPARE(profile.recentFiles(), (QStringList{a, b}));
    auto *history = w.findChild<QMenu *>("historyMenu");
    QMetaObject::invokeMethod(history, "aboutToShow", Qt::DirectConnection);
    QAction *recent = nullptr;
    for (auto *action : history->actions())
      if (action->data().toString() == b)
        recent = action;
    QVERIFY(recent);
    recent->trigger();
    QTRY_COMPARE(loaded.size(), 4);
    QCOMPARE(profile.recentFiles(), (QStringList{b, a}));
    QMetaObject::invokeMethod(history, "aboutToShow", Qt::DirectConnection);
    w.findChild<QAction *>("action_clearHistory")->trigger();
    QVERIFY(profile.recentFiles().isEmpty());
    QCOMPARE(profile.favorites().size(), 2);
    QCOMPARE(profile.settings.value("history/enabled", true).toBool(), true);
    w.close();
    Profile restored(d.path());
    QCOMPARE(restored.favorites().size(), 2);
  }

  void previewIsolation() {
    const auto file = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                          .filePath("chapters.mkv");
    QVERIFY(QFileInfo::exists(file));
    Player p(nullptr, "software");
    p.resize(640, 360);
    p.show();
    QVERIFY(QTest::qWaitForWindowExposed(&p));
    p.set("volume", 0);
    QSignalSpy loaded(&p, &Player::loaded);
    p.command({"loadfile", file});
    QTRY_COMPARE(loaded.size(), 1);
    p.set("pause", true);
    p.command({"seek", "1", "absolute+exact"});
    QTRY_VERIFY(std::abs(p.get("time-pos").toDouble() - 1) < .1);
    FramePreview preview;
    QSignalSpy ready(&preview, &FramePreview::ready),
        failed(&preview, &FramePreview::unavailable);
    preview.request(file, 2);
    preview.request(file, 5);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 6000);
    QCOMPARE(ready.first()[1].toDouble(), 5.0);
    auto image = qvariant_cast<QImage>(ready.first()[0]);
    QVERIFY(hasColor(image));
    QCOMPARE(image.size(), QSize(320, 180));
    QVERIFY(p.get("pause").toBool());
    QVERIFY(std::abs(p.get("time-pos").toDouble() - 1) < .1);
    preview.request(file, 5);
    QCOMPARE(ready.size(), 2); // Cached frame.
    preview.request(file, 3);
    preview.cancel();
    QTest::qWait(400);
    QCOMPARE(ready.size(), 2);
    preview.setFilters("transpose=clock");
    preview.request(file, 2);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 3, 6000);
    image = qvariant_cast<QImage>(ready.last()[0]);
    QVERIFY(image.height() > image.width());
    preview.request(file + ".missing", 0);
    QCOMPARE(failed.size(), 1);
  }
  void mediaOptionsAndSnapshots() {
    QTemporaryDir d;
    const auto source = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"));
    const auto video = d.filePath("movie.mp4");
    QVERIFY(QFile::copy(source.filePath("Русский H264.mp4"), video));
    QVERIFY(
        QFile::copy(source.filePath("subtitle.srt"), d.filePath("movie.srt")));
    Profile profile(d.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("subtitles/size", 44);
    profile.settings.setValue("subtitles/color", "#00ff00");
    profile.settings.setValue("audio/device", "missing-device");
    Window w(profile);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *p = w.findChild<Player *>();
    QVERIFY(p);
    QSignalSpy loaded(p, &Player::loaded), saved(&w, &Window::frameSaved),
        errors(p, &Player::error);
    w.openFiles({video});
    QTRY_COMPARE(loaded.size(), 1);
    p->set("pause", true);
    p->command({"seek", "2", "absolute+exact"});
    QTRY_VERIFY(std::abs(p->get("time-pos").toDouble() - 2) < .1);
    // time-pos can update before the paused seek supplies a frame to the VO.
    QTRY_VERIFY(!p->get("seeking").toBool());
    bool external = false;
    for (auto track : p->get("track-list").toList())
      external |= track.toMap().value("external").toBool();
    QVERIFY(external);
    QCOMPARE(p->get("sub-font-size").toDouble(), 44.0);
    QCOMPARE(p->get("audio-device").toString(), QString("auto"));
    QVERIFY(w.saveFrame(d.filePath("raw.png"), false));
    QTRY_COMPARE(saved.size(), 1);
    QVERIFY(saved.last()[1].toBool());
    QImage raw(d.filePath("raw.png"));
    QCOMPARE(raw.size(), QSize(320, 180));
    QVERIFY(hasColor(raw));
    QVERIFY(w.saveFrame(d.filePath("sub.png"), true));
    QTRY_COMPARE(saved.size(), 2);
    QVERIFY(saved.last()[1].toBool());
    QVERIFY(QImage(d.filePath("sub.png")) != raw);
    profile.settings.setValue("video/saturation", -100);
    profile.settings.setValue("audio/normalize", true);
    profile.settings.setValue("audio/channels", "stereo");
    MediaOptions::apply(p, profile.settings);
    QTest::qWait(300);
    QVERIFY(w.saveFrame(d.filePath("gray.png"), false));
    QTRY_COMPARE(saved.size(), 3);
    QVERIFY(saved.last()[1].toBool());
    QVERIFY(!hasColor(QImage(d.filePath("gray.png"))));
    QCOMPARE(errors.size(), 0);
    QVERIFY(w.saveFrame(d.filePath("missing/frame.png"), false));
    QTRY_COMPARE(saved.size(), 4);
    QVERIFY(!saved.last()[1].toBool());
    profile.settings.setValue("subtitles/autoLoad", false);
    MediaOptions::apply(p, profile.settings);
    w.openFiles({video});
    QTRY_COMPARE(loaded.size(), 2);
    for (auto track : p->get("track-list").toList())
      QVERIFY(!track.toMap().value("external").toBool());
  }
  void presentationPreferences() {
    QTemporaryDir d;
    Profile profile(d.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    Window w(profile);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto action = [&](const QString &id) {
      return w.findChild<QAction *>("action_" + id);
    };
    bool edited = false;
    afterPreferencesReady(&w, [&] {
      auto *dialog = w.findChild<QDialog *>("preferencesDialog");
      if (!dialog)
        return;
      dialog->findChild<QCheckBox *>("ui/onTop")->setChecked(true);
      dialog->findChild<QCheckBox *>("ui/frameless")->setChecked(true);
      dialog->findChild<QCheckBox *>("ui/remainingTime")->setChecked(true);
      dialog->findChild<QSpinBox *>("video/brightness")->setValue(10);
      edited = true;
      dialog->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Ok)
          ->click();
    });
    action("preferences")->trigger();
    QVERIFY(edited);
    QVERIFY(w.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    QVERIFY(w.windowFlags().testFlag(Qt::FramelessWindowHint));
    QCOMPARE(profile.settings.value("video/brightness").toInt(), 10);
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded);
    w.openFiles({QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                     .filePath("chapters.mkv")});
    QTRY_COMPARE(loaded.size(), 1);
    auto *osd = w.findChild<QLabel *>("osd");
    action("play")->trigger();
    QTRY_VERIFY(osd->isVisible());
    QTRY_VERIFY(
        w.findChild<QLabel *>("playbackClock")->text().startsWith("- "));
    QTRY_VERIFY_WITH_TIMEOUT(!osd->isVisible(), 2500);
    profile.settings.setValue("ui/osd", false);
    action("play")->trigger();
    QTest::qWait(100);
    QVERIFY(!osd->isVisible());
    const auto folder = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!folder.isEmpty()) {
      QDir().mkpath(folder);
      w.grab().save(folder + "/presentation.png");
    }
  }

  void outputFallback() {
    const QString media = qEnvironmentVariable("FOXICMP_TEST_MEDIA");
    if (media.isEmpty())
      QSKIP("Set FOXICMP_TEST_MEDIA");
    Player p(nullptr, "opengl");
    p.resize(640, 360);
    p.set("hwdec", "no");
    p.set("volume", 0);
    // No visible GL surface: initialization cannot occur. The deadline must
    // select software and retain the open request queued before renderer setup.
    QSignalSpy loaded(&p, &Player::loaded), events(&p, &Player::property);
    QVERIFY(p.command({"loadfile", QDir(media).filePath("chapters.mkv")}));
    QTRY_VERIFY_WITH_TIMEOUT(p.ready(), 6000);
    QCOMPARE(p.outputName(), QString("Software"));
    QTRY_VERIFY(loaded.size() == 1);
    bool reported = false;
    for (auto event : events)
      if (event.first().toString() == "render-fallback")
        reported = true;
    QVERIFY(reported);
    p.show();
    QVERIFY(QTest::qWaitForWindowExposed(&p));
    QTRY_VERIFY(hasColor(p.grabFramebuffer()));
  }
  void continuousPlayback() {
    const int seconds =
        qEnvironmentVariableIntValue("FOXICMP_TEST_SOAK_SECONDS");
    if (seconds <= 0)
      QSKIP("Optional continuous playback: set FOXICMP_TEST_SOAK_SECONDS");
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(640, 360);
    p.show();
    QVERIFY(QTest::qWaitForWindowExposed(&p));
    p.set("hwdec", "no");
    p.set("volume", 0);
    p.set("loop-file", "inf");
    QSignalSpy loaded(&p, &Player::loaded), errors(&p, &Player::error);
    p.command({"loadfile", QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                               .filePath("chapters.mkv")});
    QTRY_VERIFY(loaded.size() > 0);
    int ticks = 0;
    QTimer heartbeat;
    heartbeat.setInterval(50);
    connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
    heartbeat.start();
    QElapsedTimer clock;
    clock.start();
    double maxAvSync = 0;
    int syncSamples = 0;
    while (clock.elapsed() < seconds * 1000) {
      QTest::qWait(250);
      QVERIFY(!p.get("idle-active").toBool());
      QCOMPARE(errors.size(), 0);
      const auto sync = p.get("avsync");
      if (clock.elapsed() > 1000 && sync.isValid() &&
          p.get("time-pos").toDouble() > 0.5) {
        maxAvSync = std::max(maxAvSync, std::abs(sync.toDouble()));
        ++syncSamples;
      }
    }
    qInfo() << "Duration seconds:" << seconds << "heartbeat ticks:" << ticks
            << "internal avsync samples:" << syncSamples
            << "maximum absolute seconds:" << maxAvSync
            << "audio output:" << p.get("current-ao")
            << "decoder frame drops:" << p.get("decoder-frame-drop-count");
    QVERIFY(syncSamples > 0);
    QVERIFY(ticks > seconds * 10);
    QVERIFY(hasColor(p.grabFramebuffer()));
    double before = p.get("time-pos").toDouble();
    p.showMinimized();
    QTest::qWait(800);
    QVERIFY(p.get("time-pos").toDouble() != before);
    p.showNormal();
    QTRY_VERIFY(hasColor(p.grabFramebuffer()));
  }
  void playback_data() {
    QTest::addColumn<QString>("file");
    const QString dir = qEnvironmentVariable("FOXICMP_TEST_MEDIA");
    if (dir.isEmpty())
      QSKIP("Set FOXICMP_TEST_MEDIA");
    for (const QString &f : QDir(dir).entryList(
             {"*.mkv", "*.mp4", "*.avi", "*.mov", "*.webm"}, QDir::Files))
      QTest::newRow(qPrintable(f)) << QDir(dir).filePath(f);
  }
  void playback() {
    QFETCH(QString, file);
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(640, 360);
    QSignalSpy outputEvents(&p, &Player::property);
    p.show();
    QVERIFY(QTest::qWaitForWindowExposed(&p));
    QTRY_VERIFY_WITH_TIMEOUT(p.ready(), 15000);
    if (qEnvironmentVariable("FOXICMP_TEST_OUTPUT") == "opengl") {
      bool fallback = false;
      for (auto event : outputEvents)
        if (event.first().toString() == "render-fallback")
          fallback = true;
      QVERIFY(p.outputName() == "OpenGL" ||
              (p.outputName() == "Software" && fallback));
      qInfo() << "Requested OpenGL; actual output:" << p.outputName();
    } else
      QCOMPARE(p.outputName(), QString("Software"));
    QVERIFY(p.set("hwdec", "no"));
    QSignalSpy loaded(&p, &Player::loaded), errors(&p, &Player::error);
    QVERIFY(p.command({"loadfile", file}));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > 0.2, 10000);
    QVERIFY(p.get("duration").toDouble() > 5);
    QVERIFY(!p.get("video-codec").toString().isEmpty());
    QVERIFY(!p.get("audio-codec-name").toString().isEmpty());
    QVERIFY(p.set("pause", true));
    QTest::qWait(250);
    double pos = p.get("time-pos").toDouble();
    QTest::qWait(250);
    QVERIFY(std::abs(p.get("time-pos").toDouble() - pos) < 0.1);
    QVERIFY(p.command({"seek", "3", "absolute+exact"}));
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(p.get("time-pos").toDouble() - 3) < 0.15,
                             10000);
    QVERIFY(p.set("volume", 42));
    QCOMPARE(p.get("volume").toDouble(), 42.0);
    QVERIFY(p.set("mute", true));
    QCOMPARE(p.get("mute").toBool(), true);

    QImage image;
    auto rendered = [&] {
      image = p.grabFramebuffer();
      if (image.isNull())
        return false;
      for (int y = 0; y < image.height(); y += 20)
        for (int x = 0; x < image.width(); x += 20) {
          QColor c = image.pixelColor(x, y);
          if (c.red() != c.blue() || c.green() != c.blue())
            return true;
        }
      return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(rendered(), 10000);
    image.save(QFileInfo(file).absolutePath() + "/" +
               QFileInfo(file).fileName() + ".png");
    if (file.endsWith("chapters.mkv")) {
      QCOMPARE(p.get("chapter-list").toList().size(), 2);
      int audio = 0, sub = 0;
      for (auto v : p.get("track-list").toList()) {
        auto t = v.toMap();
        if (t["type"] == "audio")
          audio++;
        if (t["type"] == "sub")
          sub++;
      }
      QCOMPARE(audio, 2);
      QCOMPARE(sub, 1);
      QVERIFY(p.set("aid", 2));
      QCOMPARE(p.get("aid").toLongLong(), 2);
      QVERIFY(p.set("sid", "no"));
      QVERIFY(p.command(
          {"sub-add",
           QDir(QFileInfo(file).absolutePath()).filePath("external.ass"),
           "select"}));
      QTRY_VERIFY(p.get("sid").toLongLong() > 0);
    }
    QVERIFY(p.set("speed", 1.25));
    QCOMPARE(p.get("speed").toDouble(), 1.25);
    QVERIFY(p.set("pause", false));
    QTRY_VERIFY(p.get("time-pos").toDouble() > 3.3);
    QVERIFY(p.command({"stop"}));
    QTRY_VERIFY(p.get("idle-active").toBool());
    QCOMPARE(errors.size(), 0);
    // A failed open must not poison the next decoder/renderer session.
    QVERIFY(p.command({"loadfile", file + ".missing"}));
    QTRY_VERIFY(errors.size() > 0);
    QVERIFY(p.command({"loadfile", file}));
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(p.get("time-pos").toDouble() > 0.1);
  }
};
int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  Q_INIT_RESOURCE(resources);
  applyTheme(app);
  std::setlocale(LC_NUMERIC, "C");
  PlayerTest t;
  return QTest::qExec(&t, argc, argv);
}
#include "player_test.moc"
