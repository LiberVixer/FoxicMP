#pragma once
#include <QByteArray>
#include <QVariantMap>
class Player;
class QSettings;
namespace VideoDisplay {
QVariantMap defaults();
QVariantMap values(QSettings &);
QString validateIcc(const QByteArray &);
QByteArray readIcc(const QString &path, QString *error);
QString importIcc(const QString &source, const QString &directory,
                  QString *error);
QString resolveIcc(const QString &saved, const QString &directory);
QString effectiveSync(const QVariantMap &, bool openGl, int swapInterval,
                      bool bitstream, double refreshRate, bool visible);
bool apply(Player *, QSettings &);
void updateTiming(Player *);
} // namespace VideoDisplay
