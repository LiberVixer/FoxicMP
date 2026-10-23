#pragma once
#include <QMap>
#include <QStringList>
class QSettings;
namespace Associations {
struct Type {
  QString mime, category, extensions, description;
};
QList<Type> catalog();
QStringList mimeTypes();
QString configFile();
bool readDefaults(QMap<QString, QString> *values, QString *error = nullptr);
// Edits only selected defaults in the user mimeapps.list; undo journal is
// portable profile state, never a wholesale backup of another app's settings.
bool assign(QSettings &profile, const QStringList &types,
            QString *error = nullptr);
bool restore(QSettings &profile, int *restored = nullptr,
             int *skipped = nullptr, QString *error = nullptr);
bool hasDesktopReferences(const QString &desktopId);
} // namespace Associations
