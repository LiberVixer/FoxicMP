#include "associations.h"
#include "integration.h"
#include "player.h"
#include "profile.h"
#include "window.h"
#include <QLockFile>
#include <QtTest>
#include <QtWidgets>

class AssociationsTest : public QObject {
  Q_OBJECT
  QString workspace;
  void write(const QString &path, const QByteArray &data) {
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(data), data.size());
  }
  QByteArray read(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  }
  QString dataFile(const QString &name) {
    return QDir(qEnvironmentVariable("XDG_DATA_HOME")).filePath(name);
  }
  QByteArray run(const QString &command, const QStringList &args) {
    QProcess p;
    p.start(command, args);
    if (!p.waitForFinished(8000) || p.exitCode())
      return p.readAllStandardError();
    return p.readAllStandardOutput();
  }
  QString current(const QString &mime) {
    auto text = QString::fromUtf8(run("gio", {"mime", mime}));
    return text.section('\n', 0, 0).section(": ", 1).trimmed();
  }
  void seed() {
    write(dataFile("applications/other.desktop"),
          "[Desktop Entry]\nType=Application\nName=Other\nExec=/usr/bin/true "
          "%F\nMimeType=video/mp4;audio/flac;audio/mpeg;\n");
  }
private slots:
  void initTestCase() {
    workspace = qEnvironmentVariable("FOXICMP_ASSOCIATIONS_TEST_ROOT");
    QVERIFY(!workspace.isEmpty());
    QVERIFY(!QStandardPaths::findExecutable("gio").isEmpty());
    seed();
  }
  void init() {
    QFile::remove(Associations::configFile());
    QFile::remove(QFileInfo(Associations::configFile()).absolutePath() +
                  "/foxicmp-test-mimeapps.list");
  }
  void mimeCoverageAndNoFolderOrUrlHijack() {
    const auto catalog = Associations::catalog();
    QVERIFY(catalog.size() > 50);
    const auto types = Associations::mimeTypes();
    QMimeDatabase db;
    for (const auto &mime : db.allMimeTypes())
      if (mime.name().startsWith("video/") || mime.name().startsWith("audio/"))
        QVERIFY(types.contains(mime.name()));
    QVERIFY(!types.contains("inode/directory"));
    QVERIFY(!types.contains("application/octet-stream"));
    for (const auto &type : types)
      QVERIFY(!type.startsWith("x-scheme-handler/"));
    QCOMPARE(QSet<QString>(types.begin(), types.end()).size(), types.size());
    QTemporaryDir temp;
    QSettings settings(temp.filePath("profile.ini"), QSettings::IniFormat);
    QString error;
    QVERIFY(!Associations::assign(settings, {"inode/directory"}, &error));
    QVERIFY(!QFile::exists(Associations::configFile()));
    QVERIFY(!Associations::assign(settings, {}, &error));
  }
  void selectedDefaultsRealGioAndPreciseUndo() {
    seed();
    QTemporaryDir temp;
    Profile profile(temp.path());
    const auto source = QByteArray(
        "# user comment\r\n[Default Applications]\r\nvideo/mp4 = "
        "other.desktop;fallback.desktop;\r\naudio/flac=other.desktop;\r\ninode/"
        "directory=nemo.desktop;\r\nx-scheme-handler/"
        "https=browser.desktop;\r\n[Added "
        "Associations]\r\nvideo/mp4=other.desktop;\r\n[Removed "
        "Associations]\r\nvideo/"
        "mp4=foxicmp-qt.desktop;blocked.desktop;\r\n[X-Custom]\r\nkeep=some "
        "value\r\n");
    write(Associations::configFile(), source);
    QString error;
    QVERIFY(Integration::registerApplication(
        QCoreApplication::applicationFilePath(), temp.path(), &error));
    QCOMPARE(current("audio/flac"), QString("other.desktop"));
    QVERIFY2(Associations::assign(profile.settings, {"video/mp4"}, &error),
             qPrintable(error));
    QCOMPARE(current("video/mp4"), QString("foxicmp-qt.desktop"));
    QCOMPARE(current("audio/flac"), QString("other.desktop"));
    const auto changed = read(Associations::configFile());
    QVERIFY(changed.contains(
        "video/mp4 =foxicmp-qt.desktop;other.desktop;fallback.desktop;\r\n"));
    QVERIFY(changed.contains(
        "[Removed Associations]\r\nvideo/mp4=blocked.desktop;"));
    QVERIFY(
        changed.contains("[Added Associations]\r\nvideo/mp4=other.desktop;"));
    QVERIFY(changed.contains("[X-Custom]\r\nkeep=some value\r\n"));
    QVERIFY(changed.contains("inode/directory=nemo.desktop;"));
    QVERIFY(changed.contains("x-scheme-handler/https=browser.desktop;"));
    // Repeat must retain the original predecessor, not replace it with FoxicMP.
    QVERIFY(Associations::assign(profile.settings, {"video/mp4"}, &error));
    int restored = 0, skipped = 0;
    QVERIFY2(
        Associations::restore(profile.settings, &restored, &skipped, &error),
        qPrintable(error));
    QCOMPARE(restored, 2);
    QCOMPARE(skipped, 0);
    QCOMPARE(read(Associations::configFile()), source);
    QCOMPARE(current("video/mp4"), QString("other.desktop"));
    QVERIFY(!profile.settings.contains("integration/defaultsBackupV1"));
  }
  void registrationCacheAndBulkVideoDefaults() {
    seed();
    QTemporaryDir temp;
    QSettings profile(temp.filePath("profile.ini"), QSettings::IniFormat);
    QString error;
    write(
        Associations::configFile(),
        "[Default "
        "Applications]\nvideo/mp4=other.desktop;\naudio/flac=other.desktop;\n");
    QVERIFY(Integration::registerApplication(
        QCoreApplication::applicationFilePath(), temp.path(), &error));
    bool finished = false;
    QString result;
    Integration::refreshDatabase(this, [&](const QString &message) {
      result = message;
      finished = true;
    });
    QTRY_VERIFY(finished);
    QVERIFY2(result.isEmpty(), qPrintable(result));
    QCOMPARE(current("video/mp4"), QString("other.desktop"));
    const auto report = QString::fromUtf8(run("gio", {"mime", "video/mp4"}));
    QVERIFY(report.section("Registered applications:", 1)
                .contains("foxicmp-qt.desktop"));
    QStringList videos;
    for (const auto &type : Associations::catalog())
      if (type.category == "video")
        videos << type.mime;
    QVERIFY(videos.size() > 30);
    QVERIFY(Associations::assign(profile, videos, &error));
    QMap<QString, QString> defaults;
    QVERIFY(Associations::readDefaults(&defaults));
    for (const auto &type : videos)
      QCOMPARE(defaults[type], QString("foxicmp-qt.desktop"));
    QCOMPARE(current("audio/flac"), QString("other.desktop"));
    int restored = 0, skipped = 0;
    QVERIFY(Associations::restore(profile, &restored, &skipped, &error));
    QCOMPARE(restored, videos.size());
    QCOMPARE(skipped, 0);
    QCOMPARE(current("video/mp4"), QString("other.desktop"));
  }
  void menuCacheDeadlineFailureAndProfileWriteError() {
    QTemporaryDir temp;
    const auto bin = temp.filePath("bin");
    QVERIFY(QDir().mkpath(bin));
    const auto fake = QDir(bin).filePath("update-desktop-database");
    const auto previous = qgetenv("PATH");
    auto restorePath = qScopeGuard([&] { qputenv("PATH", previous); });
    write(fake, "#!/usr/bin/python3\nimport time\ntime.sleep(10)\n");
    QVERIFY(QFile::setPermissions(fake, QFile::ReadOwner | QFile::WriteOwner |
                                            QFile::ExeOwner));
    qputenv("PATH", bin.toUtf8() + ":" + previous);
    bool done = false, responsive = false;
    QString message;
    QElapsedTimer clock;
    clock.start();
    QTimer::singleShot(100, this, [&] { responsive = true; });
    Integration::refreshDatabase(this, [&](const QString &error) {
      message = error;
      done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(responsive);
    QVERIFY(!message.isEmpty());
    QVERIFY(clock.elapsed() < 5000);
    write(fake, "#!/usr/bin/python3\nraise SystemExit(6)\n");
    done = false;
    Integration::refreshDatabase(this, [&](const QString &error) {
      message = error;
      done = true;
    });
    QTRY_VERIFY(done);
    QVERIFY(!message.isEmpty());
    const auto blocked = temp.filePath("blocked");
    write(blocked, "file");
    QSettings unavailable(blocked + "/profile.ini", QSettings::IniFormat);
    write(Associations::configFile(),
          "[Default Applications]\nvideo/mp4=other.desktop;\n");
    const auto before = read(Associations::configFile());
    QString error;
    QVERIFY(!Associations::assign(unavailable, {"video/mp4"}, &error));
    QCOMPARE(read(Associations::configFile()), before);
  }
  void externalChangeAndEmptyDefaultsUndo() {
    seed();
    QTemporaryDir temp;
    QSettings profile(temp.filePath("profile.ini"), QSettings::IniFormat);
    QString error;
    write(Associations::configFile(),
          "[Default "
          "Applications]\nvideo/mp4=other.desktop;\n[Other]\nkeep=yes\n");
    QVERIFY(Associations::assign(profile, {"video/mp4", "audio/flac"}, &error));
    auto bytes = read(Associations::configFile());
    bytes.replace("video/mp4=foxicmp-qt.desktop;other.desktop;",
                  "video/mp4=later.desktop;");
    write(Associations::configFile(), bytes);
    int restored = 0, skipped = 0;
    QVERIFY(Associations::restore(profile, &restored, &skipped, &error));
    QCOMPARE(restored, 1);
    QCOMPARE(skipped, 1);
    const auto result = read(Associations::configFile());
    QVERIFY(result.contains("video/mp4=later.desktop;"));
    QVERIFY(!result.contains("audio/flac="));
    QVERIFY(result.contains("[Other]\nkeep=yes"));
    QVERIFY(Associations::assign(profile, {"video/mp4"}, &error));
    QVERIFY(Associations::restore(profile, &restored, &skipped, &error));
    QVERIFY(
        read(Associations::configFile()).contains("video/mp4=later.desktop;"));
  }
  void locksCorruptionAndWrongProfileLocationFailWithoutChanges() {
    QTemporaryDir temp;
    QSettings profile(temp.filePath("profile.ini"), QSettings::IniFormat);
    QString error;
    const QByteArray duplicate(
        "[Default "
        "Applications]\nvideo/mp4=one.desktop;\nvideo/mp4=two.desktop;\n");
    write(Associations::configFile(), duplicate);
    QVERIFY(!Associations::assign(profile, {"video/mp4"}, &error));
    QCOMPARE(read(Associations::configFile()), duplicate);
    write(Associations::configFile(),
          "[Default Applications]\nvideo/mp4=other.desktop;\n");
    const auto old = read(Associations::configFile());
    QLockFile lock(Associations::configFile() + ".foxicmp.lock");
    QVERIFY(lock.tryLock());
    QVERIFY(!Associations::assign(profile, {"video/mp4"}, &error));
    QCOMPARE(read(Associations::configFile()), old);
    lock.unlock();
    profile.setValue("integration/defaultsBackupV1", QByteArray("{broken"));
    QVERIFY(!Associations::assign(profile, {"video/mp4"}, &error));
    QCOMPARE(read(Associations::configFile()), old);
    profile.remove("integration/defaultsBackupV1");
    QVERIFY(Associations::assign(profile, {"video/mp4"}, &error));
    const auto journal =
        QJsonDocument::fromJson(
            profile.value("integration/defaultsBackupV1").toByteArray())
            .object();
    auto tampered = journal;
    tampered["file"] = "/another/user/mimeapps.list";
    profile.setValue("integration/defaultsBackupV1",
                     QJsonDocument(tampered).toJson());
    const auto owned = read(Associations::configFile());
    QVERIFY(!Associations::restore(profile, nullptr, nullptr, &error));
    QCOMPARE(read(Associations::configFile()), owned);
    auto records = journal["entries"].toObject();
    auto item = records.begin().value().toObject();
    item["before"] = "other.desktop;\n[Injected]";
    records[records.begin().key()] = item;
    tampered = journal;
    tampered["entries"] = records;
    profile.setValue("integration/defaultsBackupV1",
                     QJsonDocument(tampered).toJson());
    QVERIFY(!Associations::restore(profile, nullptr, nullptr, &error));
    QCOMPARE(read(Associations::configFile()), owned);
  }
  void desktopOverrideAndUninstallKeepReferencedLauncher() {
    seed();
    QTemporaryDir temp;
    QSettings profile(temp.filePath("profile.ini"), QSettings::IniFormat);
    QString error;
    QVERIFY(Integration::installForProfile("/tmp/player", temp.path(), &error));
    QVERIFY(Associations::assign(profile, {"video/mp4"}, &error));
    QVERIFY(Integration::remove(&error));
    QVERIFY(!QFile::exists(dataFile("nemo/actions/foxicmp-qt.nemo_action")));
    QVERIFY(QFile::exists(dataFile("applications/foxicmp-qt.desktop")));
    const auto override = QFileInfo(Associations::configFile()).absolutePath() +
                          "/foxicmp-test-mimeapps.list";
    write(override, "[Default Applications]\nvideo/mp4=other.desktop;\n");
    QCOMPARE(current("video/mp4"), QString("other.desktop"));
    QMap<QString, QString> configured;
    QVERIFY(Associations::readDefaults(&configured));
    QCOMPARE(configured["video/mp4"], QString("foxicmp-qt.desktop"));
    QCOMPARE(read(override),
             QByteArray("[Default Applications]\nvideo/mp4=other.desktop;\n"));
    QVERIFY(Associations::restore(profile, nullptr, nullptr, &error));
    QVERIFY(Integration::remove(&error));
    QVERIFY(!QFile::exists(dataFile("applications/foxicmp-qt.desktop")));
  }
  void launcherQuotedProfilePathsAndRelocation() {
    QTemporaryDir temp;
    const auto exe = temp.filePath("Русский $cash%\"`player");
    write(exe, "#!/usr/bin/python3\nimport "
               "os,sys,json\nopen(os.environ['FOXICMP_ARG_LOG'],'w').write("
               "json.dumps(sys.argv[1:]))\n");
    QVERIFY(QFile::setPermissions(exe, QFile::ReadOwner | QFile::WriteOwner |
                                           QFile::ExeOwner));
    const auto profile = temp.filePath("профиль $cash%\"`\\dir");
    const auto record = temp.filePath("args.json");
    qputenv("FOXICMP_ARG_LOG", record.toUtf8());
    const auto entry = temp.filePath("launch.desktop");
    write(entry, Integration::desktopText(exe, profile).toUtf8());
    QProcess validate;
    validate.start("desktop-file-validate", {entry});
    QVERIFY(validate.waitForFinished());
    QVERIFY2(validate.exitCode() == 0,
             validate.readAllStandardOutput().constData());
    const auto file = temp.filePath("-файл $ 'test.mp4");
    write(file, {});
    run("gio", {"launch", entry, file});
    QTRY_VERIFY(QFile::exists(record));
    const auto args = QJsonDocument::fromJson(read(record)).array();
    QCOMPARE(args, QJsonArray({"--profile", profile, "--", file}));
    QVERIFY(Integration::desktopText("/tmp/bad\nplayer", profile).isEmpty());
    QVERIFY(Integration::desktopText("relative", profile).isEmpty());
    QVERIFY(Integration::desktopText("/tmp/a=b", profile).isEmpty());
    QString error;
    QVERIFY(Integration::installForProfile(exe, profile, &error));
    QVERIFY(read(dataFile("nemo/actions/foxicmp-qt.nemo_action"))
                .contains("--profile"));
    QVERIFY(Integration::installForProfile("/tmp/moved/player", temp.path(),
                                           &error));
    const auto desktop = read(dataFile("applications/foxicmp-qt.desktop"));
    QVERIFY(desktop.contains("/tmp/moved/player"));
    QVERIFY(!desktop.contains(exe.toUtf8()));
    QVERIFY(desktop.contains("--enqueue"));
  }
  void uiSelectionAssignmentQueryUndoAndScreenshot() {
    seed();
    QTemporaryDir temp;
    Profile profile(temp.path());
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
    Window w(profile);
    w.show();
    auto *p = w.findChild<Player *>();
    QSignalSpy loaded(p, &Player::loaded);
    w.openFiles({QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                     .filePath("chapters.mkv")});
    QTRY_COMPARE(loaded.size(), 1);
    p->set("pause", true);
    p->command({"seek", "2", "absolute+exact"});
    QTRY_VERIFY(std::abs(p->get("time-pos").toDouble() - 2) < .1);
    const auto old = read(Associations::configFile());
    QTimer::singleShot(70, &w, [&] {
      auto *d = w.findChild<QDialog *>("associationsDialog");
      QVERIFY(d);
      auto closeDialog = qScopeGuard([&] { d->reject(); });
      auto *table = d->findChild<QTableWidget *>("associationTypes");
      QVERIFY(table);
      QCOMPARE(read(Associations::configFile()), old);
      d->findChild<QPushButton *>("selectAssociations_video")->click();
      for (int i = 0; i < table->rowCount(); ++i)
        QCOMPARE(table->item(i, 0)->checkState() == Qt::Checked,
                 table->item(i, 0)->data(Qt::UserRole) == "video");
      d->findChild<QPushButton *>("selectAssociations_")->click();
      int row = -1;
      for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i, 0)->text() == "video/mp4")
          row = i;
      QVERIFY(row >= 0);
      table->item(row, 0)->setCheckState(Qt::Checked);
      table->selectRow(row);
      d->findChild<QLineEdit *>("associationFilter")->setText("mp4");
      QVERIFY(!table->isRowHidden(row));
      d->findChild<QPushButton *>("assignAssociations")->click();
      QVERIFY(read(Associations::configFile())
                  .contains("video/mp4=foxicmp-qt.desktop;"));
      auto *query = d->findChild<QPushButton *>("queryAssociation");
      QTRY_VERIFY(query->isEnabled());
      query->click();
      QTRY_VERIFY(query->isEnabled());
      const auto report = d->findChild<QLabel *>("associationStatus")->text();
      QVERIFY(report.contains("foxicmp-qt.desktop"));
      QVERIFY(report.section("Registered applications:", 1)
                  .contains("foxicmp-qt.desktop"));
      const auto images = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      if (!images.isEmpty()) {
        QDir().mkpath(images);
        QVERIFY(
            d->grab().save(QDir(images).filePath("associations-settings.png")));
      }
      d->findChild<QPushButton *>("restoreAssociations")->click();
      QVERIFY(!read(Associations::configFile()).contains("video/mp4="));
      d->findChild<QPushButton *>("installNemoMenu")->click();
      QVERIFY(profile.settings.value("integration/enabled").toBool());
      d->findChild<QPushButton *>("removeNemoMenu")->click();
      QVERIFY(!profile.settings.value("integration/enabled").toBool());
      QTRY_VERIFY([&] {
        for (auto *job : d->findChildren<QProcess *>("desktopDatabaseUpdater"))
          if (job->state() != QProcess::NotRunning)
            return false;
        return true;
      }());
      d->reject();
    });
    w.findChild<QAction *>("action_associations")->trigger();
    QVERIFY(p->get("pause").toBool());
    QVERIFY(std::abs(p->get("time-pos").toDouble() - 2) < .1);
    w.close();
  }
};
int main(int argc, char **argv) {
  QTemporaryDir isolated;
  qputenv("FOXICMP_ASSOCIATIONS_TEST_ROOT", isolated.path().toUtf8());
  qputenv("XDG_CONFIG_HOME", (isolated.path() + "/config").toUtf8());
  qputenv("XDG_DATA_HOME", (isolated.path() + "/data").toUtf8());
  qputenv("XDG_CONFIG_DIRS", (isolated.path() + "/system-config").toUtf8());
  qputenv("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
  qputenv("XDG_CURRENT_DESKTOP", "FoxicMP-Test");
  qputenv("LC_ALL", "C.UTF-8");
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  Q_INIT_RESOURCE(resources);
  AssociationsTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "associations_test.moc"
