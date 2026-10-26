#include "player.h"
#include "audiomirror.h"
#include "mpvlifetime.h"
#include "progressivesource.h"
#include "stdinstream.h"
#include "../../shared/progressivereaderlease.h"
#include "windowsruntime.h"
#include <QtWidgets>
#include <QtTest>
#include <QScopeGuard>
#include <future>
class PlayerRetirementTest:public QObject {
  Q_OBJECT
private slots:
  void init(){QTRY_COMPARE_WITH_TIMEOUT(EngineRetirement::occupied(),size_t(0),10000);}
  void coreAndSoftwareRendererOutliveDeletedWidget() {
    auto player=std::make_unique<Player>(nullptr,"software",QMap<QString,QString>{{"ao","null"},{"hwdec","no"}});
    player->show();QTRY_VERIFY(player->ready());
    const auto media=QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("Русский H264.mp4");
    QVERIFY(player->loadSource(media,{}));QTRY_VERIFY_WITH_TIMEOUT(player->get("time-pos").toDouble()>.1,10000);
    auto* native=player->lifetime.get();auto closed=native->closed;std::weak_ptr<MpvLifetime> lifetime=player->lifetime;
    auto entered=std::make_shared<std::promise<void>>();auto ready=entered->get_future();auto release=std::make_shared<std::promise<void>>();auto gate=release->get_future().share();
    native->retirementHook=[entered,gate]{entered->set_value();gate.wait();};
    const auto cleanup=qScopeGuard([release]{try{release->set_value();}catch(const std::future_error&){};});
    QElapsedTimer elapsed;elapsed.start();player.reset();QVERIFY(elapsed.elapsed()<200);
    QTRY_VERIFY_WITH_TIMEOUT(ready.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,3000);
    QTRY_VERIFY_WITH_TIMEOUT([&]{int idle=0;return mpv_get_property(native->handle,"idle-active",MPV_FORMAT_FLAG,&idle)>=0 && idle;}(),5000);
    QVERIFY(lifetime.expired());QVERIFY(!closed->load());QVERIFY(native->handle);QVERIFY(native->softwareRenderer);QVERIFY(native->streams);QVERIFY(native->stdinStream);QVERIFY(!native->owner);
    // Callbacks are still registered during the deliberately blocked native
    // close. They must not dereference the destroyed QWidget.
    Player::wakeup(native);Player::redraw(native);
    int ticks=0;QTimer tick;connect(&tick,&QTimer::timeout,&tick,[&]{++ticks;});tick.start(5);QTRY_VERIFY_WITH_TIMEOUT(ticks>=4,1000);
    release->set_value();QTRY_COMPARE_WITH_TIMEOUT(EngineRetirement::occupied(),size_t(0),10000);QVERIFY(closed->load());
  }
  void secondaryDecoderOutlivesDeletedAudioMirror() {
    Player primary(nullptr,"software",{{"ao","null"},{"hwdec","no"}});primary.show();QTRY_VERIFY(primary.ready());
    auto mirror=std::make_unique<AudioMirror>(&primary,"auto",[](bool,const QString&){},"null");
    QVERIFY(primary.loadSource(QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("Русский H264.mp4"),{}));
    QTRY_VERIFY_WITH_TIMEOUT(mirror->active(),10000);
    auto* native=mirror->lifetime.get();auto closed=native->closed;std::weak_ptr<MpvLifetime> lifetime=mirror->lifetime;
    auto entered=std::make_shared<std::promise<void>>();auto ready=entered->get_future();auto release=std::make_shared<std::promise<void>>();auto gate=release->get_future().share();
    native->retirementHook=[entered,gate]{entered->set_value();gate.wait();};
    const auto cleanup=qScopeGuard([release]{try{release->set_value();}catch(const std::future_error&){};});
    QElapsedTimer elapsed;elapsed.start();mirror.reset();QVERIFY(elapsed.elapsed()<200);
    QTRY_VERIFY_WITH_TIMEOUT(ready.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,3000);
    QTRY_VERIFY_WITH_TIMEOUT([&]{int idle=0;return mpv_get_property(native->handle,"idle-active",MPV_FORMAT_FLAG,&idle)>=0 && idle;}(),5000);
    QVERIFY(lifetime.expired());QVERIFY(!closed->load());QVERIFY(native->handle);QVERIFY(native->streams);QVERIFY(!native->softwareRenderer);
    release->set_value();QTRY_COMPARE_WITH_TIMEOUT(EngineRetirement::occupied(),size_t(1),10000);QVERIFY(closed->load());
  }
  void progressiveLeaseWaitsForNativeCoreAndActualSourceClose() {
    QTemporaryDir root;QVERIFY(root.isValid());QJsonObject tracks;
    for(const auto& kind:{QString("video"),QString("audio")}) {
      const auto name=kind=="video"?QString("video.mp4"):QString("audio.m4a");
      const auto path=QDir(root.path()).filePath(name);QVERIFY(QFile::copy(QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("progressive/"+name),path));const auto size=QFileInfo(path).size();
      tracks[kind]=QJsonObject{{"path",name},{"container",kind=="video"?"mp4":"m4a"},{"fragmented",true},{"initializationReady",true},{"availablePrefixBytes",size},{"expectedTotalBytes",size},{"state","complete"}};
    }
    const auto registry=QDir(root.path()).filePath("readers");
    QJsonObject descriptor{{"schemaVersion",1},{"sessionId",QUuid::createUuid().toString(QUuid::Id128)},{"generation",1},{"revision",1},{"state","complete"},{"duration100ns",200000000},{"tracks",tracks},{"sessionDirectory",root.path()}};
    auto source=ProgressiveSource::openPublished(descriptor,registry);const auto sourceClosed=source->releaseReceipt();const auto reader=source->registeredReaderId();QVERIFY(!reader.isEmpty());
    auto player=std::make_unique<Player>(nullptr,"software",QMap<QString,QString>{{"ao","null"},{"hwdec","no"}});player->show();QTRY_VERIFY(player->ready());player->progressiveSource(source);
    const auto url="foxicmp-session://"+source->streamId();QVERIFY(player->loadSource(url+"/video",{{"audio-files-append",url+"/audio"},{"demuxer-lavf-format","mov"}}));QTRY_VERIFY_WITH_TIMEOUT(player->get("time-pos").toDouble()>.1,10000);
    auto* native=player->lifetime.get();auto closed=native->closed;auto entered=std::make_shared<std::promise<void>>();auto ready=entered->get_future();auto release=std::make_shared<std::promise<void>>();auto gate=release->get_future().share();
    native->retirementHook=[entered,gate]{entered->set_value();gate.wait();};const auto cleanup=qScopeGuard([release]{try{release->set_value();}catch(const std::future_error&){};});
    QElapsedTimer elapsed;elapsed.start();player.reset();source.reset();QVERIFY(elapsed.elapsed()<200);QTRY_VERIFY_WITH_TIMEOUT(ready.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,3000);
    QVERIFY(native->retainedSource);QVERIFY(!closed->load());QVERIFY(!sourceClosed->load());
    const auto status=[&]{return QJsonDocument::fromJson(QByteArray::fromStdString(foxicmp::progressive::ReaderRequest(std::filesystem::u8path(registry.toStdString()),"{\"version\":1,\"action\":\"readerStatus\",\"readerId\":\""+reader.toStdString()+"\"}"))).object();};
    QVERIFY(!status()["reader"].toObject()["released"].toBool());
    release->set_value();QTRY_COMPARE_WITH_TIMEOUT(EngineRetirement::occupied(),size_t(0),10000);QVERIFY(closed->load());QTRY_VERIFY_WITH_TIMEOUT(sourceClosed->load(),10000);QTRY_VERIFY_WITH_TIMEOUT(status()["reader"].toObject()["released"].toBool(),5000);
  }
  void capacityReservedBeforeCreatingNativeEngine() {
    std::vector<std::shared_ptr<int>> occupied;
    for(size_t i=0;i<EngineRetirement::Capacity;++i)occupied.push_back(EngineRetirement::create<int>([]{return new int(1);}));
    const auto cleanup=qScopeGuard([&]{occupied.clear();EngineRetirement::waitEmpty(std::chrono::seconds(3));});
    QTRY_COMPARE_WITH_TIMEOUT(foxicmp::progressive::SourceRetirement::occupied(),size_t(0),5000);
    QVERIFY_EXCEPTION_THROWN(Player(nullptr,"software",{{"ao","null"}}),EngineRetirementBusy);
    QCOMPARE(EngineRetirement::occupied(),EngineRetirement::Capacity);occupied.clear();QTRY_COMPARE_WITH_TIMEOUT(EngineRetirement::occupied(),size_t(0),3000);
    auto retry=std::make_unique<Player>(nullptr,"software",QMap<QString,QString>{{"ao","null"}});QVERIFY(retry->ready());
  }
  void invalidOptionsRetireUninitializedNativeCore() {
    QVERIFY_EXCEPTION_THROWN(Player(nullptr,"software",{{"foxicmp-nonexistent-option","yes"}}),std::runtime_error);
    QTRY_COMPARE_WITH_TIMEOUT(EngineRetirement::occupied(),size_t(0),5000);
  }
  void cleanupTestCase(){QTRY_COMPARE_WITH_TIMEOUT(EngineRetirement::occupied(),size_t(0),10000);}
};
int main(int argc,char** argv){
#ifdef Q_OS_WIN
 WindowsRuntime runtime;if(!runtime.valid())return 1;
#endif
 QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QApplication app(argc,argv);PlayerRetirementTest test;return QTest::qExec(&test,argc,argv);
}
#include "playerretirement_test.moc"
