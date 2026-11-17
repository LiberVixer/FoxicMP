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
 QApplication app(argc,argv);auto a=app.arguments();
 const auto selection=a.size()>=11?a[8]:QString();
 const bool packetAudio=selection=="packet-opus";
 const bool derived=packetAudio || selection=="derived-av";
 const bool nominal=selection=="nominal-vfr",autoCount=derived || nominal || selection=="auto-vfr";
 const bool growing=derived && a.size()==12;
 const bool seekPcm=packetAudio && a.size()==13 && a[11]=="--seek-sample";
 const auto seekSample=seekPcm?a[12].toULongLong():0;
 const bool simple=autoCount || selection=="simple-vfr",vfr=simple || selection=="vfr",cfr=selection=="cfr";
 check(a.size()==8 || (a.size()==9 && a[8]=="ntsc") || vfr || cfr,"snapshot worker registry cache output channels samples");
 auto path=[](const QString& s){return std::filesystem::u8path(s.toStdString());};std::ifstream file(path(a[1]));std::string json{std::istreambuf_iterator<char>(file),{}};
 const auto clock=packetAudio?harvester::WebmContinuousClock::VfrPacketOpus(a[10].toULongLong()):derived?harvester::WebmContinuousClock::VfrDerivedAv(a[10].toULongLong()):nominal?harvester::WebmContinuousClock::VfrNominal(a[10].toULongLong()):autoCount?harvester::WebmContinuousClock::VfrAutoCount(a[10].toULongLong()):vfr?harvester::WebmContinuousClock::Vfr(a[9].toULongLong(),a[10].toULongLong()):cfr?harvester::WebmContinuousClock(a[9].toUInt(),a[10].toUInt()):harvester::WebmContinuousClock();
 auto owner=std::make_shared<p::StreamCacheOwner>(std::make_shared<p::StreamCacheBudget>());
 auto session=std::make_unique<p::CachedRemuxSession>(path(a[1]).parent_path(),path(a[3]),path(a[4]),json,"clip",path(a[2]),owner,64*1024*1024,p::StreamRemuxLimits{},packetAudio?p::StreamRemuxProfile::WebmVp9OpusPacketClock:derived?p::StreamRemuxProfile::WebmVp9OpusDerivedAv:nominal?p::StreamRemuxProfile::WebmVp9OpusNominalVfr:autoCount?p::StreamRemuxProfile::WebmVp9OpusSimpleAutoVfr:simple?p::StreamRemuxProfile::WebmVp9OpusSimpleVfr:vfr?p::StreamRemuxProfile::WebmVp9OpusVfr:cfr?p::StreamRemuxProfile::WebmVp9OpusCfr:a.size()==9?p::StreamRemuxProfile::WebmVp9OpusNtsc:p::StreamRemuxProfile::WebmVp9Opus,a[7].toULongLong(),clock);
 auto spin=[&](auto fn){QElapsedTimer t;t.start();while(!fn()){check(t.elapsed()<30000,"Opus decoder timeout");QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(5);}};
 std::cerr<<"cache-start"<<std::endl;spin([&]{auto s=session->status();check(s.output.failure.empty(),s.output.failure.c_str());return growing?(s.output.initialized && s.output.videoSamples && s.output.audioPresentationSamples>648):s.done;});std::cerr<<"cache-ready"<<std::endl;check(growing?!session->status().output.complete:session->status().output.complete,"cache phase");auto source=session->decoderSource(true);check(source->hasAudio() && !source->audioWindow(),"Opus misclassified as AAC");
 bool published=false;uint64_t partialSamples=0;
 bool loaded=false,ended=false,natural=false;QString failure;
 {
  Player player(nullptr,"software",{{"vid",(growing || seekPcm)?"auto":"no"},{"vo","null"},{"ao","pcm"},{"ao-pcm-file",a[5]},{"ao-pcm-waveheader","no"},{"audio-format","s16"},{"audio-samplerate","48000"},{"audio-channels",a[6]=="1"?"mono":"stereo"},{"audio-display","no"},{"gapless-audio","no"},{"hwdec","no"},{"untimed","yes"},{"log-file",a[5]+".mpv.log"},{"msg-level","all=v"}});
  player.resize(160,90);player.show();player.set("volume",100);player.set("mute",false);check(player.setAudioGraph("anull","s16",48000),"audio config");
  QObject::connect(&player,&Player::loaded,[&]{loaded=true;});QObject::connect(&player,&Player::error,[&](const QString& s){failure=s;});QObject::connect(&player,&Player::ended,[&](bool eof){ended=true;natural=eof;});
  QMap<QString,QString> loadOptions;
  if(seekPcm)loadOptions={{"start",QString::number(double(seekSample)/48000.,'f',9)},{"hr-seek","yes"},{"hr-seek-demuxer-offset","0.25"}};
  check(player.loadContinuousSource(source,loadOptions),"Opus load rejected");std::cerr<<"playback-start"<<std::endl;spin([&]{
   if(growing && !published && QFileInfo(a[5]).size()>=48000*(a[6]=="1"?2:4)){
    check(!session->status().output.complete,"PCM prefix already complete");partialSamples=uint64_t(QFileInfo(a[5]).size())/(a[6]=="1"?2:4);
    std::ifstream done(path(a[11]));std::string complete{std::istreambuf_iterator<char>(done),{}};check(!complete.empty(),"complete snapshot");session->publish(complete);published=true;
   }
   return ended || !failure.isEmpty();});check(!growing || published,"No PCM before source complete");std::cerr<<"playback-complete"<<std::endl;if(!(loaded && ended && natural && failure.isEmpty()))std::cerr<<"decode-state loaded="<<loaded<<" ended="<<ended<<" natural="<<natural<<" error="<<failure.toStdString()<<std::endl;check(loaded && ended && natural && failure.isEmpty(),"Opus decode failed");if(!seekPcm)check(!player.pcmWindowActive(),"AAC trim applied to Opus");
 }
 source.reset();session->retire();session.reset();owner->collect();check(owner->count()==0,"cache not released");
 std::cout<<"{\"partialPcmSamples\":"<<partialSamples<<",\"growingDecoder\":"<<(growing?"true":"false")<<",\"result\":\"PASS\",\"audioDeviceOpened\":false,\"naturalEof\":true,\"aacWindowApplied\":false}"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
