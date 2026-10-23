#pragma once
#include <QVariantMap>
class QSettings;
class Player;
class SubtitleDocument;
namespace SubtitleStyle {
QVariantMap values(QSettings &);
bool isCopy(const QString &path, const QString &profileDirectory);
bool apply(Player *, QSettings &, bool styledCopy = false);
QString assCopy(const SubtitleDocument &, QSettings &, QString *error);
} // namespace SubtitleStyle
