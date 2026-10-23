#pragma once
#include <QVariantMap>
class Player;
class QSettings;
namespace AudioProcessing {
QVariantMap defaults();
QVariantMap values(QSettings &);
QString graph(const QVariantMap &);
QString outputChannels(const QVariantMap &);
bool apply(Player *, QSettings &);
QString validateGraph(const QString &);
} // namespace AudioProcessing
