#pragma once
#include <QList>
#include <QString>
class Player;
class QSettings;
namespace VideoShaders {
struct Entry {
  QString name, stage = "MAIN", source;
  bool enabled = true;
};
QList<Entry> read(QSettings &, QString *error = nullptr);
bool importMpvFile(const QString &path, QList<Entry> *entries,
                   QString *error = nullptr);
QByteArray encode(const QList<Entry> &);
QString validateStructure(const QList<Entry> &);
QString compile(const QString &);
QString preset(const QString &);
QString nativeSource(const Entry &);
bool apply(Player *, QSettings &);
} // namespace VideoShaders
