// Production Window controls over a pinned growing M4A, private profile.
// GPL-3.0-or-later.
#include "window.h"
#include "player.h"
#include "profile.h"
#include "progressivesource.h"
#include "mpvlifetime.h"
#include "windowsruntime.h"
#include <QtWidgets>
#include <iostream>
#include <stdexcept>
static void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static QJsonObject model(const QString& path){QFile f(path);check(f.open(QIODevice::ReadOnly),"snapshot input");return QJsonDocument::fromJson(f.readAll()).object();}
class ProgressiveApiTest {
public:
 static void run(const QStringList& args){
  Profile profile(args[4]);profile.settings.setValue("integration/prompted",true);profile.settings.setValue("input/mpris",false);
  profile.settings.setValue("audio/mute",true);profile.settings.setValue("video/output","software");profile.settings.setValue("playback/hardwareDecoding",false);
  Window w(profile);w.show();auto* player=w.findChild<Player*>();check(player,"Player missing");
  bool natural=false;QObject::connect(player,&Player::ended,&w,[&](bool eof){natural|=eof;});
  auto source=ProgressiveSource::openPublished(model(args[1]),args[4]+".readers");check(w.openProgressiveSource(source),"M4A open");
  const auto status=[&]{auto s=w.progressivePlaybackStatus();check(s["state"]!="error",qPrintable(s["error"].toString()));return s;};
  const auto wait=[&](auto ready,const char* why,int timeout=15000){QElapsedTimer timer;timer.start();while(!ready()){source->publishedHeartbeat();if(timer.elapsed()>=timeout){std::cerr<<QJsonDocument(status()).toJson(QJsonDocument::Compact).constData()<<" pending="<<w.progressivePendingSeek<<std::endl;check(false,why);}QCoreApplication::processEvents();QThread::msleep(5);}};
  wait([&]{const auto s=status();return s["state"]=="playing" && s["positionSeconds"].toDouble()>.25;},"growing playback");
  w.actions["play"]->trigger();wait([&]{return status()["state"]=="paused";},"pause");
  check(!w.seekTo(15),"unconfirmed seek accepted");check(status()["state"]=="paused","rejected seek lost pause");
  check(w.seekTo(6) && w.seekTo(3),"growing seek rejected");
  wait([&]{const auto s=status();return s["state"]=="paused" &&
      ((s["seeking"].toBool() && s["seekTargetSeconds"].toDouble()==3) || std::abs(s["positionSeconds"].toDouble()-3)<.12);},"paused seek target");
  // Some audio outputs retain the pre-seek clock while paused. Confirm the
  // actual decoder position after resuming, as well as the retained UI target.
  w.actions["play"]->trigger();
  wait([&]{const auto s=status();return s["state"]=="playing" && !s["seeking"].toBool() && s["positionSeconds"].toDouble()>3.1 && s["positionSeconds"].toDouble()<3.75;},"superseded growing decoder seek",1500);
  w.actions["play"]->trigger();wait([&]{return status()["state"]=="paused";},"pause after growing seek");
  check(player->pcmWindowActive(),"seek lost PCM trim");QString error;check(source->publish(model(args[2]),&error),qPrintable(error));
  QElapsedTimer hold;hold.start();while(hold.elapsed()<300){QCoreApplication::processEvents();QThread::msleep(5);}
  check(status()["state"]=="paused" && !source->status().complete,"publication lost pause");
  check(source->publish(model(args[3]),&error),qPrintable(error));check(w.seekTo(13.5),"completed seek rejected");
  wait([&]{const auto s=status();return s["state"]=="paused" && ((s["seeking"].toBool() && s["seekTargetSeconds"].toDouble()==13.5) || s["positionSeconds"].toDouble()>13.4);},"completed seek");
  check(player->pcmWindowActive(),"completed seek lost PCM trim");natural=false;w.actions["play"]->trigger();
  wait([&]{return natural && source->status().complete;},"natural M4A EOF");w.closeCurrent();
 }
};
int main(int argc,char** argv)try{
#ifdef Q_OS_WIN
 WindowsRuntime runtime;check(runtime.valid(),"Windows runtime");
#endif
 QApplication app(argc,argv);check(app.arguments().size()==5,"initial full complete profile");ProgressiveApiTest::run(app.arguments());
 check(EngineRetirement::waitEmpty(std::chrono::seconds(5)),"decoder retirement");
 std::cout<<"{\"result\":\"PASS\",\"pause\":true,\"growingSeek\":true,\"supersededSeek\":true,\"completeSeek\":true,\"trimPreserved\":true,\"unconfirmedSeekRejected\":true}"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
