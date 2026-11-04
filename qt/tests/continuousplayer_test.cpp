#include "player.h"
#include "mpvlifetime.h"
#include "continuousstreams.h"
#include "windowsruntime.h"
#include "window.h"
#include "profile.h"
#include "progressiveapi.h"
#include "../../shared/progressivestreamcontroller.h"
#include "../../shared/progressivestreamplayback.h"
#include "../../shared/progressivecachedremux.h"
#include <QtWidgets>
#include <QtTest>
#include <qscopeguard.h>
#include <future>
#include <QLocalSocket>
namespace p=foxicmp::progressive;
class ContinuousPlayerTest:public QObject {
  Q_OBJECT
  QByteArray bytes;QString original;
  QTemporaryDir media;
  static QJsonObject block(const QByteArray& raw,int at,int n){return {{"offset",QString::number(at)},{"length",QString::number(n)},{"sha256",QString::fromLatin1(QCryptographicHash::hash(raw.mid(at,n),QCryptographicHash::Sha256).toHex())}};}
private slots:
  void initTestCase(){
    QVERIFY(media.isValid());original=media.filePath("video.mp4");
    QProcess ffmpeg;ffmpeg.start("ffmpeg",{"-v","error","-y","-f","lavfi","-i","testsrc2=size=160x90:rate=25:duration=12","-c:v","libx264","-threads","2","-preset","veryfast","-bf","0","-g","25","-keyint_min","25","-sc_threshold","0","-movflags","+faststart",original});
    QVERIFY(ffmpeg.waitForFinished(20000));QCOMPARE(ffmpeg.exitCode(),0);QFile f(original);QVERIFY(f.open(QIODevice::ReadOnly));bytes=f.readAll();QVERIFY(bytes.size()>10000);
  }
  void playback_data(){QTest::addColumn<QString>("mode");for(const auto& mode:{"complete","stop","replace","delete","producer-error"})QTest::newRow(mode)<<QString(mode);}
  void playback(){
    QFETCH(QString,mode);QTemporaryDir dir;QVERIFY(dir.isValid());const auto base=std::filesystem::u8path(dir.path().toStdString());const auto registry=base/"registry";
    QFile file(dir.filePath("input.part"));QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),qint64(bytes.size()));file.close();
    const auto sid=QString::fromStdString(p::lease_detail::RandomId()),writer=QString::fromStdString(p::lease_detail::RandomId());
    int prefix=bytes.size()/3,revision=1;QJsonArray blocks{block(bytes,0,prefix)};
    auto snapshot=[&](bool complete){const auto state=complete?"complete":"downloading";return QJsonObject{{"schemaVersion",2},{"sessionId",sid},{"generation","1"},{"revision",QString::number(revision)},{"writer",QJsonObject{{"instanceId",writer},{"state",state}}},{"timeline",QJsonObject{{"timebase",QJsonObject{{"numerator",1},{"denominator",10000000}}},{"live",false},{"duration","120000000"},{"timestampOffset","0"}}},{"resources",QJsonArray{QJsonObject{{"id","clip"},{"name","input.part"},{"length",QString::number(bytes.size())},{"state",state},{"ranges",QJsonArray{QJsonObject{{"offset","0"},{"length",QString::number(prefix)}}}},{"integrity",QJsonObject{{"algorithm","sha256"},{"blocks",blocks}}}}}},{"tracks",QJsonArray{QJsonObject{{"id","video"},{"kind","video"},{"required",true},{"codecHint","h264"},{"containerHint","mp4"},{"state",state},{"initializationReady",false},{"source",QJsonObject{{"resourceId","clip"}}}}}}};};
    auto text=[&](bool complete){return QJsonDocument(snapshot(complete)).toJson(QJsonDocument::Compact).toStdString();};
    const auto child=std::filesystem::u8path(QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
      "foxicmp-remux-stream-worker.exe"
#else
      "foxicmp-remux-stream-worker"
#endif
      ).toStdString());
    auto owner=std::make_shared<p::StreamCacheOwner>(std::make_shared<p::StreamCacheBudget>());auto session=std::make_unique<p::CachedRemuxSession>(base,registry,base/"cache",text(false),"clip",child,owner);
    QTRY_VERIFY_WITH_TIMEOUT(session->status().output.fragments>0,15000);const auto worker=session->status().source.workerId;
    auto source=session->decoderSource();auto player=std::make_unique<Player>(nullptr,"software",QMap<QString,QString>{{"ao","null"},{"hwdec","no"}});player->show();QTRY_VERIFY(player->ready());
    QSignalSpy ended(player.get(),&Player::ended);QSignalSpy errors(player.get(),&Player::error);
    QVERIFY(player->set("volume",17));QVERIFY(player->set("mute",true));QVERIFY(player->set("speed",4));
    QVERIFY(player->loadContinuousSource(source,{{"pause","no"}}));QTRY_VERIFY_WITH_TIMEOUT(player->get("time-pos").toDouble()>.1,12000);
    QVERIFY(!player->grabFramebuffer().isNull());QVERIFY(ended.empty());QVERIFY(!session->status().output.complete);QVERIFY(session->status().output.activeReaders>0);
    auto publish=[&](int next,bool complete){if(next>prefix)blocks.append(block(bytes,prefix,next-prefix));prefix=next;++revision;session->publish(text(complete));};
    if(mode=="complete"){
      QVERIFY(player->set("pause",true));QTest::qWait(100);const auto pos=player->get("time-pos").toDouble();publish(bytes.size()*2/3,false);QTest::qWait(250);
      QVERIFY(player->get("pause").toBool());QVERIFY(std::abs(player->get("time-pos").toDouble()-pos)<.1);QCOMPARE(session->status().source.workerId,worker);
      QCOMPARE(player->get("volume").toDouble(),17.);QVERIFY(player->get("mute").toBool());QCOMPARE(player->get("speed").toDouble(),4.);
      QVERIFY(player->set("pause",false));publish(bytes.size(),false);QTest::qWait(200);QVERIFY(ended.empty());publish(bytes.size(),true);
      QTRY_VERIFY_WITH_TIMEOUT(!ended.empty(),12000);QVERIFY(ended.last().at(0).toBool());QVERIFY(errors.empty());QVERIFY(session->status().output.complete);
      QVERIFY(player->command({"stop"}));
    }else if(mode=="stop"){
      // Detach must release only this Player's cursor, not cancel other readers.
      auto independent=source->reader();QVERIFY(player->command({"stop"}));QTRY_COMPARE(source->status().activeReaders,size_t(1));QVERIFY(!source->stopped());char b;QVERIFY(independent->read(0,&b,1).data());independent->close();
    }else if(mode=="replace"){
      QVERIFY(player->loadSource(original,{{"pause","yes"}}));QTRY_COMPARE_WITH_TIMEOUT(QDir::fromNativeSeparators(player->get("path").toString()),QDir::fromNativeSeparators(original),10000);QVERIFY(!source->stopped());QTRY_COMPARE(source->status().activeReaders,size_t(0));QVERIFY(player->command({"stop"}));
    }else if(mode=="producer-error"){
      session->cancel();QTRY_VERIFY_WITH_TIMEOUT(!ended.empty(),10000);QVERIFY(!ended.last().at(0).toBool());QVERIFY(!source->status().failure.empty());QVERIFY(player->command({"stop"}));
    }
    auto closed=player->lifetime->closed;QElapsedTimer elapsed;elapsed.start();player.reset();QVERIFY(elapsed.elapsed()<200);
    QTRY_VERIFY_WITH_TIMEOUT(closed->load(),10000);QTRY_COMPARE(source->status().activeReaders,size_t(0));
    session->cancel();QTRY_VERIFY_WITH_TIMEOUT(session->status().done,10000);const auto final=session->status();session.reset();owner->collect();QVERIFY(source->status().removed);
    std::vector<std::string> ids=final.source.readerIds;ids.insert(ids.end(),final.output.readerIds.begin(),final.output.readerIds.end());
    for(const auto& id:ids){rapidjson::Document d;d.Parse(p::ReaderRequest(registry,"{\"version\":1,\"action\":\"readerStatus\",\"readerId\":\""+id+"\"}").c_str());QVERIFY(d["reader"]["released"].GetBool());QCOMPARE(std::string(d["reader"]["reason"].GetString()),std::string("files-closed"));}
  }
  void windowClose_data(){
    QTest::addColumn<QString>("mode");
    for(const auto* mode:{"close-window","destroy-api-before-window","reader-release","reader-release-held-reader","delayed-reader-release","delayed-reader-release-replace"})QTest::newRow(mode)<<QString(mode);
  }
  void windowClose(){
    QFETCH(QString,mode);QTemporaryDir dir;QVERIFY(dir.isValid());const auto profilePath=dir.filePath("profile");
    Profile profile(profilePath);profile.settings.setValue("input/mpris",false);profile.settings.setValue("video/output","software");profile.settings.setValue("audio/mute",true);profile.settings.setValue("ui/osd",false);profile.settings.setValue("desktop/closeToTray",false);profile.settings.setValue("integration/prompted",true);
    auto window=std::make_unique<Window>(profile);window->show();auto api=std::make_unique<ProgressiveApi>(window.get());QVERIFY(api->listen(profilePath));
    auto* player=window->findChild<Player*>();QVERIFY(player);QTRY_VERIFY(player->ready());
    const auto registry=ProgressiveApi::readerRegistryFor(profilePath);const auto cleanup=qScopeGuard([&]{if(p::ContinuousRetirement::occupied()==0 && EngineRetirement::occupied()==0){QDir(registry).removeRecursively();QDir(registry+".continuous-cache").removeRecursively();}});
    QFile file(dir.filePath("input.part"));QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),qint64(bytes.size()));file.close();
    const auto sid=QString::fromStdString(p::lease_detail::RandomId()),writer=QString::fromStdString(p::lease_detail::RandomId());const int prefix=bytes.size()*2/3;
    const auto model=QJsonObject{{"schemaVersion",2},{"sessionId",sid},{"generation","1"},{"revision","1"},{"writer",QJsonObject{{"instanceId",writer},{"state","downloading"}}},{"timeline",QJsonObject{{"timebase",QJsonObject{{"numerator",1},{"denominator",10000000}}},{"live",false},{"duration","120000000"},{"timestampOffset","0"}}},{"resources",QJsonArray{QJsonObject{{"id","clip"},{"name","input.part"},{"length",QString::number(bytes.size())},{"state","downloading"},{"ranges",QJsonArray{QJsonObject{{"offset","0"},{"length",QString::number(prefix)}}}},{"integrity",QJsonObject{{"algorithm","sha256"},{"blocks",QJsonArray{block(bytes,0,prefix)}}}}}}},{"tracks",QJsonArray{QJsonObject{{"id","video"},{"kind","video"},{"required",true},{"codecHint","h264"},{"containerHint","mp4"},{"state","downloading"},{"initializationReady",false},{"source",QJsonObject{{"resourceId","clip"}}}}}}};
    auto job=api->request({{"version",1},{"action","continuousRemuxStart"},{"requestId",QString::fromStdString(p::lease_detail::RandomId())},{"sessionDirectory",dir.path()},{"resourceId","clip"},{"session",model}});QVERIFY(job["ok"].toBool());
    const auto query=QJsonObject{{"version",1},{"action","continuousRemuxStatus"},{"jobId",job["jobId"]}};
    QTRY_VERIFY_WITH_TIMEOUT(api->request(query)["playbackReady"].toBool(),15000);
    auto playing=api->request({{"version",1},{"action","continuousRemuxPlay"},{"requestId",QString::fromStdString(p::lease_detail::RandomId())},{"jobId",job["jobId"]},{"sessionId",sid},{"generation","1"},{"writerInstanceId",writer}});QVERIFY(playing["ok"].toBool());
    QTRY_VERIFY_WITH_TIMEOUT(player->get("time-pos").toDouble()>.1,10000);const auto nativeClosed=player->lifetime->closed;
    const QJsonObject playbackQuery{{"version",1},{"action","continuousPlaybackStatus"},{"playbackId",playing["playbackId"]}};
    if(mode=="close-window"){QVERIFY(window->close());QVERIFY(!window->isVisible());QCOMPARE(api->request(playbackQuery)["state"].toString(),QString("closed"));}
    if(mode.contains("reader-release")){
      QTRY_VERIFY_WITH_TIMEOUT(!api->request(playbackQuery)["readerIds"].toArray().isEmpty(),3000);
      const auto id=api->request(playbackQuery)["readerIds"].toArray().first();
      const QJsonObject releaseQuery{{"version",1},{"action","readerRelease"},{"readerId",id}};
      if(mode.startsWith("reader-release")){
        const auto actual=api->request({{"version",1},{"action","readerStatus"},{"readerId",id}});
        const auto owns=[&](const QJsonObject& value){return api->continuousPlayback->holdsOutputReader(QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString());};
        QVERIFY(owns(actual));QVERIFY(!api->continuousPlayback->holdsOutputReader("[]"));
        for(const auto* key:{"released","filesClosed","sessionId","generation","resources"}){
          auto wrong=actual;auto receipt=wrong["reader"].toObject();
          if(QString(key)=="released" || QString(key)=="filesClosed")receipt[key]=true;
          else if(QString(key)=="sessionId")receipt[key]=QString(32,'0');
          else if(QString(key)=="generation")receipt[key]="2";
          else {auto resource=receipt["resources"].toArray().first().toObject();resource["fileId"]="0";receipt[key]=QJsonArray{resource};}
          wrong["reader"]=receipt;QVERIFY(!owns(wrong));
        }
        std::shared_ptr<p::StreamOutputStore::Reader> independent;
        if(mode.endsWith("held-reader"))independent=api->continuousPlayback->source->reader();
        const auto reply=api->request(releaseQuery);QVERIFY2(reply["ok"].toBool(),QJsonDocument(reply).toJson().constData());
        if(independent){
          QTRY_COMPARE_WITH_TIMEOUT(api->request(playbackQuery)["decoderReaders"].toInt(),1,5000);
          const QJsonObject heldQuery{{"version",1},{"action","readerRelease"},{"readerId",QString::fromStdString(independent->id())}};
          const auto held=api->request(heldQuery)["reader"].toObject();QVERIFY(!held["released"].toBool());QVERIFY(!held["filesClosed"].toBool());
          QVERIFY(!api->request(query)["released"].toBool());
          QVERIFY(!QDir(registry+".continuous-cache").entryList({"remux-*"},QDir::Dirs|QDir::NoDotAndDotDot).isEmpty());
          independent->close();
        }
      }
      else {
        auto entered=std::make_shared<std::promise<void>>();auto future=entered->get_future();
        auto release=std::make_shared<std::promise<void>>();auto gate=release->get_future().share();
        api->readerIoHook=[entered,gate]{entered->set_value();gate.wait();};
        const auto unblock=qScopeGuard([release]{try{release->set_value();}catch(const std::future_error&){};});
        QLocalSocket socket;socket.connectToServer(ProgressiveApi::endpointFor(profilePath));QVERIFY(socket.waitForConnected(1000));
        socket.write(QJsonDocument(releaseQuery).toJson(QJsonDocument::Compact)+'\n');socket.flush();
        QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,3000);
        int ticks=0;QTimer tick;connect(&tick,&QTimer::timeout,&tick,[&]{++ticks;});tick.start(5);QTRY_VERIFY_WITH_TIMEOUT(ticks>=4,1000);
        QVERIFY(api->request({{"version",1},{"action","capabilities"}})["ok"].toBool());
        QVERIFY(api->request(playbackQuery)["active"].toBool());
        if(mode.endsWith("-replace")){
          window->openFiles({original});QTRY_COMPARE_WITH_TIMEOUT(QDir::fromNativeSeparators(player->get("path").toString()),QDir::fromNativeSeparators(original),5000);
          QTRY_VERIFY_WITH_TIMEOUT(player->get("time-pos").toDouble()>.1,5000);
        }
        release->set_value();QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable()>0,3000);
        const auto reply=QJsonDocument::fromJson(socket.readAll()).object();QVERIFY2(reply["ok"].toBool(),QJsonDocument(reply).toJson().constData());
        if(mode.endsWith("-replace")){QCOMPARE(QDir::fromNativeSeparators(player->get("path").toString()),QDir::fromNativeSeparators(original));QVERIFY(!player->get("idle-active").toBool());}
      }
      QTRY_COMPARE_WITH_TIMEOUT(api->request(playbackQuery)["state"].toString(),QString("closed"),3000);
      const QJsonObject receiptQuery{{"version",1},{"action","readerStatus"},{"readerId",id}};
      QTRY_VERIFY_WITH_TIMEOUT(api->request(receiptQuery)["reader"].toObject()["released"].toBool(),5000);
      QCOMPARE(api->request(receiptQuery)["reader"].toObject()["reason"].toString(),QString("files-closed"));
      QVERIFY(api->request(releaseQuery)["reader"].toObject()["released"].toBool());
    }
    QElapsedTimer elapsed;elapsed.start();api.reset();QVERIFY(elapsed.elapsed()<200);elapsed.restart();window.reset();QVERIFY(elapsed.elapsed()<200);
    QTRY_VERIFY_WITH_TIMEOUT(nativeClosed->load(),10000);QTRY_COMPARE_WITH_TIMEOUT(p::ContinuousRetirement::occupied(),size_t(0),10000);
    QTRY_COMPARE_WITH_TIMEOUT(EngineRetirement::occupied(),size_t(0),10000);
    const auto receipts=QJsonDocument::fromJson(QByteArray::fromStdString(p::ReaderRequest(std::filesystem::u8path(registry.toStdString()),"{\"version\":1,\"action\":\"readers\",\"sessionId\":\""+sid.toStdString()+"\",\"generation\":\"1\"}"))).object();
    QVERIFY(receipts["ok"].toBool());QVERIFY(receipts["readers"].toArray().size()>=2);
    for(const auto& entry:receipts["readers"].toArray()){const auto receipt=entry.toObject();QVERIFY(receipt["released"].toBool());QVERIFY(receipt["filesClosed"].toBool());QCOMPARE(receipt["reason"].toString(),QString("files-closed"));}
    QVERIFY(QDir(registry+".continuous-cache").entryList({"remux-*"},QDir::Dirs|QDir::NoDotAndDotDot).isEmpty());
  }
  void cleanupTestCase(){QTRY_COMPARE_WITH_TIMEOUT(EngineRetirement::occupied(),size_t(0),10000);}
};
int main(int argc,char** argv){
#ifdef Q_OS_WIN
 WindowsRuntime runtime;if(!runtime.valid())return 1;
#endif
 QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);ContinuousPlayerTest test;return QTest::qExec(&test,argc,argv);
}
#include "continuousplayer_test.moc"
