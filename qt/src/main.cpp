#include "devicesource.h"
#include "branding.h"
#include "windowsruntime.h"
#include "stdinstream.h"
#ifdef Q_OS_WIN
#include <io.h>
#define STDIN_FILENO 0
#else
#include <unistd.h>
#endif
#include "i18n.h"
#include "profile.h"
#include "profilelaunch.h"
#include "theme.h"
#include "window.h"
#include "maintenance.h"
#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include "launchoptions.h"
#include "progressiveapi.h"
#include "player.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>
#include <clocale>
#include <cstdio>
int main(int argc, char **argv) {
#ifdef Q_OS_WIN
  WindowsRuntime windowsRuntime;
  if (!windowsRuntime.valid()) { std::fprintf(stderr, "Cannot initialize Windows Runtime\n"); return 1; }
#endif
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication app(argc, argv);
  std::setlocale(LC_NUMERIC, "C");
#ifdef Q_OS_WIN
  app.setApplicationName("FoxicMPq");
  // Helpers are portable too. This changes only this process and its children.
  qputenv("PATH", (QDir::toNativeSeparators(app.applicationDirPath()) + ';' + qEnvironmentVariable("PATH")).toLocal8Bit());
#else
  app.setApplicationName("FoxicMPQt");
#endif
  app.setApplicationVersion(FOXICMP_VERSION);
  Q_INIT_RESOURCE(resources);
  const QFileInfo startupIni(QDir(QCoreApplication::applicationDirPath()).filePath("FoxicMPQt.ini"));
  // Recovery must not read the broken INI or its configured external language
  // assets before the reset lease and file checks have run.
  if (!app.arguments().contains("--reset-settings") && !startupIni.isSymLink() &&
      (!startupIni.exists() || (startupIni.isFile() && startupIni.size() <= 8 * 1024 * 1024))) {
    QSettings startupLanguage(startupIni.absoluteFilePath(), QSettings::IniFormat);
    I18n::load(startupLanguage, QCoreApplication::applicationDirPath());
  }
  QCommandLineParser parser;
  parser.setApplicationDescription(uiTr("FoxiqMP qt — переносимый медиаплеер"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({"harvester-session",
                    uiTr("Экспериментальный локальный MP4/fMP4-сеанс"),
                    "manifest"});
  parser.addOption({"progressive-api-info", "Print local progressive API capabilities and endpoint as JSON"});
  parser.addOption(
      {"dvd", uiTr("Открыть DVD из дисковода, папки или ISO"), "path"});
  parser.addOption(
      {"bluray", uiTr("Открыть Blu-ray из дисковода, папки или ISO"), "path"});
  parser.addOption({"cdda", uiTr("Открыть Audio CD из дисковода"), "device"});
  parser.addOption(
      {"disc-title",
       uiTr("DVD: номер с 1; Blu-ray: номер MPLS; 0 — автоматически"), "number",
       "0"});
  parser.addOption({"profile",
                    uiTr("Папка профиля (по умолчанию рядом с программой)"),
                    "directory"});
  parser.addOption({"portable", uiTr("Профиль рядом с программой, игнорировать сохранённый выбор")});
  parser.addOption(
      {"reuse-instance", uiTr("Передать запрос первому окну этого профиля")});
  parser.addOption({"new-instance", uiTr("Запустить отдельное окно")});
  parser.addOption(
      {"enqueue", uiTr("Добавить файлы в очередь без смены воспроизведения")});
  parser.addOption(
      {"no-activate", uiTr("Не показывать существующее окно при передаче")});
  LaunchOptions::addArguments(parser);
  parser.addPositionalArgument("files", uiTr("Медиафайлы для воспроизведения"),
                               "[files…]");
#ifdef Q_OS_WIN
  // Qt's automatic GUI help/version handling opens a modal message box on
  // Windows. CLI callers and packaging need a bounded response on their pipe.
  if (!parser.parse(app.arguments())) {
    std::fprintf(stderr, "%s\n", qPrintable(parser.errorText())); return 1;
  }
  if (parser.isSet("help")) {
    const auto help = parser.helpText().toUtf8();
    std::fwrite(help.constData(), 1, help.size(), stdout); return 0;
  }
  if (parser.isSet("version")) {
    std::fprintf(stdout, "%s %s\n", qPrintable(app.applicationName()), qPrintable(app.applicationVersion())); return 0;
  }
#else
  parser.process(app);
#endif
  const auto positional = parser.positionalArguments();
  const bool stdinDash = positional == QStringList{"-"};
  const bool fromStdin = parser.isSet("stdin") || stdinDash;
  if (fromStdin) {
    const QStringList allowed{"stdin", "profile", "portable", "new-instance", "no-activate",
        "pause", "volume", "rate", "after", "fullscreen", "minimized", "normal",
        "window-size", "window-position"};
    for (const auto &option : parser.optionNames())
      if (!allowed.contains(option)) parser.showHelp(1);
    if ((parser.isSet("stdin") && !positional.isEmpty()) ||
        (!positional.isEmpty() && !stdinDash) || parser.optionNames().count("stdin") > 1 ||
        (parser.isSet("after") && !QStringList{"none", "exit", "close", "minimize"}.contains(parser.value("after"))))
      parser.showHelp(1);
    QString error;
    if (!StdinStream::supported(STDIN_FILENO, &error)) {
      std::fprintf(stderr, "%s\n", qPrintable(error)); return 1;
    }
  } else if (positional.contains("-")) parser.showHelp(1);
  if (parser.isSet("reset-settings") || parser.isSet("reset-library")) {
    if (!parser.isSet("reset-settings") || !parser.positionalArguments().isEmpty())
      parser.showHelp(1);
    for (const auto &option : parser.optionNames())
      if (!QStringList{"reset-settings", "reset-library", "profile", "portable"}.contains(option))
        parser.showHelp(1);
  }
  QString disc;
  int count =
      parser.isSet("dvd") + parser.isSet("bluray") + parser.isSet("cdda");
  if ((parser.isSet("portable") && parser.isSet("profile")) ||
      (parser.isSet("reuse-instance") && parser.isSet("new-instance")) ||
      (parser.isSet("enqueue") &&
       (count || parser.isSet("harvester-session"))) ||
      (parser.isSet("harvester-session") &&
       !parser.positionalArguments().isEmpty()) ||
      count > 1 ||
      (count && (parser.isSet("harvester-session") ||
                 !parser.positionalArguments().isEmpty())) ||
      (!count && parser.isSet("disc-title")))
    parser.showHelp(1);
  if (parser.isSet("clipboard") &&
      (count || parser.isSet("harvester-session") || parser.isSet("command") ||
       !parser.positionalArguments().isEmpty()))
    parser.showHelp(1);
  if (count) {
    DeviceSource::Request request;
    request.kind = parser.isSet("dvd")      ? "dvd"
                   : parser.isSet("bluray") ? "bluray"
                                            : "cdda";
    request.path = QFileInfo(parser.value(request.kind)).absoluteFilePath();
    bool ok = false;
    request.title = parser.value("disc-title").toInt(&ok);
    DeviceSource::Request checked;
    if (!ok || !DeviceSource::decode(DeviceSource::encode(request), &checked))
      parser.showHelp(1);
    disc = DeviceSource::encode(request);
  }
  if (parser.isSet("list-monitors") || parser.isSet("list-audio-devices")) {
    for (const auto &option : parser.optionNames())
      if (!QStringList{"list-monitors", "list-audio-devices", "profile", "portable"}.contains(option))
        parser.showHelp(1);
    if (!parser.positionalArguments().isEmpty()) parser.showHelp(1);
    QJsonObject information;
    if (parser.isSet("list-monitors")) {
      QJsonArray monitors;
      int index = 0;
      for (auto *screen : QGuiApplication::screens())
        monitors.append(QJsonObject{{"index", ++index}, {"name", screen->name()},
            {"primary", screen == QGuiApplication::primaryScreen()},
            {"x", screen->geometry().x()}, {"y", screen->geometry().y()},
            {"width", screen->geometry().width()}, {"height", screen->geometry().height()}});
      information["monitors"] = monitors;
    }
    if (parser.isSet("list-audio-devices")) {
      Player query;
      QJsonArray devices;
      for (const auto &entry : query.get("audio-device-list").toList()) {
        const auto device = entry.toMap();
        if (!device.value("name").toString().startsWith("pulse/foxicmp_dual_"))
          devices.append(QJsonObject::fromVariantMap(device));
      }
      information["audioDevices"] = devices;
    }
    const auto json = QJsonDocument(information).toJson();
    std::fwrite(json.constData(), 1, json.size(), stdout);
    return 0;
  }
  QString directory =
      parser.isSet("profile")
          ? QFileInfo(parser.value("profile")).absoluteFilePath()
          : QCoreApplication::applicationDirPath();
  if (!parser.isSet("profile") && !parser.isSet("portable")) {
    QString error;
    if (!ProfileLaunch::resolve(QCoreApplication::applicationDirPath(), &directory, &error)) {
      std::fprintf(stderr, "%s\n", qPrintable(error));
      return 1;
    }
  }
  if (parser.isSet("progressive-api-info")) {
    for (const auto &option : parser.optionNames())
      if (!QStringList{"progressive-api-info", "profile", "portable"}.contains(option)) parser.showHelp(1);
    if (!positional.isEmpty()) parser.showHelp(1);
    const auto json = QJsonDocument(ProgressiveApi::information(directory)).toJson();
    std::fwrite(json.constData(), 1, json.size(), stdout);
    return 0;
  }
  if (parser.isSet("reset-settings")) {
    QString backup, error;
    const bool keepLibrary = !parser.isSet("reset-library");
    if (!ProfileReset::applyDirectory(directory, keepLibrary, &backup, &error)) {
      std::fprintf(stderr, "%s\n", qPrintable(error));
      return 1;
    }
    const auto json = QJsonDocument(QJsonObject{{"reset", true}, {"profile", directory},
        {"backup", backup}, {"keptLibrary", keepLibrary}}).toJson();
    std::fwrite(json.constData(), 1, json.size(), stdout);
    return 0;
  }
  LaunchRequest request;
  request.files = fromStdin ? QStringList{} : parser.positionalArguments();
  request.disc = disc;
  if (parser.isSet("harvester-session"))
    request.session =
        QFileInfo(parser.value("harvester-session")).absoluteFilePath();
  request.enqueue = parser.isSet("enqueue");
  QString error;
  if (!LaunchOptions::parse(parser, &request.options, &error)) {
    std::fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
  }
  if (parser.isSet("clipboard") &&
      !LaunchOptions::clipboardFiles(QApplication::clipboard()->mimeData(), &request.files, &error)) {
    std::fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
  }
  if (!LaunchRequest::normalizeFiles(&request.files, &error)) {
    std::fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
  }
  LaunchRequest validated;
  if ((!fromStdin && parser.isSet("new-instance") && request.needsExisting()) ||
      !LaunchRequest::decode(request.encode(), &validated, &error)) {
    std::fprintf(stderr, "%s\n", qPrintable(error.isEmpty()
        ? uiTr("Команда требует работающего окна и несовместима с --new-instance.") : error));
    return 1;
  }
  if (!fromStdin && request.needsExisting() && !QDir(directory).exists()) {
    std::fprintf(stderr, "%s\n", qPrintable(uiTr("Нет работающего окна выбранного профиля для этой команды.")));
    return 1;
  }
  // Help, version and invalid arguments above never create a profile.
  if (!QDir().mkpath(directory)) {
    std::fprintf(stderr, "%s\n",
                 qPrintable(uiTr("Не удалось создать папку профиля.")));
    return 1;
  }
  QSettings launchSettings(QDir(directory).filePath("FoxicMPQt.ini"),
                           QSettings::IniFormat);
  I18n::load(launchSettings, directory);
  request.activate =
      (fromStdin || !request.needsExisting()) && !parser.isSet("no-activate") &&
      launchSettings.value("instance/activateOnOpen", true).toBool();
  const bool reuse =
      !parser.isSet("new-instance") &&
      (request.needsExisting() || parser.isSet("reuse-instance") || request.enqueue ||
       launchSettings.value("instance/reuseExisting", false).toBool());
  auto broker = std::make_unique<InstanceBroker>(directory);
  auto bootstrap = request;
  if (fromStdin) bootstrap.options = {}; // stdin is process-local, never sent through IPC.
  const auto result = broker->start(bootstrap, !fromStdin && reuse, !fromStdin && request.needsExisting());
  if (result == InstanceBroker::Forwarded)
    return 0;
  if (result == InstanceBroker::Failed) {
    std::fprintf(stderr, "%s\n", qPrintable(broker->errorString()));
    return 1;
  }
  applyTheme(app);
  Profile profile(directory);
  if (!profile.lease.valid() || !profile.writable()) {
    QMessageBox::critical(nullptr, "FoxiqMP qt",
                          uiTr("Папка профиля недоступна для записи."));
    return 1;
  }
  // Apply before libmpv creates decoder/render workers; no privilege elevation.
  const auto priorityResult = Maintenance::priority(
      profile.settings.value("maintenance/priority", "normal").toString(), true);
  qApp->setProperty("foxicmpPriority", priorityResult);
  try {
    int code;
    bool reset = false, keepLibrary = true;
    {
      Window window(profile, {}, {}, {}, request.options.value("web-port").toInt(-1));
      ProgressiveApi progressiveApi(&window);
      if (result == InstanceBroker::Primary && !progressiveApi.listen(directory))
        std::fprintf(stderr, "Progressive API unavailable: %s\n", qPrintable(progressiveApi.errorString()));
      window.show();
      if (fromStdin) {
        if (!window.openStdin(STDIN_FILENO, request.options)) return 1;
        // Only the player's duplicate remains, so stopping releases the pipe.
        ::close(STDIN_FILENO);
      }
      broker->setReceiver([&window](const LaunchRequest &incoming) {
        window.handleLaunchRequest(incoming);
      });
      code = app.exec();
      reset = window.requestedProfileReset(&keepLibrary);
      broker->setReceiver({});
    } // Destroy playback workers and all window writers before replacing INI.
    broker.reset(); // Stop accepting IPC before the post-exit reset/status dialog.
    if (reset) {
      QString backup, error;
      if (!ProfileReset::apply(profile, keepLibrary, &backup, &error)) {
        QMessageBox::critical(nullptr, uiTr("Сброс настроек"), error);
        return 1;
      }
      QMessageBox::information(nullptr, uiTr("Сброс настроек"),
          uiTr("Настройки сброшены. Откройте плеер снова. Резервная копия INI: %1").arg(backup));
    }
    return code;
  } catch (const std::exception &e) {
    QMessageBox::critical(nullptr, "FoxiqMP qt", QString::fromUtf8(e.what()));
    return 1;
  }
}
