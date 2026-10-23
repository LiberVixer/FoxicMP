#include "profileoptions.h"
#include "i18n.h"
#include "profile.h"
#include "profilearchive.h"
#include "profilelaunch.h"
#include <QProcess>
#include <QtWidgets>

void showProfileManagement(Profile &profile, QWidget *parent,
                           std::function<void()> checkpoint,
                           std::function<void(bool)> reset,
                           const QString &programDirectory) {
  const auto program = programDirectory.isEmpty()
      ? QCoreApplication::applicationDirPath() : programDirectory;
  QDialog dialog(parent);
  dialog.setObjectName("profileManagement");
  dialog.setWindowTitle(uiTr("Профиль и резервные копии"));
  dialog.resize(840, 640);
  auto *layout = new QVBoxLayout(&dialog);
  auto *location =
      new QLabel(uiTr("Текущий профиль: %1").arg(profile.directory));
  location->setWordWrap(true);
  location->setTextFormat(Qt::PlainText);
  location->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(location);
  auto *scope = new QLabel(uiTr(
      "Архив содержит настройки, плейлисты, историю, избранное, выбранные "
      "изображения, перевод, ICC и правки субтитров. Видео, снимки, кэш, "
      "программы и системные ассоциации не включаются. Пути к медиа "
      "сохраняются. "
      "Лимиты: 32 МиБ, 2048 файлов, 8 МиБ на файл. Архив содержит историю и "
      "сохранённые адреса — учитывайте это при передаче."));
  scope->setWordWrap(true);
  layout->addWidget(scope);
  auto *explanation = new QLabel(uiTr(
      "Импорт создаёт новую папку профиля. Открыть её можно кнопкой ниже или "
      "параметром --profile. Текущий профиль продолжает работать. Для "
      "постоянного запуска создайте ярлык с --profile и выбранным путём."));
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  auto *launchGroup = new QGroupBox(uiTr("Профиль при запуске"));
  auto *launchLayout = new QVBoxLayout(launchGroup);
  auto *selection = new QLabel;
  selection->setObjectName("profileLaunchSelection");
  selection->setWordWrap(true);
  selection->setTextFormat(Qt::PlainText);
  const auto refreshSelection = [&] {
    QString path, error;
    selection->setText(ProfileLaunch::resolve(program, &path, &error)
        ? uiTr("При обычном запуске: %1").arg(path) : error);
  };
  refreshSelection();
  launchLayout->addWidget(selection);
  auto *targetRow = new QHBoxLayout;
  auto *target = new QLineEdit(profile.directory);
  target->setObjectName("profileLaunchDirectory");
  target->setAccessibleName(uiTr("Папка профиля для запуска"));
  auto *browse = new QPushButton(uiTr("Выбрать папку…"));
  browse->setObjectName("profileLaunchBrowse");
  targetRow->addWidget(target);
  targetRow->addWidget(browse);
  launchLayout->addLayout(targetRow);
  auto *launchRow = new QHBoxLayout;
  auto *use = new QPushButton(uiTr("Использовать при обычном запуске"));
  use->setObjectName("profileLaunchUse");
  auto *portable = new QPushButton(uiTr("Профиль рядом с программой"));
  portable->setObjectName("profileLaunchPortable");
  auto *shortcut = new QPushButton(uiTr("Создать ярлык…"));
  shortcut->setObjectName("profileLaunchShortcut");
  launchRow->addWidget(use);
  launchRow->addWidget(portable);
  launchRow->addWidget(shortcut);
  launchLayout->addLayout(launchRow);
  auto *launchHelp = new QLabel(uiTr("Выбор применяется при следующем запуске без --profile. "
      "Текущий профиль не переносится. --portable игнорирует сохранённый выбор. "
      "Ярлык открывает указанную папку независимо от обычного запуска."));
  launchHelp->setWordWrap(true);
  launchLayout->addWidget(launchHelp);
  layout->addWidget(launchGroup);
  auto *row = new QHBoxLayout;
  layout->addLayout(row);
  const auto button = [&](const QString &text, const QString &name) {
    auto *b = new QPushButton(text);
    b->setObjectName(name);
    row->addWidget(b);
    return b;
  };
  auto *exportButton = button(uiTr("Экспорт…"), "profileExport");
  auto *backupButton = button(uiTr("Резервная копия"), "profileBackup");
  auto *importButton =
      button(uiTr("Импорт / восстановление…"), "profileImport");
  auto *resetButton = button(uiTr("Сброс настроек…"), "profileReset");
  resetButton->setEnabled(bool(reset));
  auto *status = new QLabel;
  status->setObjectName("profileStatus");
  status->setWordWrap(true);
  status->setTextFormat(Qt::PlainText);
  status->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(status);
  auto *open = new QPushButton(uiTr("Открыть восстановленный профиль"));
  open->setObjectName("profileOpenRestored");
  open->setEnabled(false);
  layout->addWidget(open);
  layout->addStretch();
  auto *footer = new QDialogButtonBox(QDialogButtonBox::Close);
  QObject::connect(footer, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  layout->addWidget(footer);
  QString restored;
  QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
    const auto path = QFileDialog::getExistingDirectory(&dialog,
        uiTr("Папка профиля для запуска"), target->text());
    if (!path.isEmpty()) target->setText(path);
  });
  const auto saveSelection = [&](const QString &directory) {
    QString error;
    if (ProfileLaunch::select(program, directory, &error)) {
      target->setText(directory);
      refreshSelection();
      status->setText(uiTr("Выбор сохранён. Текущий профиль продолжает работать."));
    } else status->setText(error);
  };
  QObject::connect(use, &QPushButton::clicked, &dialog, [&] { saveSelection(target->text()); });
  QObject::connect(portable, &QPushButton::clicked, &dialog, [&] { saveSelection(program); });
  QObject::connect(shortcut, &QPushButton::clicked, &dialog, [&] {
    auto folder = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (folder.isEmpty() || !QDir(folder).exists()) folder = QDir::homePath();
    const auto path = QFileDialog::getSaveFileName(&dialog, uiTr("Создать ярлык профиля"),
        QDir(folder).filePath("FoxicMPQt-profile.desktop"), uiTr("Ярлык Linux (*.desktop)"));
    if (path.isEmpty()) return;
    QString error;
    status->setText(ProfileLaunch::launcher(QCoreApplication::applicationFilePath(),
        target->text(), path, &error)
        ? uiTr("Ярлык сохранён: %1. При необходимости разрешите его запуск в файловом менеджере.").arg(path)
        : error);
  });
  bool resetRequested = false, keepLibrary = true;
  QObject::connect(resetButton, &QPushButton::clicked, &dialog, [&] {
    QMessageBox confirm(QMessageBox::Warning, uiTr("Сброс настроек"),
        uiTr("Плеер закроется, сохранит резервную копию INI в Backups и сбросит настройки. "
             "При следующем запуске будут настройки по умолчанию. Медиафайлы, изображения "
             "и правки субтитров останутся на месте. Системные ассоциации не меняются; "
             "их журнал отката сохраняется. Закройте другие окна этого профиля. "
             "Резервная копия содержит историю и сохранённые адреса."),
        QMessageBox::Ok | QMessageBox::Cancel, &dialog);
    confirm.setObjectName("profileResetConfirm");
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.setEscapeButton(QMessageBox::Cancel);
    auto *keep = new QCheckBox(uiTr("Сохранить плейлисты, историю, позиции и избранное"));
    keep->setObjectName("profileResetKeepLibrary");
    keep->setChecked(true);
    confirm.setCheckBox(keep);
    if (confirm.exec() != QMessageBox::Ok)
      return;
    keepLibrary = keep->isChecked();
    resetRequested = true;
    dialog.accept();
  });
  QObject::connect(exportButton, &QPushButton::clicked, &dialog, [&] {
    const auto path = QFileDialog::getSaveFileName(
        &dialog, uiTr("Экспорт профиля"), "FoxicMPQt.foxicmp-profile",
        uiTr("Профиль FoxicMP (*.foxicmp-profile)"));
    if (path.isEmpty())
      return;
    if (!path.endsWith(".foxicmp-profile", Qt::CaseInsensitive)) {
      status->setText(uiTr("Укажите имя с расширением .foxicmp-profile."));
      return;
    }
    checkpoint();
    QString error;
    status->setText(ProfileArchive::save(profile, path, &error)
                        ? uiTr("Архив сохранён: %1").arg(path)
                        : error);
  });
  QObject::connect(backupButton, &QPushButton::clicked, &dialog, [&] {
    checkpoint();
    QString error;
    const auto path = ProfileArchive::backup(profile, &error);
    status->setText(path.isEmpty() ? error
                                   : uiTr("Резервная копия: %1").arg(path));
  });
  QObject::connect(importButton, &QPushButton::clicked, &dialog, [&] {
    const auto path = QFileDialog::getOpenFileName(
        &dialog, uiTr("Импорт профиля"), profile.directory,
        uiTr("Профиль FoxicMP (*.foxicmp-profile)"));
    if (path.isEmpty())
      return;
    ProfileArchive::Bundle bundle;
    QString error;
    if (!ProfileArchive::read(path, &bundle, &error)) {
      status->setText(error);
      return;
    }
    const auto parent = QFileDialog::getExistingDirectory(
        &dialog, uiTr("Куда поместить новую папку профиля?"),
        profile.directory);
    if (parent.isEmpty())
      return;
    bool ok = false;
    const auto name =
        QInputDialog::getText(&dialog, uiTr("Новая папка профиля"),
                              uiTr("Имя новой папки:"), QLineEdit::Normal,
                              "FoxicMPQt-profile", &ok)
            .trimmed();
    if (!ok)
      return;
    if (name.isEmpty() || name == "." || name == ".." || name.contains('/') ||
        name.contains('\\') || name.contains(QChar::Null)) {
      status->setText(uiTr("Укажите имя новой папки без разделителей пути."));
      return;
    }
    const auto target =
        QDir(QFileInfo(parent).canonicalFilePath()).filePath(name);
    if (!ProfileArchive::restore(bundle, target, &error)) {
      status->setText(error);
      return;
    }
    restored = target;
    open->setEnabled(true);
    status->setText(uiTr("Профиль восстановлен: %1").arg(restored));
  });
  QObject::connect(open, &QPushButton::clicked, &dialog, [&] {
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                 {"--new-instance", "--profile", restored}))
      status->setText(uiTr("Не удалось открыть восстановленный профиль."));
  });
  dialog.exec();
  if (resetRequested && reset)
    reset(keepLibrary);
}
