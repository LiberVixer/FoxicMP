#pragma once
#include <QDialog>
#include <QTimer>
#include <QVariant>
#include <utility>

// Preferences deliberately paint and process events during construction. Drive
// controls only after the draft, validation and button connections are complete.
template <class Callback>
void afterPreferencesReady(QWidget *window, Callback callback) {
  auto *timer = new QTimer(window); timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, timer,
      [window, timer, callback = std::move(callback)]() mutable {
    auto *dialog = window->findChild<QDialog *>("preferencesDialog");
    if (!dialog || !dialog->property("settingsReady").toBool()) return;
    timer->stop(); timer->deleteLater(); callback();
  });
  timer->start();
}
