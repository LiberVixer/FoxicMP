#pragma once
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
class QSettings;

struct OnlineEntry {
  QString url;
  QString title;
};

namespace OnlineOptions {
QString defaultUserAgent();
QString resolveYtDlp(QSettings &settings);
QString formatSelector(QSettings &settings);
QMap<QString, QString> mpvOptions(QSettings &settings);
QStringList playlistArguments(QSettings &settings, const QString &url);
QString playbackUrl(QSettings &settings, const QString &source,
                    QString *error = nullptr);
bool parsePlaylist(const QByteArray &json, QList<OnlineEntry> *entries,
                   QString *error);
} // namespace OnlineOptions
