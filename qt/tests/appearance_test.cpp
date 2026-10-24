#include "preferencesdriver.h"
#include "appearance.h"
#include "controls.h"
#include "classicpreferences.h"
#include "framepreview.h"
#include "i18n.h"
#include "integration.h"
#include "osdlabel.h"
#include "player.h"
#include "playlistpanel.h"
#include "profile.h"
#include "theme.h"
#include "timeline.h"
#include "window.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <QtWidgets>
#include <cerrno>
#include <cmath>
#include <signal.h>
class MediaServer : public QTcpServer {
public:
  QByteArray video, lastRequest;
  int requests = 0, ranged = 0;
  explicit MediaServer(const QString &path) {
    QFile input(path);
    if (input.open(QIODevice::ReadOnly))
      video = input.readAll();
    connect(this, &QTcpServer::newConnection, this, [this] {
      while (auto *socket = nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket,
                &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
          auto request =
              socket->property("request").toByteArray() + socket->readAll();
          socket->setProperty("request", request);
          if (!request.contains("\r\n\r\n") ||
              socket->property("responded").toBool())
            return;
          socket->setProperty("responded", true);
          ++requests;
          lastRequest = request;
          if (request.contains("/hang.mp4"))
            return;
          const auto range =
              QRegularExpression("Range: bytes=(\\d+)-(\\d*)",
                                 QRegularExpression::CaseInsensitiveOption)
                  .match(QString::fromLatin1(request));
          qint64 first = 0, last = video.size() - 1;
          QByteArray status = "200 OK", headers;
          if (range.hasMatch()) {
            ++ranged;
            first = range.captured(1).toLongLong();
            if (!range.captured(2).isEmpty())
              last = std::min(last, range.captured(2).toLongLong());
            status = "206 Partial Content";
            headers = "Content-Range: bytes " + QByteArray::number(first) +
                      "-" + QByteArray::number(last) + "/" +
                      QByteArray::number(video.size()) + "\r\n";
          }
          if (first < 0 || first > last) {
            socket->write(
                "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: "
                "0\r\nConnection: close\r\n\r\n");
          } else
            socket->write(
                "HTTP/1.1 " + status +
                "\r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\n" +
                headers +
                "Content-Length: " + QByteArray::number(last - first + 1) +
                "\r\nConnection: close\r\n\r\n" +
                video.mid(first, last - first + 1));
          socket->disconnectFromHost();
        });
      }
    });
    listen(QHostAddress::LocalHost);
  }
  QString url(const QString &path = "/movie.mp4") const {
    return "http://127.0.0.1:" + QString::number(serverPort()) + path;
  }
};
class AppearanceTest : public QObject {
  Q_OBJECT
  QString media(const QString &file) {
    return QDir(qEnvironmentVariable("FOXICMP_TEST_MEDIA")).filePath(file);
  }
  void prepare(Profile &profile) {
    profile.settings.setValue("integration/prompted", true);
    profile.settings.setValue("audio/mute", true);
    profile.settings.setValue("playback/hardwareDecoding", false);
    profile.settings.setValue(
        "video/output",
        qEnvironmentVariable("FOXICMP_TEST_OUTPUT", "software"));
  }
  QAction *action(Window &window, const QString &id) {
    return window.findChild<QAction *>("action_" + id);
  }
  bool hasColor(const QImage &image) {
    int count = 0;
    for (int y = 0; y < image.height(); y += 11)
      for (int x = 0; x < image.width(); x += 11) {
        const auto c = image.pixelColor(x, y);
        count += std::max({c.red(), c.green(), c.blue()}) -
                     std::min({c.red(), c.green(), c.blue()}) >
                 50;
      }
    return count > 10;
  }
private slots:
  void themeAssetsFontAndPortablePaths() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    QImage logo(180, 90, QImage::Format_RGB32);
    logo.fill(QColor(12, 160, 54));
    QVERIFY(logo.save(temp.filePath("logo.png")));
    QImage strip(200, 20, QImage::Format_ARGB32);
    strip.fill(Qt::transparent);
    for (int index = 0; index < 10; ++index) {
      QPainter painter(&strip);
      painter.fillRect(index * 20, 0, 20, 20,
                       QColor::fromHsv(index * 30, 230, 200));
    }
    QVERIFY(strip.save(temp.filePath("buttons.png")));
    profile.settings.setValue("appearance/logoPath", "logo.png");
    profile.settings.setValue("appearance/toolbarPath", "buttons.png");
    profile.settings.setValue("appearance/theme", "light");
    profile.settings.setValue("appearance/buttonSize", 30);
    profile.settings.setValue("appearance/playlistFontSize", 16);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(qApp->palette().color(QPalette::Text).lightness() < 100);
    auto *panel = window.findChild<PlaylistPanel *>();
    QCOMPARE(panel->activeList()->font().pointSize(), 16);
    QCOMPARE(panel->createList("New")->font().pointSize(), 16);
    const auto pix =
        window.findChild<QLabel *>("playerLogo")->pixmap().toImage();
    QCOMPARE(pix.pixelColor(pix.width() / 2, pix.height() / 2),
             QColor(12, 160, 54));
    QCOMPARE(Appearance::toolbarCell("pause").pixelColor(10, 10).rgba(),
             QColor::fromHsv(30, 230, 200).rgba());
    for (auto *button :
         window.findChild<QWidget *>("controls")->findChildren<QToolButton *>())
      QCOMPARE(button->iconSize(), QSize(30, 30));
    window.close();
    profile.settings.setValue("appearance/logoPath", "missing.png");
    profile.settings.setValue("appearance/toolbarPath", "missing.png");
    profile.settings.setValue("appearance/theme", "custom");
    profile.settings.setValue("appearance/background", "#152535");
    profile.settings.setValue("appearance/foreground", "#f1f2f3");
    Window fallback(profile);
    fallback.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fallback));
    QCOMPARE(qApp->palette().color(QPalette::Window), QColor("#152535"));
    QVERIFY(!fallback.findChild<QLabel *>("playerLogo")->pixmap().isNull());
    QVERIFY(!Appearance::toolbarCell("play").isNull());
    QVERIFY(!controlIcon("play").isNull());
    fallback.close();
    // Classic's 15/16-cell layout has different previous/open/volume indices.
    QImage classic(320, 20, QImage::Format_ARGB32);
    classic.fill(Qt::transparent);
    {
      QPainter painter(&classic);
      painter.fillRect(4 * 20, 0, 20, 20, Qt::red);
      painter.fillRect(12 * 20, 0, 20, 20, Qt::green);
    }
    QVERIFY(classic.save(temp.filePath("classic.png")));
    profile.settings.setValue("appearance/toolbarPath", "classic.png");
    Appearance::apply(*qApp, profile.settings);
    QCOMPARE(Appearance::toolbarCell("previous").pixelColor(10, 10),
             QColor(Qt::red));
    QCOMPARE(Appearance::toolbarCell("volume").pixelColor(10, 10),
             QColor(Qt::green));
    QImage invalid(100, 30, QImage::Format_RGB32);
    invalid.fill(Qt::red);
    QVERIFY(invalid.save(temp.filePath("invalid.png")));
    profile.settings.setValue("appearance/toolbarPath", "invalid.png");
    Appearance::apply(*qApp, profile.settings);
    QVERIFY(!Appearance::toolbarCell("play").isNull());
    const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty()) {
      QTemporaryDir baselineRoot;
      Profile baseline(baselineRoot.path());
      prepare(baseline);
      baseline.settings.setValue("appearance/buttonSize", 16);
      Window classic(baseline);
      classic.show();
      QVERIFY(QTest::qWaitForWindowExposed(&classic));
      QVERIFY(classic.grab().save(screenshots + "/classic-layout.png"));
      classic.close();
    }
  }
  void osdRenderingAndHud() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("ui/osd", true);
    profile.settings.setValue("osd/showName", true);
    profile.settings.setValue("osd/showTime", true);
    profile.settings.setValue("osd/showClock", true);
    profile.settings.setValue("osd/fontSize", 26);
    profile.settings.setValue("osd/durationMs", 300);
    profile.settings.setValue("osd/borderWidth", 2);
    profile.settings.setValue("osd/borderColor", "#ff00ff");
    profile.settings.setValue("osd/antialias", false);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({media("Русский H264.mp4")});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    QVERIFY(window.seekTo(2.25));
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 2.25) < .1);
    auto *info = window.findChild<QLabel *>("osdInfo"),
         *clock = window.findChild<QLabel *>("osdClock"),
         *message = window.findChild<QLabel *>("osd");
    QTRY_VERIFY(info->isVisible());
    QVERIFY(info->text().contains("Русский H264"));
    QVERIFY(info->text().contains("00:00:02"));
    QVERIFY(clock->isVisible());
    QVERIFY(QRegularExpression("^\\d\\d:\\d\\d:\\d\\d$")
                .match(clock->text())
                .hasMatch());
    QCOMPARE(message->font().pixelSize(), 26);
    QVERIFY(message->font().styleStrategy() & QFont::NoAntialias);
    QTRY_VERIFY_WITH_TIMEOUT(!message->isVisible(), 2000);
    action(window, "volumeDown")->trigger();
    QTRY_VERIFY(message->isVisible());
    QVERIFY(player->rect().contains(message->geometry()));
    QVERIFY(!info->geometry().intersects(clock->geometry()));
    const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty())
      QVERIFY(window.grab().save(screenshots + "/osd.png"));
    QTRY_VERIFY_WITH_TIMEOUT(!message->isVisible(), 1500);
    QVERIFY(info->isVisible());
    QVERIFY(clock->isVisible());
    window.close();
    QWidget background;
    background.resize(250, 100);
    background.setStyleSheet("background:#008000;");
    OsdLabel label(&background);
    label.setText("Test");
    label.setGeometry(20, 20, 180, 60);
    profile.settings.setValue("osd/borderWidth", 0);
    profile.settings.setValue("osd/opacity", 0);
    label.configure(profile.settings);
    background.show();
    QVERIFY(QTest::qWaitForWindowExposed(&background));
    const auto transparent = background.grab().toImage().pixelColor(23, 23);
    profile.settings.setValue("osd/opacity", 255);
    label.configure(profile.settings);
    const auto opaque = background.grab().toImage().pixelColor(23, 23);
    QVERIFY(transparent != opaque);
    background.close();
  }
  void englishCustomAndInvalidCatalogs() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("appearance/language", "en");
    Window english(profile);
    english.show();
    QVERIFY(QTest::qWaitForWindowExposed(&english));
    QCOMPARE(action(english, "open")->text(), QString("Open…"));
    QCOMPARE(uiTr("Параметры…"), QString("Preferences…"));
    QCOMPARE(uiTr("Показано: %1 из %2. Удаление записи также удаляет её "
                  "позицию; файлы на диске сохраняются.")
                 .arg(2)
                 .arg(3),
             QString("Shown: 2 of 3. Removing an entry also removes its "
                     "position; files remain on disk."));
    afterPreferencesReady(&english, [&] {
      auto *dialog = english.findChild<QDialog *>("preferencesDialog");
      QVERIFY(dialog);
      auto *tabs = dialog->findChild<QTabWidget *>();
      QStringList names;
      for (int index = 0; index < tabs->count(); ++index)
        names.append(tabs->tabText(index));
      QVERIFY(names.contains("Appearance"));
      QVERIFY(names.contains("Launch and tray"));
      QVERIFY(names.contains("Window"));
      const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      if (!screenshots.isEmpty()) {
        tabs->setCurrentIndex(names.indexOf("Appearance"));
        QVERIFY(dialog->grab().save(screenshots + "/english-preferences.png"));
      }
      dialog->reject();
    });
    action(english, "preferences")->trigger();
    english.close();
    const auto path = temp.filePath("custom.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        QJsonDocument(
            QJsonObject{
                {"translations",
                 QJsonObject{{"Открыть…", "Custom Open"},
                             {"[Nemo Action]\nName=FoxiqMP qt\nComment=Открыть "
                              "в FoxiqMP qt\nExec=/usr/bin/env ",
                              "Not an executable template"}}}})
            .toJson());
    file.close();
    profile.settings.setValue("appearance/language", "custom");
    profile.settings.setValue("appearance/languageFile", "custom.json");
    QVERIFY(I18n::load(profile.settings, profile.directory));
    QCOMPARE(uiTr("Открыть…"), QString("Custom Open"));
    QCOMPARE(uiTr("Стоп"), QString("Стоп"));
    // Language packs provide labels, not executable desktop/Nemo templates.
    const auto oldData = qgetenv("XDG_DATA_HOME");
    const bool hadData = qEnvironmentVariableIsSet("XDG_DATA_HOME");
    const auto restoreData = qScopeGuard([&] {
      if (hadData)
        qputenv("XDG_DATA_HOME", oldData);
      else
        qunsetenv("XDG_DATA_HOME");
    });
    qputenv("XDG_DATA_HOME", temp.filePath("xdg").toUtf8());
    QVERIFY(Integration::install("/tmp/FoxiqMP qt/player"));
    QFile nemo(temp.filePath("xdg/nemo/actions/foxicmp-qt.nemo_action"));
    QVERIFY(nemo.open(QIODevice::ReadOnly));
    QVERIFY(nemo.readAll().contains("Exec=/usr/bin/env"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"translations",
                                          QJsonObject{{"Трек %1", "Bad %2"}}}})
                   .toJson());
    file.close();
    QVERIFY(!I18n::load(profile.settings, profile.directory));
    QCOMPARE(uiTr("Открыть…"), QString("Открыть…"));
    profile.settings.setValue("appearance/language", "ru");
    QVERIFY(I18n::load(profile.settings, profile.directory));
  }
  void onlinePreviewDisabledSizingAndIsolation() {
    MediaServer server(media("Русский H264.mp4"));
    QVERIFY(server.isListening());
    QVERIFY(!server.video.isEmpty());
    FramePreview preview;
    QSignalSpy ready(&preview, &FramePreview::ready),
        failed(&preview, &FramePreview::unavailable);
    preview.request(server.url(), 2);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(server.requests, 0);
    preview.configure(QSize(640, 360), true, {}, "FoxicMPTest", 3000);
    preview.request(server.url(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 6000);
    auto image = qvariant_cast<QImage>(ready.last()[0]);
    QCOMPARE(image.size(), QSize(640, 360));
    QVERIFY(hasColor(image));
    QVERIFY(server.ranged > 0);
    FramePreview local;
    local.configure(QSize(640, 360), false, {}, {});
    QSignalSpy localReady(&local, &FramePreview::ready);
    local.request(media("Русский H264.mp4"), 2);
    QTRY_COMPARE_WITH_TIMEOUT(localReady.size(), 1, 4000);
    QCOMPARE(image, qvariant_cast<QImage>(localReady.last()[0]));
    const int requests = server.requests;
    preview.request(server.url(), 2);
    QCOMPARE(ready.size(), 2);
    QCOMPARE(server.requests, requests);
    preview.configure(QSize(320, 180), true, {}, "FoxicMPTest", 1000);
    preview.request(server.url("/hang.mp4"), 2);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 2, 3000);
    preview.request(server.url(), 3);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 3, 6000);
    preview.request(server.url(), 4);
    preview.cancel();
    QTest::qWait(300);
    QCOMPARE(ready.size(), 3);
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    profile.settings.setValue("preview/online", true);
    profile.settings.setValue("preview/screenPercent", 30);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *player = window.findChild<Player *>();
    QSignalSpy loaded(player, &Player::loaded);
    window.openFiles({server.url()});
    QTRY_COMPARE(loaded.size(), 1);
    player->set("pause", true);
    QVERIFY(window.seekTo(1));
    QTRY_VERIFY(std::abs(player->get("time-pos").toDouble() - 1) < .1);
    auto *timeline = window.findChild<Timeline *>();
    auto *popup = window.findChild<QFrame *>("framePreview");
    QTest::mouseMove(timeline, QPoint(timeline->width() * 3 / 4, 8));
    const auto hasPixmap = [&] {
      for (auto *label : popup->findChildren<QLabel *>())
        if (!label->pixmap().isNull())
          return true;
      return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(hasPixmap(), 6000);
    QVERIFY(popup->isVisible());
    QVERIFY(player->get("pause").toBool());
    QVERIFY(std::abs(player->get("time-pos").toDouble() - 1) < .1);
    const int limit = std::clamp(
        window.screen()->availableGeometry().width() * 30 / 100, 160, 960);
    for (auto *label : popup->findChildren<QLabel *>())
      if (!label->pixmap().isNull())
        QCOMPARE(label->pixmap().width(), limit);
    const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty())
      QVERIFY(window.grab().save(screenshots + "/online-preview.png"));
    QTest::mouseMove(player, QPoint(20, 20));
    QTRY_VERIFY(!popup->isVisible());
    window.close();
  }
  void previewDestructionReapsWorker() {
    MediaServer server(media("Русский H264.mp4"));
    auto *preview = new FramePreview;
    preview->configure(QSize(320, 180), true, {}, "FoxicMPTest", 3000);
    preview->request(server.url("/hang.mp4"), 1);
    QTRY_VERIFY(server.requests > 0);
    auto *worker = preview->findChild<QProcess *>();
    QVERIFY(worker);
    const auto pid = worker->processId();
    QVERIFY(pid > 0);
    QElapsedTimer elapsed;
    elapsed.start();
    delete preview;
    QVERIFY(elapsed.elapsed() < 2000);
    errno = 0;
    QCOMPARE(::kill(pid, 0), -1);
    QCOMPARE(errno, ESRCH);
  }
  void extractorCoalescingAndProtocolValidation() {
    QTemporaryDir temp;
    MediaServer server(media("Русский H264.mp4"));
    const auto tool = temp.filePath("extractor with space.py"),
               calls = temp.filePath("calls.txt");
    QFile script(tool);
    QVERIFY(script.open(QIODevice::WriteOnly));
    const auto literal = [](const QString &s) {
      return QString::fromUtf8(
                 QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact))
          .mid(1)
          .chopped(1);
    };
    script.write(
        ("#!/usr/bin/python3\nimport json,time\nopen(" + literal(calls) +
         ",'a').write('call\\n')\ntime.sleep(0.4)\nprint(json.dumps({'url':" +
         literal(server.url()) +
         ",'http_headers':{'Referer':'bad\\r\\ninjected: no'}}))\n")
            .toUtf8());
    script.close();
    QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner |
                                  QFile::ExeOwner));
    FramePreview preview;
    preview.configure(QSize(320, 180), true, tool, {}, 3000);
    QSignalSpy ready(&preview, &FramePreview::ready),
        failed(&preview, &FramePreview::unavailable);
    preview.request(server.url("/watch?id=video"), 1);
    QTest::qWait(300);
    preview.request(server.url("/watch?id=video"), 2);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 6000);
    QCOMPARE(ready.last()[1].toDouble(), 2.);
    QVERIFY(!server.lastRequest.contains("injected: no"));
    QFile log(calls);
    QVERIFY(log.open(QIODevice::ReadOnly));
    QCOMPARE(log.readAll(), QByteArray("call\n"));
    log.close();
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write(
        "#!/usr/bin/python3\nprint('{\"url\":\"file:///etc/passwd\"}')\n");
    script.close();
    preview.request(server.url("/watch?id=bad"), 1);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 4000);
    QCOMPARE(ready.size(), 1);
  }
  void preferencesFollowThemeAndKeepDrafts() {
    QTemporaryDir temp;
    Profile profile(temp.path()); prepare(profile);
    profile.settings.setValue("appearance/language", "ru");
    Window window(profile); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    bool seen = false;
    afterPreferencesReady(&window, [&] {
      auto *dialog = window.findChild<QDialog *>("preferencesDialog"); QVERIFY(dialog);
      const auto closeOnExit = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
      auto *tabs = dialog->findChild<QTabWidget *>("classicPreferencesPages"); QVERIFY(tabs);
      auto *theme = dialog->findChild<QComboBox *>("appearance/theme"); QVERIFY(theme);
      auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
      const auto verify = [&](bool dark) {
        QCOMPARE(dialog->grab().toImage().pixelColor(5, 5).lightness() < 128, dark);
        auto *tree = dialog->findChild<QTreeWidget *>("classicPreferencesTree"); QVERIFY(tree);
        QCOMPARE(tree->viewport()->grab().toImage().pixelColor(2, tree->viewport()->height() - 3).lightness() < 128, dark);
        auto *edit = dialog->findChild<QLineEdit *>("appearance/toolbarPath"); QVERIFY(edit);
        const auto palette = edit->palette();
        QVERIFY(std::abs(palette.color(QPalette::Text).lightness() - palette.color(QPalette::Base).lightness()) > 100);
        QVERIFY(std::abs(dialog->palette().color(QPalette::Disabled, QPalette::Text).lightness() - dialog->palette().color(QPalette::Window).lightness()) > 50);
      };
      verify(true);
      theme->setCurrentIndex(theme->findData("light"));
      verify(true); // Changing the draft does not recolor the open dialog.
      buttons->button(QDialogButtonBox::Apply)->click(); QTest::qWait(80);
      verify(false);
      theme->setCurrentIndex(theme->findData("dark"));
      buttons->button(QDialogButtonBox::Apply)->click(); QTest::qWait(80);
      verify(true);
      const auto screenshots = qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
      for (const auto &id : {"IDD_PPAGEPLAYER", "IDD_PPAGEACCELTBL", "IDD_PPAGEINTERFACE", "IDD_PPAGEOSD", "IDD_PPAGEWINDOWSIZE"}) {
        auto *page = dialog->findChild<QScrollArea *>(id); QVERIFY(page);
        ClassicPreferences::selectPage(tabs, page); QCoreApplication::processEvents();
        QVERIFY(page->viewport()->grab().toImage().pixelColor(2, 2).lightness() < 128);
        if (!screenshots.isEmpty()) QVERIFY(dialog->grab().save(screenshots + "/dark-" + id + ".png"));
      }
      theme->setCurrentIndex(theme->findData("light"));
      dialog->reject(); seen = true;
    });
    action(window, "preferences")->trigger(); QVERIFY(seen);
    QCOMPARE(profile.settings.value("appearance/theme").toString(), QString("dark"));
    seen = false;
    afterPreferencesReady(&window, [&] {
      auto *dialog = window.findChild<QDialog *>("preferencesDialog"); QVERIFY(dialog);
      QVERIFY(dialog->grab().toImage().pixelColor(5, 5).lightness() < 128);
      dialog->reject(); seen = true;
    });
    action(window, "preferences")->trigger(); QVERIFY(seen); window.close();
  }
  void preferencesCancelApplyAndScreenshots() {
    QTemporaryDir temp;
    Profile profile(temp.path());
    prepare(profile);
    Window window(profile);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    for (bool accept : {false, true}) {
      afterPreferencesReady(&window, [&, accept] {
        auto *dialog = window.findChild<QDialog *>("preferencesDialog");
        QVERIFY(dialog);
        auto *theme = dialog->findChild<QComboBox *>("appearance/theme");
        theme->setCurrentIndex(theme->findData("light"));
        dialog->findChild<QSpinBox *>("appearance/buttonSize")->setValue(24);
        dialog->findChild<QSpinBox *>("osd/opacity")->setValue(160);
        dialog->findChild<QCheckBox *>("osd/showClock")->setChecked(true);
        dialog->findChild<QSpinBox *>("preview/screenPercent")->setValue(30);
        dialog->findChild<QCheckBox *>("preview/online")->setChecked(true);
        auto *language = dialog->findChild<QComboBox *>("appearance/language");
        language->setCurrentIndex(language->findData("en"));
        auto *tabs = dialog->findChild<QTabWidget *>();
        const auto screenshots =
            qEnvironmentVariable("FOXICMP_TEST_SCREENSHOTS");
        if (accept && !screenshots.isEmpty())
          for (const auto &entry : QList<QPair<QString, QString>>{
                 {"IDD_PPAGEINTERFACE", "appearance"}, {"IDD_PPAGEOSD", "osd"},
                 {"settingsTimeline", "preview"}}) {
            auto *page = dialog->findChild<QScrollArea *>(entry.first); QVERIFY(page);
            ClassicPreferences::selectPage(tabs, page); QCoreApplication::processEvents();
            QVERIFY(dialog->grab().save(screenshots + "/preferences-" + entry.second + ".png"));
          }
        dialog->findChild<QDialogButtonBox *>()
            ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
      });
      action(window, "preferences")->trigger();
      QCOMPARE(profile.settings.value("preview/online", false).toBool(),
               accept);
      QCOMPARE(profile.settings.value("appearance/buttonSize", 20).toInt(),
               accept ? 24 : 20);
      QCOMPARE(uiTr("Файл"),
               QString("Файл")); // Language changes on next startup.
    }
    window.close();
    Window restarted(profile);
    restarted.show();
    QVERIFY(QTest::qWaitForWindowExposed(&restarted));
    QCOMPARE(action(restarted, "open")->text(), QString("Open…"));
    QCOMPARE(qApp->palette().color(QPalette::Text), QColor("#202428"));
    restarted.close();
    profile.settings.setValue("appearance/language", "ru");
    I18n::load(profile.settings, profile.directory);
  }
};
int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  applyTheme(app);
  Q_INIT_RESOURCE(resources);
  AppearanceTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "appearance_test.moc"
