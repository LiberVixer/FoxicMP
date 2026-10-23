#include "maintenance.h"
#include "window.h"
#include "player.h"
#include "theme.h"
#include "i18n.h"
#include <QtWidgets>
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <sys/stat.h>
#include <sys/resource.h>
class MaintenanceTest : public QObject {
  Q_OBJECT
  QByteArray release(const QString &version, bool preview=false) {
    return QJsonDocument(QJsonArray{QJsonObject{{"html_url", "https://github.com/LiberVixer/FoxicMP/releases/tag/qt-v"+version},
      {"prerelease", preview}, {"assets", QJsonArray{QJsonObject{{"name", "FoxicMPQt-"+version+"-linux-x64.tar.gz"}, {"state", "uploaded"}, {"size", 1000}}}}}}).toJson();
  }
  void prepare(Profile &p) {
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("input/mpris", false);
    p.settings.setValue("audio/mute", true);
    p.settings.setValue("playback/hardwareDecoding", false);
    p.settings.setValue("video/output", qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
  }
private slots:
  void releasesVersionsAndPlatformFiltering() {
    QCOMPARE(Maintenance::releases("[]", "0.1.40", true).status, QString("unpublished"));
    QCOMPARE(Maintenance::releases("bad", "0.1.40", true).status, QString("error"));
    QCOMPARE(Maintenance::releases("{}", "0.1.40", true).status, QString("error"));
    QCOMPARE(Maintenance::releases(QByteArray(2100000,'x'), "0.1.40", true).status, QString("error"));
    QCOMPARE(Maintenance::releases(release("0.1.41"), "0.1.40", true).status, QString("available"));
    QCOMPARE(Maintenance::releases(release("0.1.40"), "0.1.40", true).status, QString("current"));
    QCOMPARE(Maintenance::releases(release("0.1.9"), "0.1.40", true).status, QString("current"));
    QCOMPARE(Maintenance::releases(release("0.1.100"), "0.1.40", true).version, QString("0.1.100"));
    QCOMPARE(Maintenance::releases(release("0.1.41",true), "0.1.40", false).status, QString("unpublished"));
    QCOMPARE(Maintenance::releases(release("0.1.41",true), "0.1.40", true).status, QString("available"));
    auto qtRelease = QJsonDocument::fromJson(release("0.1.42")).array()[0].toObject();
    auto windows = qtRelease;
    windows["assets"] = QJsonArray{QJsonObject{{"name","FoxicMP.99.0.x64.zip"},{"state","uploaded"},{"size",1000}}};
    QJsonValue older = QJsonDocument::fromJson(release("0.1.41")).array()[0];
    auto both = QJsonDocument(QJsonArray{windows,older,qtRelease}).toJson();
    QCOMPARE(Maintenance::releases(both,"0.1.40",true).version,QString("0.1.42"));
    qtRelease["draft"]=true;
    QCOMPARE(Maintenance::releases(QJsonDocument(QJsonArray{qtRelease}).toJson(),"0.1.40",true).status,QString("unpublished"));
    qtRelease["draft"]=false;
    for(const auto &url: {"https://evil.example/LiberVixer/FoxicMP/releases/tag/test", "http://github.com/LiberVixer/FoxicMP/releases/tag/test", "https://github.com/other/project/releases/tag/test", "https://user:pw@github.com/LiberVixer/FoxicMP/releases/tag/test", "https://github.com/LiberVixer/FoxicMP/releases/tag/test?x=1"}) {
      qtRelease["html_url"]=url;
      QCOMPARE(Maintenance::releases(QJsonDocument(QJsonArray{qtRelease}).toJson(),"0.1.40",true).status,QString("unpublished"));
    }
  }
  void asyncHttpErrorsRedirectAndCancellation() {
    QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
    int requests=0;
    QByteArray response=release("0.1.41");
    int status=200;
    bool hold=false;
    connect(&server,&QTcpServer::newConnection,this,[&] {
      while(server.hasPendingConnections()) {
        auto *socket=server.nextPendingConnection();
        connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
          const auto request=socket->readAll();
          if(!request.contains("\r\n\r\n"))return;
          ++requests;
          if(hold)return;
          socket->write("HTTP/1.1 "+QByteArray::number(status)+" Test\r\nContent-Type: application/json\r\nConnection: close\r\nLocation: http://127.0.0.1:1/no\r\nContent-Length: "+QByteArray::number(response.size())+"\r\n\r\n"+response);
          socket->disconnectFromHost();
        });
      }
    });
    const QUrl endpoint(QString("http://127.0.0.1:%1/releases").arg(server.serverPort()));
    UpdateCheck checker; QSignalSpy spy(&checker,&UpdateCheck::finished);
    QVERIFY(checker.startAt(endpoint,"0.1.40",true));
    QVERIFY(!checker.startAt(endpoint,"0.1.40",true));
    QTRY_COMPARE(spy.size(),1); QCOMPARE(spy.takeFirst()[0].toString(),QString("available"));
    status=403;
    QVERIFY(checker.startAt(endpoint,"0.1.40",true));
    QTRY_COMPARE(spy.size(),1); QCOMPARE(spy.takeFirst()[0].toString(),QString("error"));
    status=302;
    QVERIFY(checker.startAt(endpoint,"0.1.40",true));
    QTRY_COMPARE(spy.size(),1); QCOMPARE(spy.takeFirst()[0].toString(),QString("error"));
    QCOMPARE(requests,3);
    status=200; response="bad";
    QVERIFY(checker.startAt(endpoint,"0.1.40",true));
    QTRY_COMPARE(spy.size(),1); QCOMPARE(spy.takeFirst()[0].toString(),QString("error"));
    response=QByteArray(2100000,'x');
    QVERIFY(checker.startAt(endpoint,"0.1.40",true));
    QTRY_COMPARE(spy.size(),1); QCOMPARE(spy.takeFirst()[0].toString(),QString("error"));
    hold=true;
    QVERIFY(checker.startAt(endpoint,"0.1.40",true));
    QTRY_COMPARE(requests,6);
    checker.cancel(); QTRY_COMPARE(spy.size(),1);
    QCOMPARE(spy.takeFirst()[0].toString(),QString("error"));
    QVERIFY(checker.startAt(endpoint,"0.1.40",true));
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(),1,18000);
    QCOMPARE(spy.takeFirst()[0].toString(),QString("error"));
    // Destroying an in-flight request must not emit into a deleted dialog.
    { auto *temporary=new UpdateCheck; QVERIFY(temporary->startAt(endpoint,"0.1.40",true)); delete temporary; }
  }
  void prioritiesAreAppliedOnlyInChildProcesses() {
    QCOMPARE(Maintenance::niceValue("above"),-5);
    QCOMPARE(Maintenance::niceValue("low"),5);
    QCOMPARE(Maintenance::niceValue("bad"),0);
    const auto before=getpriority(PRIO_PROCESS,0);
    for (const auto &mode: {"normal","low","above"}) {
      QProcess child;
      auto env=QProcessEnvironment::systemEnvironment(); env.insert("FOXICMP_PRIORITY_CHILD",mode); child.setProcessEnvironment(env);
      child.start(QCoreApplication::applicationFilePath(),QStringList{});
      QVERIFY(child.waitForFinished(10000));
      QCOMPARE(child.exitCode(),0);
      const auto result=QJsonDocument::fromJson(child.readAllStandardOutput()).object();
      QVERIFY(!result.isEmpty());
      if(result["applied"].toBool())
        QCOMPARE(result["actualNice"].toInt(),Maintenance::niceValue(mode));
      else QVERIFY(!result["error"].toString().isEmpty() || result["actualNice"].toInt()!=Maintenance::niceValue(mode));
    }
    QCOMPARE(getpriority(PRIO_PROCESS,0),before);
  }
  void reportDetailsAndAtomicWrite() {
    const QVariantMap data{{"version","0.1.40"},{"source","/private/movie.mkv"},{"profileDirectory","/private/profile"},{"lastError","secret error"},{"audioDevice","secret device"},{"audioDevices",QVariantList{"secret device"}},{"renderer","Software"}};
    const auto simple=Maintenance::report(data,{"secret message"},false);
    QVERIFY(!simple.contains("/private")); QVERIFY(!simple.contains("secret"));
    QVERIFY(simple.contains("recentMessageCount"));
    const auto details=Maintenance::report(data,{"secret message"},true);
    QVERIFY(details.contains("/private")); QVERIFY(details.contains("secret message"));
    QTemporaryDir dir; QString error;
    const auto file=dir.filePath("report.json");
    QVERIFY(Maintenance::saveReport(file,simple,&error));
    QFile f(file); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),simple); f.close();
    QVERIFY(Maintenance::saveReport(file,details,&error));
    QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),details); f.close();
    QVERIFY(QFile::link(file,dir.filePath("link")));
    QVERIFY(!Maintenance::saveReport(dir.filePath("link"),simple,&error));
    QVERIFY(!Maintenance::saveReport(dir.path(),simple,&error));
    const auto fifo=dir.filePath("fifo"); QVERIFY(::mkfifo(QFile::encodeName(fifo),0600)==0);
    QVERIFY(!Maintenance::saveReport(fifo,simple,&error));
    QVERIFY(!Maintenance::saveReport(dir.filePath("missing/report.json"),simple,&error));
    QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),details);
  }
  void englishDialogAndNoImplicitRequests() {
    QTemporaryDir dir; Profile p(dir.path());prepare(p);
    p.settings.setValue("appearance/language","en");
    Window w(p);w.show();w.maintenanceDialog();
    auto *dialog=w.findChild<QDialog *>("maintenanceDialog");QVERIFY(dialog);
    QCOMPARE(dialog->windowTitle(),QString("Maintenance and diagnostics"));
    QVERIFY(dialog->findChild<QLabel *>("maintenanceUpdateStatus")->text().contains("checked manually"));
    QVERIFY(dialog->findChild<QPushButton *>("maintenanceCheckUpdates")->isEnabled());
    QVERIFY(!dialog->findChild<QPushButton *>("maintenanceOpenRelease")->isEnabled());
    QVERIFY(!p.settings.contains("maintenance/priority"));
    QVERIFY(!p.settings.contains("maintenance/updatePreviews"));
    dialog->close();w.close();
    p.settings.setValue("appearance/language","ru");QVERIFY(I18n::load(p.settings,dir.path()));
  }
  void realWindowReportPreferencesAndPlayback() {
    QTemporaryDir dir; Profile p(dir.path()); prepare(p);
    const auto beforeNice=getpriority(PRIO_PROCESS,0);
    Window w(p); w.show();
    auto *player=w.findChild<Player *>(); QSignalSpy loaded(player,&Player::loaded);
    const auto file=QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv");
    QVERIFY(QFileInfo::exists(file)); w.openFiles({file});
    QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),10000);
    QTRY_VERIFY(!player->get("pause").toBool());
    QSignalSpy properties(player,&Player::property);
    QVERIFY(player->set("pause",true));
    const auto pauseSeen=[&] { for(const auto &event:properties) if(event[0].toString()=="pause" && event[1].toBool()) return true; return false; };
    QTRY_VERIFY(pauseSeen());
    QTRY_VERIFY(player->get("pause").toBool());
    QSignalSpy errors(player,&Player::error);
    player->command({"foxicmp-no-such-command"});
    QTRY_VERIFY(!errors.isEmpty());
    QVERIFY(!w.diagnosticMessages.isEmpty());
    QVERIFY(w.diagnosticMessages.join("\n").contains(errors.last()[0].toString()));
    const double position=player->get("time-pos").toDouble();
    w.findChild<QAction *>("action_maintenance")->trigger();
    auto *dialog=w.findChild<QDialog *>("maintenanceDialog"); QVERIFY(dialog);
    auto *report=dialog->findChild<QPlainTextEdit *>("maintenanceReport"); QVERIFY(report);
    QVERIFY(!report->toPlainText().contains(dir.path()));
    QVERIFY(report->toPlainText().contains("mpv-version"));
    QVERIFY(report->toPlainText().contains("video-codec"));
    auto *details=dialog->findChild<QCheckBox *>("maintenanceReportDetails"); details->setChecked(true);
    QVERIFY(report->toPlainText().contains(dir.path())); QVERIFY(report->toPlainText().contains("chapters.mkv"));
    for(int i=0;i<110;++i)player->diagnosticMessage(QString("test message %1").arg(i));
    dialog->findChild<QPushButton *>("maintenanceRefreshReport")->click();
    const auto object=QJsonDocument::fromJson(report->toPlainText().toUtf8()).object();
    QCOMPARE(object["recentMessageCount"].toInt(),100);
    QVERIFY(!report->toPlainText().contains("test message 0\""));
    QVERIFY(report->toPlainText().contains("test message 109"));
    dialog->findChild<QPushButton *>("maintenanceCopyReport")->click();
    QCOMPARE(QApplication::clipboard()->text(),report->toPlainText());
    auto *priority=dialog->findChild<QComboBox *>("maintenancePriority");
    priority->setCurrentIndex(priority->findData("low"));
    dialog->close(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    QVERIFY(!p.settings.contains("maintenance/priority"));
    w.maintenanceDialog();dialog=w.findChild<QDialog *>("maintenanceDialog");QVERIFY(dialog);
    priority=dialog->findChild<QComboBox *>("maintenancePriority");priority->setCurrentIndex(priority->findData("above"));
    dialog->findChild<QCheckBox *>("maintenancePreviews")->setChecked(false);
    dialog->findChild<QPushButton *>("maintenanceSavePreferences")->click();
    QCOMPARE(p.settings.value("maintenance/priority").toString(),QString("above"));
    QCOMPARE(p.settings.value("maintenance/updatePreviews").toBool(),false);
    QCOMPARE(getpriority(PRIO_PROCESS,0),beforeNice);
    QVERIFY(player->get("pause").toBool()); QVERIFY(std::abs(player->get("time-pos").toDouble()-position)<0.2);
    const auto shots=qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if(!shots.isEmpty()){QDir().mkpath(shots);QVERIFY(dialog->grab().save(shots+"/maintenance.png"));}
    dialog->close();w.close();
    QSettings saved(dir.filePath("FoxicMPQt.ini"),QSettings::IniFormat);
    QCOMPARE(saved.value("maintenance/priority").toString(),QString("above"));
  }
};
int main(int argc,char **argv) {
  if(qEnvironmentVariableIsSet("FOXICMP_PRIORITY_CHILD")) {
    const auto data=Maintenance::priority(qEnvironmentVariable("FOXICMP_PRIORITY_CHILD"),true);
    const auto bytes=QJsonDocument(QJsonObject::fromVariantMap(data)).toJson(QJsonDocument::Compact);
    fwrite(bytes.data(),1,bytes.size(),stdout);return 0;
  }
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc,argv);applyTheme(app);Q_INIT_RESOURCE(resources);
  if(app.arguments().contains("--check-github")) {
    UpdateCheck checker;QEventLoop loop;int code=1;
    QObject::connect(&checker,&UpdateCheck::finished,&loop,[&](const QString &status,const QString &version,const QUrl &page,const QString &error) {
      const auto bytes=QJsonDocument(QJsonObject{{"status",status},{"version",version},{"page",page.toString()},{"error",error}}).toJson();
      fwrite(bytes.data(),1,bytes.size(),stdout);code=status=="error"?1:0;loop.quit();
    });
    checker.start("0.1.40",true);loop.exec();return code;
  }
  MaintenanceTest test;return QTest::qExec(&test,argc,argv);
}
#include "maintenance_test.moc"
