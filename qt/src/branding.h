#pragma once
#include <QString>

// Presentation only. Executable/profile names and IPC identities stay compatible.
namespace Branding {
inline QString name() { return QStringLiteral("FoxiqMP qt"); }
inline QString edition() {
#ifdef Q_OS_WIN
  return QStringLiteral("Windows QT");
#else
  return QStringLiteral("Linux QT");
#endif
}
inline QString logo(bool light) {
#ifdef Q_OS_WIN
  const QString base = QStringLiteral(":/foxiqmp-windows-qt");
#else
  const QString base = QStringLiteral(":/foxiqmp-linux-qt");
#endif
  return base + (light ? QStringLiteral("-light.png") : QStringLiteral(".png"));
}
} // namespace Branding
