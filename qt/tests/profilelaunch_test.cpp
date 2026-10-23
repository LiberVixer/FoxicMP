#include "profile.h"
#include "profilelaunch.h"
#include "profileoptions.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <sys/stat.h>

class ProfileLaunchTest : public QObject {
  Q_OBJECT
  void write(const QString &path, const QByteArray &bytes) {
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes), bytes.size());
  }
  QByteArray read(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
private slots:
  void portableDefaultAndExternalChoice() {
    QTemporaryDir program, other;
    QString path, error;
    QVERIFY(ProfileLaunch::resolve(program.path(), &path, &error));
    QCOMPARE(path, program.path());
    QVERIFY(!QFileInfo::exists(ProfileLaunch::configPath(program.path())));
    QVERIFY(ProfileLaunch::select(program.path(), other.path(), &error));
    QVERIFY(ProfileLaunch::resolve(program.path(), &path, &error));
    QCOMPARE(path, other.path());
    QVERIFY(!QFileInfo::exists(other.filePath("FoxicMPQt.ini")));
    QVERIFY(ProfileLaunch::select(program.path(), program.path(), &error));
    QCOMPARE(QJsonDocument::fromJson(read(ProfileLaunch::configPath(program.path()))).object()["directory"].toString(), QString("."));
    QVERIFY(ProfileLaunch::resolve(program.path(), &path, &error));
    QCOMPARE(path, program.path());
  }
  void relativeChoiceSurvivesProgramMove() {
    QTemporaryDir root;
    const auto old = root.filePath("original"), moved = root.filePath("moved");
    const auto profile = old + "/Profiles/Русский $%`\"\\";
    QVERIFY(QDir().mkpath(profile));
    QString error, resolved;
    QVERIFY(ProfileLaunch::select(old, profile, &error));
    auto object = QJsonDocument::fromJson(read(ProfileLaunch::configPath(old))).object();
    QVERIFY(!QDir::isAbsolutePath(object["directory"].toString()));
    QVERIFY(QDir().rename(old, moved));
    QVERIFY(ProfileLaunch::resolve(moved, &resolved, &error));
    QCOMPARE(resolved, moved + "/Profiles/Русский $%`\"\\");
  }
  void rejectsInvalidChoice_data() {
    QTest::addColumn<QString>("kind");
    for (const auto &kind : {"broken", "oversize", "version", "unknown", "empty", "missing", "control", "link", "fifo"})
      QTest::newRow(kind) << QString(kind);
  }
  void rejectsInvalidChoice() {
    QFETCH(QString, kind);
    QTemporaryDir root;
    const auto config = ProfileLaunch::configPath(root.path());
    QJsonObject value{{"version",1},{"directory","."}};
    if (kind == "version") value["version"] = 2;
    if (kind == "unknown") value["extra"] = true;
    if (kind == "empty") value["directory"] = "";
    if (kind == "missing") value["directory"] = "not-there";
    if (kind == "control") value["directory"] = "bad\npath";
    const auto bytes = QJsonDocument(value).toJson();
    if (kind == "link") {
      write(root.filePath("outside"), bytes);
      QVERIFY(QFile::link(root.filePath("outside"), config));
    } else if (kind == "fifo") QCOMPARE(::mkfifo(QFile::encodeName(config).constData(),0600), 0);
    else write(config, kind == "broken" ? QByteArray("bad") : kind == "oversize" ? QByteArray(16385,' ') : bytes);
    QString resolved, error;
    QVERIFY(!ProfileLaunch::resolve(root.path(), &resolved, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(root.filePath("not-there")));
  }
  void failedSelectionPreservesConfigAndTarget() {
    QTemporaryDir root, other;
    QString error;
    QVERIFY(ProfileLaunch::select(root.path(), other.path(), &error));
    const auto old = read(ProfileLaunch::configPath(root.path()));
    QVERIFY(!ProfileLaunch::select(root.path(), other.filePath("absent"), &error));
    QCOMPARE(read(ProfileLaunch::configPath(root.path())),old);
    write(other.filePath("FoxicMPQt.ini"), "[profile]\nversion=1\n");
    const auto ini = read(other.filePath("FoxicMPQt.ini"));
    QVERIFY(QFile::remove(ProfileLaunch::configPath(root.path())));
    QVERIFY(QFile::link(other.filePath("FoxicMPQt.ini"), ProfileLaunch::configPath(root.path())));
    QVERIFY(!ProfileLaunch::select(root.path(), other.path(), &error));
    QCOMPARE(read(other.filePath("FoxicMPQt.ini")), ini);
  }
  void missingSelectedProfileDoesNotRecreateIt() {
    QTemporaryDir root;
    const auto target = root.filePath("selected"); QVERIFY(QDir().mkdir(target));
    QString error, resolved;
    QVERIFY(ProfileLaunch::select(root.path(), target, &error));
    QVERIFY(QDir(target).removeRecursively());
    QVERIFY(!ProfileLaunch::resolve(root.path(), &resolved, &error));
    QVERIFY(!QFileInfo::exists(target));
  }
  void shortcutEscapesPathsAndDoesNotOverwrite() {
    QTemporaryDir root;
    const auto program = root.filePath("Русский $cash%\"`player");
    const auto profile = root.filePath("Профиль $%\"`\\");
    QVERIFY(QDir().mkpath(profile));
    write(program, "#!/usr/bin/python3\nimport os,sys,json\nopen(os.environ['FOXICMP_ARG_LOG'],'w').write(json.dumps(sys.argv[1:]))\n");
    QVERIFY(QFile::setPermissions(program,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
    const auto record = root.filePath("args.json"), entry = root.filePath("profile.desktop");
    qputenv("FOXICMP_ARG_LOG",record.toUtf8());
    QString error;
    QVERIFY2(ProfileLaunch::launcher(program, profile, entry, &error),qPrintable(error));
    const auto before = read(entry);
    QProcess validate; validate.start("desktop-file-validate",{entry});
    QVERIFY(validate.waitForFinished()); QCOMPARE(validate.exitCode(),0);
    const auto media = root.filePath("-Русский $ % '\".mp4"); write(media,{});
    QProcess gio; gio.start("gio",{"launch",entry,media});
    QVERIFY(gio.waitForFinished()); QCOMPARE(gio.exitCode(),0);
    QTRY_VERIFY(QFileInfo::exists(record));
    QCOMPARE(QJsonDocument::fromJson(read(record)).array(),QJsonArray({"--profile",profile,"--",media}));
    QVERIFY(!ProfileLaunch::launcher(program, root.path(), entry, &error));
    QCOMPARE(read(entry),before);
    QVERIFY(!ProfileLaunch::launcher(program, profile, root.filePath("bad.txt"), &error));
    QVERIFY(QFile::link(entry,root.filePath("link.desktop")));
    QVERIFY(!ProfileLaunch::launcher(program, profile, root.filePath("link.desktop"), &error));
    QVERIFY(!QFileInfo::exists(QDir(qEnvironmentVariable("XDG_DATA_HOME")).filePath("applications/foxicmp-qt.desktop")));
  }
  void dialogChangesOnlyLaunchSelectionAndScreenshots() {
    QTemporaryDir current, program, selected;
    Profile profile(current.path());
    profile.settings.sync(); const auto ini = read(profile.settings.fileName());
    QWidget parent;
    QTimer::singleShot(40,&parent,[&] {
      auto *dialog=parent.findChild<QDialog *>("profileManagement"); QVERIFY(dialog);
      auto close=qScopeGuard([&]{dialog->reject();});
      auto *edit=dialog->findChild<QLineEdit *>("profileLaunchDirectory"); QVERIFY(edit);
      edit->setText(selected.path());
      dialog->findChild<QPushButton *>("profileLaunchUse")->click();
      QString error,path;
      QVERIFY(ProfileLaunch::resolve(program.path(),&path,&error)); QCOMPARE(path,selected.path());
      QCOMPARE(read(profile.settings.fileName()),ini);
      const auto images=qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      if (!images.isEmpty()) {QDir().mkpath(images);QVERIFY(dialog->grab().save(QDir(images).filePath("profile-launch.png")));}
      dialog->findChild<QPushButton *>("profileLaunchPortable")->click();
      QVERIFY(ProfileLaunch::resolve(program.path(),&path,&error)); QCOMPARE(path,program.path());
      QCOMPARE(read(profile.settings.fileName()),ini);
    });
    showProfileManagement(profile,&parent,[]{},{},program.path());
  }
  void actualPlayerStartupSelectionOverridesAndQueries() {
    QTemporaryDir program, selected, explicitProfile;
    const auto exe = program.filePath("foxicmp-qt");
    QVERIFY(QFile::copy(QDir(QCoreApplication::applicationDirPath()).filePath("foxicmp-qt"),exe));
    QString error;
    QVERIFY(ProfileLaunch::select(program.path(),selected.path(),&error));
    auto query=[&](QStringList args) {
      QProcess child;child.start(exe,args);QVERIFY(child.waitForFinished(5000));QCOMPARE(child.exitCode(),0);
    };
    query({"--version"});query({"--help"});
    QVERIFY(!QFileInfo::exists(selected.filePath("FoxicMPQt.ini")));
    const auto media = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath("chapters.mkv");
    auto play=[&](const QString &directory,const QStringList &flags) {
      {
        QSettings ini(QDir(directory).filePath("FoxicMPQt.ini"),QSettings::IniFormat);
        ini.setValue("integration/prompted",true);ini.setValue("input/mpris",false);
        ini.setValue("audio/mute",true);ini.setValue("playback/after","exit");
        ini.setValue("video/output","software");ini.setValue("playback/hardwareDecoding",false);
      }
      QProcess child; child.start(exe, flags+QStringList{media});
      QVERIFY(child.waitForFinished(15000));QVERIFY2(child.exitCode()==0,child.readAllStandardError().constData());
      QSettings ini(QDir(directory).filePath("FoxicMPQt.ini"),QSettings::IniFormat);
      QVERIFY(ini.value("history/files").toStringList().contains(media));
    };
    play(selected.path(),{});
    QVERIFY(!QFileInfo::exists(program.filePath("FoxicMPQt.ini")));
    play(explicitProfile.path(),{"--profile",explicitProfile.path()});
    write(ProfileLaunch::configPath(program.path()),"invalid");
    query({"--version"});
    play(program.path(),{"--portable"});
    QProcess failed;failed.start(exe,{});QVERIFY(failed.waitForFinished(5000));QCOMPARE(failed.exitCode(),1);
    failed.start(exe,{"--portable","--profile",selected.path()});QVERIFY(failed.waitForFinished(5000));QCOMPARE(failed.exitCode(),1);
  }
};
int main(int argc,char **argv) {
  QTemporaryDir isolated;
  qputenv("XDG_CONFIG_HOME",(isolated.path()+"/config").toUtf8());
  qputenv("XDG_DATA_HOME",(isolated.path()+"/data").toUtf8());
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication app(argc,argv);Q_INIT_RESOURCE(resources);
  ProfileLaunchTest test;return QTest::qExec(&test,argc,argv);
}
#include "profilelaunch_test.moc"
