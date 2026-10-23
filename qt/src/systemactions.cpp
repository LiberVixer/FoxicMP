#include "systemactions.h"
#include "i18n.h"
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QTimer>

QList<SystemActions::Request> SystemActions::requests(const QString &action) {
  if (action == "suspend" || action == "hibernate" || action == "shutdown") {
    const QString method = action == "suspend"     ? "Suspend"
                           : action == "hibernate" ? "Hibernate"
                                                   : "PowerOff";
    return {{true,
             "org.freedesktop.login1",
             "/org/freedesktop/login1",
             "org.freedesktop.login1.Manager",
             method,
             {true}}};
  }
  if (action == "logout")
    return {{false,
             "org.gnome.SessionManager",
             "/org/gnome/SessionManager",
             "org.gnome.SessionManager",
             "Logout",
             {QVariant::fromValue<uint>(0)}}};
  if (action == "lock")
    return {{false,
             "org.cinnamon.ScreenSaver",
             "/org/cinnamon/ScreenSaver",
             "org.cinnamon.ScreenSaver",
             "Lock",
             {QString(uiTr("FoxicMP: воспроизведение завершено"))}},
            {false,
             "org.mate.ScreenSaver",
             "/org/mate/ScreenSaver",
             "org.mate.ScreenSaver",
             "Lock",
             {}},
            {false,
             "org.freedesktop.ScreenSaver",
             "/ScreenSaver",
             "org.freedesktop.ScreenSaver",
             "Lock",
             {}},
            {false,
             "org.gnome.ScreenSaver",
             "/org/gnome/ScreenSaver",
             "org.gnome.ScreenSaver",
             "Lock",
             {}}};
  return {};
}
void SystemActions::execute(const QString &action, QObject *owner,
                            std::function<void(const QString &)> finished,
                            std::optional<QDBusConnection> transport) {
  auto connection = [transport](bool system) {
    return transport ? *transport
           : system  ? QDBusConnection::systemBus()
                     : QDBusConnection::sessionBus();
  };
  Request request;
  bool found = false;
  for (const auto &candidate : requests(action)) {
    const auto bus = connection(candidate.systemBus);
    if (bus.interface() &&
        bus.interface()->isServiceRegistered(candidate.service).value()) {
      request = candidate;
      found = true;
      break;
    }
  }
  if (!found) {
    QTimer::singleShot(0, owner, [finished] {
      finished(uiTr("Системная служба для этого действия недоступна"));
    });
    return;
  }
  auto bus = connection(request.systemBus);
  auto invoke = [owner, finished, bus, request] {
    auto message = QDBusMessage::createMethodCall(
        request.service, request.path, request.interface, request.method);
    message.setArguments(request.arguments);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), owner);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner,
                     [watcher, finished] {
                       const QDBusPendingReply<> reply = *watcher;
                       finished(reply.isError() ? reply.error().message()
                                                : QString());
                       watcher->deleteLater();
                     });
  };
  if (!request.systemBus) {
    invoke();
    return;
  }
  auto check = QDBusMessage::createMethodCall(
      request.service, request.path, request.interface, "Can" + request.method);
  auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(check), owner);
  QObject::connect(
      watcher, &QDBusPendingCallWatcher::finished, owner,
      [watcher, finished, invoke] {
        const QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError())
          finished(reply.error().message());
        else if (reply.value() == "yes" || reply.value() == "challenge")
          invoke();
        else
          finished(uiTr("Действие не поддерживается или запрещено системой"));
        watcher->deleteLater();
      });
}
