#include "mediasource.h"
#include "player.h"
#include "playlistfile.h"
#include "playlistpanel.h"
#include "profile.h"
#include "theme.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>

class PlaylistPanelTest : public QObject {
  Q_OBJECT
  QString media(const QString &name) const {
    return QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(name);
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
  QStringList sources(QListWidget *list) {
    QStringList result;
    for (int i = 0; i < list->count(); ++i)
      result.append(list->item(i)->data(PlaylistPanel::Source).toString());
    return result;
  }
private slots:
  void migrationTabsAndRestoration() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    const QStringList original{temp.filePath("movie10.mp4"),
                               temp.filePath("movie2.mp4"),
                               "https://example.invalid/a.mp4"};
    profile.settings.setValue("files/playlist", original);
    QString firstId, secondId;
    {
      PlaylistPanel panel(profile);
      panel.show();
      auto *first = panel.activeList();
      QCOMPARE(sources(first), original);
      firstId = first->item(0)->data(PlaylistPanel::Id).toString();
      action(panel, "playlistSort_name")->trigger();
      QCOMPARE(first->item(1)->text(), QString("movie2.mp4"));
      action(panel, "playlistSort_original")->trigger();
      QCOMPARE(sources(first), original);
      auto *second = panel.createList("Русский плейлист");
      panel.append(second, {original.first(), original.first()});
      secondId = second->item(1)->data(PlaylistPanel::Id).toString();
      QVERIFY(firstId != secondId);
      QVERIFY(secondId != second->item(0)->data(PlaylistPanel::Id).toString());
      QTimer::singleShot(50, &panel, [&] {
        auto *dialog = panel.findChild<QInputDialog *>();
        QVERIFY(dialog);
        dialog->setTextValue("Музыка");
        dialog->accept();
      });
      action(panel, "playlistRenameTab")->trigger();
      panel.save(first, firstId);
      profile.settings.sync();
    }
    Profile restored(temp.path());
    PlaylistPanel panel(restored);
    auto *tabs = panel.findChild<QTabWidget *>("playlistTabs");
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->tabText(1), QString("Музыка"));
    QCOMPARE(panel.activeList(), panel.lists()[1]);
    QCOMPARE(panel.savedPlayingList(), panel.lists()[0]);
    QCOMPARE(sources(panel.savedPlayingList()), original);
    QCOMPARE(panel.activeList()->item(1)->data(PlaylistPanel::Id).toString(),
             secondId);
    QCOMPARE(
        panel.savedPlayingList()->item(0)->data(PlaylistPanel::Id).toString(),
        firstId);
    action(panel, "playlistSort_reverse")->trigger();
    action(panel, "playlistSort_original")->trigger();
    QCOMPARE(panel.activeList()->item(1)->data(PlaylistPanel::Id).toString(),
             secondId);
  }
  void sortingSearchClipboardAndMissing() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    PlaylistPanel panel(profile);
    panel.show();
    const auto a = temp.filePath("movie10.mp4"),
               b = temp.filePath("movie2.mp4");
    QFile small(a), large(b);
    QVERIFY(small.open(QIODevice::WriteOnly));
    small.write("a");
    small.close();
    QVERIFY(large.open(QIODevice::WriteOnly));
    large.write("abcdef");
    large.close();
    QVERIFY(small.open(QIODevice::ReadWrite));
    QVERIFY(small.setFileTime(QDateTime::fromSecsSinceEpoch(1000),
                              QFileDevice::FileModificationTime));
    small.close();
    panel.append(panel.activeList(),
                 {a, b, "https://example.invalid/movie.mp4"});
    auto *list = panel.activeList();
    action(panel, "playlistSort_name")->trigger();
    QCOMPARE(sources(list).mid(0, 2),
             QStringList({"https://example.invalid/movie.mp4", b}));
    action(panel, "playlistSort_path")->trigger();
    QCOMPARE(list->item(0)->data(PlaylistPanel::Source).toString(), b);
    action(panel, "playlistSort_size")->trigger();
    QCOMPARE(sources(list),
             QStringList({a, b, "https://example.invalid/movie.mp4"}));
    action(panel, "playlistSort_modified")->trigger();
    QCOMPARE(list->item(0)->data(PlaylistPanel::Source).toString(), a);
    action(panel, "playlistSort_created")->trigger();
    QCOMPARE(list->item(2)->data(PlaylistPanel::Source).toString(),
             QString("https://example.invalid/movie.mp4"));
    action(panel, "playlistSort_random")->trigger();
    const auto randomized = sources(list);
    QCOMPARE(QSet<QString>(randomized.begin(), randomized.end()).size(), 3);
    action(panel, "playlistSort_original")->trigger();
    QCOMPARE(sources(list),
             QStringList({a, b, "https://example.invalid/movie.mp4"}));
    auto *search = panel.findChild<QLineEdit *>("playlistSearch");
    search->setText("MOVIE2");
    QVERIFY(list->item(0)->isHidden());
    QVERIFY(!list->item(1)->isHidden());
    action(panel, "playlistSelectAll")->trigger();
    QCOMPARE(list->selectedItems().size(), 1);
    list->setFocus();
    QTest::keyClick(list, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), b);
    search->clear();
    QTest::keyClick(list, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(list->count(), 4);
    list->clearSelection();
    list->setCurrentRow(3);
    QTest::keyClick(list, Qt::Key_X, Qt::ControlModifier);
    QCOMPARE(list->count(), 3);
    QVERIFY(QFileInfo::exists(b));
    QVERIFY(QFile::remove(a));
    action(panel, "playlistRemoveMissing")->trigger();
    QCOMPARE(sources(list),
             QStringList({b, "https://example.invalid/movie.mp4"}));
  }
  void tabsDoNotChangePlaybackOrNaturalAdvance() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    Window window(profile);
    window.show();
    auto *panel = window.findChild<PlaylistPanel *>();
    auto *player = window.findChild<Player *>();
    auto *first = panel->activeList();
    QSignalSpy loaded(player, &Player::loaded), errors(player, &Player::error);
    const auto a = media("progressive-indexed/video.mp4"),
               b = media("Русский H264.mp4");
    window.openFiles({a, b});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    auto *second = panel->createList("Другая очередь");
    window.addFiles({media("VP9.webm")});
    QCOMPARE(second->count(), 1);
    QCOMPARE(first->count(), 2);
    action(window, "playlistClear")->trigger();
    QCOMPARE(second->count(), 0);
    QCOMPARE(player->get("path").toString(), a);
    QVERIFY(player->get("pause").toBool());
    window.addFiles({media("VP9.webm")});
    QVERIFY(window.savePlaylist(temp.filePath("other.m3u8")));
    QStringList exported;
    QString error;
    QVERIFY(PlaylistFile::read(temp.filePath("other.m3u8"), &exported, &error));
    QCOMPARE(exported, sources(second));
    // Duration completion must not reset the shuffle trail or replace playback.
    QTRY_VERIFY(first->item(0)->data(PlaylistPanel::Duration).toDouble() > 19);
    QCOMPARE(player->get("path").toString(), a);
    player->command({"seek", "19.8", "absolute+exact"});
    player->set("pause", false);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 6000);
    QCOMPARE(player->get("path").toString(), b);
    player->set("pause", true);
    QCOMPARE(panel->activeList(), second);
    second->setCurrentRow(0);
    second->setFocus();
    QTest::keyClick(second, Qt::Key_Return);
    QTRY_COMPARE(loaded.size(), 3);
    QCOMPARE(player->get("path").toString(), media("VP9.webm"));
    player->set("pause", true);
    window.close();
    QCOMPARE(errors.size(), 0);
    Window restored(profile);
    auto *restoredPanel = restored.findChild<PlaylistPanel *>();
    QCOMPARE(restoredPanel->lists().size(), 2);
    QCOMPARE(sources(restoredPanel->lists()[0]), QStringList({a, b}));
    QCOMPARE(restoredPanel->savedPlayingList(), restoredPanel->activeList());
  }
  void currentSurvivesSortAndBackgroundTabRemoval() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    Window window(profile);
    window.show();
    auto *panel = window.findChild<PlaylistPanel *>();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles(
        {media("Русский H264.mp4"), media("VP9.webm"), media("chapters.mkv")});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    auto *first = panel->activeList();
    const auto id = first->item(0)->data(PlaylistPanel::Id).toString();
    action(window, "playlistSort_reverse")->trigger();
    QVERIFY(first->item(2)->font().bold());
    QCOMPARE(first->item(2)->data(PlaylistPanel::Id).toString(), id);
    action(window, "playlistSort_original")->trigger();
    action(window, "next")->trigger();
    QTRY_COMPARE(loaded.size(), 2);
    QCOMPARE(player->get("path").toString(), media("VP9.webm"));
    player->set("pause", true);
    auto *other = panel->createList("Удаляемая");
    panel->append(other, {media("HEVC.mov")});
    QTimer::singleShot(50, &window, [&] {
      auto *confirm = window.findChild<QMessageBox *>();
      QVERIFY(confirm);
      confirm->button(QMessageBox::Yes)->click();
    });
    action(window, "playlistCloseTab")->trigger();
    QCOMPARE(panel->lists().size(), 1);
    QCOMPARE(player->get("path").toString(), media("VP9.webm"));
    QVERIFY(player->get("pause").toBool());
    // Deleting the playing tab stops playback and leaves an empty usable tab.
    QTimer::singleShot(50, &window, [&] {
      window.findChild<QMessageBox *>()->button(QMessageBox::Yes)->click();
    });
    action(window, "playlistCloseTab")->trigger();
    QTRY_VERIFY(player->get("idle-active").toBool());
    QCOMPARE(panel->lists().size(), 1);
    QCOMPARE(panel->activeList()->count(), 0);
    window.addFiles({media("Русский H264.mp4")});
    action(window, "play")->trigger();
    QTRY_COMPARE(loaded.size(), 3);
    window.close();
  }
  void durationFolderBrowserAndInfo() {
    QTemporaryDir temp;
    const auto folder = temp.filePath("Видео");
    QVERIFY(QDir().mkpath(folder));
    const auto file = QDir(folder).filePath("Тест.mp4");
    QVERIFY(QFile::copy(media("Русский H264.mp4"), file));
    Profile profile(temp.path());
    prepare(profile);
    Window window(profile);
    window.show();
    auto *panel = window.findChild<PlaylistPanel *>();
    bool added = false;
    QTimer::singleShot(50, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("playlistFolderBrowser");
      QVERIFY(dialog);
      auto *path = dialog->findChild<QLineEdit *>("playlistFolderPath");
      path->setText(folder);
      QTest::keyClick(path, Qt::Key_Return);
      for (auto *button : dialog->findChildren<QPushButton *>())
        if (button->text() == "Добавить всю папку")
          button->click();
      added = true;
      dialog->accept();
    });
    action(window, "playlistBrowse")->trigger();
    QVERIFY(added);
    QCOMPARE(sources(panel->activeList()), QStringList{file});
    QTRY_VERIFY_WITH_TIMEOUT(
        panel->activeList()->item(0)->data(PlaylistPanel::Duration).toDouble() >
            7.9,
        8000);
    action(window, "playlistSort_duration")->trigger();
    panel->activeList()->setCurrentRow(0);
    bool reportRead = false;
    QTimer timer;
    connect(&timer, &QTimer::timeout, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("playlistInfoDialog");
      if (!dialog)
        return;
      const auto text =
          dialog->findChild<QPlainTextEdit *>("playlistInfoReport")
              ->toPlainText();
      if (text.contains("h264", Qt::CaseInsensitive) || text.contains("AVC")) {
        reportRead = true;
        dialog->accept();
      }
    });
    timer.start(50);
    QTimer::singleShot(12000, &window, [&] {
      if (auto *dialog = window.findChild<QDialog *>("playlistInfoDialog"))
        dialog->reject();
    });
    action(window, "playlistInfo")->trigger();
    QVERIFY(reportRead);
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({file, media("chapters.mkv"), media("VP9.webm")});
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(player->get("time-pos").toDouble() > .1);
    player->set("pause", true);
    QTRY_VERIFY(
        panel->activeList()->item(2)->data(PlaylistPanel::Duration).toDouble() >
        7);
    action(window, "playlist")->trigger();
    window.resize(1180, 680);
    panel->createList("Музыка");
    window.addFiles({media("progressive-indexed/audio.m4a")});
    panel->findChild<QTabWidget *>("playlistTabs")->setCurrentIndex(0);
    window.resizeDocks({window.findChild<QDockWidget *>()}, {360},
                       Qt::Horizontal);
    const auto screenshotDir = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!screenshotDir.isEmpty()) {
      QTest::qWait(100);
      QVERIFY(window.grab().save(screenshotDir + "/playlist-expanded.png"));
    }
    window.close();
  }
  void trashIsSeparateConfirmedAndRemovesDuplicates() {
    QTemporaryDir temp;
    const auto file = temp.filePath("delete-only-this-test-file.mp4");
    QFile data(file);
    QVERIFY(data.open(QIODevice::WriteOnly));
    data.write("test");
    data.close();
    Profile profile(temp.path());
    PlaylistPanel panel(profile);
    panel.show();
    auto *first = panel.activeList();
    panel.append(first, {file, file});
    auto *second = panel.createList("Копия");
    panel.append(second, {file});
    second->setCurrentRow(0);
    for (auto answer : {QMessageBox::No, QMessageBox::Yes}) {
      QTimer::singleShot(50, &panel, [&] {
        auto *confirm = panel.findChild<QMessageBox *>("playlistTrashConfirm");
        QVERIFY(confirm);
        QCOMPARE(confirm->defaultButton(), confirm->button(QMessageBox::No));
        confirm->done(answer);
      });
      action(panel, "playlistTrash")->trigger();
      QCOMPARE(QFileInfo::exists(file), answer == QMessageBox::No);
      QCOMPARE(first->count(), answer == QMessageBox::No ? 2 : 0);
      QCOMPARE(second->count(), answer == QMessageBox::No ? 1 : 0);
    }
  }
};
int main(int argc, char **argv) {
  QTemporaryDir isolatedTrash;
  qputenv("XDG_DATA_HOME", isolatedTrash.path().toUtf8());
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  applyTheme(app);
  Q_INIT_RESOURCE(resources);
  PlaylistPanelTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "playlistpanel_test.moc"
