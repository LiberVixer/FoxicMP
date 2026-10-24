#include "associationoptions.h"
#include "associations.h"
#include "i18n.h"
#include "integration.h"
#include "profile.h"
#include <QtWidgets>
QWidget *createAssociationsPage(Profile &profile, QWidget *parent, bool deferVerification) {
  auto *page = new QWidget(parent);
  auto *layout = new QVBoxLayout(page);
  auto *note = new QLabel(uiTr("FoxiqMP qt регистрируется как отдельное приложение Windows. "
      "Плеер по умолчанию выбирается в параметрах Windows. Настройки Classic, MPC и MPC-BE не изменяются."));
  note->setWordWrap(true); layout->addWidget(note);
  auto *table = new QTableWidget;
  table->setObjectName("associationTypes");
  table->setColumnCount(2);
  table->setHorizontalHeaderLabels({uiTr("Расширения"), uiTr("Описание")});
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->horizontalHeader()->setStretchLastSection(true);
  const auto types = Associations::catalog(); table->setRowCount(types.size());
  for (int i = 0; i < types.size(); ++i) {
    table->setItem(i, 0, new QTableWidgetItem(types[i].extensions));
    table->setItem(i, 1, new QTableWidgetItem(types[i].description));
  }
  table->setColumnWidth(0, 230); layout->addWidget(table, 1);
  auto *reg = new QPushButton(uiTr("Зарегистрировать плеер"));
  reg->setObjectName("registerApplication"); layout->addWidget(reg);
  auto *defaults = new QPushButton(uiTr("Приложения по умолчанию в Windows…"));
  defaults->setObjectName("windowsDefaultApps"); layout->addWidget(defaults);
  auto *install = new QPushButton(uiTr("Пункт меню для всех видеофайлов"));
  install->setObjectName("installNemoMenu"); layout->addWidget(install);
  auto *remove = new QPushButton(uiTr("Удалить пункт меню FoxiqMP qt"));
  remove->setObjectName("removeNemoMenu"); layout->addWidget(remove);
  auto *verify = new QCheckBox(uiTr("Проверять путь в меню при запуске"));
  verify->setObjectName("integration/checkOnStartup");
  verify->setChecked(profile.settings.value("integration/checkOnStartup", false).toBool());
  layout->addWidget(verify);
  auto *status = new QLabel;
  status->setObjectName("associationStatus"); status->setWordWrap(true); layout->addWidget(status);
  if (!deferVerification) QObject::connect(verify, &QCheckBox::toggled, page, [&profile](bool checked) {
    profile.settings.setValue("integration/checkOnStartup", checked);
  });
  QObject::connect(defaults, &QPushButton::clicked, page, [] { QDesktopServices::openUrl(QUrl("ms-settings:defaultapps")); });
  QObject::connect(reg, &QPushButton::clicked, page, [=, &profile] {
    QString error;
    status->setText(Integration::registerApplication(QCoreApplication::applicationFilePath(), profile.directory, &error)
        ? uiTr("Плеер зарегистрирован. Назначения по умолчанию не изменены.") : error);
  });
  QObject::connect(install, &QPushButton::clicked, page, [=, &profile] {
    QString error;
    if (Integration::installForProfile(QCoreApplication::applicationFilePath(), profile.directory, &error)) {
      profile.settings.setValue("integration/enabled", true);
      profile.settings.setValue("integration/prompted", true);
      status->setText(uiTr("Пункт FoxiqMP qt добавлен; программа и профиль обновлены."));
    } else status->setText(error);
  });
  QObject::connect(remove, &QPushButton::clicked, page, [=, &profile] {
    QString error;
    if (Integration::remove(&error)) {
      profile.settings.setValue("integration/enabled", false);
      status->setText(uiTr("Пункт меню FoxiqMP qt удалён."));
    } else status->setText(error);
  });
  return page;
}
void showAssociations(Profile &profile, QWidget *parent) {
  QDialog dialog(parent); dialog.setObjectName("associationsDialog");
  dialog.setWindowTitle(uiTr("Форматы и ассоциации Windows")); dialog.resize(900, 650);
  auto *layout = new QVBoxLayout(&dialog); layout->addWidget(createAssociationsPage(profile, &dialog));
  auto *close = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(close);
  QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); dialog.exec();
}
