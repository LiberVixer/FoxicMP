#include "systemactions.h"
#include "i18n.h"
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#include <powrprof.h>
#endif

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
#ifdef Q_OS_WIN
  Q_UNUSED(transport);
  QTimer::singleShot(0, owner, [action, finished] {
    bool ok = false;
    if (action == "lock") ok = LockWorkStation();
    else if (action == "logout") ok = ExitWindowsEx(EWX_LOGOFF, 0);
    else if (action == "suspend" || action == "hibernate")
      ok = SetSuspendState(action == "hibernate", FALSE, FALSE);
    else if (action == "shutdown") {
      HANDLE token = nullptr;
      TOKEN_PRIVILEGES requested{}, previous{};
      DWORD bytes = sizeof(previous);
      if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        requested.PrivilegeCount = 1;
        LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &requested.Privileges[0].Luid);
        requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (AdjustTokenPrivileges(token, FALSE, &requested, sizeof(previous), &previous, &bytes) &&
            GetLastError() != ERROR_NOT_ALL_ASSIGNED) {
          ok = ExitWindowsEx(EWX_POWEROFF, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_FLAG_PLANNED);
          AdjustTokenPrivileges(token, FALSE, &previous, 0, nullptr, nullptr);
        }
        CloseHandle(token);
      }
    }
    finished(ok ? QString() : QString("Windows error %1").arg(GetLastError()));
  });
#else
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
#endif
}
