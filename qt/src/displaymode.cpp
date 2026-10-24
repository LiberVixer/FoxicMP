#include "displaymode.h"
#include "i18n.h"
#include <QGuiApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#include <QSet>
namespace {
DisplayMode::Result windowsDisplayCommand(const QStringList &args) {
  if (args == QStringList{"--query"}) {
    QString text;
    DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
    for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &device, 0); ++index) {
      if (!(device.StateFlags & DISPLAY_DEVICE_ACTIVE) || (device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)) continue;
      DEVMODEW current{}; current.dmSize = sizeof(current);
      if (!EnumDisplaySettingsW(device.DeviceName, ENUM_CURRENT_SETTINGS, &current)) continue;
      text += QString("%1 connected %2x%3+0+0 (normal)\n")
          .arg(QString::fromWCharArray(device.DeviceName)).arg(current.dmPelsWidth).arg(current.dmPelsHeight);
      QSet<QString> seen;
      DEVMODEW mode{}; mode.dmSize = sizeof(mode);
      for (DWORD row = 0; EnumDisplaySettingsW(device.DeviceName, row, &mode); ++row) {
        if (mode.dmBitsPerPel != current.dmBitsPerPel || mode.dmDisplayFrequency <= 1 ||
            (mode.dmDisplayFlags & DM_INTERLACED)) continue;
        const auto key = QString("%1x%2 %3").arg(mode.dmPelsWidth).arg(mode.dmPelsHeight).arg(mode.dmDisplayFrequency);
        if (seen.contains(key)) continue;
        seen.insert(key);
        const bool active = mode.dmPelsWidth == current.dmPelsWidth && mode.dmPelsHeight == current.dmPelsHeight &&
                            mode.dmDisplayFrequency == current.dmDisplayFrequency;
        text += "  " + key + (active ? "*" : "") + "\n";
      }
    }
    return {true, text};
  }
  const auto value = [&args](const QString &key) { const int at = args.indexOf(key); return at >= 0 ? args.value(at + 1) : QString(); };
  const auto output = value("--output"), resolution = value("--mode"), rate = value("--rate");
  DEVMODEW mode{}; mode.dmSize = sizeof(mode);
  const auto native = reinterpret_cast<const wchar_t *>(output.utf16());
  for (DWORD row = 0; EnumDisplaySettingsW(native, row, &mode); ++row) {
    if (QString("%1x%2").arg(mode.dmPelsWidth).arg(mode.dmPelsHeight) != resolution ||
        QString::number(mode.dmDisplayFrequency) != rate) continue;
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;
    const LONG result = ChangeDisplaySettingsExW(native, &mode, nullptr, CDS_FULLSCREEN, nullptr);
    return {result == DISP_CHANGE_SUCCESSFUL, QString::number(result)};
  }
  return {false, QString("Windows display mode unavailable")};
}
}
#endif
DisplayMode::DisplayMode(Command runner) : command(std::move(runner)) {
  if (!command)
#ifdef Q_OS_WIN
    command = windowsDisplayCommand;
#else
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
#endif
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
