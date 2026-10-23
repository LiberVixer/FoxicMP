#pragma once
#include <QVariantMap>
class Player;
class QSettings;
namespace VideoOutput {
QVariantMap defaults();
QVariantMap values(QSettings &);
bool apply(Player *, QSettings &);
} // namespace VideoOutput
