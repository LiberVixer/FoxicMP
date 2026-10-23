#pragma once
#include <QString>
#include <QStringList>

namespace MediaSource {
QString normalize(const QString &source, QString *error = nullptr);
bool isNetwork(const QString &source);
bool isUrl(const QString &source);
QString displayName(const QString &source);
QStringList filesInDirectory(const QString &directory);
} // namespace MediaSource
