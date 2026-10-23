#include "preferencesdriver.h"
#include "player.h"
#include "playlistpanel.h"
#include "profile.h"
#include "subtitledocument.h"
#include "subtitleeditor.h"
#include "theme.h"
#include "window.h"
#include <QtTest>
#include <QtWidgets>
#include <cmath>
class SubtitleTest : public QObject {
  Q_OBJECT
  QTemporaryDir fixtures;
  QString video, subtitles, directory;
  const QString sample =
      "1\n00:00:01,000 --> 00:00:03,000\nПривет, мир\n\n2\n00:00:06,000 --> "
      "00:00:08,000\nВторая строка\n";
  void write(const QString &path, const QByteArray &data) {
    QSaveFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(data), data.size());
    QVERIFY(file.commit());
  }
  void prepare(Profile &p) {
    p.settings.setValue("integration/prompted", true);
    p.settings.setValue("input/mpris", false);
    p.settings.setValue("audio/mute", true);
    p.settings.setValue("playback/hardwareDecoding", false);
    p.settings.setValue("video/output", qEnvironmentVariable(
                                            "FOXICMP_TEST_OUTPUT", "software"));
  }
  QAction *action(Window &window, const QString &id) {
    return window.findChild<QAction *>("action_" + id);
  }
  QVariantMap selected(Player *player) {
    for (const auto &entry : player->get("track-list").toList()) {
      const auto track = entry.toMap();
      if (track.value("type") == "sub" &&
          track.value("id").toString() == player->get("sid").toString())
        return track;
    }
    return {};
  }
private slots:
  void initTestCase() {
    QVERIFY(fixtures.isValid());
    directory = fixtures.filePath("subs: русский");
    QVERIFY(QDir().mkpath(directory));
    subtitles = directory + "/film.ru.srt";
    write(subtitles, sample.toUtf8());
    video = fixtures.filePath("film.mkv");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-y", "-nostdin", "-v", "error", "-f", "lavfi",
                            "-i", "color=black:s=320x240:r=25", "-i", subtitles,
                            "-t", "12", "-c:v", "ffv1", "-c:s", "srt",
                            "-metadata:s:s:0", "language=rus", video});
    QVERIFY(ffmpeg.waitForFinished(10000));
    QCOMPARE(ffmpeg.exitCode(), 0);
  }
  void parsingRetimeAndAtomicErrors() {
    QCOMPARE(SubtitleDocument::parseTime("00:01:02,345"), 62345LL);
    QCOMPARE(SubtitleDocument::parseTime("01:02.34"), 62340LL);
    QCOMPARE(SubtitleDocument::parseTime("1:02:03.45"), 3723450LL);
    QCOMPARE(SubtitleDocument::parseTime("00:61:00.00"), -1LL);
    QCOMPARE(SubtitleDocument::parseTime("999:00:00.00"), -1LL);
    QString error;
    SubtitleDocument doc;
    QVERIFY2(doc.parse(sample, "srt", &error), qPrintable(error));
    QCOMPARE(doc.cues.size(), 2);
    QVERIFY(doc.synchronize(1000, 1500, 6000, 9000, &error));
    QCOMPARE(doc.cues[0].start, 1500LL);
    QCOMPARE(doc.cues[0].end, 4500LL);
    QCOMPARE(doc.cues[1].start, 9000LL);
    const auto valid = doc.serialize("srt", &error);
    QVERIFY(!doc.retime(1, -10000, &error));
    QCOMPARE(doc.serialize("srt", &error), valid);
    QVERIFY(!doc.synchronize(1000, 0, 1000, 500, &error));
    QVERIFY(!doc.parse("1\n99:00:00,000 --> 00:00:00,000\nx", "srt", &error));
    QCOMPARE(doc.serialize("srt", &error), valid);
    SubtitleDocument roundtrip;
    QVERIFY(roundtrip.parse(valid, "srt", &error));
    QCOMPARE(roundtrip.cues[0].text, QString("Привет, мир"));
    QVERIFY(roundtrip.parse(doc.serialize("vtt", &error), "vtt", &error));
    QCOMPARE(roundtrip.cues[1].start, 9000LL);
    QVERIFY(!doc.parse(
        "WEBVTT\n\nSTYLE\n::cue { color: red }\n\n00:01.000 --> 00:02.000\nx",
        "vtt", &error));
    const auto plainVtt = QString("WEBVTT test\n\ncue-id\n00:01.000 --> "
                                  "00:02.000 align:start position:20%\nLine\n");
    QVERIFY(doc.parse(plainVtt, "vtt", &error));
    const auto out = doc.serialize("vtt", &error);
    QVERIFY(out.contains("cue-id") && out.contains("align:start position:20%"));
  }
  void assStylesAndEncodingRoundtrip() {
    const QString ass =
        "[Script Info]\nTitle: Стиль\nScriptType: v4.00+\nPlayResX: "
        "640\nPlayResY: 480\n[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
        "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
        "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
        "Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Custom,DejaVu "
        "Sans,30,&H00FFFFFF,&H0000FFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,"
        "0,1,2,0,2,10,10,20,1\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
        "MarginV, Effect, Text\n"
        "Comment: 0,0:00:00.00,0:00:01.00,Custom,,0,0,0,,keep me\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Custom,Actor,0,0,0,,{\\i1}Привет, "
        "мир\\NСтрока\n";
    SubtitleDocument doc;
    QString error;
    QVERIFY(doc.parse(ass, "ass", &error));
    QVERIFY(doc.retime(1, 123, &error));
    const auto out = doc.serialize("ass", &error);
    QVERIFY(out.contains("Style: Custom,DejaVu Sans,30"));
    QVERIFY(out.contains("Comment: 0,0:00:00.00"));
    QVERIFY(out.contains(
        "Dialogue: 0,0:00:01.12,0:00:03.13,Custom,Actor,0,0,0,,{\\i1}Привет, "
        "мир\\NСтрока"));
    const auto srt = doc.serialize("srt", &error);
    QVERIFY(srt.contains("Привет, мир\nСтрока") && !srt.contains("{\\i1}"));
    QVERIFY(doc.parse(sample, "srt", &error));
    QTemporaryDir dir;
    for (const auto &encoding :
         {"UTF-8", "CP1251", "KOI8-R", "UTF-16LE", "UTF-16BE"}) {
      const auto path = dir.filePath(QString(encoding) + ".srt");
      QVERIFY2(doc.save(path, "srt", encoding, &error), qPrintable(error));
      SubtitleDocument read;
      const auto input =
          QString(encoding) == "KOI8-R" ? QString("KOI8-R") : QString("auto");
      QVERIFY2(read.load(path, input, &error), qPrintable(error));
      QCOMPARE(read.cues[0].text, QString("Привет, мир"));
    }
    const auto target = dir.filePath("unchanged.srt");
    write(target, "original");
    QVERIFY(!doc.save(target, "srt", "ISO-8859-1", &error));
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("original"));
  }
  void editorRowsAnchorsApplyAndCancel() {
    QString error;
    SubtitleDocument doc;
    QVERIFY(doc.parse(sample, "srt", &error));
    QTemporaryDir profile;
    double now = 1.5, seek = -1;
    SubtitleEditor editor(
        doc, subtitles, profile.path(), [&] { return now; },
        [&](double value) { seek = value; });
    editor.show();
    auto *table = editor.findChild<QTableWidget *>("subtitleCueTable");
    QVERIFY(table);
    auto click = [&](const char *name) {
      editor.findChild<QPushButton *>(name)->click();
    };
    table->setCurrentCell(0, 0);
    click("subtitleJump");
    QCOMPARE(seek, 1.);
    click("subtitleAnchor1");
    table->setCurrentCell(1, 0);
    now = 9;
    click("subtitleAnchor2");
    click("subtitleTwoPoint");
    QCOMPARE(table->item(0, 1)->text(), QString("00:00:01,500"));
    QCOMPARE(table->item(1, 1)->text(), QString("00:00:09,000"));
    table->item(1, 3)->setText("Исправлено");
    auto *search = editor.findChild<QLineEdit *>("subtitleSearch");
    search->setText("Исправлено");
    QVERIFY(table->isRowHidden(0) && !table->isRowHidden(1));
    search->clear();
    table->setCurrentCell(1, 0);
    click("subtitleCopySelected");
    QCOMPARE(QApplication::clipboard()->text(), QString("Исправлено"));
    editor.findChild<QDoubleSpinBox *>("subtitleShiftSeconds")->setValue(-100);
    click("subtitleShift");
    QCOMPARE(table->item(0, 1)->text(), QString("00:00:01,500"));
    QSignalSpy applied(&editor, &SubtitleEditor::applyRequested);
    click("subtitleApplyCopy");
    QCOMPARE(applied.size(), 1);
    const auto path = applied[0][0].toString();
    QVERIFY(path.startsWith(profile.path() + "/Subtitles/Edits/"));
    SubtitleDocument corrected;
    QVERIFY(corrected.load(path, "UTF-8", &error));
    QCOMPARE(corrected.cues[1].text, QString("Исправлено"));
    SubtitleDocument original;
    QVERIFY(original.load(subtitles, "UTF-8", &error));
    QCOMPARE(original.cues[1].text, QString("Вторая строка"));
    table->item(0, 2)->setText("00:00:00,100");
    QVERIFY(!editor.saveTo(profile.filePath("bad.srt"), "UTF-8", &error));
    QVERIFY(!QFileInfo::exists(profile.filePath("bad.srt")));
    click("subtitleResetEdits");
    QCOMPARE(table->item(0, 1)->text(), QString("00:00:01,000"));
    const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty())
      editor.grab().save(screenshots + "/subtitle-editor.png");
    table->item(0, 3)->setText("Unsaved");
    QTimer::singleShot(100, &editor, [&] {
      auto *box = editor.findChild<QMessageBox *>();
      QVERIFY(box);
      box->button(QMessageBox::Discard)->click();
    });
    QTest::keyClick(&editor, Qt::Key_Escape);
    QVERIFY(!editor.isVisible());
  }
  void preferencesCancelApplyAndPaths() {
    QTemporaryDir directory;
    Profile p(directory.path());
    prepare(p);
    Window window(p);
    window.show();
    bool save = false;
    const auto edit = [&] {
      afterPreferencesReady(&window, [&] {
        auto *dialog = window.findChild<QDialog *>("preferencesDialog");
        QVERIFY(dialog);
        for (const char *key :
             {"subtitles/preferExternal", "subtitles/blockEmbedded",
              "subtitles/autoReload"})
          dialog->findChild<QCheckBox *>(key)->setChecked(true);
        auto *paths =
            dialog->findChild<QPlainTextEdit *>("subtitles/searchPaths");
        QVERIFY(paths);
        paths->setPlainText("subs: русский\n/tmp/sub title\nrelative");
        dialog->findChild<QComboBox *>("subtitles/codepage")
            ->setCurrentText("CP1251");
        dialog->findChild<QLineEdit *>("subtitles/editorProgram")
            ->setText("/usr/bin/xed");
        auto *scroll = paths->parentWidget();
        while (scroll && !qobject_cast<QScrollArea *>(scroll))
          scroll = scroll->parentWidget();
        auto *tabs = dialog->findChild<QTabWidget *>();
        QVERIFY(scroll);
        tabs->setCurrentWidget(scroll);
        const auto screenshots =
            qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        if (save && !screenshots.isEmpty())
          dialog->grab().save(screenshots + "/subtitle-files.png");
        dialog->findChild<QDialogButtonBox *>()
            ->button(save ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
        const bool stillOpen = dialog->isVisible();
        if (stillOpen)
          dialog->reject();
        QVERIFY2(!stillOpen, "Preferences rejected the requested values");
      });
      action(window, "preferences")->trigger();
    };
    edit();
    QVERIFY(!p.settings.contains("subtitles/searchPaths"));
    save = true;
    edit();
    QVERIFY(p.settings.value("subtitles/blockEmbedded").toBool());
    auto *player = window.findChild<Player *>();
    QCOMPARE(player->get("sub-file-paths").toStringList(),
             QStringList({"subs: русский", "/tmp/sub title", "relative"}));
    QCOMPARE(player->get("sub-codepage").toString(), QString("CP1251"));
  }
  void playbackSearchPolicyNavigationCopyAndReload() {
    write(subtitles, sample.toUtf8());
    QTemporaryDir directory;
    Profile p(directory.path());
    prepare(p);
    p.settings.setValue("subtitles/searchPaths", this->directory);
    p.settings.setValue("subtitles/codepage", "UTF-8");
    p.settings.setValue("subtitles/blockEmbedded", true);
    p.settings.setValue("subtitles/preferExternal", true);
    p.settings.setValue("subtitles/autoReload", true);
    Window window(p);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({video});
    QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(selected(player).value("external").toBool());
    player->set("pause", true);
    QVERIFY(window.seekTo(1.2));
    QTRY_COMPARE(player->get("sub-text").toString(), QString("Привет, мир"));
    action(window, "subtitleCopy")->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QString("Привет, мир"));
    auto *menu = window.findChild<QMenu *>("subtitleTracksMenu");
    QVERIFY(menu);
    for (const auto &track : player->get("track-list").toList())
      if (track.toMap().value("type") == "sub" &&
          !track.toMap().value("external").toBool())
        for (auto *entry : menu->actions())
          if (entry->data().toInt() == track.toMap().value("id").toInt())
            QVERIFY(!entry->isEnabled());
    action(window, "subtitleCueNext")->trigger();
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 6) < .15);
    QVERIFY(window.seekTo(1.2));
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 1.2) < .05);
    action(window, "subtitleSyncNext")->trigger();
    QTRY_VERIFY(player->get("sub-delay").toDouble() < -4);
    player->set("sub-delay", 0);
    // Observe the stable initial snapshot, then replace the file atomically.
    QTest::qWait(900);
    write(subtitles, QString(sample)
                         .replace("Привет, мир", "Обновлено автоматически")
                         .toUtf8());
    QTRY_COMPARE_WITH_TIMEOUT(player->get("sub-text").toString(),
                              QString("Обновлено автоматически"), 5000);
    write(
        subtitles,
        QString(sample).replace("Привет, мир", "Перечитано вручную").toUtf8());
    action(window, "subtitleReload")->trigger();
    QTRY_COMPARE(player->get("sub-text").toString(),
                 QString("Перечитано вручную"));
    action(window, "subtitleNext")->trigger();
    QVERIFY(!selected(player).value("external").toBool());
    action(window, "subtitleNext")->trigger();
    QTRY_VERIFY(selected(player).value("external").toBool());
    window.closeCurrent();
    write(subtitles, sample.toUtf8());
  }
  void embeddedEditorExtractionAndAppliedCopy() {
    QTemporaryDir directory;
    Profile p(directory.path());
    prepare(p);
    p.settings.setValue("subtitles/autoLoad", false);
    Window window(p);
    window.show();
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({video});
    QTRY_COMPARE(loaded.size(), 1);
    for (const auto &entry : player->get("track-list").toList()) {
      const auto track = entry.toMap();
      if (track.value("type") == "sub" && !track.value("external").toBool()) {
        player->set("sid", track.value("id"));
        break;
      }
    }
    QTRY_VERIFY(!selected(player).isEmpty());
    QVERIFY(!selected(player).value("external").toBool());
    player->set("pause", true);
    player->set("sub-delay", .5);
    bool visited = false;
    QTimer probe;
    probe.setInterval(50);
    connect(&probe, &QTimer::timeout, &window, [&] {
      auto *dialog = window.findChild<SubtitleEditor *>("subtitleEditor");
      if (!dialog)
        return;
      probe.stop();
      visited = true;
      auto *table = dialog->findChild<QTableWidget *>("subtitleCueTable");
      QCOMPARE(table->rowCount(), 2);
      table->item(0, 3)->setText("Исправленная встроенная");
      dialog->findChild<QPushButton *>("subtitleApplyCopy")->click();
      dialog->close();
    });
    probe.start();
    action(window, "subtitleEditor")->trigger();
    QVERIFY(visited);
    QTRY_VERIFY(selected(player).value("external").toBool());
    QCOMPARE(player->get("sub-delay").toDouble(), 0.);
    QVERIFY(window.seekTo(1.2));
    QTRY_COMPARE(player->get("sub-text").toString(),
                 QString("Исправленная встроенная"));
    const auto files = QDir(directory.path() + "/Subtitles/Edits")
                           .entryList({"*.srt"}, QDir::Files);
    QCOMPARE(files.size(), 1);
    QVERIFY(QDir(directory.path() + "/Subtitles/Extracted")
                .entryList(QDir::Files)
                .isEmpty());
  }
};
int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  Q_INIT_RESOURCE(resources);
  applyTheme(app);
  SubtitleTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "subtitle_test.moc"
