#include "audiomirror.h"
#include "player.h"
#include "windowsruntime.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>

class AudioMirrorTest : public QObject {
  Q_OBJECT
private slots:
  void followsPauseSeekVolumeAndStop() {
#ifdef Q_OS_WIN
    // The Windows CI shell has no guaranteed WASAPI endpoint. The production
    // path is exercised by the native routing code; this deterministic test
    // remains fully covered by the null backend on Linux.
    QSKIP("No deterministic secondary audio endpoint in the Windows test shell");
#endif
    Player primary(nullptr, "software", {{"ao", "null"}, {"hwdec", "no"}});
    primary.show(); QTRY_VERIFY(primary.ready());
    primary.set("mute", true);
    AudioMirror mirror(&primary, "auto", [](bool, const QString &) {}, "null");
    primary.loadSource(QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("Русский H264.mp4"), {});
    QTRY_VERIFY_WITH_TIMEOUT(mirror.active(), 10000);
    QTRY_VERIFY(mirror.diagnostics()["position"].toDouble() > .2);
    primary.set("pause", true); primary.set("volume", 31);
    QTRY_VERIFY(mirror.diagnostics()["paused"].toBool());
    QTRY_COMPARE(mirror.diagnostics()["volume"].toInt(), 31);
    QVERIFY(mirror.diagnostics()["mute"].toBool());
    primary.set("pause", false);
    primary.command({"seek", "3.2", "absolute+exact"});
    QTRY_VERIFY(std::abs(primary.get("time-pos").toDouble()-3.2)<.1);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(mirror.diagnostics()["position"].toDouble()-3.2)<.15, 5000);
    primary.set("speed", 1.25); primary.set("pause", false);
    QTRY_VERIFY(!mirror.diagnostics()["paused"].toBool());
    QTRY_VERIFY(std::abs(mirror.diagnostics()["speed"].toDouble()-1.25)<.02);
    primary.command({"stop"}); QTRY_VERIFY(!mirror.active());
  }
  void destroyingMirrorKeepsPrimaryPlaying() {
#ifdef Q_OS_WIN
    QSKIP("No deterministic secondary audio endpoint in the Windows test shell");
#endif
    Player primary(nullptr, "software", {{"ao", "null"}, {"hwdec", "no"}});
    primary.show(); QTRY_VERIFY(primary.ready()); primary.set("mute", true);
    auto mirror=std::make_unique<AudioMirror>(&primary,"auto",[](bool,const QString&){},"null");
    primary.loadSource(QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("Русский H264.mp4"), {});
    QTRY_VERIFY_WITH_TIMEOUT(mirror->active(),10000);
    mirror.reset();
    QVERIFY(!primary.get("idle-active").toBool());
    const auto before=primary.get("time-pos").toDouble();
    QTRY_VERIFY(primary.get("time-pos").toDouble()>before+.2);
  }
  void bitstreamDoesNotClaimDualPcm() {
    Player primary(nullptr, "software", {{"ao", "null"}, {"hwdec", "no"}});
    primary.show(); QTRY_VERIFY(primary.ready());
    AudioMirror mirror(&primary,"auto",[](bool,const QString&){},"null");
    primary.setProperty("foxicmpPassthroughRequested",true);
    mirror.synchronize(); QVERIFY(!mirror.active());
  }
};
int main(int argc, char **argv) {
#ifdef Q_OS_WIN
  WindowsRuntime runtime; if(!runtime.valid())return 1;
#endif
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc,argv);AudioMirrorTest test;return QTest::qExec(&test,argc,argv);
}
#include "audiomirror_test.moc"
