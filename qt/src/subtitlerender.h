#pragma once
#include <QVariantMap>
class QSettings;
class Player;
namespace SubtitleRender {
QVariantMap defaults();
QVariantMap values(QSettings &);
bool apply(Player *, QSettings &);
void refresh(Player *);
} // namespace SubtitleRender
