#include "player.h"
#include "profile.h"
#include "profilearchive.h"
#include "profileoptions.h"
#include "mediaoptions.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>
#include <sys/stat.h>

class ProfileArchiveTest : public QObject {
  Q_OBJECT
  void write(const QString &path, const QByteArray &bytes) {
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
  }
  QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  }
  void prepare(Profile &profile) {
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
  }
private slots:
  void persistedSubtitleVisibility() {
    Player player; player.show(); QTRY_VERIFY_WITH_TIMEOUT(player.ready(), 15000);
    for (bool visible : {true, false}) {
      QTemporaryDir dir;
      write(dir.filePath("FoxicMPQt.ini"),
            visible ? "[subtitles]\nvisible=true\n" : "[subtitles]\nvisible=false\n");
      Profile profile(dir.path()); prepare(profile);
      QSignalSpy errors(&player, &Player::error);
      MediaOptions::apply(&player, profile.settings);
      QCOMPARE(player.get("sub-visibility").toBool(), visible);
      QCOMPARE(errors.size(), 0);
    }
  }
  void unversionedProfileRetainsPreferences() {
    QTemporaryDir dir;
    {
      QSettings old(dir.filePath("FoxicMPQt.ini"), QSettings::IniFormat);
      old.setValue("audio/volume", 28);
      old.setValue("playback/rememberPosition", false);
      old.setValue("appearance/theme", "light");
    }
    Profile old(dir.path());
    QCOMPARE(old.settings.value("audio/volume").toInt(), 28);
    QCOMPARE(old.settings.value("playback/rememberPosition").toBool(), false);
    QCOMPARE(old.settings.value("appearance/theme").toString(), QString("light"));
    QCOMPARE(old.settings.value("profile/version").toInt(), 1);
  }
  void roundTripTypedSettingsAssetsAndLibrary() {
    QTemporaryDir temp;
    const auto original = temp.filePath("source");
    QVERIFY(QDir().mkpath(original));
    Profile profile(original);
    profile.settings.setValue("window/geometry", QByteArray("\0\1\xff", 3));
    profile.settings.setValue(
        "files/playlist",
        QStringList{"/film/日本語.mkv", "https://example.org/video"});
    profile.settings.setValue("playlist/tabsV1", QByteArray("{\"tabs\":[]}"));
    profile.settings.setValue("integration/defaultsBackupV1", "machine-local");
    profile.settings.setValue("integration/enabled", true);
    profile.settings.setValue("integration/checkOnStartup", true);
    const auto asset = temp.filePath("external-logo.png");
    write(asset, "custom-image");
    profile.settings.setValue("appearance/logoPath", asset);
    write(original + "/toolbar.png", "toolbar");
    profile.settings.setValue("appearance/toolbarPath", "toolbar.png");
    write(temp.filePath("external.json"), "{\"translations\":{\"a\":\"b\"}}");
    profile.settings.setValue("appearance/languageFile",
                              temp.filePath("external.json"));
    write(temp.filePath("external.icc"), "icc-resource");
    profile.settings.setValue("video/iccProfile",
                              temp.filePath("external.icc"));
    write(original + "/Subtitles/Edits/日本語.srt",
          "1\n00:00:01,000 --> 00:00:02,000\nHi\n");
    const auto edited = original + "/Subtitles/Edits/日本語.srt";
    const QJsonObject metadata{
        {"subtitleFiles", QJsonArray{edited, "/external/subtitles.srt"}},
        {"selectedSubtitleFile", edited},
        {"note", edited}};
    const QJsonObject entry{{"source", "/film/a.mkv"}, {"metadata", metadata}};
    profile.settings.setValue(
        "playlist/tabsV1",
        QJsonDocument(
            QJsonObject{{"tabs", QJsonArray{QJsonObject{
                                     {"entries", QJsonArray{entry}}}}}})
            .toJson(QJsonDocument::Compact));
    write(original + "/Screenshots/private.png", "excluded");
    write(original + "/video.mkv", "excluded-media");
    write(original + "/Shaders/generated.glsl", "excluded-generated");
    profile.savePosition("/film/a.mkv", 23.5, 120);
    profile.recordRecent("/film/a.mkv");
    const auto id = profile.addFavorite("Пример", "/film/a.mkv", 23.5);
    QString error;
    const auto archive = temp.filePath("archive.foxicmp-profile");
    QVERIFY2(ProfileArchive::save(profile, archive, &error), qPrintable(error));
    QVERIFY(profile.settings.value("integration/enabled").toBool());
    QCOMPARE(profile.settings.value("appearance/logoPath").toString(), asset);
    ProfileArchive::Bundle bundle;
    QVERIFY2(ProfileArchive::read(archive, &bundle, &error), qPrintable(error));
    QCOMPARE(bundle.files.size(), 6);
    const auto destination = temp.filePath("Новый профиль");
    QVERIFY2(ProfileArchive::restore(bundle, destination, &error),
             qPrintable(error));
    Profile restored(destination);
    QCOMPARE(restored.settings.value("window/geometry").toByteArray(),
             QByteArray("\0\1\xff", 3));
    QCOMPARE(restored.settings.value("files/playlist").toStringList(),
             profile.settings.value("files/playlist").toStringList());
    const auto restoredMetadata = [](Profile &p) {
      return QJsonDocument::fromJson(
                 p.settings.value("playlist/tabsV1").toByteArray())
          .object()
          .value("tabs")
          .toArray()
          .first()
          .toObject()
          .value("entries")
          .toArray()
          .first()
          .toObject()
          .value("metadata")
          .toObject();
    };
    QCOMPARE(restored.settings.value("playlist/tabsV1").typeId(),
             QMetaType::QByteArray);
    auto transferred = restoredMetadata(restored);
    QCOMPARE(transferred.value("selectedSubtitleFile").toString(),
             destination + "/Subtitles/Edits/日本語.srt");
    QCOMPARE(transferred.value("subtitleFiles").toArray(),
             QJsonArray({destination + "/Subtitles/Edits/日本語.srt",
                         "/external/subtitles.srt"}));
    QCOMPARE(transferred.value("note").toString(), edited);
    QCOMPARE(restored.position("/film/a.mkv"), 23.5);
    QCOMPARE(restored.recentFiles(), profile.recentFiles());
    QCOMPARE(restored.favorites().size(), 1);
    QCOMPARE(restored.favorites().first().id, id);
    const auto savedAsset =
        restored.settings.value("appearance/logoPath").toString();
    QVERIFY(savedAsset.startsWith("Assets/"));
    QCOMPARE(read(QDir(destination).filePath(savedAsset)),
             QByteArray("custom-image"));
    for (const auto &key : {"appearance/toolbarPath", "appearance/languageFile",
                            "video/iccProfile"})
      QVERIFY(QFileInfo::exists(
          QDir(destination).filePath(restored.settings.value(key).toString())));
    QVERIFY(!restored.settings.value("integration/enabled").toBool());
    QVERIFY(!restored.settings.value("integration/checkOnStartup").toBool());
    QVERIFY(!restored.settings.contains("integration/defaultsBackupV1"));
    QVERIFY(restored.settings.value("integration/prompted").toBool());
    QVERIFY(!QFileInfo::exists(destination + "/video.mkv"));
    QVERIFY(!QFileInfo::exists(destination + "/Screenshots"));
    QVERIFY(!(QFileInfo(archive).permissions() &
              (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
    const auto again = temp.filePath("again.foxicmp-profile");
    QVERIFY(ProfileArchive::save(restored, again, &error));
    QVERIFY(ProfileArchive::read(again, &bundle, &error));
    const auto third = temp.filePath("third");
    QVERIFY(ProfileArchive::restore(bundle, third, &error));
    Profile thirdProfile(third);
    QCOMPARE(
        restoredMetadata(thirdProfile).value("selectedSubtitleFile").toString(),
        third + "/Subtitles/Edits/日本語.srt");
  }
  void rejectsMalformedArchives_data() {
    QTest::addColumn<QString>("kind");
    for (const auto &kind :
         {"traversal", "absolute", "backslash", "duplicate", "hash", "base64",
          "version", "ini-version", "missing-ini", "conflict",
          "unexpected-file", "truncated"})
      QTest::newRow(kind) << QString(kind);
  }
  void rejectsMalformedArchives() {
    QFETCH(QString, kind);
    QTemporaryDir temp;
    Profile profile(temp.path());
    QString error;
    const auto archive = temp.filePath("test.foxicmp-profile");
    QVERIFY(ProfileArchive::save(profile, archive, &error));
    auto object = QJsonDocument::fromJson(read(archive)).object();
    auto files = object["files"].toArray();
    auto entry = files[0].toObject();
    if (kind == "traversal")
      entry["path"] = "Assets/../../outside";
    if (kind == "absolute")
      entry["path"] = "/tmp/escaped.ini";
    if (kind == "backslash")
      entry["path"] = "Assets/..\\outside";
    if (kind == "hash")
      entry["sha256"] = "wrong";
    if (kind == "base64")
      entry["data"] = "!invalid!";
    if (kind == "unexpected-file")
      entry["path"] = "foxicmp-qt";
    if (kind == "version")
      object["version"] = 2;
    if (kind == "ini-version") {
      const QByteArray bytes("[profile]\nversion=99\n");
      entry["data"] = QString::fromLatin1(bytes.toBase64());
      entry["sha256"] = QString::fromLatin1(
          QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    }
    files[0] = entry;
    if (kind == "duplicate")
      files.append(entry);
    if (kind == "missing-ini")
      files = {};
    if (kind == "conflict") {
      entry["path"] = "Assets/name";
      files.append(entry);
      entry["path"] = "Assets/name/child";
      files.append(entry);
    }
    object["files"] = files;
    auto bytes = QJsonDocument(object).toJson();
    if (kind == "truncated")
      bytes.chop(8);
    write(archive, bytes);
    ProfileArchive::Bundle bundle;
    bundle.files.insert("sentinel", "unchanged");
    QVERIFY(!ProfileArchive::read(archive, &bundle, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(bundle.files.value("sentinel"), QByteArray("unchanged"));
    QVERIFY(!QFileInfo::exists(temp.filePath("outside")));
  }
  void existingTargetsLinksAndFailedWritesStayUntouched() {
    QTemporaryDir temp;
    const auto source = temp.filePath("source");
    QVERIFY(QDir().mkpath(source));
    Profile profile(source);
    QString error;
    const auto archive = temp.filePath("test.foxicmp-profile");
    QVERIFY(ProfileArchive::save(profile, archive, &error));
    ProfileArchive::Bundle bundle;
    QVERIFY(ProfileArchive::read(archive, &bundle, &error));
    const auto old = read(source + "/FoxicMPQt.ini");
    QVERIFY(!ProfileArchive::restore(bundle, source, &error));
    QCOMPARE(read(source + "/FoxicMPQt.ini"), old);
    QVERIFY(
        !ProfileArchive::restore(bundle, source + "/FoxicMPQt.ini", &error));
    const auto link = temp.filePath("link");
    QVERIFY(QFile::link(source, link));
    QVERIFY(!ProfileArchive::restore(bundle, link, &error));
    const auto dangling = temp.filePath("dangling");
    QVERIFY(QFile::link(temp.filePath("absent"), dangling));
    QVERIFY(!ProfileArchive::restore(bundle, dangling, &error));
    QVERIFY(!ProfileArchive::restore(bundle, temp.filePath("missing/child"),
                                     &error));
    QVERIFY(!ProfileArchive::save(profile, archive + "/child.foxicmp-profile",
                                  &error));
    QVERIFY(!ProfileArchive::save(profile, source + "/FoxicMPQt.ini", &error));
    QCOMPARE(read(source + "/FoxicMPQt.ini"), old);
    const auto fifo = temp.filePath("pipe.foxicmp-profile");
    QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
    QVERIFY(!ProfileArchive::read(fifo, &bundle, &error));
    const auto linkedArchive = temp.filePath("link.foxicmp-profile");
    QVERIFY(QFile::link(archive, linkedArchive));
    const auto savedArchive = read(archive);
    QVERIFY(!ProfileArchive::save(profile, linkedArchive, &error));
    QCOMPARE(read(archive), savedArchive);
    bundle.files.insert("Assets/" + QString(300, 'x'), "invalid-component");
    QVERIFY(!ProfileArchive::restore(bundle, temp.filePath("failed-write"),
                                     &error));
    QVERIFY(!QFileInfo::exists(temp.filePath("failed-write")));
    QVERIFY(QDir(temp.path())
                .entryList({".foxicmp-import-*"},
                           QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                .isEmpty());
  }
  void missingAssetsAndLimitsProduceNoArchive() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    QString error;
    const auto archive = temp.filePath("test.foxicmp-profile");
    profile.settings.setValue("appearance/logoPath", "missing.png");
    QVERIFY(!ProfileArchive::save(profile, archive, &error));
    QVERIFY(!QFileInfo::exists(archive));
    const auto large = temp.filePath("large.png");
    write(large, QByteArray(8 * 1024 * 1024 + 1, 'x'));
    profile.settings.setValue("appearance/logoPath", large);
    QVERIFY(!ProfileArchive::save(profile, archive, &error));
    QVERIFY(!QFileInfo::exists(archive));
    profile.settings.remove("appearance/logoPath");
    QVERIFY(QFile::link(large, temp.filePath("Subtitles")));
    QVERIFY(!ProfileArchive::save(profile, archive, &error));
    QVERIFY(!QFileInfo::exists(archive));
    ProfileArchive::Bundle bundle;
    bundle.files.insert("FoxicMPQt.ini", "[profile]\nversion=1\n");
    bundle.files.insert("Assets/large", QByteArray(8 * 1024 * 1024 + 1, 'x'));
    QVERIFY(!ProfileArchive::restore(bundle, temp.filePath("bad"), &error));
    QVERIFY(!QFileInfo::exists(temp.filePath("bad")));
    bundle.files.remove("Assets/large");
    for (int i = 0; i < 4; ++i)
      bundle.files.insert("Assets/" + QString::number(i),
                          QByteArray(8 * 1024 * 1024, 'x'));
    QVERIFY(
        !ProfileArchive::restore(bundle, temp.filePath("bad-total"), &error));
    bundle.files.clear();
    bundle.files.insert("FoxicMPQt.ini", "[profile]\nversion=1\n");
    for (int i = 0; i < 2048; ++i)
      bundle.files.insert("Assets/" + QString::number(i), "x");
    QVERIFY(
        !ProfileArchive::restore(bundle, temp.filePath("bad-count"), &error));
    // Escaped filename metadata can exceed the archive cap even below 32 MiB.
    bundle.files.clear();
    QTemporaryDir metadata;
    Profile metadataProfile(metadata.path());
    const auto directory =
        metadata.filePath("Subtitles/Edits/" + QString(199, QChar(1)) + "/" +
                          QString(199, QChar(1)));
    const QByteArray payload(16385, 'x');
    for (int i = 0; i < 2047; ++i)
      write(QDir(directory).filePath(QString(87, QChar(1)) +
                                     QString::number(i) + ".srt"),
            payload);
    const auto oversized = metadata.filePath("metadata.foxicmp-profile");
    QVERIFY(!ProfileArchive::save(metadataProfile, oversized, &error));
    QVERIFY(error.contains("48"));
    QVERIFY(!QFileInfo::exists(oversized));
  }
  void missingInactiveLogoDoesNotBlockBackup() {
    QTemporaryDir temp;
    QVERIFY(QDir().mkpath(temp.filePath("source")));
    Profile profile(temp.filePath("source"));
    profile.settings.setValue("appearance/logoPath", "missing.png");
    profile.settings.setValue("appearance/logoExternal", false);
    profile.settings.setValue("appearance/logoId", 0);
    const auto archive = temp.filePath("inactive-logo.foxicmp-profile");
    QString error;
    QVERIFY2(ProfileArchive::save(profile, archive, &error), qPrintable(error));
    ProfileArchive::Bundle bundle;
    QVERIFY2(ProfileArchive::read(archive, &bundle, &error), qPrintable(error));
    const auto restored = temp.filePath("restored");
    QVERIFY2(ProfileArchive::restore(bundle, restored, &error), qPrintable(error));
    Profile copy(restored);
    QVERIFY(!copy.settings.value("appearance/logoExternal").toBool());
    QCOMPARE(copy.settings.value("appearance/logoId").toInt(), 0);
    QCOMPARE(copy.settings.value("appearance/logoPath").toString(), QString("missing.png"));
    profile.settings.setValue("appearance/logoExternal", true);
    QVERIFY(!ProfileArchive::save(profile, temp.filePath("active-logo.foxicmp-profile"), &error));
  }
  void resetPreservesLibraryAndExactBackup() {
    QTemporaryDir temp;
    QString saved;
    QByteArray before;
    {
      Profile profile(temp.path());
      profile.settings.setValue("audio/volume", 13);
      profile.settings.setValue("appearance/language", "en");
      profile.settings.setValue("video/customShader", "Shaders/local.glsl");
      profile.settings.setValue("integration/defaultsBackupV1", QByteArray("undo-evidence"));
      profile.settings.setValue("integration/checkOnStartup", true);
      profile.settings.setValue("files/playlist", QStringList{"/film/日本語.mkv"});
      profile.settings.setValue("files/currentIndex", 3);
      profile.settings.setValue("playlist/tabsV1", QByteArray("{\"tabs\":[]}"));
      profile.recordRecent("/film/日本語.mkv");
      profile.savePosition("/film/日本語.mkv", 31.5, 200);
      profile.addFavorite("Тест", "/film/日本語.mkv", 31.5);
      // Reset works even when a previously selected external asset is missing.
      profile.settings.setValue("appearance/logoPath", "/missing/logo.png");
      write(temp.filePath("Subtitles/Edits/keep.srt"), "subtitle");
      write(temp.filePath("film.mkv"), "media");
      profile.settings.sync();
      before = read(profile.settings.fileName());
      QString error;
      QVERIFY2(ProfileReset::apply(profile, true, &saved, &error), qPrintable(error));
      QCOMPARE(read(saved), before);
      QCOMPARE(profile.settings.value("audio/volume").toInt(), 75);
      QVERIFY(!profile.settings.contains("appearance/logoPath"));
      QVERIFY(!profile.settings.contains("video/customShader"));
      QCOMPARE(profile.recentFiles(), QStringList{"/film/日本語.mkv"});
      QCOMPARE(profile.position("/film/日本語.mkv"), 31.5);
      QCOMPARE(profile.favorites().size(), 1);
      QCOMPARE(profile.settings.value("files/currentIndex").toInt(), 3);
      QCOMPARE(profile.settings.value("playlist/tabsV1").typeId(), QMetaType::QByteArray);
      QCOMPARE(profile.settings.value("integration/defaultsBackupV1").toByteArray(), QByteArray("undo-evidence"));
      QVERIFY(!profile.settings.value("integration/checkOnStartup").toBool());
      QCOMPARE(read(temp.filePath("film.mkv")), QByteArray("media"));
      QCOMPARE(read(temp.filePath("Subtitles/Edits/keep.srt")), QByteArray("subtitle"));
    }
    Profile reopened(temp.path());
    QCOMPARE(reopened.settings.value("audio/volume").toInt(), 75);
    QCOMPARE(reopened.favorites().size(), 1);
    QCOMPARE(read(saved), before);
  }
  void resetCanClearLibraryAndRepeatWithoutReplacingBackups() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    profile.recordRecent("/film/a.mkv");
    profile.savePosition("/film/a.mkv", 20, 120);
    profile.addFavorite("A", "/film/a.mkv", 20);
    profile.settings.setValue("files/playlist", QStringList{"/film/a.mkv"});
    profile.settings.setValue("playlist/tabsV1", QByteArray("tabs"));
    QString first, second, error;
    QVERIFY2(ProfileReset::apply(profile, false, &first, &error), qPrintable(error));
    QVERIFY(profile.recentFiles().isEmpty());
    QVERIFY(profile.favorites().isEmpty());
    QCOMPARE(profile.position("/film/a.mkv"), 0.0);
    QVERIFY(!profile.settings.contains("files/playlist"));
    QVERIFY(!profile.settings.contains("playlist/tabsV1"));
    const auto original = read(first);
    QVERIFY(ProfileReset::apply(profile, false, &second, &error));
    QVERIFY(first != second);
    QCOMPARE(read(first), original);
    QCOMPARE(QDir(temp.filePath("Backups")).entryList({"before-reset-*.ini"}, QDir::Files).size(), 2);
  }
  void resetRefusesOtherInstancesAndCanonicalAliases() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    profile.settings.setValue("audio/volume", 12);
    profile.settings.sync();
    const auto before = read(profile.settings.fileName());
    QString backup, error;
    {
      QTemporaryDir links;
      QVERIFY(QFile::link(temp.path(), links.filePath("alias")));
      Profile other(links.filePath("alias"));
      QVERIFY(!ProfileReset::apply(profile, true, &backup, &error));
      QVERIFY(error.contains("окна"));
      QVERIFY(backup.isEmpty());
      QCOMPARE(read(profile.settings.fileName()), before);
      QVERIFY(!QFileInfo::exists(temp.filePath("Backups")));
    }
    QVERIFY(ProfileReset::apply(profile, true, &backup, &error));
  }
  void resetRefusesLinksAndBackupFailure_data() {
    QTest::addColumn<QString>("kind");
    for (const auto &kind : {"ini-link", "backup-link", "backup-file", "ini-fifo"})
      QTest::newRow(kind) << QString(kind);
  }
  void resetRefusesLinksAndBackupFailure() {
    QFETCH(QString, kind);
    QTemporaryDir temp, outside;
    Profile profile(temp.path());
    profile.settings.setValue("audio/volume", 12);
    profile.settings.sync();
    const auto before = read(profile.settings.fileName());
    write(outside.filePath("target.ini"), before);
    if (kind == "ini-link" || kind == "ini-fifo") {
      QVERIFY(QFile::remove(profile.settings.fileName()));
      if (kind == "ini-link")
        QVERIFY(QFile::link(outside.filePath("target.ini"), profile.settings.fileName()));
      else
        QCOMPARE(::mkfifo(QFile::encodeName(profile.settings.fileName()).constData(), 0600), 0);
    } else if (kind == "backup-link")
      QVERIFY(QFile::link(outside.path(), temp.filePath("Backups")));
    else
      write(temp.filePath("Backups"), "not-directory");
    QString backup, error;
    QVERIFY(!ProfileReset::apply(profile, true, &backup, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(backup.isEmpty());
    QCOMPARE(read(outside.filePath("target.ini")), before);
    if (kind != "ini-fifo") QCOMPARE(read(profile.settings.fileName()), before);
    else {
      QVERIFY(QFile::remove(profile.settings.fileName()));
      write(profile.settings.fileName(), before);
    }
  }
  void resetLeaseBlocksNewWriters() {
    QTemporaryDir temp;
    Profile owner(temp.path());
    owner.settings.sync();
    QVERIFY(owner.lease.exclusive());
    {
      Profile other(temp.path());
      QVERIFY(!other.lease.valid());
    }
    owner.lease.shared();
    Profile other(temp.path());
    QVERIFY(other.lease.valid());
  }
  void resetAfterWindowShutdownDoesNotRestoreOldSettings() {
    QTemporaryDir dir;
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {"--reset-shutdown-probe", dir.path()});
    QVERIFY(child.waitForFinished(10000));
    QVERIFY2(child.exitCode() == 0, child.readAllStandardError().constData());
    QSettings saved(dir.filePath("FoxicMPQt.ini"), QSettings::IniFormat);
    QCOMPARE(saved.value("audio/volume").toInt(), 75);
    QVERIFY(!saved.contains("desktop/closeToTray"));
    const auto backups = QDir(dir.filePath("Backups")).entryList({"before-reset-*.ini"}, QDir::Files);
    QCOMPARE(backups.size(), 1);
    QSettings old(dir.filePath("Backups/") + backups.first(), QSettings::IniFormat);
    QCOMPARE(old.value("audio/volume").toInt(), 31);
    QVERIFY(old.value("desktop/closeToTray").toBool());
  }
  void resetDialogCancellationAndOptions() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    profile.settings.setValue("audio/volume", 31);
    profile.settings.sync();
    const auto before = read(profile.settings.fileName());
    QWidget parent;
    bool requested = false, keep = true;
    QTimer::singleShot(40, &parent, [&] {
      auto *dialog = parent.findChild<QDialog *>("profileManagement");
      QVERIFY(dialog);
      QTimer driver;
      int stage = 0;
      connect(&driver, &QTimer::timeout, dialog, [&] {
        auto *confirm = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!confirm) return;
        QVERIFY(confirm->checkBox()->isChecked());
        if (stage == 0) {
          QCOMPARE(confirm->defaultButton(), confirm->button(QMessageBox::Cancel));
          stage = 1;
          confirm->button(QMessageBox::Cancel)->click();
        } else {
          confirm->checkBox()->setChecked(false);
          const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
          if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            QVERIFY(confirm->grab().save(QDir(screenshots).filePath("profile-reset.png")));
          }
          confirm->button(QMessageBox::Ok)->click();
        }
      });
      driver.start(30);
      dialog->findChild<QPushButton *>("profileReset")->click();
      QVERIFY(!requested);
      QCOMPARE(read(profile.settings.fileName()), before);
      dialog->findChild<QPushButton *>("profileReset")->click();
      driver.stop();
    });
    showProfileManagement(profile, &parent, [] {}, [&](bool value) {requested = true; keep = value;});
    QVERIFY(requested);
    QVERIFY(!keep);
    QCOMPARE(read(profile.settings.fileName()), before);
  }
  void backupsAreDistinctAndDoNotIncludeEarlierBackups() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    QString error;
    const auto first = ProfileArchive::backup(profile, &error);
    const auto second = ProfileArchive::backup(profile, &error);
    QVERIFY2(!first.isEmpty() && !second.isEmpty(), qPrintable(error));
    QVERIFY(first != second);
    ProfileArchive::Bundle bundle;
    QVERIFY(ProfileArchive::read(second, &bundle, &error));
    QCOMPARE(bundle.files.size(), 1);
    QCOMPARE(QDir(temp.filePath("Backups"))
                 .entryList({"*.foxicmp-profile"}, QDir::Files)
                 .size(),
             2);
  }
  void dialogExportImportAndCancellation() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("audio/volume", 31);
    profile.settings.sync();
    const auto original = read(profile.settings.fileName());
    const auto archive = temp.filePath("ui.foxicmp-profile");
    QWidget parent;
    QTimer::singleShot(40, &parent, [&] {
      auto *dialog = parent.findChild<QDialog *>("profileManagement");
      QVERIFY(dialog);
      auto close = qScopeGuard([&] { dialog->reject(); });
      QTimer picker;
      int stage = 0;
      connect(&picker, &QTimer::timeout, dialog, [&] {
        auto *file =
            qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (!file || stage != 0)
          return;
        file->selectFile(archive);
        QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
        if (!file->isVisible())
          stage = 1;
      });
      picker.start(30);
      dialog->findChild<QPushButton *>("profileExport")->click();
      picker.stop();
      QCOMPARE(stage, 1);
      QVERIFY(QFileInfo::exists(archive));
      picker.disconnect();
      stage = 0;
      connect(&picker, &QTimer::timeout, dialog, [&] {
        auto *file =
            qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (!file || stage != 0)
          return;
        stage = 1;
        file->reject();
      });
      picker.start(30);
      dialog->findChild<QPushButton *>("profileImport")->click();
      picker.stop();
      QVERIFY(!dialog->findChild<QPushButton *>("profileOpenRestored")
                   ->isEnabled());
      picker.disconnect();
      stage = 0;
      connect(&picker, &QTimer::timeout, dialog, [&] {
        auto *modal = QApplication::activeModalWidget();
        if (auto *file = qobject_cast<QFileDialog *>(modal)) {
          if (stage == 0) {
            // selectFile() may leave a focused filename edit unchanged.
            file->findChild<QLineEdit *>("fileNameEdit")->setText(archive);
            QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
            if (!file->isVisible())
              stage = 1;
          } else if (stage == 1 && file->fileMode() == QFileDialog::Directory) {
            file->setDirectory(temp.path());
            file->findChild<QLineEdit *>("fileNameEdit")->setText(temp.path());
            QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
            if (!file->isVisible())
              stage = 2;
          }
        } else if (auto *input = qobject_cast<QInputDialog *>(modal);
                   input && stage == 2) {
          stage = 3;
          input->setTextValue("imported");
          input->accept();
        }
      });
      picker.start(30);
      dialog->findChild<QPushButton *>("profileImport")->click();
      picker.stop();
      QCOMPARE(stage, 3);
      QVERIFY(
          dialog->findChild<QPushButton *>("profileOpenRestored")->isEnabled());
      QCOMPARE(read(profile.settings.fileName()), original);
      Profile restored(temp.filePath("imported"));
      QCOMPARE(restored.settings.value("audio/volume").toInt(), 31);
    });
    showProfileManagement(profile, &parent, [] {});
  }
  void dialogBackupCapturesLiveStateAndScreenshot() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    Window window(profile);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    const auto media = QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA"))
                           .filePath("chapters.mkv");
    window.openFiles({media});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    player->command({"seek", "2", "absolute+exact"});
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 2) < .1);
    QTimer::singleShot(100, &window, [&] {
      auto *dialog = window.findChild<QDialog *>("profileManagement");
      QVERIFY(dialog);
      auto close = qScopeGuard([&] { dialog->reject(); });
      QVERIFY(!dialog->findChild<QPushButton *>("profileOpenRestored")
                   ->isEnabled());
      dialog->findChild<QPushButton *>("profileBackup")->click();
      QVERIFY(dialog->findChild<QLabel *>("profileStatus")
                  ->text()
                  .contains("Backups"));
      const auto files = QDir(temp.filePath("Backups"))
                             .entryList({"*.foxicmp-profile"}, QDir::Files);
      QCOMPARE(files.size(), 1);
      ProfileArchive::Bundle bundle;
      QString error;
      QVERIFY(ProfileArchive::read(temp.filePath("Backups/") + files.first(),
                                   &bundle, &error));
      QVERIFY(
          ProfileArchive::restore(bundle, temp.filePath("restored"), &error));
      Profile restored(temp.filePath("restored"));
      QCOMPARE(restored.settings.value("files/playlist").toStringList(),
               QStringList{media});
      QVERIFY(std::abs(restored.position(media) - 2) < .1);
      QVERIFY(player->get("pause").toBool());
      QCOMPARE(loaded.size(), 1);
      const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      if (!screenshots.isEmpty()) {
        // Let the status label wrap the full archive filename before capture.
        QApplication::processEvents();
        QVERIFY(QDir().mkpath(screenshots));
        QVERIFY(dialog->grab().save(
            QDir(screenshots).filePath("profile-management.png")));
      }
    });
    window.findChild<QAction *>("action_profileManagement")->trigger();
  }
};
int main(int argc, char **argv) {
  QTemporaryDir isolated;
  qputenv("XDG_CONFIG_HOME", (isolated.path() + "/config").toUtf8());
  qputenv("XDG_DATA_HOME", (isolated.path() + "/data").toUtf8());
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication app(argc, argv);
  Q_INIT_RESOURCE(resources);
  const auto args = app.arguments();
  if (args.size() == 3 && args[1] == "--reset-shutdown-probe") {
    Profile profile(args[2]);
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("input/mpris", false);
    profile.settings.setValue("audio/volume", 31);
    profile.settings.setValue("desktop/closeToTray", true);
    profile.settings.setValue("desktop/tray", true);
    bool reset = false, keep = true;
    int code;
    {
      Window window(profile, {}, {}, [] { return true; });
      window.show();
      QTimer driver;
      bool resetClicked = false;
      QObject::connect(&driver, &QTimer::timeout, &window, [&] {
        if (auto *confirm = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            confirm && confirm->objectName() == "profileResetConfirm")
          confirm->button(QMessageBox::Ok)->click();
        else if (!resetClicked) {
          auto *dialog = window.findChild<QDialog *>("profileManagement");
          if (dialog && dialog->isVisible()) {
            resetClicked = true;
            QTimer::singleShot(0, dialog, [dialog] {
              dialog->findChild<QPushButton *>("profileReset")->click();
            });
          }
        }
      });
      driver.start(30);
      QTimer::singleShot(100, &window, [&] {
        window.findChild<QAction *>("action_profileManagement")->trigger();
      });
      QTimer::singleShot(5000, &app, [&] { app.exit(9); });
      code = app.exec();
      reset = window.requestedProfileReset(&keep);
    }
    QString backup, error;
    const bool applied = code == 0 && reset && ProfileReset::apply(profile, keep, &backup, &error);
    if (!applied) qWarning() << "reset probe" << code << reset << error;
    return applied ? 0 : 7;
  }
  ProfileArchiveTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "profilearchive_test.moc"
