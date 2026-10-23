#include "systemicc.h"
#include "i18n.h"
#include "player.h"
#include "videodisplay.h"
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <memory>
#include <unistd.h>

namespace {
const QString service = "org.freedesktop.ColorManager";
const QString root = "/org/freedesktop/ColorManager";
const QString deviceInterface = service + ".Device";
QString objectPath(const QDBusMessage &reply) {
  if (reply.arguments().size() != 1)
    return {};
  const auto path =
      qdbus_cast<QDBusObjectPath>(reply.arguments().first()).path();
  return path == "/" ? QString() : path;
}
QVariantMap properties(const QDBusMessage &reply) {
  return reply.arguments().size() == 1
             ? qdbus_cast<QVariantMap>(reply.arguments().first())
             : QVariantMap();
}
} // namespace
SystemIcc::SystemIcc(Player *p, const QDBusConnection &connection,
                     const QString &platformName, bool trackWindow)
    : QObject(p), player(p), bus(connection),
      platform(platformName.isEmpty() ? QGuiApplication::platformName()
                                      : platformName),
      timer(new QTimer(this)), followWindow(trackWindow) {
  setObjectName("foxicmpSystemIcc");
  timer->setInterval(500);
  connect(timer, &QTimer::timeout, this, &SystemIcc::tick);
  connect(qGuiApp, &QGuiApplication::screenRemoved, this,
          [this] { QTimer::singleShot(0, this, &SystemIcc::tick); });
}
void SystemIcc::configure(bool enabled, const QString &profileDirectory) {
  ++generation; // Invalidate replies from the previous settings snapshot.
  active = enabled;
  pending = false;
  directory = profileDirectory;
  output.clear();
  player->setProperty("foxicmpSystemIccFile", QString());
  player->setProperty("foxicmpSystemIccOutput", QString());
  checked.invalidate();
  timer->stop();
  if (active) {
    timer->start();
    QTimer::singleShot(0, this, &SystemIcc::tick);
  }
}
void SystemIcc::tick() {
  if (!active || !followWindow)
    return;
  auto *currentWindow = player->window()->windowHandle();
  if (window != currentWindow) {
    disconnect(screenConnection);
    window = currentWindow;
    if (window)
      screenConnection = connect(window, &QWindow::screenChanged, this, [this] {
        checked.invalidate();
        tick();
      });
  }
  const auto *screen = window ? window->screen() : player->screen();
  const auto name = screen ? screen->name() : QString();
  // A native window may report the same screen more than once while the
  // asynchronous colord request is still in flight. Do not invalidate that
  // request unless the output actually changed.
  if (name != output ||
      (!pending && (!checked.isValid() || checked.elapsed() >= 5000)))
    refresh(name);
}
void SystemIcc::ask(quint64 revision, const QString &path,
                    const QString &interface, const QString &method,
                    const QVariantList &args,
                    std::function<void(const QDBusMessage &)> callback) {
  auto message =
      QDBusMessage::createMethodCall(service, path, interface, method);
  message.setArguments(args);
  // Read-only methods; normal D-Bus activation of installed colord is allowed.
  // No authorization requests or synchronous waits on the UI thread.
  auto *watcher =
      new QDBusPendingCallWatcher(bus.asyncCall(message, 1500), this);
  // Qt/D-Bus timeouts may be checked coarsely. Enforce a local deadline too,
  // and discard a late success after reporting failure.
  auto settled = std::make_shared<bool>(false);
  auto *deadline = new QTimer(watcher);
  deadline->setSingleShot(true);
  deadline->setTimerType(Qt::PreciseTimer);
  connect(deadline, &QTimer::timeout, this, [this, watcher, revision, settled] {
    if (*settled)
      return;
    *settled = true;
    watcher->deleteLater();
    finish(revision, uiTr("Авто ICC: colord не ответил за 1,5 секунды."));
  });
  deadline->start(1500);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this, watcher, revision, callback, settled, deadline] {
            if (*settled)
              return;
            *settled = true;
            deadline->stop();
            const QDBusMessage reply = watcher->reply();
            watcher->deleteLater();
            if (!active || revision != generation)
              return;
            if (reply.type() == QDBusMessage::ErrorMessage)
              finish(revision, uiTr("Авто ICC: colord не вернул профиль (%1).")
                                   .arg(reply.errorName().left(180)));
            else
              callback(reply);
          });
}
void SystemIcc::refresh(const QString &screenName) {
  if (!active)
    return;
  const bool changed = output != screenName;
  output = screenName;
  const auto revision = ++generation;
  checked.restart();
  pending = true;
  if (changed || platform != "xcb" || output.isEmpty()) {
    player->set("icc-profile", "");
    player->setProperty("foxicmpSystemIccFile", QString());
    player->setProperty("foxicmpSystemIccOutput", output);
  }
  player->setProperty("foxicmpIccStatus",
                      uiTr("Авто ICC: поиск профиля экрана %1…").arg(output));
  if (platform != "xcb") {
    finish(revision, uiTr("Авто ICC доступен в X11 через colord. Для этого "
                          "сеанса используйте ручной профиль; управление "
                          "цветом Wayland требует отдельной поддержки."));
    return;
  }
  if (output.isEmpty()) {
    finish(revision, uiTr("Авто ICC: текущий экран не определён."));
    return;
  }
  ask(revision, root, service, "FindDeviceByProperty", {"XRANDR_name", output},
      [this, revision](const QDBusMessage &reply) {
        const auto device = objectPath(reply);
        if (device.isEmpty()) {
          finish(revision,
                 uiTr("Авто ICC: экран не зарегистрирован в colord."));
          return;
        }
        ask(revision, device, "org.freedesktop.DBus.Properties", "GetAll",
            {deviceInterface}, [this, revision, device](const QDBusMessage &r) {
              const auto values = properties(r);
              const auto owner = values.value("Owner").toUInt();
              const auto metadata =
                  qdbus_cast<QMap<QString, QString>>(values.value("Metadata"));
              if (values.value("Kind").toString() != "display" ||
                  !values.value("Enabled").toBool() ||
                  metadata.value("XRANDR_name") != output ||
                  (owner != 0 && owner != uint(getuid()))) {
                finish(revision,
                       uiTr("Авто ICC: устройство выключено или не "
                            "соответствует текущему экрану и пользователю."));
                return;
              }
              // Profiles[0] is not necessarily applicable. This method honors
              // the default selection and profiling inhibitors in colord.
              ask(revision, device, deviceInterface, "GetProfileForQualifiers",
                  {QStringList{"*"}}, [this, revision](const QDBusMessage &r) {
                    const auto profile = objectPath(r);
                    if (profile.isEmpty()) {
                      finish(revision,
                             uiTr("Авто ICC: у экрана нет активного профиля."));
                      return;
                    }
                    ask(revision, profile, "org.freedesktop.DBus.Properties",
                        "GetAll", {service + ".Profile"},
                        [this, revision](const QDBusMessage &r) {
                          const auto filename =
                              properties(r).value("Filename").toString();
                          if (!QDir::isAbsolutePath(filename))
                            finish(revision,
                                   uiTr("Авто ICC: нет абсолютного пути к "
                                        "файлу системного профиля."));
                          else
                            finish(revision, {}, filename);
                        });
                  });
            });
      });
}
void SystemIcc::finish(quint64 revision, const QString &failure,
                       const QString &source) {
  if (!active || revision != generation)
    return;
  pending = false;
  QString error = failure, relative;
  if (error.isEmpty())
    relative = VideoDisplay::importIcc(source, directory, &error);
  const auto path =
      relative.isEmpty() ? QString() : QDir(directory).filePath(relative);
  if (player->get("icc-profile").toString() != path &&
      !player->set("icc-profile", path))
    error = uiTr("libmpv отклонил ICC-настройку; профиль не подтверждён.");
  player->setProperty("foxicmpSystemIccOutput", output);
  player->setProperty("foxicmpSystemIccFile",
                      error.isEmpty() ? relative : QString());
  player->setProperty(
      "foxicmpIccStatus",
      !error.isEmpty()
          ? uiTr("Экран %1. %2 ICC не применяется.").arg(output, error)
          : uiTr("Авто ICC: экран %1, профиль %2. Действует "
                 "только в OpenGL.")
                .arg(output, relative));
}
