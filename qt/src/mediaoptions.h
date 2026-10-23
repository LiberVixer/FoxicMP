#pragma once
#include <QString>
class Player;
class QSettings;
namespace MediaOptions {
void apply(Player *player, QSettings &settings);
QString colorFilter(QSettings &settings);
QString previewFilters(QSettings &settings);
} // namespace MediaOptions
