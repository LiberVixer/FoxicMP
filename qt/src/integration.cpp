#include "integration.h"
#include "associations.h"
#include "i18n.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <memory>
static QString quoteExec(QString s) {
  // Desktop Exec has two escaping layers and % field codes (not shell
  // expansion).
  s.replace('\\', "\\\\\\\\");
  s.replace('"', "\\\\\"");
  s.replace('$', "\\\\$");
  s.replace('`', "\\\\`");
  s.replace('%', "%%");
  return '"' + s + '"';
}
static bool validPath(const QString &path) {
  if (!QFileInfo(path).isAbsolute())
    return false;
  for (const auto c : path)
    if (c.unicode() < 32 || c.unicode() == 127)
      return false;
  return true;
}
static QString command(const QString &exe, const QString &profile,
                       bool enqueue = false) {
  QString result = "/usr/bin/env " + quoteExec(exe);
  if (!profile.isEmpty())
    result += " --profile " + quoteExec(profile);
  if (enqueue)
    result += " --enqueue";
  return result + " -- %F";
}
QString Integration::desktopText(const QString &exe, const QString &profile) {
  if (!validPath(exe) || exe.contains('=') ||
      (!profile.isEmpty() && !validPath(profile)))
    return {};
  return "[Desktop Entry]\nType=Application\nName=FoxicMP Qt\nComment=Media "
         "Player\nExec=" +
         command(exe, profile) +
         "\nIcon=" + QDir(QFileInfo(exe).absolutePath()).filePath("foxicmp-qt.png")
                           .replace('\\', "\\\\") +
         "\nTerminal=false\nCategories=AudioVideo;Player;"
         "\nMimeType=" +
         Associations::mimeTypes().join(';') +
         ";\nActions=Enqueue;\n\n[Desktop Action Enqueue]\nName=Add to FoxicMP "
         "queue\nName[ru]=Добавить в очередь FoxicMP\nExec=" +
         command(exe, profile, true) + "\n";
}
static bool write(const QString &path, const QString &content, QString *error) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    if (error)
      *error = uiTr("Не удалось создать папку интеграции");
    return false;
  }
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly) || f.write(content.toUtf8()) < 0 ||
      !f.commit()) {
    if (error)
      *error = f.errorString();
    return false;
  }
  return true;
}
bool Integration::registerApplication(const QString &exe,
                                      const QString &profile, QString *error) {
  const auto text = desktopText(exe, profile);
  if (text.isEmpty()) {
    if (error)
      *error = uiTr("Путь программы или профиля недопустим для меню Linux.");
    return false;
  }
  const auto data =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  return write(data + "/applications/foxicmp-qt.desktop", text, error);
}
bool Integration::install(const QString &exe, QString *error) {
  return installForProfile(exe, {}, error);
}
bool Integration::installForProfile(const QString &exe, const QString &profile,
                                    QString *error) {
#ifdef Q_OS_LINUX
  const QString data =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  if (!registerApplication(exe, profile, error))
    return false;
  // Nemo quotes %F when Quote=double; do not wrap those arguments again.
  QStringList videoTypes;
  for (const auto &mime : QMimeDatabase().allMimeTypes())
    if (mime.name().startsWith("video/"))
      videoTypes.append(mime.name());
  const QString extensions =
      "264;265;3g2;3gp;amv;asf;avi;bik;dav;divx;dsm;dvr-ms;evo;f4v;flc;fli;flv;"
      "h264;h265;hevc;ivf;m1v;m2t;m2ts;m2v;m4v;mk3d;mkv;mov;mp4;mpg;mpeg;mts;"
      "mxf;nut;obu;ogm;ogv;pva;rec;rm;rmvb;roq;sfd;smk;ssif;tp;trp;ts;vc1;vob;"
      "webm;wmv;wtv;y4m;";
  QStringList dotted;
  for (const auto &extension : extensions.split(';', Qt::SkipEmptyParts))
    dotted.append("." + extension);
  const QString action =
      QStringLiteral(
          "[Nemo Action]\nName=FoxicMP Qt\nComment=Открыть в FoxicMP "
          "Qt\nExec=/usr/bin/env ") +
      quoteExec(exe) +
      (profile.isEmpty() ? QString() : " --profile " + quoteExec(profile)) +
      " -- %F\nIcon-Name=video-x-generic\nSelection=notnone\nExtensions=" +
      dotted.join(';') + ";\nMimetypes=" + videoTypes.join(';') +
      ";\nQuote=double\n";
  return write(data + "/nemo/actions/foxicmp-qt.nemo_action", action, error);
#else
  Q_UNUSED(exe);
  Q_UNUSED(profile);
  if (error)
    *error = uiTr("Интеграция этой платформы пока не реализована");
  return false;
#endif
}
bool Integration::remove(QString *error) {
  const QString data =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  for (const QString &s : {QString("/applications/foxicmp-qt.desktop"),
                           QString("/nemo/actions/foxicmp-qt.nemo_action")}) {
    if (s.endsWith(".desktop") &&
        Associations::hasDesktopReferences("foxicmp-qt.desktop"))
      continue;
    if (QFile::exists(data + s) && !QFile::remove(data + s)) {
      if (error)
        *error = uiTr("Не удалось удалить ") + data + s;
      return false;
    }
  }
  return true;
}

void Integration::refreshDatabase(QObject *owner,
                                  std::function<void(const QString &)> done) {
  const auto tool = QStandardPaths::findExecutable("update-desktop-database");
  if (tool.isEmpty()) {
    if (done)
      QTimer::singleShot(0, owner, [done] {
        done(uiTr("Не найден update-desktop-database (desktop-file-utils). "
                  "Список «Открыть с помощью» может обновиться позже."));
      });
    return;
  }
  auto *process = new QProcess(owner);
  process->setObjectName("desktopDatabaseUpdater");
  QObject::connect(owner, &QObject::destroyed, process, [process] {
    if (process->state() != QProcess::NotRunning) {
      process->kill();
      process->waitForFinished(1000);
    }
  });
  auto *deadline = new QTimer(process);
  deadline->setSingleShot(true);
  auto settled = std::make_shared<bool>(false);
  auto complete = [process, deadline, settled, done](const QString &error) {
    if (*settled)
      return;
    *settled = true;
    deadline->stop();
    if (done)
      done(error);
    process->deleteLater();
  };
  QObject::connect(deadline, &QTimer::timeout, process, [process] {
    process->setProperty("timedOut", true);
    process->kill();
  });
  QObject::connect(process, &QProcess::readyReadStandardError, process,
                   [process] {
                     if (process->readAllStandardError().size() > 65536)
                       process->kill();
                   });
  QObject::connect(
      process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), owner,
      [process, complete](int code, QProcess::ExitStatus status) {
        complete(code == 0 && status == QProcess::NormalExit &&
                         !process->property("timedOut").toBool()
                     ? QString()
                     : uiTr("Не удалось обновить кэш меню Linux. Назначения "
                            "сохранены; проверьте их в системе."));
      });
  QObject::connect(
      process, &QProcess::errorOccurred, owner,
      [complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
          complete(uiTr("Не удалось запустить обновление кэша меню Linux."));
      });
  process->setStandardOutputFile(QProcess::nullDevice());
  process->start(tool, {QDir(QStandardPaths::writableLocation(
                                 QStandardPaths::GenericDataLocation))
                            .filePath("applications")});
  deadline->start(3000);
}
