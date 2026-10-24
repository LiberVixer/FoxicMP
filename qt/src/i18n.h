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

inline QString uiPlatformTr(const char *linuxText, const char *windowsText) {
#ifdef Q_OS_WIN
  Q_UNUSED(linuxText); return uiTr(windowsText);
#else
  Q_UNUSED(windowsText); return uiTr(linuxText);
#endif
}
