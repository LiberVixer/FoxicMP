#pragma once
#include <QCommandLineParser>
#include <QJsonObject>
class QMimeData;

// Only explicit, bounded player options are accepted by CLI and local IPC.
namespace LaunchOptions {
void addArguments(QCommandLineParser &);
bool parse(const QCommandLineParser &, QJsonObject *, QString *error);
bool validate(const QJsonObject &);
bool hasPlayback(const QJsonObject &);
bool clipboardFiles(const QMimeData *, QStringList *, QString *error);
// Singular strings remain supported for earlier IPC clients; CLI emits arrays.
QStringList externalFiles(const QJsonObject &, const QString &key);
}
