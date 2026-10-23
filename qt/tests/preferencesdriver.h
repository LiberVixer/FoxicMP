#pragma once
#include <QDialog>
#include <QTimer>
#include <QVariant>
#include <utility>

// Preferences deliberately paint and process events during construction. Drive
// controls only after the draft, validation and button connections are complete.
template <class Callback>
void afterDialogReady(QWidget *window, const QString &objectName,
                      Callback callback) {
  auto *timer = new QTimer(window); timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, timer,
      [window, objectName, timer, callback = std::move(callback)]() mutable {
    QDialog *dialog = nullptr;
    if (!objectName.isEmpty())
      dialog = window->findChild<QDialog *>(objectName);
    else {
      for (auto *candidate : window->findChildren<QDialog *>())
        if (candidate->isVisible()) { dialog = candidate; break; }
    }
    if (!dialog || !dialog->isVisible()) return;
    if (dialog->property("settingsReady").isValid() &&
        !dialog->property("settingsReady").toBool()) return;
    timer->stop(); timer->deleteLater(); callback(dialog);
  });
  timer->start();
}

template <class Callback>
void afterPreferencesReady(QWidget *window, Callback callback) {
  afterDialogReady(window, QStringLiteral("preferencesDialog"),
                   [callback = std::move(callback)](QDialog *) mutable {
                     callback();
                   });
}
