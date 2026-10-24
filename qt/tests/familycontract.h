#pragma once
#include "preferencesdriver.h"
#include "player.h"
#include "profile.h"
#include "window.h"
#include <QScopeGuard>
#include <QtTest>
#include <QtWidgets>

// The same user scenario is run by the native Windows and Linux Qt suites.
inline void verifyFamilyContract() {
  QTemporaryDir fresh, legacy;
  QVERIFY(fresh.isValid() && legacy.isValid());
  {
    Profile profile(fresh.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("video/output", "software");
    profile.settings.setValue("keys/menu", "Ctrl+0");
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(window.findChild<QAction *>("action_classicCaptionMenu")->shortcut().isEmpty());
    auto *player = window.findChild<Player *>(); QVERIFY(player);
    QTRY_COMPARE(player->get("volume").toInt(), 75);
    auto *volumeAction = window.findChild<QAction *>("action_volumeUp");
    QVERIFY(volumeAction);
    QSignalSpy volumeTriggered(volumeAction, &QAction::triggered);
    volumeAction->trigger();
    QCOMPARE(volumeTriggered.size(), 1);
    bool visited = false;
    afterPreferencesReady(&window, [&] {
      auto *dialog = window.findChild<QDialog *>("preferencesDialog"); QVERIFY(dialog);
      const auto close = qScopeGuard([&] { dialog->reject(); });
      auto *tree = dialog->findChild<QTreeWidget *>("classicPreferencesTree"); QVERIFY(tree);
      auto *pages = dialog->findChild<QTabWidget *>("classicPreferencesPages"); QVERIFY(pages);
      QVERIFY(tree->isVisible()); QVERIFY(pages->isVisible());
      QCOMPARE(tree->topLevelItemCount(), 10); QCOMPARE(pages->count(), 40);
      QVERIFY(pages->tabBar()->isVisible());
      auto *search = dialog->findChild<QLineEdit *>("settingsSearch"); QVERIFY(search);
      QVERIFY(search->isVisible());
      auto *osd = dialog->findChild<QCheckBox *>("ui/osd"); QVERIFY(osd);
      QVERIFY(!osd->isChecked()); osd->setChecked(true);
      visited = true;
    });
    window.findChild<QAction *>("action_preferences")->trigger();
    QVERIFY(visited);
    QVERIFY(!profile.settings.value("ui/osd").toBool()); // Cancel keeps defaults.
    window.close();
  }
  {
    QSettings old(legacy.filePath("FoxicMPQt.ini"), QSettings::IniFormat);
    old.setValue("audio/volume", 41);
    old.setValue("ui/osd", true);
    old.setValue("ui/rememberGeometry", false);
    old.setValue("keys/volumeUp", "Ctrl+Alt+F8");
    old.setValue("keys/quit", ""); // Explicitly disabled is also a user choice.
  }
  Profile profile(legacy.path());
  QCOMPARE(profile.settings.value("ui/osd").toBool(), true);
  QCOMPARE(profile.settings.value("ui/rememberGeometry").toBool(), false);
  QCOMPARE(profile.settings.value("keys/quit").toString(), QString());
  profile.settings.setValue("integration/prompted", true);
  profile.settings.setValue("input/mpris", false);
  profile.settings.setValue("audio/mute", true);
  profile.settings.setValue("video/output", "software");
  Window window(profile); window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window)); window.activateWindow();
  auto *player = window.findChild<Player *>(); QVERIFY(player);
  QTRY_COMPARE(player->get("volume").toInt(), 41);
  QVERIFY(window.findChild<QAction *>("action_quit")->shortcut().isEmpty());
  auto *customVolume = window.findChild<QAction *>("action_volumeUp");
  QVERIFY(customVolume);
  QSignalSpy customTriggered(customVolume, &QAction::triggered);
  customVolume->trigger();
  QCOMPARE(customTriggered.size(), 1);
  QTRY_COMPARE(player->get("volume").toInt(), 46);
  QCOMPARE(customVolume->shortcut(), QKeySequence("Ctrl+Alt+F8"));
  QCOMPARE(profile.settings.value("keys/volumeUp").toString(), QString("Ctrl+Alt+F8"));
  window.close();
}
