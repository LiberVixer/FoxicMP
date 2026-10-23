#pragma once
#include <QString>
namespace ProfileLaunch {
QString configPath(const QString &programDirectory);
// No file means portable defaults. Explicit --profile / --portable bypass this.
bool resolve(const QString &programDirectory, QString *directory, QString *error);
bool select(const QString &programDirectory, const QString &directory, QString *error);
bool launcher(const QString &executable, const QString &directory,
              const QString &desktopFile, QString *error);
}
