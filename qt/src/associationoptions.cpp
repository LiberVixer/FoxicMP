#include "associationoptions.h"
#include "associations.h"
#include "i18n.h"
#include "integration.h"
#include "profile.h"
#include <QtWidgets>

QWidget *createAssociationsPage(Profile &profile, QWidget *parent, bool deferVerification) {
  auto *container = new QWidget(parent);
  auto *layout = new QVBoxLayout(container);
  auto *note = new QLabel(
      uiTr("Назначения применяются сразу по кнопке только для текущего "
           "пользователя. Отмечайте MIME-типы, а не отдельные расширения. Меню "
           "«Открыть с помощью» использует текущую программу и профиль. "
           "Регистрация заменяет одноимённый пункт другой копии FoxicMP."));
  note->setWordWrap(true);
  layout->addWidget(note);
  auto *filter = new QLineEdit;
  filter->setObjectName("associationFilter");
  filter->setPlaceholderText(uiTr("Поиск MIME-типа или расширения"));
  layout->addWidget(filter);
  auto *presets = new QHBoxLayout;
  layout->addLayout(presets);
  auto *table = new QTableWidget;
  table->setObjectName("associationTypes");
  table->setColumnCount(4);
  table->setHorizontalHeaderLabels({uiTr("MIME-тип"), uiTr("Расширения"),
                                    uiTr("Описание"),
                                    uiTr("Назначение пользователя")});
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->verticalHeader()->hide();
  const auto types = Associations::catalog();
  table->setRowCount(types.size());
  for (int i = 0; i < types.size(); ++i) {
    auto *item = new QTableWidgetItem(types[i].mime);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
    item->setData(Qt::UserRole, types[i].category);
    table->setItem(i, 0, item);
    table->setItem(i, 1, new QTableWidgetItem(types[i].extensions));
    table->setItem(i, 2, new QTableWidgetItem(types[i].description));
    table->setItem(i, 3, new QTableWidgetItem);
  }
  table->setColumnWidth(0, 255);
  table->setColumnWidth(1, 170);
  table->setColumnWidth(2, 230);
  table->horizontalHeader()->setStretchLastSection(true);
  layout->addWidget(table, 1);
  for (const auto &pair :
       QList<QPair<QString, QString>>{{uiTr("Все видео"), "video"},
                                      {uiTr("Всё аудио"), "audio"},
                                      {uiTr("Плейлисты"), "playlist"},
                                      {uiTr("Снять отметки"), ""}}) {
    auto *button = new QPushButton(pair.first);
    button->setObjectName("selectAssociations_" + pair.second);
    presets->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, container, [table, pair] {
      for (int i = 0; i < table->rowCount(); ++i)
        table->item(i, 0)->setCheckState(
            !pair.second.isEmpty() &&
                    table->item(i, 0)->data(Qt::UserRole) == pair.second
                ? Qt::Checked
                : Qt::Unchecked);
    });
  }
  QObject::connect(
      filter, &QLineEdit::textChanged, container, [table](const QString &text) {
        for (int i = 0; i < table->rowCount(); ++i) {
          bool found = false;
          for (int c = 0; c < 3; ++c)
            found |=
                table->item(i, c)->text().contains(text, Qt::CaseInsensitive);
          table->setRowHidden(i, !found);
        }
      });
  auto *path =
      new QLabel(uiTr("Пользовательский файл: ") + Associations::configFile());
  path->setWordWrap(true);
  path->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(path);
  auto *scope = new QLabel(uiTr(
      "В таблице — первый пункт из пользовательского mimeapps.list, а не "
      "гарантия эффективного назначения. Настройки рабочего стола могут иметь "
      "приоритет. Кнопка проверки читает действующее назначение выделенного "
      "типа через gio. Папки, URL и автозапуск дисков не назначаются."));
  scope->setWordWrap(true);
  layout->addWidget(scope);
  auto *status = new QLabel;
  status->setObjectName("associationStatus");
  status->setMaximumHeight(105);
  status->setWordWrap(true);
  status->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(status);
  const auto refresh = [table, status] {
    QMap<QString, QString> defaults;
    QString error;
    if (!Associations::readDefaults(&defaults, &error)) {
      status->setText(error);
      return;
    }
    for (int i = 0; i < table->rowCount(); ++i)
      table->item(i, 3)->setText(defaults.value(table->item(i, 0)->text()));
  };
  refresh();
  auto *buttons = new QHBoxLayout;
  layout->addLayout(buttons);
  const auto button = [=, &profile](const QString &text, const QString &name) {
    auto *b = new QPushButton(text);
    b->setObjectName(name);
    buttons->addWidget(b);
    return b;
  };
  auto *reg = button(uiTr("Зарегистрировать плеер"), "registerApplication");
  auto *assign = button(uiTr("Назначить отмеченные"), "assignAssociations");
  auto *undo = button(uiTr("Откатить мои назначения"), "restoreAssociations");
  auto *query = button(uiTr("Проверить в системе"), "queryAssociation");
  const auto registerPlayer = [=, &profile] {
    QString error;
    const bool ok = Integration::registerApplication(
        QCoreApplication::applicationFilePath(), profile.directory, &error);
    if (!ok)
      status->setText(error);
    else {
      query->setEnabled(false);
      Integration::refreshDatabase(container,
                                   [query, status](const QString &message) {
                                     query->setEnabled(true);
                                     if (!message.isEmpty())
                                       status->setText(message);
                                   });
    }
    return ok;
  };
  QObject::connect(reg, &QPushButton::clicked, container, [=, &profile] {
    if (registerPlayer())
      status->setText(
          uiTr("Плеер зарегистрирован. Назначения по умолчанию не изменены."));
  });
  QObject::connect(assign, &QPushButton::clicked, container, [=, &profile] {
    QStringList selected;
    for (int i = 0; i < table->rowCount(); ++i)
      if (table->item(i, 0)->checkState() == Qt::Checked)
        selected << table->item(i, 0)->text();
    if (selected.isEmpty()) {
      status->setText(uiTr("Выберите типы медиафайлов."));
      return;
    }
    if (!registerPlayer())
      return;
    QString error;
    if (Associations::assign(profile.settings, selected, &error)) {
      refresh();
      status->setText(uiTr("Пользовательские назначения записаны для типов: "
                           "%1. Проверьте результат в системе.")
                          .arg(selected.size()));
    } else
      status->setText(error);
  });
  QObject::connect(undo, &QPushButton::clicked, container, [=, &profile] {
    int restored = 0, skipped = 0;
    QString error;
    if (Associations::restore(profile.settings, &restored, &skipped, &error)) {
      refresh();
      status->setText(uiTr("Восстановлено записей: %1. Пропущено изменённых "
                           "другими программами: %2.")
                          .arg(restored)
                          .arg(skipped));
    } else
      status->setText(error);
  });
  auto *process = new QProcess(container);
  auto *deadline = new QTimer(process);
  deadline->setSingleShot(true);
  deadline->setInterval(3000);
  QObject::connect(deadline, &QTimer::timeout, process, [process, status] {
    process->kill();
    status->setText(uiTr("Проверка назначения превысила 3 секунды."));
  });
  QObject::connect(
      query, &QPushButton::clicked, container, [=] {
        if (table->currentRow() < 0) {
          status->setText(uiTr("Выделите строку для проверки."));
          return;
        }
        if (QStandardPaths::findExecutable("gio").isEmpty()) {
          status->setText(
              uiTr("Для системной проверки требуется gio (libglib2.0-bin)."));
          return;
        }
        if (process->state() != QProcess::NotRunning)
          return;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("LC_ALL", "C");
        process->setProcessEnvironment(env);
        process->setProcessChannelMode(QProcess::MergedChannels);
        process->setProperty("timedOut", false);
        process->start("gio",
                       {"mime", table->item(table->currentRow(), 0)->text()});
        query->setEnabled(false);
        deadline->start();
      });
  QObject::connect(process, &QProcess::readyRead, container, [process, status] {
    auto output =
        process->property("report").toByteArray() + process->readAll();
    if (output.size() > 65536) {
      process->kill();
      output = output.left(65536);
    }
    process->setProperty("report", output);
    status->setText(QString::fromUtf8(output));
  });
  QObject::connect(
      process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
      container, [process, deadline, query](int, QProcess::ExitStatus) {
        deadline->stop();
        query->setEnabled(true);
        process->setProperty("report", QByteArray());
      });
  QObject::connect(
      process, &QProcess::errorOccurred, container,
      [process, deadline, query, status](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
          deadline->stop();
          query->setEnabled(true);
          status->setText(process->errorString());
        }
      });
  auto *menuRow = new QHBoxLayout;
  layout->addLayout(menuRow);
  auto *installMenu = new QPushButton(uiTr("Добавить / исправить пункт Nemo"));
  installMenu->setObjectName("installNemoMenu");
  menuRow->addWidget(installMenu);
  auto *removeMenu = new QPushButton(uiTr("Удалить пункт Nemo"));
  removeMenu->setObjectName("removeNemoMenu");
  menuRow->addWidget(removeMenu);
  auto *verify = new QCheckBox(uiTr("Проверять путь в меню при запуске"));
  verify->setChecked(
      profile.settings.value("integration/checkOnStartup", false).toBool());
  verify->setObjectName("integration/checkOnStartup");
  menuRow->addWidget(verify);
  if (!deferVerification) QObject::connect(verify, &QCheckBox::toggled, container, [=, &profile](bool checked) {
    profile.settings.setValue("integration/checkOnStartup", checked);
  });
  QObject::connect(installMenu, &QPushButton::clicked, container, [=, &profile] {
    QString error;
    if (Integration::installForProfile(QCoreApplication::applicationFilePath(),
                                       profile.directory, &error)) {
      Integration::refreshDatabase(container, [status](const QString &message) {
        if (!message.isEmpty())
          status->setText(message);
      });
      profile.settings.setValue("integration/enabled", true);
      profile.settings.setValue("integration/prompted", true);
      status->setText(
          uiTr("Пункт Nemo добавлен; программа и профиль обновлены."));
    } else
      status->setText(error);
  });
  QObject::connect(removeMenu, &QPushButton::clicked, container, [=, &profile] {
    QString error;
    if (Integration::remove(&error)) {
      Integration::refreshDatabase(container, [status](const QString &message) {
        if (!message.isEmpty())
          status->setText(message);
      });
      profile.settings.setValue("integration/enabled", false);
      status->setText(uiTr("Пункт Nemo удалён. Ярлык плеера сохранён, если на "
                           "него ссылаются ассоциации."));
    } else
      status->setText(error);
  });
  QObject::connect(container, &QObject::destroyed, process, [process] {
    if (process->state() != QProcess::NotRunning) { process->kill(); process->waitForFinished(1000); }
  });
  return container;
}
void showAssociations(Profile &profile, QWidget *parent) {
  QDialog dialog(parent);
  dialog.setObjectName("associationsDialog");
  dialog.setWindowTitle(uiTr("Форматы и ассоциации Linux"));
  dialog.resize(990, 700);
  auto *layout = new QVBoxLayout(&dialog);
  layout->addWidget(createAssociationsPage(profile, &dialog));
  auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
  QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(close);
  dialog.exec();
}
