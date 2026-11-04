#include "player.h"
#include "mpvlifetime.h"
#include "continuousstreams.h"
#include "windowsruntime.h"
#include "../../shared/progressivecachedremux.h"
#include <QtWidgets>
#include <QtTest>
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
  void cleanupTestCase(){QTRY_COMPARE_WITH_TIMEOUT(EngineRetirement::occupied(),size_t(0),10000);}
};
int main(int argc,char** argv){
#ifdef Q_OS_WIN
 WindowsRuntime runtime;if(!runtime.valid())return 1;
#endif
 QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QApplication app(argc,argv);ContinuousPlayerTest test;return QTest::qExec(&test,argc,argv);
}
#include "continuousplayer_test.moc"
