#include "displaymode.h"
#include "i18n.h"
#include <QGuiApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <utility>
DisplayMode::DisplayMode(Command runner) : command(std::move(runner)) {
  if (!command)
    command = [](const QStringList &args) -> Result {
      if (QGuiApplication::platformName() != "xcb" ||
          qEnvironmentVariable("XDG_SESSION_TYPE")
                  .compare("wayland", Qt::CaseInsensitive) == 0)
        return {false,
                uiTr("Смена режима экрана доступна только в сеансе X11")};
      const auto executable = QStandardPaths::findExecutable("xrandr");
      if (executable.isEmpty())
        return {false, uiTr("Для смены режима нужен xrandr")};
      QProcess process;
      auto env = QProcessEnvironment::systemEnvironment();
      env.insert("LC_ALL", "C");
      process.setProcessEnvironment(env);
      process.start(executable, args);
      if (!process.waitForFinished(1800)) {
        process.kill();
        process.waitForFinished(200);
        return {false, uiTr("xrandr не ответил вовремя")};
      }
      const bool ok = process.exitStatus() == QProcess::NormalExit &&
                      process.exitCode() == 0;
      return {ok, QString::fromUtf8(ok ? process.readAllStandardOutput()
                                       : process.readAllStandardError())
                      .left(1024 * 1024)};
    };
}
DisplayMode::~DisplayMode() { restore(); }
QList<DisplayMode::Mode> DisplayMode::parse(const QString &text) {
  QList<Mode> result;
  QString output;
  // Exclude disconnected, disabled and rotated/reflected outputs. Restoring a
  // rotated layout requires more than selecting a resolution and refresh rate.
  const QRegularExpression header(
      "^(\\S+) connected (?:primary )?\\d+x\\d+[+-]\\d+[+-]\\d+(?: \\(normal| "
      "normal \\().*");
  const QRegularExpression row("^\\s+(\\S+)\\s+(.+)$"),
      rate("^([0-9]+(?:\\.[0-9]+)?)([*+]*)$");
  for (const auto &line : text.split('\n')) {
    if (!line.startsWith(' ') && !line.startsWith('\t')) {
      const auto match = header.match(line);
      output = match.hasMatch() ? match.captured(1) : QString();
      continue;
    }
    if (output.isEmpty())
      continue;
    const auto match = row.match(line);
    if (!match.hasMatch())
      continue;
    for (const auto &token : match.captured(2).split(QRegularExpression("\\s+"),
                                                     Qt::SkipEmptyParts)) {
      const auto hz = rate.match(token);
      if (hz.hasMatch() && hz.captured(1).toDouble() > 1)
        result.append({output, match.captured(1), hz.captured(1),
                       hz.captured(2).contains('*')});
    }
  }
  return result;
}
QList<DisplayMode::Mode> DisplayMode::modes(QString *error) const {
  const auto result = command({"--query"});
  if (!result.ok) {
    if (error)
      *error = result.text;
    return {};
  }
  return parse(result.text);
}
bool DisplayMode::enter(const QString &output, const QString &name,
                        const QString &rate, QString *error) {
  if (changed && !restore(error))
    return false;
  QString message;
  const auto list = modes(&message);
  Mode current;
  bool allowed = false;
  for (const auto &entry : list) {
    if (entry.output != output)
      continue;
    if (entry.current)
      current = entry;
    if (entry.name == name && entry.rate == rate)
      allowed = true;
  }
  if (!allowed || current.name.isEmpty()) {
    if (error)
      *error = message.isEmpty()
                   ? uiTr("Выбранный монитор или режим сейчас недоступен")
                   : message;
    return false;
  }
  if (current.name == name && current.rate == rate)
    return true;
  previous = current;
  // A failed command can still have partially changed the output. Keep the
  // original mode available for restoration on exit and retry after failure.
  changed = true;
  const auto result =
      command({"--output", output, "--mode", name, "--rate", rate});
  if (!result.ok) {
    if (error)
      *error = result.text;
    restore();
    return false;
  }
  return true;
}
bool DisplayMode::restore(QString *error) {
  if (!changed)
    return true;
  const auto result = command({"--output", previous.output, "--mode",
                               previous.name, "--rate", previous.rate});
  if (!result.ok) {
    if (error)
      *error = result.text;
    return false;
  }
  changed = false;
  return true;
}
