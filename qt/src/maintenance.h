#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <functional>
class QNetworkAccessManager;
class QNetworkReply;
namespace Maintenance {
struct Update {
  QString status, version, error;
  QUrl page;
};
Update releases(const QByteArray &, const QString &current, bool previews);
int niceValue(const QString &mode);
QVariantMap priority(const QString &mode, bool apply);
QByteArray report(const QVariantMap &runtime, const QStringList &messages,
                  bool details);
bool saveReport(const QString &path, const QByteArray &, QString *error);
}
class UpdateCheck : public QObject {
  Q_OBJECT
public:
  explicit UpdateCheck(QObject *parent = nullptr);
  bool start(const QString &current, bool previews);
  // Separate entry point for local HTTP test fixtures, never persisted in a profile.
  bool startAt(const QUrl &, const QString &current, bool previews);
  void cancel();
signals:
  void finished(const QString &status, const QString &version,
                const QUrl &page, const QString &error);
private:
  QNetworkAccessManager *network;
  QNetworkReply *reply = nullptr;
};
