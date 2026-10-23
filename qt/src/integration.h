#pragma once
#include <QString>
#include <functional>
class QObject;
// Linux desktop entries are integration files; all player preferences remain
// portable.
namespace Integration {
void refreshDatabase(QObject *owner, std::function<void(const QString &)> done = {});
bool install(const QString &executable, QString *error = nullptr);
bool remove(QString *error = nullptr);
QString desktopText(const QString &executable,
                    const QString &profileDirectory = {});
bool registerApplication(const QString &executable,
                         const QString &profileDirectory = {},
                         QString *error = nullptr);
bool installForProfile(const QString &executable,
                       const QString &profileDirectory,
                       QString *error = nullptr);
} // namespace Integration
