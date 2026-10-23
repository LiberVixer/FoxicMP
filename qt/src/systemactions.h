#pragma once
#include <QDBusConnection>
#include <QVariantList>
#include <functional>
#include <optional>
class QObject;
namespace SystemActions {
struct Request {
  bool systemBus = false;
  QString service, path, interface, method;
  QVariantList arguments;
};
QList<Request> requests(const QString &action);
void execute(const QString &action, QObject *owner,
             std::function<void(const QString &)> finished,
             std::optional<QDBusConnection> transport = std::nullopt);
} // namespace SystemActions
