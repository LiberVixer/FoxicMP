#include "preferencesdriver.h"
#include "associationoptions.h"
#include "instancebroker.h"
#include "mediaoptions.h"
#include "platformpaths.h"
#include "player.h"
#include "profile.h"
#include "profilearchive.h"
#include "profilelaunch.h"
#include "subtitlerender.h"
#include "window.h"
#include "windowsruntime.h"
#include <QtTest>
#include <QtWidgets>
#include <QScopeGuard>
#include <QSslSocket>
#include <cmath>
#include <windows.h>
#include <dwmapi.h>

class WindowsPortTest : public QObject {
  Q_OBJECT
  void prepare(Profile &profile) {
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("playback/hardwareDecoding", false);
    profile.settings.setValue("video/output", "software");
  }
private slots:
  void initTestCase() { Q_INIT_RESOURCE(resources); qApp->setStyle("Fusion"); }
  void commandLineMetadataAndTls() {
    QVERIFY(QSslSocket::supportsSsl());
    QProcess child;
    const auto exe = QDir(QCoreApplication::applicationDirPath()).filePath("FoxicMPq.exe");
    child.start(exe, {"--version"}); QVERIFY(child.waitForFinished(10000));
    QCOMPARE(child.exitCode(), 0); QVERIFY(child.readAllStandardOutput().startsWith("FoxicMPq 0."));
    child.start(exe, {"--help"}); QVERIFY(child.waitForFinished(10000));
    QCOMPARE(child.exitCode(), 0); QVERIFY(child.readAllStandardOutput().contains("--profile"));
    QTemporaryDir dir; const auto absent = dir.filePath("not-created");
    child.start(exe, {"--profile", absent, "--progressive-api-info"});
    QVERIFY(child.waitForFinished(10000)); QCOMPARE(child.exitCode(), 0);
    const auto json = QJsonDocument::fromJson(child.readAllStandardOutput()).object();
    QCOMPARE(json.value("supported"), QJsonValue(false)); QVERIFY(!QDir(absent).exists());
  }
  void portableIsolationAndLeases() {
    QTemporaryDir first, second;
    QVERIFY(first.isValid() && second.isValid());
    {
      Profile a(first.path()), b(second.path());
      QVERIFY(a.lease.valid() && b.lease.valid());
      a.settings.setValue("audio/volume", 37); a.settings.sync();
      QCOMPARE(b.settings.value("audio/volume").toInt(), 75);
      QVERIFY(InstanceBroker::endpointFor(first.path()) != InstanceBroker::endpointFor(second.path()));
      QCOMPARE(InstanceBroker::endpointFor(first.path()), InstanceBroker::endpointFor(first.path().toUpper()));
      {
        ProfileLease other(first.path()); QVERIFY(other.valid());
        QVERIFY(!a.lease.exclusive()); a.lease.shared();
      }
      QVERIFY(a.lease.exclusive()); a.lease.shared();
    }
    Profile reopened(first.path()); QCOMPARE(reopened.settings.value("audio/volume").toInt(), 37);
  }
  void capturedDefaultsOnlyForNewProfiles() {
    QTemporaryDir fresh, legacy;
    Profile p(fresh.path());
    QCOMPARE(p.settings.value("appearance/theme").toString(), QString("dark"));
    QCOMPARE(p.settings.value("appearance/language").toString(), QString("ru"));
    QCOMPARE(p.settings.value("appearance/themeBrightness").toInt(), 15);
    QCOMPARE(p.settings.value("mouse/windowDrag").toString(), QString("left"));
    QCOMPARE(p.settings.value("ui/showMenu").toBool(), false);
    QCOMPARE(p.settings.value("ui/rememberGeometry").toBool(), true);
    QCOMPARE(p.settings.value("ui/keepVideoAspect").toBool(), false);
    QCOMPARE(p.settings.value("video/output").toString(), QString("software"));
    QCOMPARE(p.settings.value("keys/open").toString(), QString("Ctrl+Q"));
    QCOMPARE(p.settings.value("subtitles/font").toString(), QString("Tahoma"));
    QVERIFY(!p.settings.value("integration/checkOnStartup").toBool());
    for (const auto &key : p.settings.allKeys()) {
      QVERIFY(!key.startsWith("files/") && !key.startsWith("positions/"));
      QVERIFY(key != "history/files" && key != "playlist/tabsV1" &&
              key != "window/geometry" && key != "integration/prompted" &&
              key != "integration/enabled");
    }
    p.settings.setValue("appearance/theme", "light"); p.settings.sync();
    Profile reopened(fresh.path());
    QCOMPARE(reopened.settings.value("appearance/theme").toString(), QString("light"));
    {
      QSettings old(legacy.filePath("FoxicMPQt.ini"), QSettings::IniFormat);
      old.setValue("audio/volume", 28); old.setValue("ui/showMenu", true);
    }
    Profile old(legacy.path());
    QCOMPARE(old.settings.value("audio/volume").toInt(), 28);
    QCOMPARE(old.settings.value("ui/showMenu").toBool(), true);
    QVERIFY(!old.settings.contains("appearance/theme"));
  }
  void nativeBorderAndVideoDrag() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    Window window(profile); window.move(200, 150); window.show();
    window.raise(); window.activateWindow();
    auto *player = window.findChild<Player *>(); QVERIFY(player);
    QTRY_VERIFY_WITH_TIMEOUT(player->ready(), 15000);
    const COLORREF noBorder = 0xfffffffe;
    const auto hwnd = reinterpret_cast<HWND>(window.winId());
    // BORDER_COLOR is set-only: DwmGetWindowAttribute returns E_INVALIDARG.
    // Check support for suppressing the outline while retaining native resizing.
    const auto result = DwmSetWindowAttribute(hwnd,
        static_cast<DWMWINDOWATTRIBUTE>(34), &noBorder, sizeof(noBorder));
    if (QSysInfo::kernelVersion().section('.', 2, 2).toInt() >= 22000)
      QCOMPARE(result, HRESULT(S_OK));
    QVERIFY(GetWindowLongPtr(hwnd, GWL_STYLE) & WS_THICKFRAME);
    QVERIFY(GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CAPTION);
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv")});
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
    QTRY_VERIFY(player->get("time-pos").toDouble() > .1);
    const auto oldPosition = window.QWidget::pos();
    const QPoint local(100, 100), global = player->mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(player, &press);
    QMouseEvent move(QEvent::MouseMove, local + QPoint(55, 25), global + QPoint(55, 25),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(player, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global + QPoint(55, 25),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(player, &release);
    QTRY_COMPARE(window.QWidget::pos(), oldPosition + QPoint(55, 25));
    QTest::qWait(QApplication::doubleClickInterval() + 80);
    QVERIFY(!player->get("pause").toBool());
    QCOMPARE(profile.settings.value("mouse/window/left").toString(), QString("play"));
    QSignalSpy clicks(window.findChild<QAction *>("action_play"), &QAction::triggered);
    QTest::mouseClick(player, Qt::LeftButton, Qt::NoModifier, local);
    QTRY_COMPARE(clicks.size(), 1);
    QTRY_VERIFY(player->get("pause").toBool());
    QTest::mouseDClick(player, Qt::LeftButton);
    QTRY_VERIFY(window.isFullScreen());
    window.findChild<QAction *>("action_fullscreen")->trigger();
    QTRY_VERIFY(!window.isFullScreen());
    const auto report = qEnvironmentVariable("FOXICMP_WINDOWS_REPORT");
    if (!report.isEmpty()) {
      // Let the video surface repaint at window size after leaving fullscreen.
      QTest::qWait(250);
      RECT rect; QVERIFY(GetWindowRect(hwnd, &rect));
      QImage capture(rect.right - rect.left, rect.bottom - rect.top, QImage::Format_ARGB32);
      capture.fill(Qt::transparent);
      HDC dc = GetDC(hwnd), memory = CreateCompatibleDC(dc);
      HBITMAP bitmap = CreateCompatibleBitmap(dc, capture.width(), capture.height());
      auto previous = SelectObject(memory, bitmap);
      QVERIFY(PrintWindow(hwnd, memory, 2));
      BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = capture.width(); info.bmiHeader.biHeight = -capture.height();
      info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
      SelectObject(memory, previous);
      const auto rows = GetDIBits(memory, bitmap, 0, capture.height(), capture.bits(), &info, DIB_RGB_COLORS);
      DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(hwnd, dc);
      QCOMPARE(rows, capture.height());
      capture = capture.convertToFormat(QImage::Format_RGB32);
      QVERIFY(capture.save(QDir(report).filePath("native-window.png")));
    }
  }
  void archiveAndShortcutNoOverwrite() {
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.settings.setValue("audio/volume", 41);
    QString error;
    const auto archive = dir.filePath("test.foxicmp-profile");
    QVERIFY2(ProfileArchive::save(profile, archive, &error), qPrintable(error));
    ProfileArchive::Bundle bundle;
    QVERIFY2(ProfileArchive::read(archive, &bundle, &error), qPrintable(error));
    const auto restored = dir.filePath("Профиль с пробелами");
    QVERIFY2(ProfileArchive::restore(bundle, restored, &error), qPrintable(error));
    QVERIFY(!ProfileArchive::restore(bundle, restored, &error));
    Profile copy(restored); QCOMPARE(copy.settings.value("audio/volume").toInt(), 41);
    const auto shortcut = dir.filePath("Ярлык FoxicMP Qt.lnk");
    QVERIFY2(ProfileLaunch::launcher(QCoreApplication::applicationFilePath(), restored, shortcut, &error), qPrintable(error));
    QFile f(shortcut); QVERIFY(f.open(QIODevice::ReadOnly)); const auto original = f.readAll(); f.close();
    QVERIFY(original.size() > 76);
    QVERIFY(!ProfileLaunch::launcher(QCoreApplication::applicationFilePath(), restored, shortcut, &error));
    QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), original);
  }
  void fileOperationsNeverReplace() {
    QTemporaryDir dir;
    QFile a(dir.filePath("a")), b(dir.filePath("b"));
    QVERIFY(a.open(QIODevice::WriteOnly)); a.write("a"); a.close();
    QVERIFY(b.open(QIODevice::WriteOnly)); b.write("b"); b.close();
    QVERIFY(!PlatformPaths::moveNoReplace(a.fileName(), b.fileName()));
    QVERIFY(a.exists() && b.exists());
    QVERIFY(PlatformPaths::moveNoReplace(a.fileName(), dir.filePath("c")));
  }
  void playback_data() {
    QTest::addColumn<QString>("file");
    for (const auto &file : {"Русский H264.mp4", "chapters.mkv", "VP9.webm", "AV1.mkv", "HEVC.mov", "MPEG4.avi"})
      QTest::newRow(file) << QString::fromUtf8(file);
  }
  void playback() {
    QFETCH(QString, file);
    const auto path = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(file);
    QVERIFY(QFileInfo(path).isFile());
    Player p(nullptr, qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    p.resize(640, 360); p.show();
    QTRY_VERIFY_WITH_TIMEOUT(p.ready(), 15000);
    QVERIFY(p.set("hwdec", "no")); QVERIFY(p.set("mute", true));
    QSignalSpy loaded(&p, &Player::loaded), failed(&p, &Player::fileFailed);
    QVERIFY(p.loadSource(path, {{"pause", "no"}}));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > .2, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!p.grabFramebuffer().isNull(), 10000);
    QVERIFY(!p.get("video-codec").toString().isEmpty());
    QVERIFY(p.set("pause", true)); QTest::qWait(200);
    const auto before = p.get("time-pos").toDouble(); QTest::qWait(250);
    QVERIFY(std::abs(p.get("time-pos").toDouble() - before) < .1);
    QVERIFY(p.command({"seek", "1", "absolute+exact"}));
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(p.get("time-pos").toDouble() - 1) < .2, 10000);
    QVERIFY(p.set("pause", false));
    QTRY_VERIFY_WITH_TIMEOUT(p.get("time-pos").toDouble() > 1.2, 10000);
    QCOMPARE(failed.size(), 0);
    if (!qEnvironmentVariableIsEmpty("FOXICMP_WINDOWS_REPORT"))
      QVERIFY(p.grabFramebuffer().save(QDir(qEnvironmentVariable("FOXICMP_WINDOWS_REPORT")).filePath(file + ".png")));
  }
  void subtitleCompatibility() {
    QTemporaryDir dir; Profile profile(dir.path());
    Player p; p.show(); QTRY_VERIFY_WITH_TIMEOUT(p.ready(), 15000);
    QSignalSpy errors(&p, &Player::error);
    QVERIFY(SubtitleRender::apply(&p, profile.settings));
    QCOMPARE(errors.size(), 0);
    QCOMPARE(p.get("sub-ass-use-video-data").toString(), QString("all"));
    profile.settings.setValue("subtitles/render/blurCompat", false);
    QVERIFY(SubtitleRender::apply(&p, profile.settings));
    QCOMPARE(p.get("sub-ass-use-video-data").toString(), QString("aspect-ratio"));
    profile.settings.setValue("subtitles/render/aspectCompat", false);
    QVERIFY(SubtitleRender::apply(&p, profile.settings));
    QCOMPARE(p.get("sub-ass-use-video-data").toString(), QString("none"));
    QCOMPARE(errors.size(), 0);
  }
  void persistedSubtitleVisibility() {
    Player p; p.show(); QTRY_VERIFY_WITH_TIMEOUT(p.ready(), 15000);
    for (bool visible : {true, false}) {
      QTemporaryDir dir;
      QFile file(dir.filePath("FoxicMPQt.ini")); QVERIFY(file.open(QIODevice::WriteOnly));
      file.write(visible ? "[subtitles]\nvisible=true\n" : "[subtitles]\nvisible=false\n"); file.close();
      Profile profile(dir.path()); prepare(profile);
      QSignalSpy errors(&p, &Player::error);
      MediaOptions::apply(&p, profile.settings);
      QCOMPARE(p.get("sub-visibility").toBool(), visible);
      QCOMPARE(errors.size(), 0);
    }
  }
  void settingsAndNativeAssociationsPage() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    Window window(profile); window.show();
    auto *player = window.findChild<Player *>(); QVERIFY(player);
    QTRY_VERIFY_WITH_TIMEOUT(player->ready(), 15000);
    bool checked = false;
    afterPreferencesReady(&window, [&] {
      auto *dialog = window.findChild<QDialog *>("preferencesDialog");
      const auto close = qScopeGuard([&] { dialog->reject(); });
      QVERIFY(dialog->findChild<QTreeWidget *>());
      QVERIFY(dialog->findChild<QTabWidget *>());
      QVERIFY(dialog->findChild<QPushButton *>("windowsDefaultApps"));
      QVERIFY(!dialog->findChild<QPushButton *>("assignAssociations"));
      const auto report = qEnvironmentVariable("FOXICMP_WINDOWS_REPORT");
      if (!report.isEmpty()) QVERIFY(dialog->grab().save(QDir(report).filePath("settings.png")));
      checked = true;
    });
    auto *action = window.findChild<QAction *>("action_preferences"); QVERIFY(action); action->trigger();
    QVERIFY(checked);
    const auto report = qEnvironmentVariable("FOXICMP_WINDOWS_REPORT");
    if (!report.isEmpty()) QVERIFY(window.grab().save(QDir(report).filePath("interface.png")));
  }
  void commandLineUnicodeAndStdin_data() {
    QTest::addColumn<bool>("standardInput");
    QTest::newRow("Unicode file argument") << false;
    QTest::newRow("Binary stdin pipe") << true;
  }
  void commandLineUnicodeAndStdin() {
    QFETCH(bool, standardInput);
    QTemporaryDir dir;
    { Profile profile(dir.path()); prepare(profile); profile.settings.sync(); }
    const auto media = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(standardInput ? "chapters.mkv" : "Русский H264.mp4");
    QStringList arguments{"--profile", dir.path(), "--volume", "0", "--after", "exit"};
    if (standardInput) arguments << "--stdin";
    else arguments << "--" << media;
    QProcess child;
    const auto cleanup = qScopeGuard([&] { if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(); } });
    child.start(QDir(QCoreApplication::applicationDirPath()).filePath("FoxicMPq.exe"), arguments);
    QVERIFY(child.waitForStarted());
    if (standardInput) {
      QFile file(media); QVERIFY(file.open(QIODevice::ReadOnly));
      const auto bytes = file.readAll(); QCOMPARE(child.write(bytes), bytes.size()); child.closeWriteChannel();
    }
    QTRY_VERIFY_WITH_TIMEOUT(child.state() == QProcess::NotRunning, 25000);
    QCOMPARE(child.exitStatus(), QProcess::NormalExit); QCOMPARE(child.exitCode(), 0);
    Profile result(dir.path());
    if (!standardInput) QVERIFY(result.recentFiles().contains(QDir::fromNativeSeparators(media)));
    else QVERIFY(result.recentFiles().isEmpty());
  }
  void namedPipeCommandForwarding() {
    QTemporaryDir dir; Profile profile(dir.path()); prepare(profile);
    InstanceBroker broker(dir.path());
    LaunchRequest initial; QCOMPARE(broker.start(initial, true), InstanceBroker::Primary);
    QList<LaunchRequest> received; broker.setReceiver([&](const LaunchRequest &r) { received.append(r); });
    QTRY_COMPARE(received.size(), 1);
    QProcess child;
    child.start(QDir(QCoreApplication::applicationDirPath()).filePath("FoxicMPq.exe"),
        {"--profile", dir.path(), "--command", "pause", "--no-activate"});
    QVERIFY(child.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(child.state() == QProcess::NotRunning, 10000);
    QCOMPARE(child.exitStatus(), QProcess::NormalExit); QCOMPARE(child.exitCode(), 0);
    QTRY_COMPARE(received.size(), 2); QCOMPARE(received.last().options.value("command").toString(), QString("pause"));
  }
};
int main(int argc, char **argv) {
  WindowsRuntime runtime;
  if (!runtime.valid()) return 1;
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  WindowsPortTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "windows_port_test.moc"
