#pragma once
#include "player.h"
#include "profile.h"
#include "timeline.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>

inline void verifyToolbarContract() {
  QTemporaryDir directory;
  Profile profile(directory.path());
  profile.settings.setValue("integration/prompted", true);
  profile.settings.setValue("input/mpris", false);
  profile.settings.setValue("audio/mute", true);
  profile.settings.setValue("video/output", "software");
  profile.settings.setValue("playback/hardwareDecoding", false);
  Window window(profile); window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto *player = window.findChild<Player *>();
  auto *timeline = window.findChild<Timeline *>();
  const auto button = [&](const char *name) {return window.findChild<QToolButton *>(QString("control_") + name);};
#ifdef Q_OS_WIN
  QCOMPARE(button("frameNext")->toolTip(), QString::fromUtf8("Шаг / Перейти"));
#endif
  QObject::connect(player, &Player::loaded, &window, [player] {player->set("pause", true);});
  QSignalSpy loaded(player, &Player::loaded);
  const QDir media(qEnvironmentVariable("FOXICMP_TEST_MEDIA"));
  window.openFiles({media.filePath("chapters.mkv"), media.filePath("Русский H264.mp4")});
  QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
  QTRY_COMPARE(timeline->chapters.size(), 2);
  QVERIFY(window.seekTo(4.5));
  QTRY_VERIFY(std::abs(player->get("time-pos").toDouble()-4.5)<.1);
  QTest::mouseClick(button("previous"), Qt::LeftButton);
  QTRY_VERIFY(player->get("time-pos").toDouble()<.1); // Classic's three-second threshold.
  QTest::mouseClick(button("next"), Qt::LeftButton);
  QTRY_VERIFY(std::abs(player->get("time-pos").toDouble()-4)<.1);
  QTest::mouseClick(button("next"), Qt::LeftButton);
  QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 15000); // Last chapter -> next file.
  QTest::mouseClick(button("previous"), Qt::RightButton);
  QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 3, 15000); // Right click always skips files.
  QTRY_COMPARE(timeline->chapters.size(), 2);
  for (const auto &entry : QList<QPair<const char *, const char *>>{
       {"frameNext", "chaptersMenu"}, {"open", "historyMenu"},
       {"audioQuick", "classicAudioOptionsMenu"}, {"subtitleQuick", "classicSubtitleOptionsMenu"}}) {
    auto *menu = window.findChild<QMenu *>(entry.second); QVERIFY(menu);
#ifdef Q_OS_WIN
    if (qstrcmp(entry.first, "frameNext") == 0)
      QCOMPARE(menu->title(), QString::fromUtf8("Перейти"));
#endif
    QTest::mouseClick(button(entry.first), Qt::RightButton);
    QTRY_VERIFY(menu->isVisible()); QVERIFY(!menu->actions().isEmpty()); menu->hide();
    QVERIFY(!window.findChild<QDialog *>("preferencesDialog"));
  }
  auto *queue = window.findChild<QListWidget *>(); QVERIFY(queue);
  const auto count = queue->count();
  QTest::mouseClick(button("stop"), Qt::RightButton);
  QTRY_VERIFY(player->get("idle-active").toBool());
  QCOMPARE(queue->count(), count); // Classic closes media without clearing its queue.
}
