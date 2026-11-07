#include <QtCore>
#include <QtTest>
#include <cstdio>

static bool sameValues(QSettings &s) {
  return s.value("untouched/value").toString()=="keep" &&
      s.value("probe/text").toString()==QString::fromUtf8("Профиль с пробелами") &&
      s.value("probe/bytes").toByteArray()==QByteArray("a\0b\xff",4) &&
      s.value("probe/iteration").toInt()==3;
}
class SettingsPathsTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    const auto actual=QDir::tempPath(),expected=qEnvironmentVariable("FOXICMP_EXPECTED_TEMP");
    QVERIFY2(!actual.contains(QChar::Null),"Temporary path contains an embedded NUL");
    qInfo()<<"Temporary path characters"<<actual.size();
    if(!expected.isEmpty())
      QVERIFY2(QDir::cleanPath(actual).toCaseFolded()==QDir::cleanPath(QDir::fromNativeSeparators(expected)).toCaseFolded(),"Temporary path does not match the explicitly supplied directory");
  }
  void repeatedAtomicSyncAndConcurrentMerge_data() {
    QTest::addColumn<int>("length");
    QTest::newRow("ordinary")<<120;
    QTest::newRow("long-320")<<320;
    QTest::newRow("long-1100")<<1100;
  }
  void repeatedAtomicSyncAndConcurrentMerge() {
    QFETCH(int,length);QTemporaryDir temporary;QVERIFY(temporary.isValid());
    auto root=temporary.path();
    while(root.size()<length)root=QDir(root).filePath(QString::fromUtf8("Профиль настроек xxxxxxxxxxxxxxxxxxxxxxxx"));
    QVERIFY(QDir().mkpath(root));const auto ini=QDir(root).filePath("profile.ini");
    {QFile file(ini);QVERIFY(file.open(QIODevice::WriteOnly));const QByteArray initial("[untouched]\nvalue=keep\n");QCOMPARE(file.write(initial),qint64(initial.size()));}
    {
      QSettings s(ini,QSettings::IniFormat);QVERIFY(s.isAtomicSyncRequired());
      for(int i=1;i<=3;++i){
        s.setValue("probe/text",QString::fromUtf8("Профиль с пробелами"));
        s.setValue("probe/bytes",QByteArray("a\0b\xff",4));s.setValue("probe/iteration",i);s.sync();
        QCOMPARE(s.status(),QSettings::NoError);
        // No other writer exists yet: an orphan here would block next sync.
        QVERIFY2(!QFileInfo::exists(ini+".lock"),qPrintable(ini));
      }
      QVERIFY(sameValues(s));
    }
    auto read=[&](bool merge){
      QProcess reader;reader.start(QCoreApplication::applicationFilePath(),{"--read",ini,merge?"merged":"plain"});
      const bool ended=reader.waitForFinished(7000);
      if(!ended){reader.kill();reader.waitForFinished();}
      return ended && reader.exitStatus()==QProcess::NormalExit && reader.exitCode()==0;
    };
    QVERIFY(read(false));
    QProcess left,right;
    left.start(QCoreApplication::applicationFilePath(),{"--merge",ini,"left"});
    right.start(QCoreApplication::applicationFilePath(),{"--merge",ini,"right"});
    const bool leftDone=left.waitForFinished(7000),rightDone=right.waitForFinished(7000);
    if(!leftDone){left.kill();left.waitForFinished();}if(!rightDone){right.kill();right.waitForFinished();}
    QVERIFY(leftDone && rightDone);QCOMPARE(left.exitCode(),0);QCOMPARE(right.exitCode(),0);
    QVERIFY(!QFileInfo::exists(ini+".lock"));QVERIFY(read(true));
    QFile file(ini);QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();QVERIFY(!bytes.isEmpty());
    qInfo()<<"INI path length"<<ini.size()<<"atomic sync, independent read and two-process merge passed; SHA256"<<QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
  }
};
int main(int argc,char**argv){
  QCoreApplication app(argc,argv);const auto args=app.arguments();
  if(args==QStringList{args[0],"--temp-info"}){
    const auto actual=QDir::tempPath();if(actual.contains(QChar::Null))return 6;
    const auto bytes=QJsonDocument(QJsonObject{{"actualTemp",QFileInfo(actual).canonicalFilePath()},{"length",actual.size()}}).toJson(QJsonDocument::Compact);
    std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);return 0;
  }
  if(args.size()==4 && (args[1]=="--read" || args[1]=="--merge")){
    QSettings s(args[2],QSettings::IniFormat);
    if(args[1]=="--merge"){
      for(int i=1;i<=3;++i){s.setValue("merge/"+args[3],i);s.sync();if(s.status()!=QSettings::NoError)return 3;}
      return 0;
    }
    if(!sameValues(s))return 4;
    if(args[3]=="merged" && (s.value("merge/left").toInt()!=3 || s.value("merge/right").toInt()!=3))return 5;
    return 0;
  }
  SettingsPathsTest test;return QTest::qExec(&test,argc,argv);
}
#include "settingspaths_test.moc"
