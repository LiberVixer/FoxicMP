#pragma once
#include <QFont>
#include <QImage>
#include <QString>
class QApplication;
class QSettings;
class QWidget;
namespace Appearance {
void apply(QApplication &, QSettings &);
void stylePreferences(QWidget &);
QFont playlistFont(QSettings &, const QFont &fallback);
QString assetPath(const QString &directory, const QString &path);
QImage image(const QString &path, int maximumDimension = 4096);
QImage logo(QSettings &, const QString &directory);
QImage builtinLogo(const QSettings &, bool preview = false);
QImage toolbarCell(const QString &name);
} // namespace Appearance
