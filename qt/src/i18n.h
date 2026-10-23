#pragma once
#include <QCoreApplication>
#include <QString>
class QSettings;
inline QString uiTr(const char *text) {
  return QCoreApplication::translate("FoxicMP", text);
}
namespace I18n {
bool load(QSettings &settings, const QString &directory);
}
