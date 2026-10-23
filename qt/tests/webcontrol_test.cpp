#include "webcontrol.h"
#include "window.h"
#include "player.h"
#include "theme.h"
#include "playlistpanel.h"
#include <sys/stat.h>
#include <memory>
#include "i18n.h"
#include <QtTest>
#include <QtWidgets>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QNetworkProxy>
class WebControlTest : public QObject {
  Q_OBJECT
  QByteArray request(WebControl &server,const QByteArray &method,const QByteArray &path,const QByteArray &body={},const QByteArray &extra={},bool auth=true) {
    const auto token=auth?"Authorization: Bearer "+server.token().toLatin1()+"\r\n":QByteArray();
    return method+" "+path+" HTTP/1.1\r\nHost: "+server.address().toString().toLatin1()+":"+QByteArray::number(server.port())+"\r\n"+token+extra+(method=="POST"?"Content-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n":QByteArray())+"\r\n"+body;
  }
  QByteArray exchange(WebControl &server,const QByteArray &bytes,bool fragmented=false) {
    QTcpSocket socket;socket.setProxy(QNetworkProxy::NoProxy);QByteArray response;QEventLoop loop;
    connect(&socket,&QTcpSocket::connected,&loop,[&] {
      if(fragmented) {socket.write(bytes.left(19));QTimer::singleShot(20,&socket,[&]{socket.write(bytes.mid(19));});}
      else socket.write(bytes);
    });
    connect(&socket,&QTcpSocket::readyRead,&loop,[&]{response+=socket.readAll();});
    connect(&socket,&QTcpSocket::disconnected,&loop,&QEventLoop::quit);
    QTimer::singleShot(5000,&loop,&QEventLoop::quit);
    socket.connectToHost(server.address(),server.port());loop.exec();response+=socket.readAll();return response;
  }
  QByteArray body(const QByteArray &response){return response.mid(response.indexOf("\r\n\r\n")+4);}
  int status(const QByteArray &response){return response.split(' ').value(1).toInt();}
  QByteArray post(WebControl &s,const QString &command,const QJsonValue &value=QJsonValue::Undefined) {
    QJsonObject obj{{"command",command}};if(!value.isUndefined())obj["value"]=value;
    return exchange(s,request(s,"POST","/api/command",QJsonDocument(obj).toJson(QJsonDocument::Compact)));
  }
  void prepare(Profile &p){p.settings.setValue("integration/prompted",true);p.settings.setValue("input/mpris",false);p.settings.setValue("audio/mute",true);p.settings.setValue("playback/hardwareDecoding",false);p.settings.setValue("video/output",qEnvironmentVariable("FOXICMP_TEST_OUTPUT","software"));}
private slots:
  void snapshotEndpointAuthenticationAndMime() {
    const QByteArray jpeg("fake-jpeg");
    WebControl server([](int){return QVariantMap{};},[](auto,auto){return true;},nullptr,[&]{return jpeg;});
    QVERIFY(server.start(0));
    QCOMPARE(status(exchange(server,request(server,"GET","/api/snapshot.jpg",{},{},false))),403);
    const auto response=exchange(server,request(server,"GET","/api/snapshot.jpg"));
    QCOMPARE(status(response),200);QVERIFY(response.contains("Content-Type: image/jpeg"));QCOMPARE(body(response),jpeg);
  }
  void endpointAuthenticationAssetsAndRotation() {
    int calls=0;WebControl s([](int){return QVariantMap{{"title","<script>test</script>"}};},[&](auto,auto){++calls;return true;});
    QVERIFY(!s.running());QVERIFY(s.start(0));QVERIFY(s.port()>0);QCOMPARE(s.token().size(),64);
    QVERIFY(s.url().startsWith("http://127.0.0.1:"));QVERIFY(s.url().contains('#'));
    QCOMPARE(status(exchange(s,request(s,"GET","/api/state",{}, {},false))),403);
    auto page=exchange(s,request(s,"GET","/",{}, {},false));QCOMPARE(status(page),200);QVERIFY(page.contains("Content-Security-Policy:"));QVERIFY(!page.contains(s.token().toLatin1()));
    QCOMPARE(status(exchange(s,request(s,"GET","/app.js",{}, {},false))),200);
    const auto js=body(exchange(s,request(s,"GET","/app.js",{}, {},false)));
    QVERIFY(js.contains("textContent"));QVERIFY(!js.contains("innerHTML"));QVERIFY(js.contains("history.replaceState"));QVERIFY(!js.contains("localStorage"));
    QCOMPARE(status(exchange(s,request(s,"GET","/app.css"))),200);
    const auto response=exchange(s,request(s,"GET","/api/state"),true);QCOMPARE(status(response),200);
    QCOMPARE(QJsonDocument::fromJson(body(response)).object()["title"].toString(),QString("<script>test</script>"));
    QCOMPARE(status(post(s,"play")),200);QCOMPARE(calls,1);
    const auto oldToken=s.token();const auto oldRequest=request(s,"GET","/api/state");const auto port=s.port();s.stop();QVERIFY(!s.running());QVERIFY(s.url().isEmpty());QVERIFY(s.token().isEmpty());
    QVERIFY(s.start(port));QVERIFY(s.token()!=oldToken);QCOMPARE(status(exchange(s,oldRequest)),403);
  }
  void addressPolicyAndExplicitBinding() {
    for (const auto &ip : {"10.0.0.1", "172.16.0.1", "172.31.255.254", "192.168.11.111", "169.254.2.3"})
      QVERIFY(WebControl::isPrivateAddress(QHostAddress(ip)));
    for (const auto &ip : {"0.0.0.0", "127.0.0.1", "172.15.0.1", "172.32.0.1", "192.168.0.0:3", "192.0.2.1", "8.8.8.8", "224.0.0.1", "::1", "::ffff:192.168.1.1"})
      QVERIFY(!WebControl::isPrivateAddress(QHostAddress(ip)));
    WebControl s([](int) {return QVariantMap{};}, [](auto,auto) {return true;});
    QVERIFY(s.start(0)); QVERIFY(!s.lan()); QCOMPARE(s.address(), QHostAddress(QHostAddress::LocalHost));
    const auto token = s.token(); const auto port = s.port();
    for (const auto &ip : {QHostAddress(QHostAddress::Any), QHostAddress(QHostAddress::AnyIPv4), QHostAddress("192.0.2.1"), QHostAddress("::1")}) {
      QVERIFY(!s.start(0, ip)); QVERIFY(s.running()); QCOMPARE(s.port(), port); QCOMPARE(s.token(), token);
    }
    for (const auto &entry : WebControl::lanAddresses()) QVERIFY(WebControl::isPrivateAddress(entry.address));
  }
  void lanAuthenticationHostOriginAndRotation() {
    const auto addresses = WebControl::lanAddresses();
    if (addresses.isEmpty()) QSKIP("No active private IPv4 interface for LAN integration test");
    const auto address = addresses.first().address;
    int calls = 0;
    WebControl s([](int) {return QVariantMap{{"status", "Paused"}};}, [&](auto,auto) {++calls;return true;});
    QVERIFY2(s.start(0, address), qPrintable(s.errorString())); QVERIFY(s.lan()); QCOMPARE(s.address(),address);
    QVERIFY(s.url().startsWith("http://"+address.toString()+":"));
    auto page = exchange(s,request(s,"GET","/",{},{},false));
    QCOMPARE(status(page),200); QVERIFY(!page.contains(s.token().toLatin1()));
    QCOMPARE(status(exchange(s,request(s,"GET","/api/state",{},{},false))),403);
    QCOMPARE(status(exchange(s,request(s,"POST","/api/command","{\"command\":\"play\"}",{ },false))),403);
    QCOMPARE(calls,0);
    const auto origin = "Origin: http://" + address.toString().toLatin1() + ":" + QByteArray::number(s.port()) + "\r\n";
    QCOMPARE(status(exchange(s,request(s,"POST","/api/command","{\"command\":\"play\"}",origin))),200); QCOMPARE(calls,1);
    auto badHost = request(s,"GET","/api/state"); badHost.replace(address.toString().toLatin1(), "evil.example");
    QCOMPARE(status(exchange(s,badHost)),403);
    auto localHost = request(s,"GET","/api/state"); localHost.replace(address.toString().toLatin1(), "localhost");
    QCOMPARE(status(exchange(s,localHost)),403);
    QCOMPARE(status(exchange(s,request(s,"GET","/api/state",{},"Origin: null\r\n"))),403);
    QCOMPARE(status(exchange(s,request(s,"GET","/api/state",{},"Origin: http://evil.example\r\n"))),403);
    const auto old = request(s,"GET","/api/state"); const auto port=s.port(); const auto token=s.token();
    QTcpSocket partial;partial.setProxy(QNetworkProxy::NoProxy);partial.connectToHost(address,port);
    QTRY_COMPARE(partial.state(),QAbstractSocket::ConnectedState);partial.write("G");QTest::qWait(20);
    s.stop();QTRY_COMPARE(partial.state(),QAbstractSocket::UnconnectedState);
    QVERIFY(s.start(port,address));QVERIFY(s.token()!=token);QCOMPARE(status(exchange(s,old)),403);
    QTcpSocket loopback;loopback.setProxy(QNetworkProxy::NoProxy);loopback.connectToHost(QHostAddress::LocalHost,port);
    QTRY_COMPARE(loopback.state(),QAbstractSocket::UnconnectedState);
  }
  void lanSettingsPersistenceRollbackAndTemporaryLocalPort() {
    const auto addresses = WebControl::lanAddresses();
    if (addresses.isEmpty()) QSKIP("No active private IPv4 interface for LAN settings test");
    const auto address = addresses.first().address;
    QTemporaryDir dir; quint16 port=0; QString firstToken;
    {
      Profile p(dir.path()); prepare(p); Window w(p);w.show();w.webControlDialog();
      auto *dialog=w.findChild<QDialog *>("webControlDialog");QVERIFY(dialog);
      auto *lan=dialog->findChild<QCheckBox *>("webLanEnabled");QVERIFY(lan);QVERIFY(!lan->isChecked());
      auto *combo=dialog->findChild<QComboBox *>("webLanAddress");QVERIFY(combo);
      QVERIFY(!combo->isEnabled());lan->setChecked(true);combo->setCurrentIndex(combo->findData(address.toString()));
      dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
      QVERIFY(!p.settings.contains("web/lanEnabled"));QVERIFY(!w.webControl->running());
      w.webControlDialog();dialog=w.findChild<QDialog *>("webControlDialog");
      lan=dialog->findChild<QCheckBox *>("webLanEnabled");combo=dialog->findChild<QComboBox *>("webLanAddress");
      lan->setChecked(true);combo->setCurrentIndex(combo->findData(address.toString()));
      QTcpServer free;QVERIFY(free.listen(address));port=free.serverPort();free.close();
      dialog->findChild<QSpinBox *>("webPort")->setValue(port);dialog->findChild<QPushButton *>("webStart")->click();
      QVERIFY(w.webControl->lan());QCOMPARE(w.webControl->address(),address);
      QVERIFY(p.settings.value("web/lanEnabled").toBool());QCOMPARE(p.settings.value("web/lanAddress").toString(),address.toString());
      const auto screenshots=qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      if(!screenshots.isEmpty()){QDir().mkpath(screenshots);QVERIFY(dialog->grab().save(screenshots+"/web-lan.png"));}
      firstToken=w.webControl->token();const auto url=w.webControl->url();auto *active=w.webControl;
      QTcpServer occupied;QVERIFY(occupied.listen(address));
      dialog->findChild<QSpinBox *>("webPort")->setValue(occupied.serverPort());dialog->findChild<QPushButton *>("webStart")->click();
      QCOMPARE(w.webControl,active);QCOMPARE(w.webControl->url(),url);QCOMPARE(p.settings.value("web/port").toInt(),int(port));
      QCOMPARE(status(exchange(*w.webControl,request(*w.webControl,"GET","/api/state"))),200);
      // A CLI override narrows the listener back to loopback without rewriting saved LAN settings.
      QVERIFY(w.setTemporaryWebPort(port));QVERIFY(!w.webControl->lan());QVERIFY(w.webControl->token()!=firstToken);
      QVERIFY(p.settings.value("web/lanEnabled").toBool());QCOMPARE(p.settings.value("web/lanAddress").toString(),address.toString());
      QVERIFY(!dialog->findChild<QCheckBox *>("webLanEnabled")->isChecked());
      dialog->close();w.close();
    }
    {
      Profile p(dir.path());prepare(p);Window w(p);w.show();
      QVERIFY(w.webControl->lan());QCOMPARE(w.webControl->address(),address);QCOMPARE(w.webControl->port(),port);
      QVERIFY(w.webControl->token()!=firstToken);
      w.webControlDialog();auto *dialog=w.findChild<QDialog *>("webControlDialog");
      dialog->findChild<QCheckBox *>("webLanEnabled")->setChecked(false);
      dialog->findChild<QPushButton *>("webStart")->click();
      QVERIFY(!w.webControl->lan());QVERIFY(!p.settings.value("web/lanEnabled").toBool());
      dialog->findChild<QPushButton *>("webStop")->click();QVERIFY(!w.webControl->running());
      QVERIFY(!p.settings.value("web/enabled").toBool());dialog->close();w.close();
    }
    QFile ini(dir.filePath("FoxicMPQt.ini"));QVERIFY(ini.open(QIODevice::ReadOnly));QVERIFY(!ini.readAll().contains(firstToken.toUtf8()));
  }
  void unavailableSavedAddressDoesNotFallbackOrExposeWildcard_data() {
    QTest::addColumn<QString>("saved");
    for (const auto &ip : {"192.0.2.1", "127.0.0.1", "0.0.0.0", "8.8.8.8", "::1"}) QTest::newRow(ip) << QString(ip);
    QSet<QString> available;
    for (const auto &entry : WebControl::lanAddresses()) available.insert(entry.address.toString());
    for (const auto &ip : {"10.255.255.254", "172.31.255.253", "192.168.254.252"}) {
      if (!available.contains(ip)) { QTest::newRow("unavailable-private-ip") << QString(ip); break; }
    }
  }
  void unavailableSavedAddressDoesNotFallbackOrExposeWildcard() {
    QFETCH(QString, saved);
    QTemporaryDir dir;Profile p(dir.path());prepare(p);p.settings.setValue("web/enabled",true);
    p.settings.setValue("web/lanEnabled",true);p.settings.setValue("web/lanAddress",saved);
    Window w(p);w.show();QVERIFY(!w.webControl->running());QVERIFY(!w.diagnosticMessages.isEmpty());
    w.webControlDialog();auto *dialog=w.findChild<QDialog *>("webControlDialog");
    QVERIFY(dialog->findChild<QCheckBox *>("webLanEnabled")->isChecked());
    QCOMPARE(dialog->findChild<QComboBox *>("webLanAddress")->currentData().toString(),saved);
    dialog->findChild<QPushButton *>("webStart")->click();QVERIFY(!w.webControl->running());
    QVERIFY(p.settings.value("web/lanEnabled").toBool());dialog->close();w.close();
  }
  void strictHttpAndOriginRules() {
    int calls=0;WebControl s([](int){return QVariantMap{};},[&](auto,auto){++calls;return true;});QVERIFY(s.start(0));
    auto valid=request(s,"POST","/api/command","{\"command\":\"play\"}");
    auto badHost=valid;badHost.replace("Host: 127.0.0.1:","Host: evil.example:");QCOMPARE(status(exchange(s,badHost)),403);
    QCOMPARE(status(exchange(s,request(s,"POST","/api/command","{\"command\":\"play\"}","Origin: https://evil.example\r\n"))),403);
    QCOMPARE(status(exchange(s,request(s,"POST","/api/command","{\"command\":\"play\"}","Origin: null\r\n"))),403);
    const auto same="Origin: http://127.0.0.1:"+QByteArray::number(s.port())+"\r\n";
    QCOMPARE(status(exchange(s,request(s,"POST","/api/command","{\"command\":\"play\"}",same))),200);QCOMPARE(calls,1);
    for(const auto &extra: {QByteArray("Host: localhost\r\n"),QByteArray("Transfer-Encoding: chunked\r\n"),QByteArray("Expect: 100-continue\r\n"),QByteArray("Content-Length: 0\r\n"),QByteArray(" bad: folded\r\n")})
      QCOMPARE(status(exchange(s,request(s,"POST","/api/command","{\"command\":\"play\"}",extra))),400);
    auto missingAuth=valid;missingAuth.replace("Bearer "+s.token().toLatin1(),"Bearer wrong");QCOMPARE(status(exchange(s,missingAuth)),403);
    QCOMPARE(status(exchange(s,request(s,"GET","/api/command"))),405);
    QCOMPARE(status(exchange(s,request(s,"GET","/api/state?token="+s.token().toLatin1()))),400);
    QCOMPARE(status(exchange(s,request(s,"GET","/../../FoxicMPQt.ini"))),404);
    auto badType=valid;badType.replace("application/json","text/plain");QCOMPARE(status(exchange(s,badType)),400);
    QCOMPARE(status(exchange(s,valid+valid)),400);
    QCOMPARE(calls,1);
  }
  void commandAllowlistAndBounds() {
    int calls=0;QString received;QVariant data;bool accept=true;
    WebControl s([](int){return QVariantMap{};},[&](auto cmd,auto value){++calls;received=cmd;data=value;return accept;});QVERIFY(s.start(0));
    for(const QString &command:{"play","pause","toggle","stop","next","previous","mute"}) {QCOMPARE(status(post(s,command)),200);QCOMPARE(received,command);}
    for(const QString &command:{"Quit","OpenUri","Command","delete","settings","loadfile"})QCOMPARE(status(post(s,command)),400);
    QCOMPARE(status(post(s,"volume",100)),200);QCOMPARE(data.toDouble(),100.);
    QCOMPARE(status(post(s,"volume",-1)),400);QCOMPARE(status(post(s,"volume",101)),400);
    QCOMPARE(status(post(s,"rate",.125)),200);QCOMPARE(status(post(s,"rate",16)),200);
    QCOMPARE(status(post(s,"rate",0)),400);QCOMPARE(status(post(s,"rate",17)),400);
    QCOMPARE(status(post(s,"seek",0)),200);QCOMPARE(status(post(s,"seek",604801)),400);
    QCOMPARE(status(post(s,"seek","1;rm")),400);QCOMPARE(status(post(s,"play",1)),400);
    QCOMPARE(status(exchange(s,request(s,"POST","/api/command","{\"command\":\"play\",\"extra\":1}"))),400);
    QCOMPARE(status(exchange(s,request(s,"POST","/api/command","[]"))),400);
    accept=false;QCOMPARE(status(post(s,"play")),409);
  }
  void requestSizeSlowClientsAndStop() {
    WebControl s([](int){return QVariantMap{};},[](auto,auto){return true;});QVERIFY(s.start(0));
    QCOMPARE(status(exchange(s,request(s,"POST","/api/command",QByteArray(17000,'x')))),413);
    QTcpSocket slow;slow.connectToHost(QHostAddress::LocalHost,s.port());QTRY_COMPARE(slow.state(),QAbstractSocket::ConnectedState);slow.write("GET / HTTP/1.1\r\n");
    QTRY_COMPARE_WITH_TIMEOUT(slow.state(),QAbstractSocket::UnconnectedState,5000);
    QTcpSocket partial;partial.connectToHost(QHostAddress::LocalHost,s.port());QTRY_COMPARE(partial.state(),QAbstractSocket::ConnectedState);partial.write("G");QTest::qWait(20);s.stop();QTRY_COMPARE(partial.state(),QAbstractSocket::UnconnectedState);
    QVERIFY(s.start(0));WebControl conflict([](int){return QVariantMap{};},[](auto,auto){return true;});QVERIFY(!conflict.start(s.port()));QVERIFY(!conflict.errorString().isEmpty());
    QTcpSocket probe;probe.connectToHost(QHostAddress::LocalHost,s.port());QTRY_COMPARE(probe.state(),QAbstractSocket::ConnectedState);QCOMPARE(probe.peerAddress(),QHostAddress(QHostAddress::LocalHost));
  }
  void realPlaybackAndNoUnauthenticatedChanges() {
    QTemporaryDir dir;Profile p(dir.path());prepare(p);Window w(p);w.show();auto *s=w.webControl;QVERIFY(s);QVERIFY(!s->running());
    auto *player=w.findChild<Player *>();QSignalSpy loaded(player,&Player::loaded);
    const auto media=QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv");w.openFiles({media,media});QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),10000);
    QVERIFY(s->start(0));QCOMPARE(status(post(*s,"pause")),200);QTRY_VERIFY(player->get("pause").toBool());
    const auto state=QJsonDocument::fromJson(body(exchange(*s,request(*s,"GET","/api/state")))).object();QCOMPARE(state["status"].toString(),QString("Paused"));QCOMPARE(state["queueTotal"].toInt(),2);QVERIFY(!body(exchange(*s,request(*s,"GET","/api/state"))).contains(dir.path().toUtf8()));
    QCOMPARE(status(exchange(*s,request(*s,"POST","/api/command","{\"command\":\"play\"}",{},false))),403);QVERIFY(player->get("pause").toBool());
    QCOMPARE(status(post(*s,"seek",3)),200);QTRY_VERIFY(std::abs(player->get("time-pos").toDouble()-3)<.2);
    QCOMPARE(status(post(*s,"volume",42)),200);QTRY_COMPARE(qRound(player->get("volume").toDouble()),42);QTRY_VERIFY(!player->get("mute").toBool());
    QCOMPARE(status(post(*s,"mute")),200);QTRY_VERIFY(player->get("mute").toBool());
    QCOMPARE(status(post(*s,"rate",1.5)),200);QTRY_COMPARE(player->get("speed").toDouble(),1.5);
    QCOMPARE(status(post(*s,"next")),200);QTRY_VERIFY(loaded.size()>=2);QTRY_VERIFY(player->get("pause").toBool());
    QCOMPARE(status(post(*s,"previous")),200);QTRY_VERIFY(loaded.size()>=3);
    QCOMPARE(status(post(*s,"play")),200);QTRY_VERIFY(!player->get("pause").toBool());
    QCOMPARE(status(post(*s,"stop")),200);QTRY_COMPARE(w.webState().value("status").toString(),QString("Stopped"));
    QCOMPARE(status(post(*s,"seek",1)),409);QCOMPARE(status(post(*s,"next")),409);
    w.close();
  }
  void pagingAndNewCommandValidation() {
    int calls=0;QString received;QVariant data;
    WebControl server([](int offset){return QVariantMap{{"offset",offset}};},[&](auto command,auto value){++calls;received=command;data=value;return true;});
    QVERIFY(server.start(0));
    auto page=exchange(server,request(server,"GET","/api/state?offset=500"));
    QCOMPARE(status(page),200);QCOMPARE(QJsonDocument::fromJson(body(page)).object()["offset"].toInt(),500);
    QCOMPARE(status(exchange(server,request(server,"GET","/api/state?offset=500",{},{},false))),403);
    for(const auto &query:{"", "offset=-1", "offset=1.5", "offset=10000000", "offset=0&offset=1", "offset=0&other=1", "other=0"})
      QCOMPARE(status(exchange(server,request(server,"GET",QByteArray("/api/state?")+query))),400);
    const QJsonObject target{{"list","list-1"},{"id","entry-1"}};
    for(const auto &command:{"queuePlay","queueRemove"}) {
      QCOMPARE(status(post(server,command,target)),200);QCOMPARE(received,QString(command));
      for(const auto &value:{QJsonValue(1),QJsonValue("entry-1"),QJsonValue(QJsonObject{{"list","list-1"}}),QJsonValue(QJsonObject{{"list","list-1"},{"id",""}}),QJsonValue(QJsonObject{{"list","list-1"},{"id","entry-1"},{"path","/tmp"}})})
        QCOMPARE(status(post(server,command,value)),400);
    }
    for(const auto &command:{"open","enqueue"}) {
      QCOMPARE(status(post(server,command,"/tmp/a video.mkv")),200);QCOMPARE(received,QString(command));
      for(const auto &value:{QJsonValue(1),QJsonValue(""),QJsonValue("   "),QJsonValue("/tmp/a\nfile"),QJsonValue(QString(4097,'x'))})
        QCOMPARE(status(post(server,command,value)),400);
      const auto count=calls;
      const auto payload=QJsonDocument(QJsonObject{{"command",command},{"value","/tmp/video.mkv"}}).toJson(QJsonDocument::Compact);
      QCOMPARE(status(exchange(server,request(server,"POST","/api/command",payload,{},false))),403);QCOMPARE(calls,count);
    }
  }
  void queueStableIdsRemovalAndOtherTabIsolation() {
    QTemporaryDir dir;Profile p(dir.path());prepare(p);Window w(p);w.show();
    const auto media=QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv");
    auto *player=w.findChild<Player *>();QSignalSpy loaded(player,&Player::loaded);
    w.openFiles({media,media,media});QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),10000);player->set("pause",true);QTRY_VERIFY(player->get("pause").toBool());
    auto *panel=w.findChild<PlaylistPanel *>();auto *list=panel->activeList();auto *server=w.webControl;QVERIFY(server->start(0));
    const auto state=w.webState();const auto queue=state["queue"].toList();QCOMPARE(queue.size(),3);
    auto target=[&](int row){return QJsonObject{{"list",state["queueList"].toString()},{"id",queue[row].toMap()["id"].toString()}};};
    QVERIFY(target(0)["id"]!=target(1)["id"]);QVERIFY(target(1)["id"]!=target(2)["id"]);
    auto *moved=list->takeItem(2);list->insertItem(0,moved);
    auto before=loaded.size();QCOMPARE(status(post(*server,"queuePlay",target(2))),200);QTRY_VERIFY(loaded.size()>before);
    QTRY_VERIFY(player->get("pause").toBool());QVERIFY(w.webState()["queue"].toList()[0].toMap()["current"].toBool());
    auto *other=panel->createList("Other tab");panel->append(other,{media});QCOMPARE(panel->activeList(),other);
    QCOMPARE(status(post(*server,"queueRemove",target(0))),200);QCOMPARE(list->count(),2);QCOMPARE(other->count(),1);
    QVERIFY(player->get("pause").toBool());QVERIFY(QFileInfo::exists(media));
    QCOMPARE(status(post(*server,"queuePlay",QJsonObject{{"list",other->property("tabId").toString()},{"id",target(2)["id"]}})),409);
    QCOMPARE(status(post(*server,"queueRemove",target(2))),200);QCOMPARE(list->count(),1);
    QTRY_COMPARE(w.webState()["status"].toString(),QString("Stopped"));
    QCOMPARE(status(post(*server,"queueRemove",target(2))),409);QCOMPARE(status(post(*server,"queuePlay",target(2))),409);
    QVERIFY(QFileInfo::exists(media));w.close();
  }
  void openEnqueueValidationAndPlayingTab() {
    QTemporaryDir dir;Profile p(dir.path());prepare(p);Window w(p);w.show();
    const auto media=QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv");
    auto *player=w.findChild<Player *>();QSignalSpy loaded(player,&Player::loaded);
    w.openFiles({media});QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),10000);player->set("pause",true);QTRY_VERIFY(player->get("pause").toBool());
    auto *panel=w.findChild<PlaylistPanel *>();auto *list=panel->activeList();const auto initial=w.webState();
    auto *other=panel->createList("Other tab");panel->append(other,{media});
    auto *server=w.webControl;QVERIFY(server->start(0));auto count=loaded.size();
    QCOMPARE(status(post(*server,"enqueue",QUrl::fromLocalFile(media).toString())),200);
    QCOMPARE(list->count(),2);QCOMPARE(other->count(),1);QCOMPARE(loaded.size(),count);QVERIFY(player->get("pause").toBool());
    const auto stable=w.webState()["queue"].toList()[0].toMap()["id"].toString();
    const auto fifo=dir.filePath("media.fifo");QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(),0600),0);
    const auto localPlaylist=dir.filePath("test.m3u8");QFile file(localPlaylist);QVERIFY(file.open(QIODevice::WriteOnly));file.write(("#EXTM3U\n"+media+"\n").toUtf8());file.close();
    for(const auto &source:QStringList{"relative.mkv",dir.filePath("absent.mkv"),dir.path(),"/dev/null",fifo,localPlaylist,"file://evil.example/tmp/video.mkv","fd://0","lavfi://testsrc","javascript:alert(1)","--player-operation"}) {
      QCOMPARE(status(post(*server,"open",source)),409);QCOMPARE(status(post(*server,"enqueue",source)),409);
      QCOMPARE(list->count(),2);QCOMPARE(w.webState()["queue"].toList()[0].toMap()["id"].toString(),stable);
    }
    QCOMPARE(status(post(*server,"open",media)),200);QTRY_VERIFY(loaded.size()>count);QCOMPARE(list->count(),1);QCOMPARE(other->count(),1);
    QCOMPARE(w.webState()["queueList"],initial["queueList"]);QCOMPARE(panel->activeList(),other);
    QCOMPARE(status(post(*server,"queuePlay",QJsonObject{{"list",initial["queueList"].toString()},{"id",stable}})),409);
    w.close();
  }
  void pagedQueueCoversAllEntriesAndClampsAfterRemoval() {
    QTemporaryDir dir;Profile p(dir.path());prepare(p);Window w(p);w.show();
    const auto media=QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv");
    auto *panel=w.findChild<PlaylistPanel *>();QStringList sources;for(int i=0;i<507;++i)sources.append(media);panel->append(panel->activeList(),sources,false);
    auto *server=w.webControl;QVERIFY(server->start(0));
    const auto firstResponse=exchange(*server,request(*server,"GET","/api/state"));QCOMPARE(status(firstResponse),200);
    const auto first=QJsonDocument::fromJson(body(firstResponse)).object();QCOMPARE(first["queue"].toArray().size(),500);QCOMPARE(first["queueTotal"].toInt(),507);
    const auto secondResponse=exchange(*server,request(*server,"GET","/api/state?offset=500"));QCOMPARE(status(secondResponse),200);
    const auto second=QJsonDocument::fromJson(body(secondResponse)).object();const auto tail=second["queue"].toArray();QCOMPARE(tail.size(),7);QCOMPARE(second["queueOffset"].toInt(),500);
    QSet<QString> ids;for(const auto &entry:first["queue"].toArray())ids.insert(entry.toObject()["id"].toString());
    for(const auto &entry:tail){const auto id=entry.toObject()["id"].toString();QVERIFY(!ids.contains(id));ids.insert(id);}
    QCOMPARE(ids.size(),507);
    for(const auto &entry:tail)QCOMPARE(status(post(*server,"queueRemove",QJsonObject{{"list",second["queueList"]},{"id",entry.toObject()["id"]}})),200);
    const auto clamped=exchange(*server,request(*server,"GET","/api/state?offset=9999999"));QCOMPARE(status(clamped),200);
    const auto result=QJsonDocument::fromJson(body(clamped)).object();QCOMPARE(result["queueOffset"].toInt(),0);QCOMPARE(result["queueTotal"].toInt(),500);
    w.close();
  }
  void stdinQueueGuardsAndExplicitOpen() {
    QTemporaryDir dir;Profile p(dir.path());prepare(p);Window w(p);w.show();
    const auto media=QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv");
    w.addFiles({media});const auto initial=w.webState();const auto id=initial["queue"].toList().first().toMap()["id"].toString();
    auto *player=w.findChild<Player *>();QSignalSpy loaded(player,&Player::loaded);
    QFile input(media);QVERIFY(input.open(QIODevice::ReadOnly));QVERIFY(w.openStdin(input.handle()));input.close();
    QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),10000);player->set("pause",true);QTRY_VERIFY(player->get("pause").toBool());
    auto *server=w.webControl;QVERIFY(server->start(0));
    QVERIFY(!w.webState()["canEditQueue"].toBool());QVERIFY(!w.webState()["canEnqueue"].toBool());
    const QJsonObject target{{"list",initial["queueList"].toString()},{"id",id}};
    QCOMPARE(status(post(*server,"queuePlay",target)),409);QCOMPARE(status(post(*server,"queueRemove",target)),409);
    QCOMPARE(status(post(*server,"enqueue",media)),409);QCOMPARE(w.webState()["queueTotal"].toInt(),1);
    const auto count=loaded.size();QCOMPARE(status(post(*server,"open",media)),200);QTRY_VERIFY(loaded.size()>count);
    QVERIFY(w.webState()["canEditQueue"].toBool());QTRY_VERIFY(w.webState()["canSeek"].toBool());w.close();
  }
  void realHttpSourceOpenAndEnqueue() {
    QFile fixture(QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv"));QVERIFY(fixture.open(QIODevice::ReadOnly));const auto bytes=fixture.readAll();
    QTcpServer http;QVERIFY(http.listen(QHostAddress::LocalHost));
    connect(&http,&QTcpServer::newConnection,&http,[&]{while(http.hasPendingConnections()){
      auto *socket=http.nextPendingConnection();auto request=std::make_shared<QByteArray>();
      connect(socket,&QTcpSocket::readyRead,socket,[&,socket,request]{request->append(socket->readAll());if(!request->contains("\r\n\r\n")||socket->property("sent").toBool())return;socket->setProperty("sent",true);socket->write("HTTP/1.1 200 OK\r\nContent-Type: video/x-matroska\r\nContent-Length: "+QByteArray::number(bytes.size())+"\r\nConnection: close\r\n\r\n"+bytes);socket->disconnectFromHost();});
      connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    }});
    QTemporaryDir dir;Profile p(dir.path());prepare(p);Window w(p);w.show();auto *player=w.findChild<Player *>();QSignalSpy loaded(player,&Player::loaded);auto *server=w.webControl;QVERIFY(server->start(0));
    const auto url=QString("http://127.0.0.1:%1/real-video.mkv").arg(http.serverPort());
    QCOMPARE(status(post(*server,"enqueue",url)),200);QCOMPARE(w.webState()["queueTotal"].toInt(),1);QCOMPARE(loaded.size(),0);
    QCOMPARE(status(post(*server,"open",url)),200);QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),10000);
    QTRY_VERIFY(w.webState()["duration"].toDouble()>0);QCOMPARE(w.webState()["queueTotal"].toInt(),1);w.close();
  }
  void settingsPersistenceConflictAndScreenshots() {
    QTemporaryDir dir;quint16 port=0;QString token;
    {Profile p(dir.path());prepare(p);Window w(p);w.show();w.webControlDialog();auto *dialog=w.findChild<QDialog *>("webControlDialog");QVERIFY(dialog);
      QVERIFY(!w.webControl->running());QVERIFY(!p.settings.contains("web/enabled"));
      QTcpServer freePort;QVERIFY(freePort.listen(QHostAddress::LocalHost));port=freePort.serverPort();freePort.close();
      auto *spin=dialog->findChild<QSpinBox *>("webPort");spin->setValue(port);
      dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!p.settings.contains("web/port"));
      w.webControlDialog();dialog=w.findChild<QDialog *>("webControlDialog");dialog->findChild<QSpinBox *>("webPort")->setValue(port);dialog->findChild<QPushButton *>("webStart")->click();QVERIFY(w.webControl->running());QCOMPARE(p.settings.value("web/port").toInt(),int(port));QVERIFY(p.settings.value("web/enabled").toBool());token=w.webControl->token();
      dialog->findChild<QPushButton *>("webCopy")->click();QCOMPARE(QApplication::clipboard()->text(),w.webControl->url());
      const auto screenshots=qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");if(!screenshots.isEmpty()){QDir().mkpath(screenshots);QVERIFY(dialog->grab().save(screenshots+"/web-control.png"));}
      // Another server cannot displace a running one.
      WebControl conflict([](int){return QVariantMap{};},[](auto,auto){return true;});QVERIFY(!conflict.start(port));
      dialog->close();w.close();
    }
    {Profile p(dir.path());prepare(p);Window w(p);w.show();QVERIFY(w.webControl->running());QCOMPARE(w.webControl->port(),port);QVERIFY(w.webControl->token()!=token);w.webControlDialog();auto *dialog=w.findChild<QDialog *>("webControlDialog");dialog->findChild<QPushButton *>("webStop")->click();QVERIFY(!w.webControl->running());QVERIFY(!p.settings.value("web/enabled").toBool());dialog->close();w.close();}
    QSettings ini(dir.filePath("FoxicMPQt.ini"),QSettings::IniFormat);QVERIFY(!ini.allKeys().join(" ").contains("token"));QVERIFY(!QFile(dir.filePath("FoxicMPQt.ini")).exists()||!ini.value("web/token").isValid());
  }
  void englishDialogAndInvalidStartupPort() {
    QTemporaryDir dir;Profile p(dir.path());prepare(p);p.settings.setValue("appearance/language","en");p.settings.setValue("web/enabled",true);p.settings.setValue("web/port",1);
    Window w(p);w.show();QVERIFY(!w.webControl->running());QVERIFY(!w.diagnosticMessages.isEmpty());w.webControlDialog();auto *dialog=w.findChild<QDialog *>("webControlDialog");QCOMPARE(dialog->windowTitle(),QString("FoxicMP Qt web control"));QVERIFY(!dialog->findChild<QPushButton *>("webOpen")->isEnabled());dialog->close();w.close();p.settings.setValue("appearance/language","ru");I18n::load(p.settings,dir.path());
  }
};
int main(int argc,char **argv){QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QApplication app(argc,argv);applyTheme(app);Q_INIT_RESOURCE(resources);
if(app.arguments().contains("--serve-web") || app.arguments().contains("--serve-lan")) {
 QTemporaryDir dir;Profile profile(dir.path());profile.settings.setValue("integration/prompted",true);profile.settings.setValue("input/mpris",false);profile.settings.setValue("audio/mute",true);profile.settings.setValue("video/output","software");
 Window w(profile);w.show();auto *player=w.findChild<Player *>();
 QObject::connect(player,&Player::loaded,&w,[&]{player->set("pause",true);});
 w.openFiles({QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv")});
 const auto address = app.arguments().contains("--serve-lan")
     ? QHostAddress(qEnvironmentVariable("FOXICMP_WEB_TEST_ADDRESS")) : QHostAddress(QHostAddress::LocalHost);
 if(!w.webControl->start(0,address))return 1;
 QFile url(qEnvironmentVariable("FOXICMP_WEB_TEST_URL"));if(!url.open(QIODevice::WriteOnly))return 2;url.write(w.webControl->url().toUtf8());url.close();
 QTimer::singleShot(1200000,&app,&QApplication::quit);return app.exec();
}
WebControlTest test;return QTest::qExec(&test,argc,argv);}
#include "webcontrol_test.moc"
