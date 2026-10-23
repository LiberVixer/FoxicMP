#pragma once
#include <QStringList>
#include <QVariantMap>
struct PlaylistEntry {
  QString source, title;
  double duration = -1;
  QVariantMap metadata;
};
class PlaylistFile {
public:
  static bool isPlaylist(const QString &path);
  static bool readEntries(const QString &path, QList<PlaylistEntry> *entries,
                          QString *error, bool skipInvalid = false);
  static bool writeEntries(const QString &path,
                           const QList<PlaylistEntry> &entries, QString *error);
  static bool read(const QString &path, QStringList *files, QString *error);
  static bool write(const QString &path, const QStringList &files,
                    QString *error);
};
