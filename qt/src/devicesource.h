#pragma once
#include <QMap>
#include <QStringList>

namespace DeviceSource {
struct Request {
  QString kind, path, audio, channels, channel;
  int title = 0, angle = 1, card = 0;
};
struct Plan {
  QString url, error;
  QMap<QString, QString> options;
  bool live = false;
};
bool isDevice(const QString &source);
QString encode(const Request &request);
bool decode(const QString &source, Request *request, QString *error = nullptr);
QString label(const QString &source);
bool rememberPosition(const QString &source);
bool isLive(const QString &source);
Plan resolve(const QString &source, const QStringList &protocols,
             const QStringList &formats, const QString &profileDirectory);
QStringList opticalDrives();
QList<QPair<QString, QString>> videoDevices();
QStringList channels(const QByteArray &data, QString *error = nullptr);
} // namespace DeviceSource
