#pragma once
#include <QVariantMap>
class Player;
class QSettings;
namespace AudioEncoding {
QVariantMap defaults();
QVariantMap values(QSettings &);
bool applyDecoder(Player *, QSettings &, const QVariantMap &processing,
                  bool *passthrough, bool *restart);
QString quantizer(const QVariantMap &);
} // namespace AudioEncoding
