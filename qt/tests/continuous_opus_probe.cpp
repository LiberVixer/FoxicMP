// Real Player callback views -> Opus decoder -> private file AO. No audio device.
#include "player.h"
#include "windowsruntime.h"
#include "../../shared/progressivecachedremux.h"
#include <QtWidgets>
#include <fstream>
#include <iostream>
namespace p=foxicmp::progressive;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv)try{
#ifdef Q_OS_WIN
 WindowsRuntime runtime;check(runtime.valid(),"Windows runtime");
#endif
 QApplication app(argc,argv);auto a=app.arguments();check(a.size()==8 || (a.size()==9 && a[8]=="ntsc") || (a.size()==11 && a[8]=="cfr"),"snapshot worker registry cache output channels samples");
 auto path=[](const QString& s){return std::filesystem::u8path(s.toStdString());};std::ifstream file(path(a[1]));std::string json{std::istreambuf_iterator<char>(file),{}};
 const bool cfr=a.size()==11;const auto clock=cfr?harvester::WebmContinuousClock(a[9].toUInt(),a[10].toUInt()):harvester::WebmContinuousClock();
 auto owner=std::make_shared<p::StreamCacheOwner>(std::make_shared<p::StreamCacheBudget>());
 auto session=std::make_unique<p::CachedRemuxSession>(path(a[1]).parent_path(),path(a[3]),path(a[4]),json,"clip",path(a[2]),owner,64*1024*1024,p::StreamRemuxLimits{},cfr?p::StreamRemuxProfile::WebmVp9OpusCfr:a.size()==9?p::StreamRemuxProfile::WebmVp9OpusNtsc:p::StreamRemuxProfile::WebmVp9Opus,a[7].toULongLong(),clock);
 auto spin=[&](auto fn){QElapsedTimer t;t.start();while(!fn()){check(t.elapsed()<30000,"Opus decoder timeout");QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(5);}};
 std::cerr<<"cache-start"<<std::endl;spin([&]{auto s=session->status();check(s.output.failure.empty(),s.output.failure.c_str());return s.done;});std::cerr<<"cache-complete"<<std::endl;check(session->status().output.complete,"cache incomplete");auto source=session->decoderSource(true);check(source->hasAudio() && !source->audioWindow(),"Opus misclassified as AAC");
 bool loaded=false,ended=false,natural=false;QString failure;
 {
  Player player(nullptr,"software",{{"vid","no"},{"ao","pcm"},{"ao-pcm-file",a[5]},{"ao-pcm-waveheader","no"},{"audio-format","s16"},{"audio-samplerate","48000"},{"audio-channels",a[6]=="1"?"mono":"stereo"},{"audio-display","no"},{"gapless-audio","no"},{"hwdec","no"},{"untimed","yes"},{"log-file",a[5]+".mpv.log"},{"msg-level","all=v"}});
  player.resize(160,90);player.show();player.set("volume",100);player.set("mute",false);check(player.setAudioGraph("anull","s16",48000),"audio config");
  QObject::connect(&player,&Player::loaded,[&]{loaded=true;});QObject::connect(&player,&Player::error,[&](const QString& s){failure=s;});QObject::connect(&player,&Player::ended,[&](bool eof){ended=true;natural=eof;});
  check(player.loadContinuousSource(source,{}),"Opus load rejected");std::cerr<<"playback-start"<<std::endl;spin([&]{return ended || !failure.isEmpty();});std::cerr<<"playback-complete"<<std::endl;check(loaded && ended && natural && failure.isEmpty(),"Opus decode failed");check(!player.pcmWindowActive(),"AAC trim applied to Opus");
 }
 source.reset();session->retire();session.reset();owner->collect();check(owner->count()==0,"cache not released");
 std::cout<<"{\"result\":\"PASS\",\"audioDeviceOpened\":false,\"naturalEof\":true,\"aacWindowApplied\":false}"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
