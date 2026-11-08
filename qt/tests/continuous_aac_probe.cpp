// Real Player continuous AAC window, file AO, private test profile only.
#include "player.h"
#include "windowsruntime.h"
#include "mpvlifetime.h"
#include "../../shared/progressivecachedremux.h"
#include <QtWidgets>
#include <fstream>
#include <iostream>
#include <future>
namespace p=foxicmp::progressive;
static void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
static std::string text(const QString& path){std::ifstream f(std::filesystem::u8path(path.toStdString()),std::ios::binary);check(f.good(),"input JSON");return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv)try{
#ifdef Q_OS_WIN
 WindowsRuntime runtime;check(runtime.valid(),"Windows runtime");
#endif
 QApplication app(argc,argv);const auto a=app.arguments();check(a.size()==11 || a.size()==12,"initial full complete worker registry cache output mode rate channels [original oracle]");
 auto path=[](const QString& s){return std::filesystem::u8path(s.toStdString());};const auto mode=a[8];const bool seeking=mode=="seek" || mode=="seek-ended";
 const bool growingSeek=mode=="growing-seek",audioGuard=mode=="audio-guard";
 const auto input=text(mode=="complete" || audioGuard || seeking?a[3]:a[1]);const auto model=p::Parse(input);
 auto owner=std::make_shared<p::StreamCacheOwner>(std::make_shared<p::StreamCacheBudget>());
 auto session=std::make_unique<p::CachedRemuxSession>(path(a[1]).parent_path(),path(a[5]),path(a[6]),input,"clip",path(a[4]),owner,64*1024*1024,p::StreamRemuxLimits{},p::StreamRemuxProfile::H264AacWindow);
 auto spin=[&](auto fn,int ms=20000){QElapsedTimer timer;timer.start();while(!fn()){check(timer.elapsed()<ms,"Player test timeout");QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(5);}};
 spin([&]{const auto s=session->status();check(s.output.failure.empty(),s.output.failure.c_str());return s.output.fragments>0 && (!growingSeek || s.output.indexedEnd100ns>=15000000);});
 const auto startupIndexedEnd=session->status().output.indexedEnd100ns;
 auto source=session->decoderSource(true);check(source->audioWindow()!=nullptr,"missing immutable PCM plan");
 const auto worker=session->status().source.workerId;bool loaded=false,ended=false,natural=false;QString failure;double stopSeconds=0;bool beforeComplete=false;
 {
  Player player(nullptr,"software",{{"ao","pcm"},{"ao-pcm-file",a[7]},{"ao-pcm-waveheader","no"},{"audio-format","s16"},{"audio-samplerate",a[9]},
    {"audio-channels",a[10]=="1"?"mono":"stereo"},{"log-file",a[7]+".mpv.log"},{"msg-level","all=v"},{"audio-display","no"},{"gapless-audio","no"},{"hwdec","no"},{"untimed","yes"}});
  player.resize(160,90);player.show();player.set("volume",100);player.set("mute",false);check(player.setAudioGraph("anull","s16",a[9].toInt()),"managed audio config");
  QObject::connect(&player,&Player::error,[&](const QString& s){failure=s;});
  QObject::connect(&player,&Player::loaded,[&]{loaded=true;});
  QObject::connect(&player,&Player::ended,[&](bool eof){ended=true;natural=eof;});
  check(!player.loadContinuousSource(source,{{"af-add","anull"}}),"AAC filter override admitted");
  QMap<QString,QString> options{{"pause","yes"}};
  if(seeking || growingSeek){options["start"]=mode=="seek-ended"?(a[9]=="44100"?"0.01":"4.1"):"1";options["hr-seek"]="yes";options["hr-seek-demuxer-offset"]="0.25";}
  if(a.size()==12){
   options["aid"]="auto";options["sub-auto"]="no";options["audio-file-auto"]="no";options["rebase-start-time"]="no";
   options["demuxer-lavf-o"]="fflags=+ignidx";options["demuxer-lavf-analyzeduration"]="0.1";options["demuxer-readahead-secs"]="0";options["cache"]="no";
   spin([&]{return session->status().done;});check(session->status().output.complete,"oracle cache incomplete");
   QString reference=a[11];
   if(a[11]=="@cache"){
    // Compare the callback views with ordinary files holding the same complete
    // validated projections; AAC seek history must match on both paths.
    p::StreamFragmentDecoder views(source);views.refresh();
    for(unsigned kind=0;kind<2;++kind){
     const auto file=a[7]+(kind?".audio.m4a":".video.mp4");std::ofstream out(path(file),std::ios::binary);
     std::vector<char> bytes(65536);for(uint64_t at=0;at<views.viewSize(kind);){const auto n=size_t((std::min)(uint64_t(bytes.size()),views.viewSize(kind)-at));const auto r=views.readView(kind,at,bytes.data(),n);check(r.data() && r.bytes==n,"oracle projection read");out.write(bytes.data(),n);at+=n;}check(out.good(),"oracle projection write");
     if(kind)options["audio-files-append"]=file;else reference=file;
    }
   }
   check(mode=="seek" && player.loadPcmWindowSource(reference,options,{0,source->audioWindow()->pcm.Samples(),a[9].toInt()}),"AAC file oracle load rejected");
  }
  std::atomic<bool> guardHeld{false},releaseGuard{false};std::future<void> guardTask;
  if(audioGuard){
   spin([&]{return session->status().done;});
   guardTask=std::async(std::launch::async,[&]{
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!source->status().activeReaders){check(std::chrono::steady_clock::now()<deadline,"video admission not observed");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    p::lease_detail::Directory directory(path(a[5]),true);auto guard=directory.file(".guard",true);check(guard.lock(),"private registry guard unavailable");guardHeld=true;
    while(!releaseGuard && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
   });
  }
  if(a.size()!=12)check(player.loadContinuousSource(source,options),"continuous AAC load rejected");
  if(audioGuard){
   spin([&]{return !failure.isEmpty();});releaseGuard=true;guardTask.get();
   check(guardHeld && failure=="continuous-required-audio-unavailable" && !natural,"missing required AAC was not rejected");
  }else{
  spin([&]{check(failure.isEmpty(),failure.toStdString().c_str());return loaded;});
  check(player.pcmWindowActive(),"PCM window missing after file-loaded");
  check(player.setAudioGraph("anull","s16",a[9].toInt()) && player.pcmWindowActive(),"audio reconfiguration lost window");
  player.set("pause",false);
  if(mode=="growing" || growingSeek){
   spin([&]{check(failure.isEmpty(),failure.toStdString().c_str());return player.get("time-pos").toDouble()>(growingSeek?1.1:.1);});
   beforeComplete=!session->status().output.complete;check(beforeComplete && !ended,"early EOF");
   player.set("pause",true);session->publish(text(a[2]));QElapsedTimer paused;paused.start();while(paused.elapsed()<200){QApplication::processEvents();QThread::msleep(5);}
   check(player.get("pause").toBool() && !ended,"manual pause lost");player.set("pause",false);
   spin([&]{check(failure.isEmpty(),failure.toStdString().c_str());return session->status().source.code=="waiting";});
   check(!ended && !session->status().output.complete,"unconfirmed EOF");session->publish(text(a[3]));
  }else if(mode=="stop" || mode=="failure"){
   spin([&]{return player.get("time-pos").toDouble()>.1;});
   if(mode=="failure")session->cancel();
   const auto start=std::chrono::steady_clock::now();source->cancel();check(player.command({"stop"}),"Player stop rejected");source->closeCancelledDecoderReaders();
   stopSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();check(stopSeconds<2,"stop latency");
  }
  if(mode!="stop" && mode!="failure"){
   spin([&]{check(failure.isEmpty(),failure.toStdString().c_str());return ended;});check(natural,"not natural EOF");
   spin([&]{return session->status().done;});check(session->status().output.complete,"cache not complete");
  }
  }
 }
 check(EngineRetirement::waitEmpty(std::chrono::seconds(5)),"Player engine not retired");
 session->cancel();spin([&]{return session->status().done;});const auto final=session->status();check(final.source.workerId==worker,"worker replaced");session.reset();owner->collect();check(source->status().removed,"cache not retired");
 for(const auto& id:source->status().readerIds){rapidjson::Document d;d.Parse(p::ReaderRequest(path(a[5]),"{\"version\":1,\"action\":\"readerStatus\",\"readerId\":\""+id+"\"}").c_str());check(d["reader"]["released"].GetBool() && d["reader"]["filesClosed"].GetBool(),"decoder reader leaked");}
 std::cout<<QJsonDocument(QJsonObject{{"result","PASS"},{"loaded",loaded},{"naturalEof",natural},{"requiredAudioRejected",audioGuard},{"playedBeforeComplete",beforeComplete},{"startupIndexedEnd100ns",double(startupIndexedEnd)},{"stopSeconds",stopSeconds},{"windowApplied",!audioGuard},{"audioDeviceOpened",false},{"cacheRemoved",true}}).toJson(QJsonDocument::Compact).constData()<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
