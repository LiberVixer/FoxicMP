#include "preferencesdriver.h"
#include "player.h"
#include "profile.h"
#include "theme.h"
#include "timeline.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>

class ToolsTest : public QObject {
  Q_OBJECT
private slots:
  void chapterVisibilityAndTooltipPreferences() {
    Timeline timeline;
    timeline.resize(401, 16);
    timeline.move(100, 300);
    timeline.duration = 100; timeline.position = 10;
    timeline.chapters = {QVariantMap{{"time", 50.}, {"title", "Middle"}}};
    timeline.previewEnabled = false;
    timeline.show(); QVERIFY(QTest::qWaitForWindowExposed(&timeline));
    QCOMPARE(timeline.grab().toImage().pixelColor(200, 5), QColor(94, 99, 104));
    timeline.chapterMarkers = false;
    QCOMPARE(timeline.grab().toImage().pixelColor(200, 5), QColor(31, 35, 38));
    QTest::mouseMove(&timeline, QPoint(200, 8));
    QTRY_VERIFY(QToolTip::isVisible()); QVERIFY(QToolTip::text().contains("Middle"));
    const auto tooltipGeometry = [] {
      for (auto *widget : QApplication::topLevelWidgets())
        if (widget->windowType() == Qt::ToolTip && widget->isVisible()) return widget->frameGeometry();
      return QRect();
    };
    QTRY_VERIFY(!tooltipGeometry().isEmpty());
    QVERIFY(tooltipGeometry().bottom() < timeline.mapToGlobal(QPoint()).y());
    timeline.timeTooltipEnabled = false;
    QTest::mouseMove(&timeline, QPoint(210, 8));
    QTRY_VERIFY(!QToolTip::isVisible());
    timeline.timeTooltipEnabled = true; timeline.tooltipBelow = true;
    QTest::mouseMove(&timeline, QPoint(215, 8));
    QTRY_VERIFY(QToolTip::isVisible());
    QVERIFY(tooltipGeometry().top() > timeline.mapToGlobal(QPoint(0, timeline.height())).y());
    QToolTip::hideText();
    QSignalSpy hover(&timeline, &Timeline::hovered);
    timeline.previewEnabled = true;
    QTest::mouseMove(&timeline, QPoint(220, 8));
    QTRY_VERIFY(!hover.isEmpty());
  }
  void timelineAndDialogs() {
    QTemporaryDir dir;
    Profile profile(dir.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    Window w(profile);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    auto *player = w.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    w.openFiles({QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                     .filePath("chapters.mkv")});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    player->command({"seek", "1", "absolute+exact"});
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 1) < .1);
    auto *timeline = w.findChild<Timeline *>();
    auto *popup = w.findChild<QFrame *>("framePreview");
    // Exercise the actual hover wiring near the right edge of the screen.
    w.move(1280 - w.width(), 0);
    QTest::qWait(100);
    QTest::mouseMove(timeline, QPoint(timeline->width() * 3 / 4, 8));
    QTRY_VERIFY(popup->isVisible());
    auto hasPixmap = [&] {
      for (auto *label : popup->findChildren<QLabel *>())
        if (!label->pixmap().isNull())
          return true;
      return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(hasPixmap(), 6000);
    QVERIFY(player->get("pause").toBool());
    QVERIFY(std::abs(player->get("time-pos").toDouble() - 1) < .1);
    QVERIFY(popup->mapToGlobal(QPoint(popup->width() - 1, 0)).x() <=
            popup->screen()->availableGeometry().right());
    QTest::qWait(200); // Allow the tooltip expose/paint after the worker reply.
    QVERIFY(popup->isVisible());
    const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty()) {
      QDir().mkpath(screenshots);
      popup->grab().save(screenshots + "/preview-popup.png");
      w.screen()->grabWindow(0).save(screenshots + "/preview.png");
    }
    QTest::mouseMove(player, QPoint(500, 100));
    QTRY_VERIFY(!popup->isVisible());
    const QRect geometry = w.geometry();
    auto *frameAction = w.findChild<QAction *>("action_frameless");
    frameAction->trigger();
    QTest::qWait(100);
    QCOMPARE(w.geometry(), geometry);
    frameAction->trigger();
    QTest::qWait(100);
    QCOMPARE(w.geometry(), geometry);
    bool canceled = false;
    afterPreferencesReady(&w, [&] {
      auto *dialog = w.findChild<QDialog *>("preferencesDialog");
      if (!dialog)
        return;
      auto *tabs = dialog->findChild<QTabWidget *>();
      tabs->setCurrentIndex(3);
      auto *size = dialog->findChild<QSpinBox *>("subtitles/size");
      size->setValue(70);
      if (!screenshots.isEmpty())
        dialog->grab().save(screenshots + "/subtitle-settings.png");
      canceled = true;
      dialog->reject();
    });
    w.findChild<QAction *>("action_preferences")->trigger();
    QVERIFY(canceled);
    QVERIFY(!profile.settings.contains("subtitles/size"));
    QCOMPARE(player->get("sub-font-size").toDouble(), 38.0);
    profile.settings.setValue("ui/previews", false);
    // Apply through the actual preferences acceptance path.
    afterPreferencesReady(&w, [&] {
      auto *dialog = w.findChild<QDialog *>("preferencesDialog");
      dialog->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Ok)
          ->click();
    });
    w.findChild<QAction *>("action_preferences")->trigger();
    QTest::mouseMove(timeline, QPoint(timeline->width() / 2, 8));
    QTest::qWait(400);
    QVERIFY(!popup->isVisible());
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  Q_INIT_RESOURCE(resources);
  applyTheme(app);
  ToolsTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "tools_test.moc"
