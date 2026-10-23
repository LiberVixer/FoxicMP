#include "maintenance.h"
#include "window.h"
#include "player.h"
#include "i18n.h"
#include <QtWidgets>

QVariantMap Window::diagnosticRuntime() const {
  QVariantMap data{{"application", "FoxicMP Qt"}, {"version", QCoreApplication::applicationVersion()},
                   {"platformPlugin", QGuiApplication::platformName()},
                   {"profileDirectory", profile.directory}, {"source", current},
                   {"lastError", lastError}, {"stopped", stopped}, {"paused", paused},
                   {"position", pos}, {"duration", duration},
                   {"renderer", player->outputName()},
                   {"requestedRenderer", profile.settings.value("video/output", "software")},
                   {"renderStatistics", player->renderStatistics()},
                   {"priority", Maintenance::priority(profile.settings.value("maintenance/priority", "normal").toString(), false)},
                   {"startupPriority", qApp->property("foxicmpPriority")}};
  for (const auto *key : {"mpv-version", "ffmpeg-version", "libass-version",
       "video-codec", "audio-codec-name", "video-params", "video-out-params",
       "audio-params", "audio-out-params", "hwdec-current", "vo-configured", "ao",
       "audio-device", "audio-device-list", "avsync", "decoder-frame-drop-count",
       "frame-drop-count"}) {
    const QString name = QString(key) == "audio-device-list" ? "audioDevices"
                       : QString(key) == "audio-device" ? "audioDevice" : QString(key);
    data[name] = player->get(key);
  }
  return data;
}
void Window::maintenanceDialog(bool checkUpdates) {
  if (auto *existing = findChild<QDialog *>("maintenanceDialog")) {
    existing->show(); existing->raise();
    if (checkUpdates)
      existing->findChild<QPushButton *>("maintenanceCheckUpdates")->click();
    return;
  }
  auto *dialog = new QDialog(this);
  dialog->setObjectName("maintenanceDialog");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(uiTr("Обслуживание и диагностика"));
  dialog->resize(900, 700);
  auto *layout = new QVBoxLayout(dialog);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  auto *version = new QLabel(uiTr("FoxicMP Qt %1 — Linux x64").arg(QCoreApplication::applicationVersion()));
  version->setObjectName("maintenanceVersion");
  form->addRow(version);
  auto *previews = new QCheckBox(uiTr("Учитывать предварительные выпуски Qt"));
  previews->setObjectName("maintenancePreviews");
  previews->setChecked(profile.settings.value("maintenance/updatePreviews", true).toBool());
  form->addRow(previews);
  auto *check = new QPushButton(uiTr("Проверить обновления Linux/Qt"));
  check->setObjectName("maintenanceCheckUpdates");
  auto *open = new QPushButton(uiTr("Открыть страницу выпуска"));
  open->setObjectName("maintenanceOpenRelease");
  open->setEnabled(false);
  auto *updateRow = new QHBoxLayout;
  updateRow->addWidget(check); updateRow->addWidget(open);
  form->addRow(updateRow);
  auto *updateStatus = new QLabel(uiTr("Проверка выполняется вручную. Автоматической установки нет."));
  updateStatus->setObjectName("maintenanceUpdateStatus");
  updateStatus->setTextFormat(Qt::PlainText);
  updateStatus->setWordWrap(true);
  form->addRow(updateStatus);
  auto *priority = new QComboBox;
  priority->setObjectName("maintenancePriority");
  priority->addItem(uiTr("Обычный (nice 0)"), "normal");
  priority->addItem(uiTr("Выше обычного (nice −5)"), "above");
  priority->addItem(uiTr("Ниже обычного (nice 5)"), "low");
  priority->setCurrentIndex(std::max(0, priority->findData(profile.settings.value("maintenance/priority", "normal"))));
  form->addRow(uiTr("Приоритет со следующего запуска:"), priority);
  const auto actual = Maintenance::priority("normal", false).value("actualNice").toInt();
  auto *priorityStatus = new QLabel(uiTr("Сейчас nice %1. Повышение приоритета может быть запрещено Linux; права администратора не запрашиваются.").arg(actual));
  priorityStatus->setWordWrap(true);
  const auto startup = qApp->property("foxicmpPriority").toMap();
  if (!startup.value("error").toString().isEmpty())
    priorityStatus->setText(priorityStatus->text() + "\n" +
        uiTr("При запуске: %1").arg(startup.value("error").toString()));
  form->addRow(priorityStatus);
  auto *savePreferences = new QPushButton(uiTr("Сохранить параметры обслуживания"));
  savePreferences->setObjectName("maintenanceSavePreferences");
  form->addRow(savePreferences);
  auto *details = new QCheckBox(uiTr("Включить пути, имена устройств и последние сообщения движка"));
  details->setObjectName("maintenanceReportDetails");
  form->addRow(details);
  auto *note = new QLabel(uiTr("Отчёт остаётся у вас. Просмотрите его перед отправкой. Журнал хранит до 100 сообщений только в памяти текущего запуска; дамп аварийного завершения не создаётся."));
  note->setWordWrap(true); form->addRow(note);
  auto *report = new QPlainTextEdit;
  report->setObjectName("maintenanceReport"); report->setReadOnly(true);
  layout->addWidget(report, 1);
  auto *status = new QLabel;
  status->setObjectName("maintenanceStatus"); status->setTextFormat(Qt::PlainText);
  status->setWordWrap(true); layout->addWidget(status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  auto *refresh = buttons->addButton(uiTr("Обновить отчёт"), QDialogButtonBox::ActionRole);
  refresh->setObjectName("maintenanceRefreshReport");
  auto *copy = buttons->addButton(uiTr("Копировать отчёт"), QDialogButtonBox::ActionRole);
  copy->setObjectName("maintenanceCopyReport");
  auto *save = buttons->addButton(uiTr("Сохранить JSON…"), QDialogButtonBox::ActionRole);
  save->setObjectName("maintenanceSaveReport");
  layout->addWidget(buttons);
  auto *client = new UpdateCheck(dialog);
  auto releasePage = std::make_shared<QUrl>();
  connect(check, &QPushButton::clicked, dialog, [=] {
    check->setEnabled(false); open->setEnabled(false); *releasePage = QUrl();
    updateStatus->setText(uiTr("Проверка GitHub…"));
    client->start(QCoreApplication::applicationVersion(), previews->isChecked());
  });
  connect(client, &UpdateCheck::finished, dialog, [=](const QString &result, const QString &latest, const QUrl &page, const QString &error) {
    check->setEnabled(true);
    if (result == "available") {
      *releasePage = page; open->setEnabled(true);
      updateStatus->setText(uiTr("Доступна FoxicMP Qt %1. Текущая версия: %2.").arg(latest, QCoreApplication::applicationVersion()));
    } else if (result == "current")
      updateStatus->setText(uiTr("Более новой Linux/Qt-сборки нет. Опубликована %1; установлена %2.").arg(latest, QCoreApplication::applicationVersion()));
    else if (result == "unpublished")
      updateStatus->setText(uiTr("Среди последних 100 выпусков нет подходящей Linux/Qt-сборки. Windows-сборки не учитываются."));
    else
      updateStatus->setText(error);
  });
  connect(open, &QPushButton::clicked, dialog, [=] {
    if (releasePage->isValid()) QDesktopServices::openUrl(*releasePage);
  });
  connect(savePreferences, &QPushButton::clicked, dialog, [=] {
    profile.settings.setValue("maintenance/priority", priority->currentData());
    profile.settings.setValue("maintenance/updatePreviews", previews->isChecked());
    profile.settings.sync();
    status->setText(profile.settings.status() == QSettings::NoError
        ? uiTr("Параметры сохранены. Приоритет применяется при следующем запуске.")
        : uiTr("Не удалось сохранить параметры в профиль."));
  });
  const auto refreshReport = [=] {
    report->setPlainText(QString::fromUtf8(Maintenance::report(diagnosticRuntime(), diagnosticMessages, details->isChecked())));
  };
  connect(refresh, &QPushButton::clicked, dialog, refreshReport);
  connect(details, &QCheckBox::toggled, dialog, refreshReport);
  connect(copy, &QPushButton::clicked, dialog, [=] {
    QApplication::clipboard()->setText(report->toPlainText());
    status->setText(uiTr("Отчёт скопирован."));
  });
  connect(save, &QPushButton::clicked, dialog, [=] {
    const auto file = QFileDialog::getSaveFileName(dialog, uiTr("Сохранить диагностический отчёт"),
        QDir(profile.directory).filePath("FoxicMPQt-diagnostic.json"), "JSON (*.json)");
    if (file.isEmpty()) return;
    QString error;
    status->setText(Maintenance::saveReport(file, report->toPlainText().toUtf8(), &error)
        ? uiTr("Отчёт сохранён: %1").arg(file) : error);
  });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  refreshReport(); dialog->show();
  if (checkUpdates) check->click();
}
