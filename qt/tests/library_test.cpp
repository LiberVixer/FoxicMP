#include "preferencesdriver.h"
#include "devicesource.h"
#include "mediasource.h"
#include "player.h"
#include "profile.h"
#include "theme.h"
#include "window.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <QtWidgets>
#include <cmath>
class LibraryTest : public QObject {
  Q_OBJECT
  QString media(const QString &name) const {
    return QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(name);
  }
  void prepare(Profile &profile) {
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("playback/hardwareDecoding", false);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
  }
  QAction *action(Window &window, const QString &id) {
    return window.findChild<QAction *>("action_" + id);
  }
  QStringList favoriteIds(Profile &profile) {
    QStringList ids;
    for (const auto &entry : profile.favorites())
      ids.append(entry.id);
    return ids;
  }
  QTreeWidgetItem *historyItem(QTreeWidget *list, const QString &source) {
    for (int row = 0; row < list->topLevelItemCount(); ++row)
      if (list->topLevelItem(row)->data(0, Qt::UserRole).toString() == source)
        return list->topLevelItem(row);
    return nullptr;
  }
  QListWidgetItem *favoriteItem(QListWidget *list, const QString &id) {
    for (int row = 0; row < list->count(); ++row)
      if (list->item(row)->data(Qt::UserRole).toString() == id)
        return list->item(row);
    return nullptr;
  }
private slots:
  void historyCapacityDeleteAndSuppression() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    profile.setHistoryLimits(3, 2);
    for (int n = 0; n < 5; ++n) {
      const auto path = temp.filePath(QString::number(n) + ".mp4");
      profile.recordRecent(path);
      profile.savePosition(path, n + 10, 100);
    }
    const auto a = temp.filePath("4.mp4"), b = temp.filePath("3.mp4"),
               c = temp.filePath("2.mp4");
    QCOMPARE(profile.recentFiles(), QStringList({a, b, c}));
    QCOMPARE(profile.historyMenuLimit(), 2);
    const auto favorite = profile.addFavorite("Keep", b, 7);
    profile.removeRecent({b});
    QCOMPARE(profile.recentFiles(), QStringList({a, c}));
    QCOMPARE(profile.position(b), 0.);
    profile.savePosition(b, 25, 100);
    QCOMPARE(profile.position(b), 0.);
    profile.recordRecent(b);
    profile.savePosition(b, 25, 100);
    QCOMPARE(profile.position(b), 25.);
    profile.clearHistory();
    profile.savePosition(a, 30, 100);
    profile.savePosition(b, 30, 100);
    QVERIFY(profile.recentFiles().isEmpty());
    QCOMPARE(profile.position(a), 0.);
    QCOMPARE(profile.position(b), 0.);
    QCOMPARE(profile.favorites().first().id, favorite);
    profile.settings.setValue("history/enabled", false);
    profile.recordRecent(a);
    profile.savePosition(a, 11, 100);
    QVERIFY(profile.recentFiles().isEmpty());
    QCOMPARE(profile.position(a), 11.);
    profile.setHistoryLimits(0, 1000);
    QCOMPARE(profile.historyLimit(), 1);
    QCOMPARE(profile.historyMenuLimit(), 100);
    profile.settings.sync();
    Profile restored(temp.path());
    QCOMPARE(restored.position(a), 11.);
    QCOMPARE(restored.favorites().first().id, favorite);
  }
  void portableFavoritesAndLegacyMigration() {
    QTemporaryDir temp;
    const auto before = temp.filePath("before"), after = temp.filePath("after");
    QVERIFY(QDir().mkpath(before + "/media"));
    const auto file = before + "/media/Кино #%.mp4",
               image = before + "/media/disc.iso";
    QString fileId, discId, networkId, deviceId;
    {
      Profile profile(before);
      fileId = profile.addFavorite("Film", file, 2.25, false, true);
      DeviceSource::Request disc;
      disc.kind = "dvd";
      disc.path = image;
      disc.title = 2;
      discId = profile.addFavorite("DVD", DeviceSource::encode(disc), 12.5,
                                   true, true);
      networkId = profile.addFavorite("Network", "https://example.invalid/a", 3,
                                      true, true);
      DeviceSource::Request device;
      device.kind = "test-video";
      deviceId = profile.addFavorite("Device", DeviceSource::encode(device), 3,
                                     true, true);
      const auto entries = profile.favorites();
      QCOMPARE(entries.size(), 4);
      QVERIFY(!entries[0].rememberPosition);
      QVERIFY(entries[0].portable);
      QVERIFY(entries[1].rememberPosition);
      QVERIFY(entries[1].portable);
      QVERIFY(!entries[2].portable);
      QVERIFY(!entries[3].portable);
      QVERIFY(!entries[3].rememberPosition);
      const auto raw =
          QJsonDocument::fromJson(
              profile.settings.value("favorites/entries").toByteArray())
              .array();
      QCOMPARE(raw[0].toObject().value("file").toString(),
               QString("media/Кино #%.mp4"));
      QVERIFY(!profile.settings.value("favorites/entries")
                   .toByteArray()
                   .contains(before.toUtf8()));
      QVERIFY(profile.reorderFavorites({deviceId, fileId, networkId, discId}));
      const auto order = favoriteIds(profile);
      QVERIFY(!profile.reorderFavorites({fileId, fileId, networkId, discId}));
      QCOMPARE(favoriteIds(profile), order);
      QVERIFY(!profile.reorderFavorites({fileId}));
      QVERIFY(!profile.updateFavorite(fileId, "", 1, true, false));
      QVERIFY(!profile.updateFavorite(fileId, "bad", qQNaN(), true, false));
      profile.settings.sync();
    }
    QVERIFY(QDir().rename(before, after));
    Profile profile(after);
    const auto entries = profile.favorites();
    QCOMPARE(entries[1].id, fileId);
    QCOMPARE(entries[1].file, after + "/media/Кино #%.mp4");
    DeviceSource::Request disc;
    QVERIFY(DeviceSource::decode(entries[3].file, &disc));
    QCOMPARE(disc.path, after + "/media/disc.iso");
    QCOMPARE(disc.title, 2);
    QCOMPARE(entries[3].position, 12.5);
    QCOMPARE(Profile::favoriteCategory(entries[0].file), QString("devices"));
    QCOMPARE(Profile::favoriteCategory(entries[3].file), QString("discs"));
    QVERIFY(profile.updateFavorite(fileId, "Edited", 6.25, true, false));
    QCOMPARE(profile.favorites()[1].title, QString("Edited"));
    QVERIFY(!profile.favorites()[1].portable);
    QTemporaryDir legacy;
    Profile old(legacy.path());
    old.settings.setValue(
        "favorites/entries",
        QJsonDocument(QJsonArray{QJsonObject{{"id", "legacy"},
                                             {"title", "Old"},
                                             {"file", file},
                                             {"position", 4.5}}})
            .toJson());
    QCOMPARE(old.favorites().first().position, 4.5);
    QVERIFY(old.favorites().first().rememberPosition);
    QVERIFY(!old.favorites().first().portable);
  }
  void historyDialogSearchCopyRemoveAndCategories() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    const auto a = media("Русский H264.mp4"), b = media("VP9.webm"),
               missing = temp.filePath("missing.mp4");
    const QString network = "https://example.invalid/movie.mp4";
    DeviceSource::Request live;
    live.kind = "test-video";
    const auto device = DeviceSource::encode(live);
    for (const auto &source : {a, b, missing, network, device})
      profile.recordRecent(source);
    profile.savePosition(a, 2.25, 8);
    profile.addFavorite("Favorite", a, 1.5);
    Window window(profile);
    window.show();
    bool handled = false;
    QTimer::singleShot(80, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("historyDialog");
      QVERIFY(dialog);
      auto *list = dialog->findChild<QTreeWidget *>("historyList");
      QCOMPARE(list->columnCount(), 3);
      QCOMPARE(list->topLevelItemCount(), 5);
      auto *search = dialog->findChild<QLineEdit *>("historySearch");
      search->setText("РУССКИЙ");
      QVERIFY(!historyItem(list, a)->isHidden());
      QVERIFY(historyItem(list, b)->isHidden());
      list->setCurrentItem(historyItem(list, a));
      list->setFocus();
      QTest::keyClick(list, Qt::Key_C, Qt::ControlModifier);
      QCOMPARE(QApplication::clipboard()->text(), a);
      QVERIFY(QApplication::clipboard()->mimeData()->hasUrls());
      search->clear();
      list->clearSelection();
      list->setCurrentItem(historyItem(list, a));
      dialog->findChild<QPushButton *>("historyRemove")->click();
      QVERIFY(!profile.recentFiles().contains(a));
      QCOMPARE(profile.position(a), 0.);
      QVERIFY(QFileInfo::exists(a));
      dialog->findChild<QPushButton *>("historyRemoveMissing")->click();
      QVERIFY(!profile.recentFiles().contains(missing));
      QVERIFY(profile.recentFiles().contains(network));
      QVERIFY(profile.recentFiles().contains(device));
      auto *category = dialog->findChild<QComboBox *>("historyCategory");
      category->setCurrentIndex(category->findData("devices"));
      QVERIFY(!historyItem(list, device)->isHidden());
      QVERIFY(historyItem(list, b)->isHidden());
      category->setCurrentIndex(0);
      const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      if (!screenshots.isEmpty())
        QVERIFY(dialog->grab().save(screenshots + "/history.png"));
      dialog->findChild<QPushButton *>("historyClear")->click();
      QCOMPARE(list->topLevelItemCount(), 0);
      QCOMPARE(profile.favorites().size(), 1);
      handled = true;
      dialog->reject();
    });
    action(window, "historyManage")->trigger();
    QVERIFY(handled);
    window.close();
  }
  void historyPlaybackStartAndDeleteWhilePlaying() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    const auto file = media("Русский H264.mp4");
    profile.recordRecent(file);
    profile.savePosition(file, 2.25, 8);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    connect(player, &Player::loaded, &window,
            [player] { player->set("pause", true); });
    QSignalSpy loaded(player, &Player::loaded), errors(player, &Player::error);
    const auto choose = [&](const QString &button) {
      QTimer::singleShot(80, &window, [&, button] {
        auto *dialog = window.findChild<QDialog *>("historyDialog");
        QVERIFY(dialog);
        auto *list = dialog->findChild<QTreeWidget *>("historyList");
        QVERIFY(historyItem(list, file));
        list->setCurrentItem(historyItem(list, file));
        dialog->findChild<QPushButton *>(button)->click();
      });
      action(window, "historyManage")->trigger();
    };
    choose("historyOpen");
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 2.25) < .1);
    choose("historyOpenStart");
    QTRY_COMPARE(loaded.size(), 2);
    QTRY_VERIFY(player->get("time-pos").toDouble() < .1);
    QVERIFY(window.seekTo(3.25));
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 3.25) < .1);
    QTimer::singleShot(80, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("historyDialog");
      auto *list = dialog->findChild<QTreeWidget *>("historyList");
      list->setCurrentItem(historyItem(list, file));
      dialog->findChild<QPushButton *>("historyRemove")->click();
      dialog->reject();
    });
    action(window, "historyManage")->trigger();
    QVERIFY(profile.recentFiles().isEmpty());
    QCOMPARE(profile.position(file), 0.);
    QMetaObject::invokeMethod(window.findChild<QMenu *>("historyMenu"),
                              "aboutToShow", Qt::DirectConnection);
    QCOMPARE(profile.position(file), 0.);
    QCOMPARE(player->get("path").toString(), file);
    QVERIFY(player->get("pause").toBool());
    window.reopenCurrent();
    QTRY_COMPARE(loaded.size(), 3);
    QVERIFY(profile.recentFiles().contains(file));
    QCOMPARE(errors.size(), 0);
    window.close();
  }
  void favoritesEditorOrderFilterAndMenus() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    const auto a = profile.addFavorite("Alpha", media("Русский H264.mp4"), 1.5),
               b = profile.addFavorite("Beta", media("VP9.webm"), 2.5);
    DeviceSource::Request disc;
    disc.kind = "dvd";
    disc.path = temp.filePath("disc.iso");
    disc.title = 2;
    const auto d = profile.addFavorite("Disc", DeviceSource::encode(disc), 3);
    DeviceSource::Request device;
    device.kind = "test-video";
    const auto v =
        profile.addFavorite("Device", DeviceSource::encode(device), 0);
    Window window(profile);
    window.show();
    bool handled = false;
    QTimer::singleShot(80, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("favoritesDialog");
      QVERIFY(dialog);
      auto *list = dialog->findChild<QListWidget *>("favoritesList");
      QCOMPARE(list->count(), 4);
      // Internal drag-and-drop uses the model's move operation. Its resulting
      // order must be saved even when no manager button was pressed.
      QVERIFY(list->model()->moveRow({}, 1, {}, 4));
      QCOMPARE(favoriteIds(profile), QStringList({a, d, v, b}));
      QVERIFY(list->model()->moveRow({}, 3, {}, 1));
      QCOMPARE(favoriteIds(profile), QStringList({a, b, d, v}));
      list->setCurrentItem(favoriteItem(list, b));
      dialog->findChild<QPushButton *>("favoriteDown")->click();
      QCOMPARE(favoriteIds(profile), QStringList({a, d, b, v}));
      auto *category = dialog->findChild<QComboBox *>("favoritesCategory");
      category->setCurrentIndex(category->findData("files"));
      list->clearSelection();
      list->setCurrentItem(favoriteItem(list, b));
      dialog->findChild<QPushButton *>("favoriteUp")->click();
      QCOMPARE(favoriteIds(profile), QStringList({b, d, a, v}));
      QCOMPARE(favoriteItem(list, d)->isHidden(), true);
      for (bool accept : {false, true}) {
        QTimer::singleShot(50, dialog, [&, accept, dialog] {
          auto *edit = dialog->findChild<QDialog *>("favoriteEditor");
          QVERIFY(edit);
          edit->findChild<QLineEdit *>("favoriteTitle")->setText("Edited Beta");
          edit->findChild<QDoubleSpinBox *>("favoritePosition")
              ->setValue(4.125);
          edit->findChild<QCheckBox *>("favoriteRememberPosition")
              ->setChecked(false);
          edit->findChild<QCheckBox *>("favoritePortable")->setChecked(true);
          edit->findChild<QDialogButtonBox *>()
              ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
              ->click();
        });
        dialog->findChild<QPushButton *>("favoriteEdit")->click();
        QCOMPARE(profile.favorites().first().title,
                 accept ? QString("Edited Beta") : QString("Beta"));
      }
      QVERIFY(profile.favorites().first().portable);
      QVERIFY(!profile.favorites().first().rememberPosition);
      QCOMPARE(profile.favorites().first().position, 4.125);
      dialog->findChild<QPushButton *>("favoriteCopy")->click();
      QCOMPARE(QApplication::clipboard()->text(), media("VP9.webm"));
      category->setCurrentIndex(category->findData("devices"));
      list->setCurrentItem(favoriteItem(list, v));
      QTimer::singleShot(50, dialog, [&, dialog] {
        auto *edit = dialog->findChild<QDialog *>("favoriteEditor");
        QVERIFY(edit);
        QVERIFY(!edit->findChild<QCheckBox *>("favoritePortable")->isEnabled());
        QVERIFY(!edit->findChild<QCheckBox *>("favoriteRememberPosition")
                     ->isEnabled());
        edit->reject();
      });
      dialog->findChild<QPushButton *>("favoriteEdit")->click();
      category->setCurrentIndex(0);
      auto *search = dialog->findChild<QLineEdit *>("favoritesSearch");
      search->setText("EDITED");
      QVERIFY(!favoriteItem(list, b)->isHidden());
      QVERIFY(favoriteItem(list, a)->isHidden());
      search->clear();
      const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      if (!screenshots.isEmpty())
        QVERIFY(dialog->grab().save(screenshots + "/favorites.png"));
      list->clearSelection();
      list->setCurrentItem(favoriteItem(list, d));
      dialog->findChild<QPushButton *>("favoriteRemove")->click();
      QCOMPARE(profile.favorites().size(), 3);
      handled = true;
      dialog->reject();
    });
    action(window, "favoriteManage")->trigger();
    QVERIFY(handled);
    // Categories are real source types, with the ordinary file bookmarks kept
    // directly accessible.
    profile.addFavorite("Disc again", DeviceSource::encode(disc), 0);
    auto *menu = window.findChild<QMenu *>("favoritesMenu");
    QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
    QVERIFY(menu->findChild<QMenu *>("favoritesCategory_discs"));
    QVERIFY(menu->findChild<QMenu *>("favoritesCategory_devices"));
    bool fileFound = false;
    for (auto *entry : menu->actions())
      if (entry->data().toString() == b)
        fileFound = true;
    QVERIFY(fileFound);
    window.close();
  }
  void bookmarkPositionChoiceAndAddCancel() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    const auto file = media("Русский H264.mp4");
    const auto favorite = profile.addFavorite("Start", file, 3.25, false);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    connect(player, &Player::loaded, &window,
            [player] { player->set("pause", true); });
    QSignalSpy loaded(player, &Player::loaded);
    auto *menu = window.findChild<QMenu *>("favoritesMenu");
    QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
    for (auto *entry : menu->actions())
      if (entry->data().toString() == favorite) {
        entry->trigger();
        break;
      }
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(player->get("time-pos").toDouble() < .1);
    QVERIFY(window.seekTo(2.125));
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 2.125) < .1);
    for (bool accept : {false, true}) {
      QTimer::singleShot(80, &window, [&, accept] {
        auto *dialog = window.findChild<QDialog *>("favoriteEditor");
        QVERIFY(dialog);
        auto *title = dialog->findChild<QLineEdit *>("favoriteTitle");
        title->clear();
        QVERIFY(!dialog->findChild<QDialogButtonBox *>()
                     ->button(QDialogButtonBox::Ok)
                     ->isEnabled());
        title->setText("New scene");
        dialog->findChild<QCheckBox *>("favoritePortable")->setChecked(true);
        dialog->findChild<QDialogButtonBox *>()
            ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
      });
      action(window, "favoriteAdd")->trigger();
      QCOMPARE(profile.favorites().size(), accept ? 2 : 1);
    }
    QVERIFY(profile.favorites().last().portable);
    QCOMPARE(profile.favorites().last().title, QString("New scene"));
    QVERIFY(std::abs(profile.favorites().last().position - 2.125) < .01);
    window.close();
  }
  void liveFavoriteKeepsDefaults() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("favorites/rememberPosition", true);
    profile.settings.setValue("favorites/portablePaths", true);
    DeviceSource::Request live;
    live.kind = "test-video";
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({DeviceSource::encode(live)});
    QTRY_COMPARE(loaded.size(), 1);
    QTimer::singleShot(80, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("favoriteEditor");
      QVERIFY(dialog);
      auto *remember =
          dialog->findChild<QCheckBox *>("favoriteRememberPosition");
      auto *portable = dialog->findChild<QCheckBox *>("favoritePortable");
      QVERIFY(!remember->isEnabled());
      QVERIFY(!remember->isChecked());
      QVERIFY(!portable->isEnabled());
      QVERIFY(!portable->isChecked());
      dialog->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Ok)
          ->click();
    });
    action(window, "favoriteAdd")->trigger();
    QCOMPARE(profile.favorites().size(), 1);
    QVERIFY(!profile.favorites().first().rememberPosition);
    QVERIFY(!profile.favorites().first().portable);
    QVERIFY(profile.settings.value("favorites/rememberPosition").toBool());
    QVERIFY(profile.settings.value("favorites/portablePaths").toBool());
    window.close();
  }
  void preferencesLimitsAndMenu() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    for (int n = 0; n < 25; ++n)
      profile.recordRecent(temp.filePath(QString::number(n) + ".mp4"));
    Window window(profile);
    window.show();
    for (bool accept : {false, true}) {
      afterPreferencesReady(&window, [&, accept] {
        auto *dialog = window.findChild<QDialog *>("preferencesDialog");
        QVERIFY(dialog);
        dialog->findChild<QSpinBox *>("history/limit")->setValue(10);
        dialog->findChild<QSpinBox *>("history/menuLimit")->setValue(3);
        dialog->findChild<QCheckBox *>("favorites/portablePaths")
            ->setChecked(true);
        dialog->findChild<QCheckBox *>("favorites/rememberPosition")
            ->setChecked(false);
        const auto screenshots =
            qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        if (accept && !screenshots.isEmpty()) {
          auto *tabs = dialog->findChild<QTabWidget *>();
          for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == "История и избранное")
              tabs->setCurrentIndex(i);
          QVERIFY(
              dialog->grab().save(screenshots + "/library-preferences.png"));
        }
        dialog->findChild<QDialogButtonBox *>()
            ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
      });
      action(window, "preferences")->trigger();
      QCOMPARE(profile.historyLimit(), accept ? 10 : 50);
      QCOMPARE(profile.recentFiles().size(), accept ? 10 : 25);
    }
    auto *menu = window.findChild<QMenu *>("historyMenu");
    QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
    int count = 0;
    for (auto *entry : menu->actions())
      if (!entry->data().toString().isEmpty())
        ++count;
    QCOMPARE(count, 3);
    profile.setHistoryLimits(10, 0);
    QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
    count = 0;
    for (auto *entry : menu->actions())
      if (!entry->data().toString().isEmpty())
        ++count;
    QCOMPARE(count, 0);
    QVERIFY(action(window, "historyManage"));
    QVERIFY(profile.settings.value("favorites/portablePaths").toBool());
    QVERIFY(!profile.settings.value("favorites/rememberPosition").toBool());
    window.close();
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  applyTheme(app);
  Q_INIT_RESOURCE(resources);
  LibraryTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "library_test.moc"
