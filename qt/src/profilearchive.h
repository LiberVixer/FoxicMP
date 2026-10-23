#pragma once
#include <QMap>
#include <QString>
class Profile;
namespace ProfileArchive {
struct Bundle {
  QMap<QString, QByteArray> files;
};
bool save(Profile &, const QString &archive, QString *error);
bool read(const QString &archive, Bundle *, QString *error);
// Publishes a complete profile into a new directory; never replaces a
// directory.
bool restore(const Bundle &, const QString &newDirectory, QString *error);
QString backup(Profile &, QString *error);
} // namespace ProfileArchive
