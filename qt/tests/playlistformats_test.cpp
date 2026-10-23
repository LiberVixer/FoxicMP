#include "preferencesdriver.h"
#include "player.h"
#include "playlistfile.h"
#include "playlistpanel.h"
#include "profile.h"
#include "theme.h"
#include "timeline.h"
#include "window.h"
#include <QStringEncoder>
#include <QtTest>
#include <QtWidgets>
#include <cmath>
class PlaylistFormatsTest : public QObject {
  Q_OBJECT
  QString media(const QString &name) const {
    return QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(name);
  }
  bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  }
  void prepare(Profile &p) {
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("audio/mute", true);
    p.settings.setValue("playback/hardwareDecoding", false);
    p.settings.setValue("video/output", qEnvironmentVariable(
                                            "FOXICMP_TEST_OUTPUT", "software"));
  }
  QAction *action(QObject &root, const QString &id) {
    return root.findChild<QAction *>("action_" + id);
  }
private slots:
  void roundTrip_data() {
    QTest::addColumn<QString>("extension");
    for (const auto &ext : {"m3u8", "m3u", "mpcpl", "pls", "asx"})
      QTest::newRow(ext) << QString(ext);
  }
  void roundTrip() {
    QFETCH(QString, extension);
    QTemporaryDir temp;
    const auto path = temp.filePath("list." + extension);
    PlaylistEntry a{
        temp.filePath("Русский, фильм & 2.mp4"),
        "Название, & <тест>",
        8.125,
        {{"aid", "2"},
         {"sid", "no"},
         {"audioFiles", QStringList{temp.filePath("зву к.m4a")}},
         {"subtitleFiles", QStringList{temp.filePath("текст.srt")}},
         {"chapters",
          QVariantList{QVariantMap{{"time", 0.}, {"title", "Первая"}},
                       QVariantMap{{"time", 3.5}, {"title", "Вторая"}}}}}};
    PlaylistEntry b{"https://example.invalid/video.mp4?a=1&b=2", "URL", -1, {}};
    QString error;
    QVERIFY2(PlaylistFile::writeEntries(path, {a, b}, &error),
             qPrintable(error));
    QList<PlaylistEntry> entries;
    QVERIFY2(PlaylistFile::readEntries(path, &entries, &error),
             qPrintable(error));
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].source, a.source);
    QCOMPARE(entries[0].title, a.title);
    QCOMPARE(entries[0].duration, a.duration);
    QCOMPARE(entries[0].metadata, a.metadata);
    QCOMPARE(entries[1].source, b.source);
    QVERIFY(PlaylistFile::isPlaylist(path));
    // Failed export preserves an already written file.
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto before = saved.readAll();
    saved.close();
    a.title = "invalid\nlabel";
    QVERIFY(!PlaylistFile::writeEntries(path, {a}, &error));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), before);
  }
  void portableExportMovesWithMedia() {
    QTemporaryDir temp;
    for (const auto &extension : {"m3u8", "mpcpl", "pls", "asx"}) {
      const QString original = temp.filePath(QString("original-") + extension);
      const QString moved = temp.filePath(QString("moved-") + extension);
      QVERIFY(QDir().mkpath(original));
      PlaylistEntry entry{
          QDir(original).filePath("#Название & %.mp4"),
          "Portable",
          10.,
          {{"audioFiles", QStringList{QDir(original).filePath("звук.m4a")}},
           {"selectedAudioFile", QDir(original).filePath("звук.m4a")}}};
      const QString basename = QString("list.") + extension;
      QString error;
      QVERIFY2(PlaylistFile::writeEntries(QDir(original).filePath(basename),
                                          {entry}, &error),
               qPrintable(error));
      QVERIFY(QDir().rename(original, moved));
      QList<PlaylistEntry> entries;
      QVERIFY2(PlaylistFile::readEntries(QDir(moved).filePath(basename),
                                         &entries, &error),
               qPrintable(error));
      QCOMPARE(entries[0].source, QDir(moved).filePath("#Название & %.mp4"));
      QCOMPARE(entries[0].metadata.value("audioFiles").toStringList(),
               QStringList{QDir(moved).filePath("звук.m4a")});
      QCOMPARE(entries[0].metadata.value("selectedAudioFile").toString(),
               QDir(moved).filePath("звук.m4a"));
    }
  }
  void compatibilityAndAtomicImport() {
    QTemporaryDir temp;
    QString error;
    QList<PlaylistEntry> entries{{"unchanged", {}, -1, {}}};
    const QList<QPair<QString, QByteArray>> malformed{
        {"bad.pls", "[playlist]\nFile1=a.mp4\nNumberOfEntries=2\n"},
        {"bad.mpcpl", "MPCPLAYLIST\n1,type,1\n1,video,device\n"},
        {"bad.asx", "<ASX><ENTRY><REF HREF=\"a.mp4\"/></ASX>"},
        {"entity.asx",
         "<!DOCTYPE ASX [<!ENTITY x SYSTEM "
         "'file:///etc/passwd'>]><ASX><ENTRY><REF HREF='a'/></ENTRY></ASX>"},
        {"bad.cue",
         "FILE \"a.wav\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:00:75\n"},
        {"bad.m3u8", QByteArray("#EXTM3U\n") + QByteArray::fromHex("fffe")},
        {"windows.mpcpl",
         "MPCPLAYLIST\n1,type,0\n1,filename,C:\\Movies\\a.mp4\n"}};
    for (const auto &entry : malformed) {
      const auto path = temp.filePath(entry.first);
      QVERIFY(write(path, entry.second));
      QVERIFY2(!PlaylistFile::readEntries(path, &entries, &error),
               qPrintable(entry.first));
      QCOMPARE(entries[0].source, QString("unchanged"));
      QVERIFY(!error.isEmpty());
    }
    const auto mpcpl = temp.filePath("classic.mpcpl");
    QVERIFY(write(
        mpcpl,
        "MPCPLAYLIST\naudio,-1\nsubtitles,-1\n2,type,0\n2,filename,two.mp4\n1,"
        "type,0\n1,filename,folder\\one.mp4\n1,filename,audio.m4a\n1,subtitle,"
        "sub.srt\n1,label,Name, with comma\n1,time,00:01:02.500\n"));
    QVERIFY2(PlaylistFile::readEntries(mpcpl, &entries, &error),
             qPrintable(error));
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].source, temp.filePath("folder/one.mp4"));
    QCOMPARE(entries[0].duration, 62.5);
    QCOMPARE(entries[0].title, QString("Name, with comma"));
    QCOMPARE(entries[0].metadata.value("audioFiles").toStringList(),
             QStringList{temp.filePath("audio.m4a")});
    const auto pls = temp.filePath("skip.pls");
    QVERIFY(write(pls, "[playlist]\nFile1=gopher://invalid/"
                       "a\nFile2=local.mp4\nNumberOfEntries=2\n"));
    QVERIFY(!PlaylistFile::readEntries(pls, &entries, &error));
    QVERIFY(PlaylistFile::readEntries(pls, &entries, &error, true));
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries[0].source, temp.filePath("local.mp4"));
    QStringEncoder utf16(QStringEncoder::Utf16LE);
    const auto utfPath = temp.filePath("utf16.mpcpl");
    QVERIFY(write(
        utfPath,
        QByteArray::fromHex("fffe") +
            utf16(QString("MPCPLAYLIST\n1,type,0\n1,filename,Кино.mp4\n"))));
    QVERIFY2(PlaylistFile::readEntries(utfPath, &entries, &error),
             qPrintable(error));
    QCOMPARE(entries[0].source, temp.filePath("Кино.mp4"));
    QStringEncoder cp1251("Windows-1251");
    QVERIFY(cp1251.isValid());
    const auto cpPath = temp.filePath("legacy.cue");
    QVERIFY(write(
        cpPath,
        cp1251(QString("TITLE \"Альбом\"\nFILE \"Звук.wav\" WAVE\nTRACK 01 "
                       "AUDIO\n TITLE \"Песня\"\n INDEX 01 00:00:00\n"))));
    QVERIFY2(PlaylistFile::readEntries(cpPath, &entries, &error),
             qPrintable(error));
    QCOMPARE(entries[0].title, QString("Альбом"));
  }
  void cueChaptersPlaybackAndPersistence() {
    QTemporaryDir temp;
    const auto cue = temp.filePath("album.cue");
    const auto data =
        QString(
            "PERFORMER \"Исполнитель\"\nTITLE \"Альбом\"\nFILE \"%1\" WAVE\n "
            "TRACK 01 AUDIO\n TITLE \"Начало\"\n INDEX 00 00:00:00\n INDEX 01 "
            "00:00:00\n TRACK 02 AUDIO\n TITLE \"Середина\"\n INDEX 01 "
            "00:02:37\nFILE \"%2\" WAVE\n TRACK 03 AUDIO\n INDEX 01 00:01:00\n")
            .arg(media("progressive-indexed/audio.m4a"),
                 media("Русский H264.mp4"));
    QVERIFY(write(cue, data.toUtf8()));
    Profile profile(temp.path());
    prepare(profile);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded), errors(player, &Player::error);
    window.openFiles({cue});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    auto *panel = window.findChild<PlaylistPanel *>();
    QCOMPARE(panel->activeList()->count(), 2);
    auto *timeline = window.findChild<Timeline *>();
    QTRY_COMPARE(timeline->chapters.size(), 2);
    QCOMPARE(timeline->chapters[1].toMap().value("title").toString(),
             QString("Середина"));
    QTRY_VERIFY(player->get("seekable").toBool());
    QVERIFY(window.seekTo(.25));
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - .25) < .1);
    action(window, "chapterNext")->trigger();
    const double expected = 2 + 37 / 75.;
    QTest::qWait(200);
    qInfo() << "CUE seek actual" << player->get("time-pos") << "expected"
            << expected;
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - expected) < .04);
    action(window, "chapterPrevious")->trigger();
    QTRY_VERIFY(player->get("time-pos").toDouble() < .05);
    auto *chapters = window.findChild<QMenu *>("chaptersMenu");
    chapters->actions()[1]->trigger();
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - expected) < .04);
    const auto exported = temp.filePath("with-chapters.mpcpl");
    QVERIFY(window.savePlaylist(exported));
    action(window, "next")->trigger();
    QTRY_COMPARE(loaded.size(), 2);
    player->set("pause", true);
    QTRY_COMPARE(timeline->chapters.size(), 1);
    QCOMPARE(timeline->chapters[0].toMap().value("time").toDouble(), 1.);
    QVERIFY(window.loadPlaylist(exported));
    QTRY_COMPARE(loaded.size(), 3);
    player->set("pause", true);
    QTRY_COMPARE(timeline->chapters.size(), 2);
    // A malformed import preserves the playing queue and engine.
    QVERIFY(write(temp.filePath("bad.asx"), "<ASX><ENTRY>"));
    QVERIFY(!window.loadPlaylist(temp.filePath("bad.asx")));
    QCOMPARE(panel->activeList()->count(), 2);
    QCOMPARE(loaded.size(), 3);
    QCOMPARE(errors.size(), 0);
    window.close();
  }
  void trackSelectionPerEntryAndRestart() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    {
      Window window(profile);
      window.show();
      auto *player = window.findChild<Player *>();
      QSignalSpy loaded(player, &Player::loaded);
      window.openFiles({media("chapters.mkv"), media("chapters.mkv")});
      QTRY_COMPARE(loaded.size(), 1);
      player->set("pause", true);
      auto *audio = window.findChild<QMenu *>("audioTracksMenu");
      QTRY_VERIFY(audio->actions().size() >= 2);
      for (auto *entry : audio->actions())
        if (entry->data().toInt() == 2) {
          entry->trigger();
          break;
        }
      QTRY_COMPARE(player->get("aid").toInt(), 2);
      window.findChild<QMenu *>("subtitleTracksMenu")
          ->actions()
          .first()
          ->trigger();
      QTRY_VERIFY(!player->get("sid").toBool());
      action(window, "next")->trigger();
      QTRY_COMPARE(loaded.size(), 2);
      player->set("pause", true);
      QCOMPARE(player->get("aid").toInt(), 1);
      action(window, "previous")->trigger();
      QTRY_COMPARE(loaded.size(), 3);
      player->set("pause", true);
      QTRY_COMPARE(player->get("aid").toInt(), 2);
      QVERIFY(!player->get("sid").toBool());
      QVERIFY(window.savePlaylist(temp.filePath("tracks.pls")));
      window.close();
    }
    Window restored(profile);
    restored.show();
    auto *player = restored.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    action(restored, "play")->trigger();
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    QTRY_COMPARE(player->get("aid").toInt(), 2);
    QVERIFY(restored.loadPlaylist(temp.filePath("tracks.pls")));
    QTRY_COMPARE(loaded.size(), 2);
    player->set("pause", true);
    QTRY_COMPARE(player->get("aid").toInt(), 2);
    QVERIFY(!player->get("sid").toBool());
    restored.close();
  }
  void externalTracksAndDockImport() {
    QTemporaryDir temp;
    const auto playlist = temp.filePath("external.mpcpl");
    PlaylistEntry entry{
        media("Русский H264.mp4"),
        "External tracks",
        -1,
        {{"aid", "2"},
         {"sid", "1"},
         {"audioFiles", QStringList{media("progressive-indexed/audio.m4a")}},
         {"subtitleFiles", QStringList{media("subtitle.srt")}}}};
    QString error;
    QVERIFY(PlaylistFile::writeEntries(playlist, {entry}, &error));
    Profile profile(temp.path());
    prepare(profile);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded), errors(player, &Player::error);
    window.openFiles({playlist});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    QTRY_COMPARE(player->get("aid").toInt(), 2);
    QTRY_COMPARE(player->get("sid").toInt(), 1);
    auto *panel = window.findChild<PlaylistPanel *>();
    auto *list = panel->createList("Imported without playback");
    window.addFiles({playlist});
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->text(), entry.title);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(list->item(0)->data(PlaylistPanel::Metadata).toMap(),
             entry.metadata);
    QVERIFY(write(temp.filePath("bad.pls"),
                  "[playlist]\nFile1=a\nNumberOfEntries=2\n"));
    window.addFiles({playlist, temp.filePath("bad.pls")});
    QCOMPARE(list->count(), 1);
    window.reopenCurrent();
    QTRY_COMPARE(loaded.size(), 2);
    player->set("pause", true);
    QTRY_COMPARE(player->get("aid").toInt(), 2);
    QTRY_COMPARE(player->get("sid").toInt(), 1);
    QVERIFY(window.loadExternalAudio(media("progressive/audio.m4a")));
    const auto selectedAudioFile = [&] {
      for (const auto &v : player->get("track-list").toList()) {
        const auto track = v.toMap();
        if (track.value("type").toString() == "audio" &&
            track.value("selected").toBool())
          return track.value("external-filename").toString();
      }
      return QString();
    };
    QTRY_COMPARE(selectedAudioFile(), media("progressive/audio.m4a"));
    window.reopenCurrent();
    QTRY_COMPARE(loaded.size(), 3);
    player->set("pause", true);
    QTRY_COMPARE(selectedAudioFile(), media("progressive/audio.m4a"));
    QCOMPARE(errors.size(), 0);
    window.close();
  }
  void errorPolicyAndBoundedTraversal() {
    QTemporaryDir temp;
    const auto missing = temp.filePath("missing.mp4"),
               corrupt = temp.filePath("broken.mp4"),
               valid = media("Русский H264.mp4");
    QVERIFY(write(corrupt, "not media"));
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("playback/after", "exit");
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded),
        failed(player, &Player::fileFailed);
    window.openFiles({missing, corrupt, valid});
    QTRY_COMPARE(failed.size(), 1);
    QTest::qWait(100);
    QCOMPARE(loaded.size(), 0);
    QVERIFY(window.isVisible());
    profile.settings.setValue("playlist/advanceOnError", true);
    window.openFiles({missing, corrupt, valid});
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 8000);
    player->set("pause", true);
    QCOMPARE(player->get("path").toString(), valid);
    QVERIFY(window.isVisible());
    profile.settings.setValue("playlist/repeat", "repeatAll");
    const int baseline = failed.size();
    window.openFiles({missing, corrupt});
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), baseline + 2, 8000);
    QTest::qWait(300);
    QCOMPARE(failed.size(), baseline + 2);
    QVERIFY(window.isVisible());
    profile.settings.setValue("playlist/shuffle", true);
    window.openFiles({missing, corrupt});
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), baseline + 4, 8000);
    QTest::qWait(300);
    QCOMPARE(failed.size(), baseline + 4);
    // A queued error advance must respect a later manual stop.
    connect(player, &Player::fileFailed, &window,
            [&] { action(window, "stop")->trigger(); });
    window.openFiles({missing, valid});
    QTRY_COMPARE(failed.size(), baseline + 5);
    QTest::qWait(300);
    QCOMPARE(loaded.size(), 1);
    QVERIFY(window.isVisible());
    window.close();
  }
  void skipKnownInvalidAndRetry() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    auto *panel = window.findChild<PlaylistPanel *>();
    QSignalSpy loaded(player, &Player::loaded),
        failed(player, &Player::fileFailed);
    const auto missing = temp.filePath("recover.mp4");
    window.openFiles({media("Русский H264.mp4"), missing, media("VP9.webm")});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    action(window, "next")->trigger();
    QTRY_COMPARE(failed.size(), 1);
    QVERIFY(
        panel->activeList()->item(1)->data(PlaylistPanel::Invalid).toBool());
    action(window, "previous")->trigger();
    QTRY_COMPARE(loaded.size(), 2);
    player->set("pause", true);
    action(window, "next")->trigger();
    QTRY_COMPARE(loaded.size(), 3);
    player->set("pause", true);
    QCOMPARE(player->get("path").toString(), media("VP9.webm"));
    QCOMPARE(failed.size(), 1);
    // Direct selection retries an invalid row; failure does not remove it.
    panel->playRequested(panel->activeList(), 1);
    QTRY_COMPARE(failed.size(), 2);
    profile.settings.setValue("playlist/skipInvalid", false);
    action(window, "previous")->trigger();
    QTRY_COMPARE(loaded.size(), 4);
    player->set("pause", true);
    action(window, "next")->trigger();
    QTRY_COMPARE(failed.size(), 3);
    QVERIFY(QFile::copy(media("Русский H264.mp4"), missing));
    panel->playRequested(panel->activeList(), 1);
    QTRY_COMPARE(loaded.size(), 5);
    player->set("pause", true);
    QVERIFY(
        !panel->activeList()->item(1)->data(PlaylistPanel::Invalid).toBool());
    profile.settings.setValue("playlist/skipInvalid", true);
    action(window, "previous")->trigger();
    QTRY_COMPARE(loaded.size(), 6);
    player->set("pause", true);
    action(window, "next")->trigger();
    QTRY_COMPARE(loaded.size(), 7);
    QCOMPARE(player->get("path").toString(), missing);
    window.close();
  }
  void optionsAndDockPersistence() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    Window window(profile);
    window.show();
    auto *dock = window.findChild<QDockWidget *>("playlistDock");
    auto *panel = window.findChild<PlaylistPanel *>();
    for (bool accept : {false, true}) {
      afterPreferencesReady(&window, [&] {
        auto *dialog = window.findChild<QDialog *>("preferencesDialog");
        QVERIFY(dialog);
        dialog->findChild<QCheckBox *>("playlist/determineDuration")
            ->setChecked(false);
        dialog->findChild<QCheckBox *>("playlist/hideFullscreen")
            ->setChecked(false);
        dialog->findChild<QCheckBox *>("playlist/advanceOnError")
            ->setChecked(true);
        const auto screenshotDir =
            qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        if (accept && !screenshotDir.isEmpty()) {
          auto *tabs = dialog->findChild<QTabWidget *>();
          for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == "Плейлист")
              tabs->setCurrentIndex(i);
          QVERIFY(
              dialog->grab().save(screenshotDir + "/playlist-preferences.png"));
        }
        dialog->findChild<QDialogButtonBox *>()
            ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
      });
      action(window, "preferences")->trigger();
      QCOMPARE(
          profile.settings.value("playlist/determineDuration", true).toBool(),
          !accept);
    }
    window.addFiles({media("Русский H264.mp4")});
    QTest::qWait(200);
    QCOMPARE(
        panel->activeList()->item(0)->data(PlaylistPanel::Duration).toDouble(),
        -1.);
    auto *left = action(window, "playlistDock_1");
    QVERIFY(left);
    left->trigger();
    QCOMPARE(window.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QVERIFY(dock->isVisible());
    action(window, "fullscreen")->trigger();
    QVERIFY(dock->isVisible());
    action(window, "fullscreen")->trigger();
    QVERIFY(dock->isVisible());
    const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty())
      QVERIFY(window.grab().save(screenshots + "/playlist-options.png"));
    window.close();
    Window restored(profile);
    restored.show();
    auto *restoredDock = restored.findChild<QDockWidget *>("playlistDock");
    QCOMPARE(restored.dockWidgetArea(restoredDock), Qt::LeftDockWidgetArea);
    QVERIFY(restoredDock->isVisible());
    profile.settings.setValue("playlist/determineDuration", true);
    restored.findChild<PlaylistPanel *>()->applyOptions();
    QTRY_VERIFY(restored.findChild<PlaylistPanel *>()
                    ->activeList()
                    ->item(0)
                    ->data(PlaylistPanel::Duration)
                    .toDouble() > 7.9);
    profile.settings.setValue("playlist/hideFullscreen", true);
    action(restored, "fullscreen")->trigger();
    QVERIFY(!restoredDock->isVisible());
    action(restored, "fullscreen")->trigger();
    QVERIFY(restoredDock->isVisible());
    restored.close();
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  applyTheme(app);
  Q_INIT_RESOURCE(resources);
  PlaylistFormatsTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "playlistformats_test.moc"
